#include "FileTreePanel.h"

#include "imgui.h"

namespace giggle {

namespace {

// "spectra/h_ex" -> "h_ex"
std::string LeafName(const std::string& path)
{
    size_t lastSlash = path.rfind('/');
    return lastSlash == std::string::npos ? path : path.substr(lastSlash + 1);
}

} // namespace

void FileTreePanel::SetContents(const std::string& fileName, const std::vector<HistogramInfo>& histograms)
{
    m_hasFile = true;
    m_fileName = fileName;
    m_root = TreeNode{};

    for (const HistogramInfo& info : histograms)
    {
        // Walk the directory parts of the path, creating nodes as needed,
        // and place the histogram in the final node.
        TreeNode* node = &m_root;
        size_t start = 0;
        size_t slash = info.path.find('/');
        while (slash != std::string::npos)
        {
            node = &node->directories[info.path.substr(start, slash - start)];
            start = slash + 1;
            slash = info.path.find('/', start);
        }
        node->entries.push_back(info);
    }
}

FileTreeAction FileTreePanel::Draw(const std::string& selectedPath)
{
    FileTreeAction action;

    if (ImGui::Begin(Title))
    {
        if (!m_hasFile)
        {
            ImGui::TextDisabled("No file open.");
            ImGui::Spacing();
            if (ImGui::Button("Open File..."))
            {
                action.openFileRequested = true;
            }
        }
        else
        {
            ImGui::TextWrapped("%s", m_fileName.c_str());
            ImGui::Separator();
            DrawNode(m_root, selectedPath, action);
        }
    }
    ImGui::End();

    return action;
}

void FileTreePanel::DrawNode(const TreeNode& node, const std::string& selectedPath, FileTreeAction& action)
{
    for (const auto& [name, child] : node.directories)
    {
        if (ImGui::TreeNodeEx(name.c_str(), ImGuiTreeNodeFlags_DefaultOpen | ImGuiTreeNodeFlags_SpanAvailWidth))
        {
            DrawNode(child, selectedPath, action);
            ImGui::TreePop();
        }
    }

    for (const HistogramInfo& info : node.entries)
    {
        std::string displayName = LeafName(info.path);
        bool selected = info.path == selectedPath;
        if (ImGui::Selectable(displayName.c_str(), selected))
        {
            action.histogramClicked = info.path;
        }
        if (ImGui::IsItemHovered() && info.title != displayName && !info.title.empty())
        {
            ImGui::SetTooltip("%s", info.title.c_str());
        }
    }
}

} // namespace giggle
