#ifndef GIGGLE_UI_APP_H
#define GIGGLE_UI_APP_H

#include <functional>
#include <future>
#include <memory>
#include <optional>
#include <string>

#include "core/FitEngine.h"
#include "core/FitModel.h"
#include "core/FitResult.h"
#include "core/HistogramData.h"
#include "core/SpectrumSource.h"
#include "Fonts.h"
#include "Theme.h"
#include "panels/FileTreePanel.h"
#include "panels/FitModelPanel.h"
#include "panels/PlotPanel.h"
#include "panels/ResultsPanel.h"

struct GLFWwindow;

namespace giggle {

// Creates a SpectrumSource for a file path. main.cpp injects the ROOT
// implementation here, so the UI never depends on ROOT.
using SourceFactory = std::function<std::unique_ptr<SpectrumSource>(const std::string& filePath)>;

// The application shell: owns the window, the GUI contexts, the open file,
// and the panels, and runs the main loop.
class App
{
public:
    App(SourceFactory openSource, std::unique_ptr<FitEngine> fitEngine);

    // Makes the app open this file right after starting (command line use).
    void OpenFileOnStartup(const std::string& filePath);

    // Runs the application. Returns the process exit code.
    int Run();

private:
    bool Init();
    void DrawFrame();
    void DrawMainMenu();
    void DrawErrorPopup();
    void BuildDefaultLayout(unsigned int dockspaceId);
    void Shutdown();

    void OpenFileDialog();
    void OpenFile(const std::string& filePath);
    void LoadHistogram(const std::string& path);

    void StartFit();
    void PollFit();          // collects a finished fit, applies it to the model
    bool FitRunning() const { return m_fitFuture.valid(); }

    GLFWwindow* m_window = nullptr;
    Theme m_theme;
    Fonts m_fonts;

    // True until the default panel layout has been applied. Only used when
    // no saved layout (imgui.ini) exists.
    bool m_needDefaultLayout = false;

    SourceFactory m_openSource;
    std::string m_startupFile;

    std::unique_ptr<SpectrumSource> m_source;
    std::string m_selectedHistogram;
    std::optional<HistogramData> m_histogram;

    // The fit being built. The fit panel edits it and the plot draws it;
    // both see this one instance.
    FitModel m_model;

    // Fitting runs on a worker thread so the UI stays responsive. The
    // future holds the running fit; the snapshot allows "revert to
    // pre-fit".
    std::unique_ptr<FitEngine> m_fitEngine;
    std::future<FitResult> m_fitFuture;
    std::optional<FitResult> m_fitResult;
    std::optional<FitModel> m_preFitModel;

    std::string m_errorMessage; // non-empty: the error popup is shown

    FileTreePanel m_fileTreePanel;
    PlotPanel m_plotPanel;
    FitModelPanel m_fitModelPanel;
    ResultsPanel m_resultsPanel;
};

} // namespace giggle

#endif
