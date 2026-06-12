#include "PlotPanel.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui.h"
#include "implot.h"

#include "core/Shapes.h"

namespace giggle {

namespace {

// The half-max crossing distance from the mean in one direction, found
// numerically; for the asymmetric tailed gaussian, whose two sides differ.
double NumericHalfWidth(const FitComponent& peak, const FitRange& range, double mean,
                        double direction)
{
    double scale = peak.parameters.size() > 1
                       ? std::max(std::abs(peak.parameters[1].value), 1e-9)
                       : 1.0;
    double inside = 0.0;
    double outside = scale;
    int expansions = 0;
    while (ShapeValue(peak, range, mean + direction * outside) > 0.5 && expansions < 50)
    {
        inside = outside;
        outside *= 2.0;
        ++expansions;
    }
    for (int i = 0; i < 40; ++i)
    {
        double middle = 0.5 * (inside + outside);
        if (ShapeValue(peak, range, mean + direction * middle) > 0.5)
        {
            inside = middle;
        }
        else
        {
            outside = middle;
        }
    }
    return 0.5 * (inside + outside);
}

// Half width at half maximum, in x-units, for the shapes that have one.
double HalfWidthAtHalfMax(const FitComponent& peak)
{
    if (peak.parameters.size() < 2)
    {
        return 0.0;
    }
    switch (peak.shape)
    {
        case ShapeKind::Gaussian:
            return 1.177410023 * std::abs(peak.parameters[1].value); // sqrt(2 ln 2) sigma
        case ShapeKind::Lorentzian:
            return std::abs(peak.parameters[1].value); // gamma
        case ShapeKind::Voigt:
        {
            // Olivero-Longbothum approximation, halved.
            if (peak.parameters.size() < 3)
            {
                return 0.0;
            }
            double fG = 2.354820045 * std::abs(peak.parameters[1].value);
            double fL = 2.0 * std::abs(peak.parameters[2].value);
            return 0.5 * (0.5346 * fL + std::sqrt(0.2166 * fL * fL + fG * fG));
        }
        default:
            return 0.0;
    }
}

void SetHalfWidthAtHalfMax(FitComponent& peak, double halfWidth)
{
    if (peak.parameters.size() < 2 || halfWidth <= 0.0)
    {
        return;
    }
    switch (peak.shape)
    {
        case ShapeKind::Gaussian:
            peak.parameters[1].value = halfWidth / 1.177410023;
            break;
        case ShapeKind::Lorentzian:
            peak.parameters[1].value = halfWidth;
            break;
        case ShapeKind::Voigt:
        {
            // Scale sigma and gamma together so the profile keeps its
            // Gaussian/Lorentzian character while changing width.
            double current = HalfWidthAtHalfMax(peak);
            if (current > 0.0 && peak.parameters.size() >= 3)
            {
                double factor = halfWidth / current;
                peak.parameters[1].value *= factor;
                peak.parameters[2].value *= factor;
            }
            break;
        }
        default:
            break;
    }
}

// The parameter named "mean", or nullptr.
FitParameter* MeanParameter(FitComponent& component)
{
    for (FitParameter& parameter : component.parameters)
    {
        if (parameter.name == "mean")
        {
            return &parameter;
        }
    }
    return nullptr;
}

} // namespace

PlotAction PlotPanel::Draw(const HistogramData* histogram, FitModel* model,
                           const Theme& theme, ImFont* monoFont)
{
    PlotAction action;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(Title))
    {
        if (histogram != nullptr && histogram->name != m_lastDrawnName)
        {
            m_requestAxesFit = true;
            m_lastDrawnName = histogram->name;
        }
        if (m_requestAxesFit)
        {
            ImPlot::SetNextAxesToFit();
            m_requestAxesFit = false;
        }

        // "###" keeps the plot's ID stable while the visible title changes.
        std::string plotTitle = (histogram != nullptr ? histogram->name : "") + "###spectrum";

        ImGui::PushFont(monoFont, 13.0f);
        if (ImPlot::BeginPlot(plotTitle.c_str(), ImVec2(-1.0f, -1.0f), ImPlotFlags_NoMenus))
        {
            ImPlot::SetupAxes(nullptr, "counts");
            ImPlot::SetupAxisScale(ImAxis_Y1, m_logScaleY ? ImPlotScale_Log10 : ImPlotScale_Linear);

            if (histogram != nullptr)
            {
                DrawHistogram(*histogram, theme);
            }
            if (model != nullptr && histogram != nullptr)
            {
                DrawRangeTools(*model, *histogram, theme);
                DrawModelCurves(*model, *histogram, theme);
                DrawPeakHandles(*model, *histogram, theme);
                DrawBackgroundHandles(*model, *histogram, theme);
                HandleAddPeakClick(action);

                // A right click (not a right drag, which is box zoom)
                // requests the context menu. The popup itself must be
                // opened and drawn outside the plot: inside it, the plot's
                // ID is on the stack and the popup IDs would not match.
                ImVec2 rightDrag = ImGui::GetMouseDragDelta(ImGuiMouseButton_Right);
                if (ImPlot::IsPlotHovered() && ImGui::IsMouseReleased(ImGuiMouseButton_Right)
                    && rightDrag.x == 0.0f && rightDrag.y == 0.0f)
                {
                    m_contextMenuX = ImPlot::GetPlotMousePos().x;
                    m_openContextMenu = true;
                }
            }

            ImPlot::EndPlot();
        }
        ImGui::PopFont();

        if (model != nullptr && histogram != nullptr)
        {
            if (m_openContextMenu)
            {
                ImGui::OpenPopup("##plot_context");
                m_openContextMenu = false;
            }
            DrawContextMenu(*model, *histogram, action);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    return action;
}

void PlotPanel::DrawHistogram(const HistogramData& histogram, const Theme& theme)
{
    // Steps need one y value per edge; the last value is repeated so the
    // final bin is drawn to its right edge.
    const std::vector<double>& xs = histogram.binEdges;
    std::vector<double> ys = histogram.counts;
    ys.push_back(ys.empty() ? 0.0 : ys.back());

    int pointCount = static_cast<int>(xs.size());

    ImPlotSpec fillSpec;
    fillSpec.FillColor = theme.histogramFill;
    fillSpec.Flags = ImPlotStairsFlags_Shaded;
    ImPlot::PlotStairs("##data_fill", xs.data(), ys.data(), pointCount, fillSpec);

    ImPlotSpec lineSpec;
    lineSpec.LineColor = theme.histogramLine;
    lineSpec.LineWeight = 1.2f;
    ImPlot::PlotStairs("##data_line", xs.data(), ys.data(), pointCount, lineSpec);
}

void PlotPanel::DrawRangeTools(FitModel& model, const HistogramData& histogram, const Theme& theme)
{
    if (model.range.max <= model.range.min)
    {
        return;
    }

    // A soft shade over the fit region.
    ImPlotRect limits = ImPlot::GetPlotLimits();
    ImVec2 topLeft = ImPlot::PlotToPixels(model.range.min, limits.Y.Max);
    ImVec2 bottomRight = ImPlot::PlotToPixels(model.range.max, limits.Y.Min);
    ImVec4 shade = theme.accent;
    shade.w = 0.05f;
    ImPlot::PushPlotClipRect();
    ImPlot::GetPlotDrawList()->AddRectFilled(topLeft, bottomRight,
                                             ImGui::ColorConvertFloat4ToU32(shade));
    ImPlot::PopPlotClipRect();

    // Draggable edges, snapping to bin edges like every other range edit.
    ImVec4 edgeColor = theme.accent;
    edgeColor.w = 0.6f;
    bool moved = false;
    moved |= ImPlot::DragLineX(9001, &model.range.min, edgeColor, 1.5f);
    moved |= ImPlot::DragLineX(9002, &model.range.max, edgeColor, 1.5f);
    if (moved)
    {
        if (model.range.max < model.range.min)
        {
            std::swap(model.range.min, model.range.max);
        }
        model.range = SnapRangeToBinEdges(histogram, model.range);
    }
}

void PlotPanel::DrawModelCurves(const FitModel& model, const HistogramData& histogram,
                                const Theme& theme)
{
    if (model.peaks.empty() && model.background.empty())
    {
        return;
    }
    if (model.range.max <= model.range.min)
    {
        return;
    }

    // The histogram converts the model's densities into counts per bin, so
    // the curves overlay the data in the same units.
    FitCurves curves = SampleModelCurves(model, 400, &histogram);
    if (curves.x.empty())
    {
        return;
    }

    int pointCount = static_cast<int>(curves.x.size());

    // Individual components, dimmed so the data stays dominant. Curve order
    // is peaks first, then background, matching the model.
    std::vector<const FitComponent*> components;
    for (const FitComponent& peak : model.peaks)
    {
        components.push_back(&peak);
    }
    for (const FitComponent& background : model.background)
    {
        components.push_back(&background);
    }

    for (size_t i = 0; i < components.size() && i < curves.components.size(); ++i)
    {
        ImVec4 color = theme.ComponentColor(i);
        color.w = 0.75f;

        ImPlotSpec spec;
        spec.LineColor = color;
        spec.LineWeight = 1.5f;
        ImPlot::PlotLine(components[i]->label.c_str(), curves.x.data(),
                         curves.components[i].data(), pointCount, spec);
    }

    // The total on top, in the accent color.
    ImPlotSpec totalSpec;
    totalSpec.LineColor = theme.fitCurve;
    totalSpec.LineWeight = 2.2f;
    ImPlot::PlotLine("Model", curves.x.data(), curves.total.data(), pointCount, totalSpec);
}

void PlotPanel::DrawPeakHandles(FitModel& model, const HistogramData& histogram, const Theme& theme)
{
    for (size_t i = 0; i < model.peaks.size(); ++i)
    {
        FitComponent& peak = model.peaks[i];
        FitParameter* mean = MeanParameter(peak);
        if (mean == nullptr)
        {
            continue;
        }

        ImVec4 color = theme.ComponentColor(i);
        double binWidth = BinWidthAt(histogram, mean->value);
        int baseId = 100 + static_cast<int>(i) * 10;

        // The apex: drag horizontally to move the mean, vertically to set
        // the height.
        double apexX = mean->value;
        double apexY = peak.amplitude.value * binWidth;
        if (ImPlot::DragPoint(baseId, &apexX, &apexY, color, 6.0f))
        {
            mean->value = apexX;
            if (apexY > 0.0)
            {
                peak.amplitude.value = apexY / binWidth;
            }
        }

        // Two half-maximum handles: drag horizontally to set the width.
        // The tailed gaussian is asymmetric, so its sides are located
        // numerically and a drag scales sigma proportionally.
        bool asymmetric = peak.shape == ShapeKind::GaussianTail;
        double leftWidth = asymmetric ? NumericHalfWidth(peak, model.range, mean->value, -1.0)
                                      : HalfWidthAtHalfMax(peak);
        double rightWidth = asymmetric ? NumericHalfWidth(peak, model.range, mean->value, +1.0)
                                       : leftWidth;
        if (leftWidth > 0.0 && rightWidth > 0.0)
        {
            double halfY = 0.5 * peak.amplitude.value * binWidth;

            auto scaleSigma = [&peak](double factor) {
                if (peak.parameters.size() > 1 && factor > 0.0)
                {
                    peak.parameters[1].value *= factor;
                }
            };

            double leftX = mean->value - leftWidth;
            double leftY = halfY;
            if (ImPlot::DragPoint(baseId + 1, &leftX, &leftY, color, 4.0f))
            {
                double dragged = std::abs(mean->value - leftX);
                if (asymmetric)
                {
                    scaleSigma(dragged / leftWidth);
                }
                else
                {
                    SetHalfWidthAtHalfMax(peak, dragged);
                }
            }

            double rightX = mean->value + rightWidth;
            double rightY = halfY;
            if (ImPlot::DragPoint(baseId + 2, &rightX, &rightY, color, 4.0f))
            {
                double dragged = std::abs(rightX - mean->value);
                if (asymmetric)
                {
                    scaleSigma(dragged / rightWidth);
                }
                else
                {
                    SetHalfWidthAtHalfMax(peak, dragged);
                }
            }
        }
    }
}

void PlotPanel::DrawBackgroundHandles(FitModel& model, const HistogramData& histogram,
                                      const Theme& theme)
{
    if (model.range.max <= model.range.min)
    {
        return;
    }
    double pivot = RangePivot(model.range);

    for (size_t i = 0; i < model.background.size(); ++i)
    {
        FitComponent& background = model.background[i];
        ImVec4 color = theme.ComponentColor(model.peaks.size() + i);
        double binWidth = BinWidthAt(histogram, pivot);
        int baseId = 500 + static_cast<int>(i) * 10;

        // The level handle, at the pivot: drag vertically.
        double levelX = pivot;
        double levelY = background.amplitude.value * binWidth;
        if (ImPlot::DragPoint(baseId, &levelX, &levelY, color, 5.0f))
        {
            if (levelY > 0.0)
            {
                background.amplitude.value = levelY / binWidth;
            }
        }

        // A second handle right of the pivot tilts the shape, when it has
        // a slope to tilt.
        if (background.parameters.empty() || background.parameters[0].name != "slope")
        {
            continue;
        }
        FitParameter& slope = background.parameters[0];

        double offset = 0.3 * (model.range.max - model.range.min);
        double tiltX = pivot + offset;
        double tiltY = ComponentDensity(background, model.range, tiltX) * binWidth;
        if (ImPlot::DragPoint(baseId + 1, &tiltX, &tiltY, color, 4.0f))
        {
            double level = background.amplitude.value;
            if (level > 0.0 && tiltY > 0.0)
            {
                double ratio = tiltY / (binWidth * level);
                switch (background.shape)
                {
                    case ShapeKind::Linear:
                        slope.value = (ratio - 1.0) / offset;
                        break;
                    case ShapeKind::Quadratic:
                    {
                        double curvature = background.parameters.size() > 1
                                               ? background.parameters[1].value
                                               : 0.0;
                        slope.value = (ratio - 1.0 - curvature * offset * offset) / offset;
                        break;
                    }
                    case ShapeKind::Exponential:
                        slope.value = std::log(ratio) / offset;
                        break;
                    default:
                        break;
                }
            }
        }
    }
}

void PlotPanel::HandleAddPeakClick(PlotAction& action)
{
    // Escape leaves the persistent add-peak mode.
    if (m_addPeakMode && ImGui::IsKeyPressed(ImGuiKey_Escape))
    {
        m_addPeakMode = false;
    }

    bool wantAdd = m_addPeakMode || ImGui::IsKeyDown(ImGuiKey_P);
    if (!wantAdd || !ImPlot::IsPlotHovered())
    {
        return;
    }

    ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
    ImGui::SetTooltip(m_addPeakMode ? "click to add a peak (Esc to stop)"
                                    : "click to add a peak");
    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
    {
        action.addPeakAt = ImPlot::GetPlotMousePos().x;
    }
}

void PlotPanel::DrawContextMenu(FitModel& model, const HistogramData& histogram, PlotAction& action)
{
    if (!ImGui::BeginPopup("##plot_context"))
    {
        return;
    }

    if (ImGui::MenuItem("Add peak here"))
    {
        action.addPeakAt = m_contextMenuX;
    }
    if (ImGui::MenuItem("Set range start here"))
    {
        model.range.min = m_contextMenuX;
        if (model.range.max <= model.range.min)
        {
            model.range.max = histogram.XMax();
        }
        model.range = SnapRangeToBinEdges(histogram, model.range);
    }
    if (ImGui::MenuItem("Set range end here"))
    {
        model.range.max = m_contextMenuX;
        if (model.range.max <= model.range.min)
        {
            model.range.min = histogram.XMin();
        }
        model.range = SnapRangeToBinEdges(histogram, model.range);
    }
    ImGui::Separator();
    ImGui::MenuItem("Add peaks on click", nullptr, &m_addPeakMode);
    ImGui::MenuItem("Log scale Y", nullptr, &m_logScaleY);
    if (ImGui::MenuItem("Autoscale axes"))
    {
        m_requestAxesFit = true;
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Fit", "F"))
    {
        action.fitRequested = true;
    }

    ImGui::Separator();
    ImGui::TextDisabled("right-drag: box zoom (Shift: x only)");
    ImGui::TextDisabled("scroll: zoom   double-click: autoscale");
    ImGui::TextDisabled("hold P + click: add peak");

    ImGui::EndPopup();
}

} // namespace giggle
