#include "PlotPanel.h"

#include "imgui.h"
#include "implot.h"

namespace giggle {

void PlotPanel::Draw()
{
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (ImGui::Begin(Title))
    {
        if (ImPlot::BeginPlot("##spectrum", ImVec2(-1.0f, -1.0f)))
        {
            ImPlot::SetupAxes("channel", "counts");
            ImPlot::EndPlot();
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace giggle
