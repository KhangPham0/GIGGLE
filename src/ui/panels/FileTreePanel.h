#ifndef GIGGLE_UI_PANELS_FILE_TREE_PANEL_H
#define GIGGLE_UI_PANELS_FILE_TREE_PANEL_H

#include <map>
#include <optional>
#include <string>
#include <vector>

#include "core/SpectrumSource.h"
#include "ui/Theme.h"

namespace giggle {

// What the user did in the Files panel this frame.
struct FileTreeAction
{
    bool openFileRequested = false;
    std::optional<std::string> histogramClicked; // path of the clicked histogram
};

// Left panel: the open ROOT file and its tree of histograms.
class FileTreePanel
{
public:
    static constexpr const char* Title = "Files";

    // Called when a file is opened; rebuilds the directory tree.
    void SetContents(const std::string& fileName, const std::vector<HistogramInfo>& histograms);

    FileTreeAction Draw(const std::string& selectedPath, const Theme& theme);

private:
    // Histograms arranged by directory. Map keeps directories sorted by name.
    struct TreeNode
    {
        std::map<std::string, TreeNode> directories;
        std::vector<HistogramInfo> entries; // histograms directly in this node
    };

    void DrawNode(const TreeNode& node, const std::string& selectedPath, FileTreeAction& action);

    bool m_hasFile = false;
    std::string m_fileName;
    TreeNode m_root;
};

} // namespace giggle

#endif
