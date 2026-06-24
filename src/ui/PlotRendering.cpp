#include "PlotRendering.h"

#include <vector>

#include "imgui.h"
#include "implot.h"

#include "core/Shapes.h"

namespace giggle {

void RenderHistogramStairs(const HistogramData& histogram, const Theme& theme, float emphasis)
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
    lineSpec.LineWeight = 1.2f * emphasis;
    ImPlot::PlotStairs("##data_line", xs.data(), ys.data(), pointCount, lineSpec);
}

void RenderRangeShade(const FitRange& range, const Theme& theme)
{
    if (range.max <= range.min)
    {
        return;
    }
    ImPlotRect limits = ImPlot::GetPlotLimits();
    ImVec2 topLeft = ImPlot::PlotToPixels(range.min, limits.Y.Max);
    ImVec2 bottomRight = ImPlot::PlotToPixels(range.max, limits.Y.Min);
    ImVec4 shade = theme.accent;
    shade.w = 0.05f;
    ImPlot::PushPlotClipRect();
    ImPlot::GetPlotDrawList()->AddRectFilled(topLeft, bottomRight,
                                             ImGui::ColorConvertFloat4ToU32(shade));
    ImPlot::PopPlotClipRect();
}

void RenderRangeLines(const FitRange& range, const Theme& theme, float emphasis)
{
    if (range.max <= range.min)
    {
        return;
    }
    double edges[2] = { range.min, range.max };
    ImVec4 color = theme.accent;
    color.w = 0.6f;
    ImPlotSpec spec;
    spec.LineColor = color;
    spec.LineWeight = 1.3f * emphasis;
    ImPlot::PlotInfLines("##fit_range", edges, 2, spec);
}

void RenderModelCurves(const FitModel& model, const HistogramData& histogram,
                       const Theme& theme, float emphasis)
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
        spec.LineWeight = 1.5f * emphasis;
        ImPlot::PlotLine(components[i]->label.c_str(), curves.x.data(),
                         curves.components[i].data(), pointCount, spec);
    }

    // The total on top, in the accent color.
    ImPlotSpec totalSpec;
    totalSpec.LineColor = theme.fitCurve;
    totalSpec.LineWeight = 2.2f * emphasis;
    ImPlot::PlotLine("Model", curves.x.data(), curves.total.data(), pointCount, totalSpec);
}

} // namespace giggle
