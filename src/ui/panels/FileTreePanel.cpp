#include "FileTreePanel.h"

#include "imgui.h"

namespace giggle {

void FileTreePanel::Draw()
{
    if (ImGui::Begin(Title))
    {
        ImGui::TextDisabled("No file open.");
    }
    ImGui::End();
}

} // namespace giggle
