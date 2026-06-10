#ifndef GIGGLE_UI_APP_H
#define GIGGLE_UI_APP_H

#include "Fonts.h"
#include "Theme.h"
#include "panels/FileTreePanel.h"
#include "panels/FitModelPanel.h"
#include "panels/PlotPanel.h"
#include "panels/ResultsPanel.h"

struct GLFWwindow;

namespace giggle {

// The application shell: owns the window, the GUI contexts, and the panels,
// and runs the main loop.
class App
{
public:
    // Runs the application. Returns the process exit code.
    int Run();

private:
    bool Init();
    void DrawFrame();
    void BuildDefaultLayout(unsigned int dockspaceId);
    void Shutdown();

    GLFWwindow* m_window = nullptr;
    Theme m_theme;
    Fonts m_fonts;

    // True until the default panel layout has been applied. Only used when
    // no saved layout (imgui.ini) exists.
    bool m_needDefaultLayout = false;

    FileTreePanel m_fileTreePanel;
    PlotPanel m_plotPanel;
    FitModelPanel m_fitModelPanel;
    ResultsPanel m_resultsPanel;
};

} // namespace giggle

#endif
