#include "PlotPanel.h"

#include <vector>

#include "imgui.h"
#include "implot.h"

namespace giggle {

void PlotPanel::Draw(const HistogramData* histogram, const Theme& theme, ImFont* monoFont)
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
                // Steps need one y value per edge; the last value is
                // repeated so the final bin is drawn to its right edge.
                const std::vector<double>& xs = histogram->binEdges;
                std::vector<double> ys = histogram->counts;
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

            ImPlot::EndPlot();
        }
        ImGui::PopFont();
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace giggle
