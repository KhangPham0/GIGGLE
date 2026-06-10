#include "ResultsPanel.h"

#include "imgui.h"

namespace giggle {

void ResultsPanel::Draw()
{
    if (ImGui::Begin(Title))
    {
        ImGui::TextDisabled("No fit yet.");
    }
    ImGui::End();
}

} // namespace giggle
