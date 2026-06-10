#include "FitModelPanel.h"

#include "imgui.h"

namespace giggle {

void FitModelPanel::Draw()
{
    if (ImGui::Begin(Title))
    {
        if (ImGui::CollapsingHeader("Peaks", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("No peaks.");
        }
        if (ImGui::CollapsingHeader("Background", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::TextDisabled("None.");
        }
    }
    ImGui::End();
}

} // namespace giggle
