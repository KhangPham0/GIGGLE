#ifndef GIGGLE_UI_PANELS_FILE_TREE_PANEL_H
#define GIGGLE_UI_PANELS_FILE_TREE_PANEL_H

namespace giggle {

// Left panel: the open ROOT file and its tree of histograms.
class FileTreePanel
{
public:
    static constexpr const char* Title = "Files";

    void Draw();
};

} // namespace giggle

#endif
