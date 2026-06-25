#ifndef GIGGLE_UI_WIDGETS_H
#define GIGGLE_UI_WIDGETS_H

#include "imgui.h"

#include "ui/Theme.h"

namespace giggle {

// Small shared widgets that give the panels a visual hierarchy: section
// headers are drawn in the accent color on a faint accent band, so they
// stand out from the body text and numbers below them.

// A collapsing section header (Fit Range, Peaks, ...). Returns true when open.
inline bool SectionHeader(const char* label, const Theme& theme, ImGuiTreeNodeFlags flags = 0)
{
    const ImVec4& a = theme.accent;
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(a.x, a.y, a.z, 0.14f));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(a.x, a.y, a.z, 0.24f));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(a.x, a.y, a.z, 0.32f));
    ImGui::PushStyleColor(ImGuiCol_Text, a);
    bool open = ImGui::CollapsingHeader(label, flags);
    ImGui::PopStyleColor(4);
    return open;
}

// A sub-section label with a separator line (a peak's name, ...).
inline void SubHeader(const char* label, const Theme& theme)
{
    ImGui::PushStyleColor(ImGuiCol_Text, theme.accent);
    ImGui::SeparatorText(label);
    ImGui::PopStyleColor();
}

// An accent-colored tree node for a nested sub-section (Advanced, ...). The
// accent applies only to the label; the body below renders normally.
inline bool SubTree(const char* label, const Theme& theme)
{
    ImGui::PushStyleColor(ImGuiCol_Text, theme.accent);
    bool open = ImGui::TreeNode(label);
    ImGui::PopStyleColor();
    return open;
}

} // namespace giggle

#endif
