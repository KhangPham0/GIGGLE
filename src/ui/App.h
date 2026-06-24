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
#include "core/FormulaValidator.h"
#include "core/HistogramData.h"
#include "core/SpectrumSource.h"
#include "ui/PlotExport.h"
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
    App(SourceFactory openSource, std::unique_ptr<FitEngine> fitEngine,
        FormulaValidator formulaValidator);

    // Makes the app open this file right after starting (command line use).
    void OpenFileOnStartup(const std::string& filePath);

    // Command line mode: render this histogram to a PNG and exit.
    void ExportOnStartup(const std::string& histogram, const std::string& pngPath);

    // Runs the application. Returns the process exit code.
    int Run();

private:
    bool Init();
    void DrawFrame();
    void DrawMainMenu();
    void HandleShortcuts();
    void ChangeFontScale(int direction);
    void AddPeakAt(double x);
    void DrawAboutWindow();
    void DrawErrorPopup();
    void BuildDefaultLayout(unsigned int dockspaceId);
    // Window geometry persistence: first run opens at a default size, later
    // runs restore the size and place the window was left at.
    void LoadWindowState(int& width, int& height, int& posX, int& posY);
    void SaveWindowState();
    void Shutdown();

    void OpenFileDialog();
    void OpenFile(const std::string& filePath);
    void LoadHistogram(const std::string& path);

    void StartFit();
    void PollFit();          // collects a finished fit, applies it to the model
    bool FitRunning() const { return m_fitFuture.valid(); }

    void SaveResults(bool asCsv);
    void CopyResultsCsv();
    void SavePreset();
    void LoadPreset();
    void OpenExportDialog();
    void DrawExportDialog();
    void PerformPlotExport(); // runs between frames

    GLFWwindow* m_window = nullptr;
    Theme m_theme;
    Fonts m_fonts;

    // True until the default panel layout has been applied. Only used when
    // no saved layout (imgui.ini) exists.
    bool m_needDefaultLayout = false;

    // The layout file lives next to the executable: GIGGLE keeps its files
    // with itself instead of scattering them over the user's machine.
    std::string m_layoutFilePath;
    std::string m_windowStateFilePath;

    // Panel visibility; a hidden panel's dock space is reclaimed by the
    // plot until it returns.
    bool m_showFileTree = true;
    bool m_showFitModel = true;
    bool m_showResults = true;
    bool m_showAbout = false;

    SourceFactory m_openSource;
    FormulaValidator m_formulaValidator;
    std::string m_startupFile;

    std::unique_ptr<SpectrumSource> m_source;
    std::string m_sourceFilePath; // full path of the open file, for provenance
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

    // The model exactly as it was fitted, frozen for export even if the
    // user keeps editing afterwards.
    std::optional<FitModel> m_fittedModel;

    std::string m_errorMessage; // non-empty: the error popup is shown

    // The export dialog and a pending offscreen export (path empty: none).
    bool m_showExportDialog = false;
    PlotExportOptions m_exportOptions;
    std::string m_pendingExportPath;

    // Optional command line mode: export one histogram and exit.
    std::string m_startupExportHistogram;
    std::string m_startupExportPath;
    int m_startupExportSettleFrames = 0;

    FileTreePanel m_fileTreePanel;
    PlotPanel m_plotPanel;
    FitModelPanel m_fitModelPanel;
    ResultsPanel m_resultsPanel;
};

} // namespace giggle

#endif
