#include "App.h"

#include <cstdio>
#include <filesystem>

#include "imgui.h"
#include "imgui_internal.h" // DockBuilder API, used for the default layout
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"
#include "nfd.h"

#define GL_SILENCE_DEPRECATION
#include <GLFW/glfw3.h>

namespace giggle {

static void GlfwErrorCallback(int error, const char* description)
{
    std::fprintf(stderr, "GLFW error %d: %s\n", error, description);
}

App::App(SourceFactory openSource) : m_openSource(std::move(openSource))
{
}

void App::OpenFileOnStartup(const std::string& filePath)
{
    m_startupFile = filePath;
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
    }

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

    // Size the window in screen coordinates, scaled for the monitor's DPI.
    float scale = ImGui_ImplGlfw_GetContentScaleForMonitor(glfwGetPrimaryMonitor());
    int width = static_cast<int>(1280 * scale);
    int height = static_cast<int>(800 * scale);

    m_window = glfwCreateWindow(width, height, "GIGGLE", nullptr, nullptr);
    if (m_window == nullptr)
    {
        glfwTerminate();
        return false;
    }
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

    // Build the default layout only when no saved layout exists from a
    // previous run.
    m_needDefaultLayout = !std::filesystem::exists(io.IniFilename);

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

    ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport());

    if (m_needDefaultLayout)
    {
        BuildDefaultLayout(dockspaceId);
        m_needDefaultLayout = false;
    }

    FileTreeAction action = m_fileTreePanel.Draw(m_selectedHistogram);
    if (action.openFileRequested)
    {
        OpenFileDialog();
    }
    if (action.histogramClicked.has_value())
    {
        LoadHistogram(action.histogramClicked.value());
    }

    m_plotPanel.Draw(m_histogram.has_value() ? &m_histogram.value() : nullptr, m_theme, m_fonts.mono);
    m_fitModelPanel.Draw();
    m_resultsPanel.Draw();

    DrawErrorPopup();
}

void App::DrawMainMenu()
{
    if (ImGui::BeginMainMenuBar())
    {
        if (ImGui::BeginMenu("File"))
        {
            if (ImGui::MenuItem("Open..."))
            {
                OpenFileDialog();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Quit"))
            {
                glfwSetWindowShouldClose(m_window, GLFW_TRUE);
            }
            ImGui::EndMenu();
        }
        ImGui::EndMainMenuBar();
    }
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
    }
    catch (const std::exception& error)
    {
        m_errorMessage = error.what();
    }
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
