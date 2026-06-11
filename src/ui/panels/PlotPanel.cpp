#include "PlotPanel.h"

#include <vector>

#include "imgui.h"
#include "implot.h"

#include "core/Shapes.h"

namespace giggle {

void PlotPanel::Draw(const HistogramData* histogram, const FitModel* model,
                     const Theme& theme, ImFont* monoFont)
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(Title))
    {
        if (histogram != nullptr && histogram->name != m_lastDrawnName)
        {
            ImPlot::SetNextAxesToFit();
            m_lastDrawnName = histogram->name;
        }

        // "###" keeps the plot's ID stable while the visible title changes.
        std::string plotTitle = (histogram != nullptr ? histogram->name : "") + "###spectrum";

        ImGui::PushFont(monoFont, 13.0f);
        if (ImPlot::BeginPlot(plotTitle.c_str(), ImVec2(-1.0f, -1.0f)))
        {
            ImPlot::SetupAxes(nullptr, "counts");

            if (histogram != nullptr)
            {
                DrawHistogram(*histogram, theme);
            }
            if (model != nullptr && histogram != nullptr)
            {
                DrawModelOverlay(*model, *histogram, theme);
            }

            ImPlot::EndPlot();
        }
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar();
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

void PlotPanel::DrawModelOverlay(const FitModel& model, const HistogramData& histogram,
                                 const Theme& theme)
{
    bool hasComponents = !model.peaks.empty() || !model.background.empty();
    bool hasRange = model.range.max > model.range.min;
    if (!hasRange)
    {
        return;
    }

    // The fit range edges.
    double rangeEdges[2] = { model.range.min, model.range.max };
    ImPlotSpec rangeSpec;
    rangeSpec.LineColor = ImVec4(theme.accent.x, theme.accent.y, theme.accent.z, 0.45f);
    ImPlot::PlotInfLines("##fit_range", rangeEdges, 2, rangeSpec);

    if (!hasComponents)
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

} // namespace giggle
