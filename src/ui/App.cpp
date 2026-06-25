#include "App.h"

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>

#include "core/Serialization.h"
#include "core/Shapes.h"
#include "core/Version.h"
#include "ui/fonts/IconsFontAwesome5.h"

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API, used for the default layout
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "nfd.h"

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

// The shortcut labels match the platform's modifier key. ImGui already
// maps the actual behavior (ConfigMacOSXBehaviors); this fixes the labels.
#ifdef __APPLE__
#define GIGGLE_MOD "Cmd"
#else
#define GIGGLE_MOD "Ctrl"
#endif

namespace giggle {

static void GlfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

// The directory holding the running executable; the current directory as
// a fallback. Keeps GIGGLE's own files (the window layout) next to the
// binary, wherever it is launched from.
static std::filesystem::path ExecutableDirectory()
{
#ifdef __APPLE__
    char buffer[4096];
    uint32_t size = sizeof(buffer);
    if (_NSGetExecutablePath(buffer, &size) == 0)
    {
        return std::filesystem::weakly_canonical(buffer).parent_path();
    }
#else
    std::error_code error;
    std::filesystem::path self = std::filesystem::read_symlink("/proc/self/exe", error);
    if (!error)
    {
        return self.parent_path();
    }
#endif
    return std::filesystem::current_path();
}

App::App(SourceFactory openSource, std::unique_ptr<FitEngine> fitEngine,
         FormulaValidator formulaValidator)
    : m_openSource(std::move(openSource)),
      m_formulaValidator(std::move(formulaValidator)),
      m_fitEngine(std::move(fitEngine))
{
}

void App::OpenFileOnStartup(const std::string& filePath)
{
    m_startupFile = filePath;
}

void App::ExportOnStartup(const std::string& histogram, const std::string& pngPath)
{
    m_startupExportHistogram = histogram;
    m_startupExportPath = pngPath;
}

int App::Run()
{
    if (!Init())
    {
        return 1;
    }

    if (!m_startupFile.empty())
    {
        OpenFile(m_startupFile);
    }

    while (!glfwWindowShouldClose(m_window))
    {
        glfwPollEvents();

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();

        DrawFrame();

        ImGui::Render();
        int width = 0;
        int height = 0;
        glfwGetFramebufferSize(m_window, &width, &height);
        glViewport(0, 0, width, height);
        const ImVec4& clear = m_theme.windowBackground;
        glClearColor(clear.x, clear.y, clear.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        glfwSwapBuffers(m_window);

        if (!m_startupExportHistogram.empty())
        {
            LoadHistogram(m_startupExportHistogram);
            m_startupExportHistogram.clear();
            m_startupExportSettleFrames = 2; // axis fitting settles at frame end
            continue;
        }
        if (!m_startupExportPath.empty() && m_startupExportSettleFrames-- <= 0)
        {
            m_pendingExportPath = m_startupExportPath;
            m_startupExportPath.clear();
            m_exportOptions.width = 1920;
            m_exportOptions.height = 1080;
            glfwSetWindowShouldClose(m_window, GLFW_TRUE);
        }
        if (!m_pendingExportPath.empty())
        {
            PerformPlotExport();
        }
    }

    SaveWindowState();
    Shutdown();
    return 0;
}

bool App::Init()
{
    glfwSetErrorCallback(GlfwErrorCallback);
    if (!glfwInit())
    {
        return false;
    }

    // OpenGL 3.2 core profile: the newest version macOS still supports.
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 2);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);

    // First run: 80% of the monitor's work area (the screen minus the menu
    // bar and dock), centered, so all four panels have room. Later runs:
    // the size and place the window was left at. Stay hidden until
    // positioned to avoid a jump. The work area is already in screen
    // coordinates, so no DPI scaling is applied here.
    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
    float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(monitor);
    int workX = 0;
    int workY = 0;
    int workWidth = 1280;
    int workHeight = 800;
    glfwGetMonitorWorkarea(monitor, &workX, &workY, &workWidth, &workHeight);

    int width = static_cast<int>(workWidth * 0.8f);
    int height = static_cast<int>(workHeight * 0.8f);
    int posX = workX + (workWidth - width) / 2;
    int posY = workY + (workHeight - height) / 2;

    m_windowStateFilePath = (ExecutableDirectory() / "window.ini").string();
    LoadWindowState(width, height, posX, posY);

    glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    m_window = glfwCreateWindow(width, height, "GIGGLE", nullptr, nullptr);
    if (m_window == nullptr)
    {
        glfwTerminate();
        return false;
    }
    glfwSetWindowPos(m_window, posX, posY);
    glfwShowWindow(m_window);
    glfwMakeContextCurrent(m_window);
    glfwSwapInterval(1); // vsync

    if (NFD_Init() != NFD_OKAY)
    {
        std::fprintf(stderr, "could not initialize the file dialog library\n");
        glfwDestroyWindow(m_window);
        glfwTerminate();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImPlot::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    io.ConfigDpiScaleFonts = true;
    io.ConfigDpiScaleViewports = true;

    // The layout file lives next to the executable, and the default layout
    // is built only when no saved layout exists from a previous run.
    m_layoutFilePath = (ExecutableDirectory() / "imgui.ini").string();
    io.IniFilename = m_layoutFilePath.c_str();
    m_needDefaultLayout = !std::filesystem::exists(m_layoutFilePath);

    ImGuiStyle& style = ImGui::GetStyle();
    style.ScaleAllSizes(scale);
    style.FontScaleDpi = scale;

    m_theme = DarkTheme();
    ApplyTheme(m_theme);
    m_fonts = LoadFonts();

    ImGui_ImplGlfw_InitForOpenGL(m_window, true);
    ImGui_ImplOpenGL3_Init("#version 150");

    return true;
}

void App::DrawFrame()
{
    DrawMainMenu();
    HandleShortcuts();

    ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (m_needDefaultLayout)
    {
        BuildDefaultLayout(dockspaceId);
        m_needDefaultLayout = false;
    }

    PollFit();

    if (m_showFileTree)
    {
        FileTreeAction action = m_fileTreePanel.Draw(m_selectedHistogram, m_theme);
        if (action.openFileRequested)
        {
            OpenFileDialog();
        }
        if (action.histogramClicked.has_value())
        {
            LoadHistogram(action.histogramClicked.value());
        }
    }

    PlotAction plotAction = m_plotPanel.Draw(m_histogram.has_value() ? &m_histogram.value() : nullptr,
                                             &m_model, m_showFit, m_theme, m_fonts.mono);
    if (plotAction.addPeakAt.has_value())
    {
        AddPeakAt(plotAction.addPeakAt.value());
    }
    if (plotAction.fitRequested)
    {
        StartFit();
    }
    if (plotAction.savePlotRequested)
    {
        OpenExportDialog();
    }

    if (m_showFitModel)
    {
        // Adding a component from the panel (while it is open with the
        // tools off) turns the fit tools on, so the work shows on the plot.
        size_t componentsBefore = m_model.peaks.size() + m_model.background.size();

        FitPanelAction fitAction = m_fitModelPanel.Draw(m_model,
                                                        m_histogram.has_value() ? &m_histogram.value() : nullptr,
                                                        FitRunning(), m_preFitModel.has_value(),
                                                        m_theme, m_fonts.mono, m_formulaValidator);

        if (m_model.peaks.size() + m_model.background.size() > componentsBefore)
        {
            m_showFit = true;
        }
        if (fitAction.fitRequested)
        {
            StartFit();
        }
        if (fitAction.revertRequested && m_preFitModel.has_value())
        {
            m_model = m_preFitModel.value();
            m_preFitModel.reset();
            m_fitResult.reset();
            m_fittedModel.reset();
        }
        if (fitAction.savePresetRequested)
        {
            SavePreset();
        }
        if (fitAction.loadPresetRequested)
        {
            LoadPreset();
        }
    }

    if (m_showResults)
    {
        ResultsAction resultsAction =
            m_resultsPanel.Draw(m_fitResult.has_value() ? &m_fitResult.value() : nullptr,
                                m_histogram.has_value() ? &m_histogram.value() : nullptr,
                                &m_model, m_theme, m_fonts.mono);
        if (resultsAction.saveJsonRequested)
        {
            SaveResults(false);
        }
        if (resultsAction.saveCsvRequested)
        {
            SaveResults(true);
        }
        if (resultsAction.copyCsvRequested)
        {
            CopyResultsCsv();
        }
    }

    DrawExportDialog();
    DrawAboutWindow();
    DrawErrorPopup();

    // When the fit tools toggle changes, follow it with the fit panel:
    // turning the tools off hides the panel, turning them on shows it. The
    // panel can still be toggled on its own (View menu / Ctrl+J) without
    // changing the tools, since that does not move m_showFit.
    if (m_showFit != m_showFitPrev)
    {
        m_showFitModel = m_showFit;
        m_showFitPrev = m_showFit;
    }
}

void App::DrawMainMenu()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem(ICON_FA_FOLDER_OPEN "  Open..."))
            {
                OpenFileDialog();
            }
            ImGui::Separator();
            ImGui::BeginDisabled(!m_histogram.has_value());
            if (ImGui::MenuItem(ICON_FA_SAVE "  Save Fit Preset..."))
            {
                SavePreset();
            }
            if (ImGui::MenuItem(ICON_FA_FILE_IMPORT "  Load Fit Preset..."))
            {
                LoadPreset();
            }
            if (ImGui::MenuItem(ICON_FA_IMAGE "  Export Plot as PNG..."))
            {
                OpenExportDialog();
            }
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("View"))
        {
            ImGui::MenuItem("Fit tools", nullptr, &m_showFit);
            ImGui::Separator();
            ImGui::MenuItem(FileTreePanel::Title, GIGGLE_MOD "+B", &m_showFileTree);
            ImGui::MenuItem(FitModelPanel::Title, GIGGLE_MOD "+J", &m_showFitModel);
            ImGui::MenuItem(ResultsPanel::Title, nullptr, &m_showResults);
            ImGui::Separator();
            // Font scale: bigger for screen sharing, back to 1 to reset.
            if (ImGui::MenuItem("Larger text", GIGGLE_MOD "+="))
            {
                ChangeFontScale(+1);
            }
            if (ImGui::MenuItem("Smaller text", GIGGLE_MOD "+-"))
            {
                ChangeFontScale(-1);
            }
            if (ImGui::MenuItem("Reset text size", GIGGLE_MOD "+0"))
            {
                ChangeFontScale(0);
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Reset layout"))
            {
                m_showFileTree = true;
                m_showFitModel = true;
                m_showResults = true;
                m_needDefaultLayout = true;
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("About"))
        {
            if (ImGui::MenuItem(ICON_FA_INFO_CIRCLE "  About GIGGLE"))
            {
                m_showAbout = true;
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
}

void App::HandleShortcuts()
{
    // No shortcuts while the user is typing into a field. ImGui maps
    // Ctrl to Cmd on macOS.
    if (ImGui::GetIO().WantTextInput)
    {
        return;
    }

    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_B))
    {
        m_showFileTree = !m_showFileTree;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_J))
    {
        m_showFitModel = !m_showFitModel;
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Equal))
    {
        ChangeFontScale(+1);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_Minus))
    {
        ChangeFontScale(-1);
    }
    if (ImGui::IsKeyChordPressed(ImGuiMod_Ctrl | ImGuiKey_0))
    {
        ChangeFontScale(0);
    }
    if (ImGui::IsKeyPressed(ImGuiKey_F, false))
    {
        StartFit();
    }
}

// Scales every font in the application; fonts re-rasterize sharply at the
// new size (no blurry zoom). Direction +1/-1 steps, 0 resets.
void App::ChangeFontScale(int direction)
{
    ImGuiStyle& style = ImGui::GetStyle();
    if (direction == 0)
    {
        style.FontScaleMain = 1.0f;
        return;
    }
    float scale = style.FontScaleMain + 0.1f * direction;
    style.FontScaleMain = std::min(std::max(scale, 0.7f), 2.0f);
}

void App::AddPeakAt(double x)
{
    if (!m_histogram.has_value())
    {
        return;
    }
    FitComponent peak = SuggestGaussianPeak(m_histogram.value(), m_model.range, x);
    peak.label = NextPeakLabel(m_model.peaks);
    m_model.peaks.push_back(peak);
    m_showFit = true; // dropping a peak begins fitting
}

void App::DrawAboutWindow()
{
    if (!m_showAbout)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(440.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::Begin("About GIGGLE", &m_showAbout,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
    {
        ImGui::PushFont(nullptr, ImGui::GetStyle().FontSizeBase * 1.5f);
        ImGui::TextUnformatted("GIGGLE");
        ImGui::PopFont();
        ImGui::TextDisabled("Graphical Interface for Generating Gaussian Least-squares Estimates");
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::Text("Version %s", Version());
        ImGui::Spacing();
        ImGui::TextWrapped(
            "GIGGLE fits 1D spectra and extracts the counts in each peak with "
            "honest uncertainties. Counts are integrals of the fitted shapes "
            "over the fit range, with errors propagated through the covariance "
            "matrix, and every converged fit is verified against an independent "
            "re-fit of the same data.");
        ImGui::Spacing();
        ImGui::TextDisabled("Built on ROOT/Minuit2, Dear ImGui, and ImPlot.");
    }
    ImGui::End();
}

void App::DrawErrorPopup()
{
    if (!m_errorMessage.empty())
    {
        ImGui::OpenPopup("Error");
    }
    if (ImGui::BeginPopupModal("Error", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 24.0f);
        ImGui::TextWrapped("%s", m_errorMessage.c_str());
        ImGui::PopTextWrapPos();
        ImGui::Spacing();
        if (ImGui::Button("OK", ImVec2(120.0f, 0.0f)))
        {
            m_errorMessage.clear();
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void App::OpenFileDialog()
{
    nfdu8char_t* selectedPath = nullptr;
    nfdu8filteritem_t filter = { "ROOT files", "root" };
    nfdresult_t outcome = NFD_OpenDialogU8(&selectedPath, &filter, 1, nullptr);

    if (outcome == NFD_OKAY)
    {
        OpenFile(selectedPath);
        NFD_FreePathU8(selectedPath);
    }
    else if (outcome == NFD_ERROR)
    {
        m_errorMessage = NFD_GetError();
    }
    // NFD_CANCEL: the user changed their mind; nothing to do.
}

void App::OpenFile(const std::string& filePath)
{
    try
    {
        m_source = m_openSource(filePath);
        m_sourceFilePath = filePath;
        std::string fileName = std::filesystem::path(filePath).filename().string();
        m_fileTreePanel.SetContents(fileName, m_source->List());
        m_selectedHistogram.clear();
        m_histogram.reset();
    }
    catch (const std::exception& error)
    {
        m_errorMessage = error.what();
    }
}

void App::LoadHistogram(const std::string& path)
{
    try
    {
        m_histogram = m_source->Load(path);
        m_selectedHistogram = path;

        // Results belong to the histogram they were fitted on; the model
        // stays, so the same setup can be reused across histograms.
        m_fitResult.reset();
        m_fittedModel.reset();
        m_preFitModel.reset();

        // Give the fit range a sensible start, but never overwrite a range
        // the user has set: the same model is often reused across the
        // histograms of one file.
        if (m_model.range.max <= m_model.range.min)
        {
            m_model.range = { m_histogram->XMin(), m_histogram->XMax() };
        }
    }
    catch (const std::exception& error)
    {
        m_errorMessage = error.what();
    }
}

void App::StartFit()
{
    if (FitRunning() || !m_histogram.has_value())
    {
        return;
    }
    m_showFit = true; // running a fit shows it on the plot

    m_preFitModel = m_model;
    m_fitResult.reset();

    // The worker gets copies; the engine never touches live UI state.
    m_fitFuture = std::async(std::launch::async,
                             [engine = m_fitEngine.get(), histogram = m_histogram.value(),
                              model = m_model]() { return engine->Fit(histogram, model); });
}

void App::PollFit()
{
    if (!m_fitFuture.valid()
        || m_fitFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
    {
        return;
    }

    m_fitResult = m_fitFuture.get();
    if (m_fitResult->converged)
    {
        // The panel and the preview curves now show the fitted model.
        ApplyFitResult(m_fitResult.value(), m_model);
        m_fittedModel = m_model;
    }
}

void App::SaveResults(bool asCsv)
{
    if (!m_fitResult.has_value() || !m_fittedModel.has_value())
    {
        return;
    }

    // Default name and place: next to the data, named after the histogram.
    std::string histogramName = std::filesystem::path(m_selectedHistogram).filename().string();
    std::string defaultName = histogramName + "_fit." + (asCsv ? "csv" : "json");
    std::string defaultDirectory = std::filesystem::path(m_sourceFilePath).parent_path().string();

    nfdu8char_t* selectedPath = nullptr;
    nfdu8filteritem_t filter = asCsv ? nfdu8filteritem_t{ "CSV", "csv" }
                                     : nfdu8filteritem_t{ "JSON", "json" };
    nfdresult_t outcome = NFD_SaveDialogU8(&selectedPath, &filter, 1,
                                           defaultDirectory.c_str(), defaultName.c_str());
    if (outcome == NFD_CANCEL)
    {
        return;
    }
    if (outcome == NFD_ERROR)
    {
        m_errorMessage = NFD_GetError();
        return;
    }

    Provenance provenance = MakeProvenance(m_sourceFilePath, m_selectedHistogram);
    std::string content = asCsv
        ? MakeResultsCsv(provenance, m_fittedModel.value(), m_fitResult.value())
        : MakeResultsDocument(provenance, m_fittedModel.value(), m_fitResult.value()).dump(4) + "\n";

    std::ofstream file(selectedPath);
    if (file)
    {
        file << content;
    }
    if (!file)
    {
        m_errorMessage = std::string("could not write ") + selectedPath;
    }
    NFD_FreePathU8(selectedPath);
}

void App::CopyResultsCsv()
{
    if (!m_fitResult.has_value() || !m_fittedModel.has_value())
    {
        return;
    }
    Provenance provenance = MakeProvenance(m_sourceFilePath, m_selectedHistogram);
    std::string csv = MakeResultsCsv(provenance, m_fittedModel.value(), m_fitResult.value());
    ImGui::SetClipboardText(csv.c_str());
}

void App::SavePreset()
{
    std::string histogramName = std::filesystem::path(m_selectedHistogram).filename().string();
    std::string defaultName = histogramName + "_preset.json";
    std::string defaultDirectory = std::filesystem::path(m_sourceFilePath).parent_path().string();

    nfdu8char_t* selectedPath = nullptr;
    nfdu8filteritem_t filter = { "JSON", "json" };
    nfdresult_t outcome = NFD_SaveDialogU8(&selectedPath, &filter, 1,
                                           defaultDirectory.c_str(), defaultName.c_str());
    if (outcome != NFD_OKAY)
    {
        if (outcome == NFD_ERROR)
        {
            m_errorMessage = NFD_GetError();
        }
        return;
    }

    std::ofstream file(selectedPath);
    if (file)
    {
        file << ToJson(m_model).dump(4) << "\n";
    }
    if (!file)
    {
        m_errorMessage = std::string("could not write ") + selectedPath;
    }
    NFD_FreePathU8(selectedPath);
}

void App::LoadPreset()
{
    nfdu8char_t* selectedPath = nullptr;
    nfdu8filteritem_t filter = { "JSON", "json" };
    nfdresult_t outcome = NFD_OpenDialogU8(&selectedPath, &filter, 1, nullptr);
    if (outcome != NFD_OKAY)
    {
        if (outcome == NFD_ERROR)
        {
            m_errorMessage = NFD_GetError();
        }
        return;
    }

    try
    {
        std::ifstream file(selectedPath);
        if (!file)
        {
            throw std::runtime_error(std::string("could not read ") + selectedPath);
        }
        Json document = Json::parse(file);
        FitModel loaded = FitModelFromJson(document);

        // Presets travel between histograms with different binning, so the
        // range snaps to this histogram's bin edges. Old results no longer
        // describe the model.
        if (m_histogram.has_value())
        {
            loaded.range = SnapRangeToBinEdges(m_histogram.value(), loaded.range);
        }
        m_model = loaded;
        m_preFitModel.reset();
        m_fitResult.reset();
        m_fittedModel.reset();
    }
    catch (const std::exception& error)
    {
        m_errorMessage = error.what();
    }
    NFD_FreePathU8(selectedPath);
}

void App::OpenExportDialog()
{
    if (!m_histogram.has_value())
    {
        return;
    }
    m_showExportDialog = true;
}

void App::DrawExportDialog()
{
    if (!m_showExportDialog)
    {
        return;
    }

    ImGui::SetNextWindowSize(ImVec2(380.0f, 0.0f), ImGuiCond_Appearing);
    if (ImGui::Begin("Export Plot", &m_showExportDialog,
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoDocking))
    {
        ImGui::TextDisabled("The exported figure shows the view currently framed on screen.");
        ImGui::Spacing();

        ImGui::InputInt("width", &m_exportOptions.width);
        ImGui::InputInt("height", &m_exportOptions.height);

        if (ImGui::Button("1920 x 1080"))
        {
            m_exportOptions.width = 1920;
            m_exportOptions.height = 1080;
        }
        ImGui::SameLine();
        if (ImGui::Button("2560 x 1440"))
        {
            m_exportOptions.width = 2560;
            m_exportOptions.height = 1440;
        }
        ImGui::SameLine();
        if (ImGui::Button("3840 x 2160"))
        {
            m_exportOptions.width = 3840;
            m_exportOptions.height = 2160;
        }

        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderFloat("text & line scale", &m_exportOptions.emphasis, 0.0f, 4.0f,
                           m_exportOptions.emphasis <= 0.0f ? "auto" : "%.1fx");
        if (ImGui::IsItemHovered())
        {
            ImGui::SetTooltip("auto scales with the export height, keeping the\n"
                              "screen's proportions at any resolution");
        }

        ImGui::Spacing();
        if (ImGui::Button(ICON_FA_IMAGE "  Export...", ImVec2(-1.0f, 0.0f)))
        {
            std::string histogramName = std::filesystem::path(m_selectedHistogram).filename().string();
            std::string defaultName = histogramName + "_plot.png";
            std::string defaultDirectory = std::filesystem::path(m_sourceFilePath).parent_path().string();

            nfdu8char_t* selectedPath = nullptr;
            nfdu8filteritem_t filter = { "PNG", "png" };
            nfdresult_t outcome = NFD_SaveDialogU8(&selectedPath, &filter, 1,
                                                   defaultDirectory.c_str(), defaultName.c_str());
            if (outcome == NFD_OKAY)
            {
                m_pendingExportPath = selectedPath;
                NFD_FreePathU8(selectedPath);
                m_showExportDialog = false;
            }
            else if (outcome == NFD_ERROR)
            {
                m_errorMessage = NFD_GetError();
            }
        }
    }
    ImGui::End();
}

void App::PerformPlotExport()
{
    if (!m_histogram.has_value())
    {
        m_pendingExportPath.clear();
        return;
    }

    // Frame the view the user sees on screen.
    const double* limits = m_plotPanel.ViewLimits();
    for (int i = 0; i < 4; ++i)
    {
        m_exportOptions.viewLimits[i] = limits[i];
    }
    m_exportOptions.logScaleY = m_plotPanel.LogScaleY();

    // Export mirrors the plot: with the fit tools on, the model curves
    // (guess or fit) are drawn; with them off, just the data. The range
    // shade and drag handles are interactive aids and never exported.
    const FitModel* exportModel = m_showFit ? &m_model : nullptr;
    bool saved = ExportPlotOffscreen(m_pendingExportPath, m_exportOptions,
                                     m_histogram.value(), exportModel, m_theme, m_fonts.mono);
    if (!saved)
    {
        m_errorMessage = "could not export the plot to " + m_pendingExportPath;
    }
    m_pendingExportPath.clear();
}

// The default three-pane layout: file tree left, plot center, fit model
// right, results bottom.
void App::BuildDefaultLayout(unsigned int dockspaceId)
{
    ImGui::DockBuilderRemoveNode(dockspaceId);
    ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockspaceId, ImGui::GetMainViewport()->Size);

    ImGuiID center = dockspaceId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.18f, nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.28f, nullptr, &center);
    ImGuiID bottom = ImGui::DockBuilderSplitNode(center, ImGuiDir_Down, 0.25f, nullptr, &center);

    ImGui::DockBuilderDockWindow(FileTreePanel::Title, left);
    ImGui::DockBuilderDockWindow(PlotPanel::Title, center);
    ImGui::DockBuilderDockWindow(FitModelPanel::Title, right);
    ImGui::DockBuilderDockWindow(ResultsPanel::Title, bottom);

    ImGui::DockBuilderFinish(dockspaceId);
}

// Replaces the passed-in defaults with the saved geometry, when a valid
// window.ini exists. A bad or missing file leaves the defaults untouched.
//
// The saved position is validated against the monitors actually connected
// now: if it was saved on an external display that is no longer attached,
// the window would otherwise open off-screen and be unreachable. In that
// case the same size is re-centered on an available monitor instead.
void App::LoadWindowState(int& width, int& height, int& posX, int& posY)
{
    std::ifstream file(m_windowStateFilePath);
    int w = 0;
    int h = 0;
    int x = 0;
    int y = 0;
    if (!(file >> w >> h >> x >> y) || w < 400 || h < 300 || w > 20000 || h > 20000)
    {
        return; // missing or garbage: keep the centered 80% default
    }

    // Find the connected monitor whose work area the saved window overlaps
    // most. glfwGetMonitors lists the primary first, so a window that
    // overlaps nothing falls back to being centered on the primary.
    int monitorCount = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
    if (monitorCount == 0)
    {
        return;
    }

    int bestOverlap = -1;
    int areaX = 0;
    int areaY = 0;
    int areaW = 0;
    int areaH = 0;
    for (int i = 0; i < monitorCount; ++i)
    {
        int mx = 0;
        int my = 0;
        int mw = 0;
        int mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        int overlapX = std::min(x + w, mx + mw) - std::max(x, mx);
        int overlapY = std::min(y + h, my + mh) - std::max(y, my);
        int overlap = (overlapX > 0 && overlapY > 0) ? overlapX * overlapY : 0;
        if (overlap > bestOverlap)
        {
            bestOverlap = overlap;
            areaX = mx;
            areaY = my;
            areaW = mw;
            areaH = mh;
        }
    }

    // Never larger than the chosen monitor.
    width = std::min(w, areaW);
    height = std::min(h, areaH);

    if (bestOverlap > 0)
    {
        // Visible on this monitor: keep the place, nudged fully on-screen.
        posX = std::min(std::max(x, areaX), areaX + areaW - width);
        posY = std::min(std::max(y, areaY), areaY + areaH - height);
    }
    else
    {
        // The monitor the window lived on is gone: same size, re-centered.
        posX = areaX + (areaW - width) / 2;
        posY = areaY + (areaH - height) / 2;
    }
}

void App::SaveWindowState()
{
    if (m_window == nullptr)
    {
        return;
    }
    int width = 0;
    int height = 0;
    int posX = 0;
    int posY = 0;
    glfwGetWindowSize(m_window, &width, &height);
    glfwGetWindowPos(m_window, &posX, &posY);

    std::ofstream file(m_windowStateFilePath);
    if (file)
    {
        file << width << " " << height << " " << posX << " " << posY << "\n";
    }
}

void App::Shutdown()
{
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImPlot::DestroyContext();
    ImGui::DestroyContext();
    NFD_Quit();
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

} // namespace giggle
