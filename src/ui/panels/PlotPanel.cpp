#include "PlotPanel.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "imgui.h"
#include "implot.h"

#include "core/Shapes.h"
#include "ui/PlotRendering.h"
#include "ui/fonts/IconsFontAwesome5.h"

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
        case ShapeKind::CrystalBall:
            return 1.177410023 * std::abs(peak.parameters[1].value); // sqrt(2 ln 2) sigma (core)
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
        case ShapeKind::CrystalBall:
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

PlotAction PlotPanel::Draw(const HistogramData* histogram, FitModel* model, bool& showFit,
                           const Theme& theme, ImFont* monoFont)
{
    PlotAction action;

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(Title))
    {
        ImVec2 windowPos = ImGui::GetWindowPos();
        ImVec2 windowSize = ImGui::GetWindowSize();
        m_windowRect = { windowPos.x, windowPos.y, windowSize.x, windowSize.y };

        if (histogram != nullptr && histogram->name != m_lastDrawnName)
        {
            m_requestAxesFit = true;
            m_lastDrawnName = histogram->name;

            // The data's vertical extent, found once per histogram so the
            // axis constraints below cost nothing per frame.
            m_dataYMin = 0.0;
            m_dataYMax = 0.0;
            if (!histogram->counts.empty())
            {
                m_dataYMin = m_dataYMax = histogram->counts.front();
                for (double count : histogram->counts)
                {
                    m_dataYMin = std::min(m_dataYMin, count);
                    m_dataYMax = std::max(m_dataYMax, count);
                }
            }
        }
        if (m_requestAxesFit)
        {
            ImPlot::SetNextAxesToFit();
            m_requestAxesFit = false;
        }

        // "###" keeps the plot's ID stable while the visible title changes.
        std::string plotTitle = (histogram != nullptr ? histogram->name : "") + "###spectrum";

        ImGui::PushFont(monoFont, 15.0f);
        if (ImPlot::BeginPlot(plotTitle.c_str(), ImVec2(-1.0f, -1.0f), ImPlotFlags_NoMenus))
        {
            ImPlot::SetupAxes(nullptr, "counts");
            ImPlot::SetupAxisScale(ImAxis_Y1, m_logScaleY ? ImPlotScale_Log10 : ImPlotScale_Linear);

            // Bound panning to the data plus a margin, so the view can't
            // drift off into empty space on any side. The y-floor sits at 0
            // for ordinary counts; only background-subtracted spectra (which
            // go negative) get a margin below.
            if (histogram != nullptr)
            {
                double xSpan = histogram->XMax() - histogram->XMin();
                double xPad = xSpan > 0.0 ? 0.04 * xSpan : 1.0;
                ImPlot::SetupAxisLimitsConstraints(ImAxis_X1, histogram->XMin() - xPad,
                                                   histogram->XMax() + xPad);

                if (!m_logScaleY)
                {
                    bool nonNegative = m_dataYMin >= 0.0;
                    double yFloor = nonNegative ? 0.0 : m_dataYMin;
                    double yPad = m_dataYMax > yFloor ? 0.10 * (m_dataYMax - yFloor) : 1.0;
                    double yLow = nonNegative ? 0.0 : m_dataYMin - yPad;
                    ImPlot::SetupAxisLimitsConstraints(ImAxis_Y1, yLow, m_dataYMax + yPad);
                }
            }

            ImPlotRect visible = ImPlot::GetPlotLimits();
            m_viewLimits[0] = visible.X.Min;
            m_viewLimits[1] = visible.X.Max;
            m_viewLimits[2] = visible.Y.Min;
            m_viewLimits[3] = visible.Y.Max;

            if (histogram != nullptr)
            {
                DrawHistogram(*histogram, theme);
                if (m_binInspector)
                {
                    DrawBinInspector(*histogram, theme, monoFont);
                }
            }
            if (model != nullptr && histogram != nullptr)
            {
                // The fit overlay only shows when the tools are on; clicking
                // to add a peak still works and turns them on (in the App).
                if (showFit)
                {
                    DrawRangeTools(*model, *histogram, theme);
                    DrawModelCurves(*model, *histogram, theme);
                    DrawPeakHandles(*model, *histogram, theme);
                    DrawBackgroundHandles(*model, *histogram, theme);
                }
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

            // A floating, always-visible toggle in the plot's top-left
            // corner, so the fit tools are discoverable without the menus.
            if (histogram != nullptr)
            {
                DrawFitToolsButton(showFit, theme);
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
            DrawContextMenu(*model, *histogram, showFit, action);
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();

    return action;
}

void PlotPanel::DrawHistogram(const HistogramData& histogram, const Theme& theme)
{
    RenderHistogramStairs(histogram, theme);
}

// The fit-tools toggle, floated over the plot's top-left corner. Accent
// colored while on. Drawn inside the plot so it never shrinks it.
void PlotPanel::DrawFitToolsButton(bool& showFit, const Theme& theme)
{
    ImVec2 corner = ImPlot::GetPlotPos();
    ImGui::SetCursorScreenPos(ImVec2(corner.x + 10.0f, corner.y + 10.0f));

    // Capture the state before the button: it toggles showFit, so the pop
    // count must be decided from the value at push time, not after.
    bool active = showFit;
    if (active)
    {
        ImGui::PushStyleColor(ImGuiCol_Button, theme.accent);
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, theme.accentHover);
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, theme.accentActive);
        ImGui::PushStyleColor(ImGuiCol_Text, theme.windowBackground);
    }
    if (ImGui::Button(ICON_FA_CHART_LINE "  Fit tools"))
    {
        showFit = !showFit;
    }
    if (active)
    {
        ImGui::PopStyleColor(4);
    }
    if (ImGui::IsItemHovered())
    {
        ImGui::SetTooltip(active
                              ? "Fit tools on. Click for a clean view (and clean exports)."
                              : "Click to start fitting: add peaks, set the range, run the fit.");
    }
}

// Hovering highlights the bin under the cursor and reads out its contents:
// the spectroscopist's "what exactly is in this channel".
void PlotPanel::DrawBinInspector(const HistogramData& histogram, const Theme& theme,
                                 ImFont* monoFont)
{
    if (!ImPlot::IsPlotHovered())
    {
        return;
    }
    ImPlotPoint mouse = ImPlot::GetPlotMousePos();

    const std::vector<double>& edges = histogram.binEdges;
    auto above = std::upper_bound(edges.begin(), edges.end(), mouse.x);
    if (above == edges.begin() || above == edges.end())
    {
        return;
    }
    int bin = static_cast<int>(above - edges.begin()) - 1;
    double low = edges[bin];
    double high = edges[bin + 1];
    double counts = histogram.counts[bin];

    ImVec4 fill = theme.accent;
    fill.w = 0.18f;
    ImVec4 outline = theme.accent;
    outline.w = 0.55f;
    ImPlot::PushPlotClipRect();
    ImVec2 top = ImPlot::PlotToPixels(low, counts);
    ImVec2 bottom = ImPlot::PlotToPixels(high, 0.0);
    ImPlot::GetPlotDrawList()->AddRectFilled(top, bottom, ImGui::ColorConvertFloat4ToU32(fill));
    ImPlot::GetPlotDrawList()->AddRect(top, bottom, ImGui::ColorConvertFloat4ToU32(outline));
    ImPlot::PopPlotClipRect();

    ImGui::PushFont(monoFont, 13.0f);
    ImGui::SetTooltip("bin    %d\nrange  [%g, %g)\ncounts %g  +- %g",
                      bin + 1, low, high, counts, histogram.BinError(bin));
    ImGui::PopFont();
}

void PlotPanel::DrawRangeTools(FitModel& model, const HistogramData& histogram, const Theme& theme)
{
    if (model.range.max <= model.range.min)
    {
        return;
    }

    RenderRangeShade(model.range, theme);

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
    RenderModelCurves(model, histogram, theme);
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
        bool asymmetric = peak.shape == ShapeKind::GaussianTail || peak.shape == ShapeKind::Landau;
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

void PlotPanel::DrawContextMenu(FitModel& model, const HistogramData& histogram, bool& showFit,
                                PlotAction& action)
{
    if (!ImGui::BeginPopup("##plot_context"))
    {
        return;
    }

    ImGui::MenuItem("Fit tools", nullptr, &showFit);
    ImGui::Separator();

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
        showFit = true;
    }
    if (ImGui::MenuItem("Set range end here"))
    {
        model.range.max = m_contextMenuX;
        if (model.range.max <= model.range.min)
        {
            model.range.min = histogram.XMin();
        }
        model.range = SnapRangeToBinEdges(histogram, model.range);
        showFit = true;
    }
    ImGui::Separator();
    ImGui::MenuItem("Add peaks on click", nullptr, &m_addPeakMode);
    ImGui::MenuItem("Bin inspector", nullptr, &m_binInspector);
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
    if (ImGui::MenuItem("Save plot as PNG..."))
    {
        action.savePlotRequested = true;
    }

    ImGui::Separator();
    ImGui::TextDisabled("right-drag: box zoom (Shift: x only)");
    ImGui::TextDisabled("scroll: zoom   double-click: autoscale");
    ImGui::TextDisabled("hold P + click: add peak");

    ImGui::EndPopup();
}

} // namespace giggle
