#include "PlotExport.h"

#include "imgui.h"
#include "imgui_impl_opengl3.h"
#include "implot.h"

// Framebuffer objects need the core-profile GL declarations: the legacy
// header GLFW includes by default predates them.
#define GL_SILENCE_DEPRECATION
#if defined(__APPLE__)
#define GLFW_INCLUDE_GLCOREARB
#else
#define GL_GLEXT_PROTOTYPES
#endif
#include <GLFW/glfw3.h>

#include "ui/ImageExport.h"
#include "ui/PlotRendering.h"

namespace giggle {

bool ExportPlotOffscreen(const std::string& path, const PlotExportOptions& options,
                         const HistogramData& histogram, const FitModel* model,
                         const Theme& theme, ImFont* monoFont)
{
    if (options.width < 64 || options.height < 64)
    {
        return false;
    }
    float emphasis = options.emphasis > 0.0f ? options.emphasis : options.height / 720.0f;

    ImGuiIO& io = ImGui::GetIO();
    ImGuiStyle& style = ImGui::GetStyle();

    // The synthetic frame renders at the export size with scaled-up text;
    // everything is restored afterwards.
    ImVec2 savedDisplaySize = io.DisplaySize;
    ImVec2 savedFramebufferScale = io.DisplayFramebufferScale;
    float savedFontScale = style.FontScaleDpi;

    io.DisplaySize = ImVec2(static_cast<float>(options.width), static_cast<float>(options.height));
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    style.FontScaleDpi = emphasis;

    ImGui::NewFrame();

    ImGui::SetNextWindowPos(ImVec2(0.0f, 0.0f));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    ImGui::Begin("##plot_export", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings
                     | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoDocking);

    std::string plotTitle = histogram.name + "###export";
    ImGui::PushFont(monoFont, 15.0f);
    if (ImPlot::BeginPlot(plotTitle.c_str(), ImVec2(-1.0f, -1.0f),
                          ImPlotFlags_NoMenus | ImPlotFlags_NoBoxSelect))
    {
        ImPlot::SetupAxes(nullptr, "counts");
        ImPlot::SetupAxisScale(ImAxis_Y1,
                               options.logScaleY ? ImPlotScale_Log10 : ImPlotScale_Linear);
        ImPlot::SetupAxisLimits(ImAxis_X1, options.viewLimits[0], options.viewLimits[1],
                                ImGuiCond_Always);
        ImPlot::SetupAxisLimits(ImAxis_Y1, options.viewLimits[2], options.viewLimits[3],
                                ImGuiCond_Always);

        // A figure shows the data, the fit curves, and thin lines marking
        // the fit range. The heavy range shade and the drag handles are
        // interactive aids, so they are deliberately left out here.
        RenderHistogramStairs(histogram, theme, emphasis);
        if (model != nullptr)
        {
            RenderModelCurves(*model, histogram, theme, emphasis);
            RenderRangeLines(model->range, theme, emphasis);
        }

        ImPlot::EndPlot();
    }
    ImGui::PopFont();

    ImGui::End();
    ImGui::PopStyleVar();

    ImGui::Render();

    // Draw into an offscreen framebuffer of exactly the export size.
    GLuint framebuffer = 0;
    GLuint texture = 0;
    glGenFramebuffers(1, &framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);
    glGenTextures(1, &texture);
    glBindTexture(GL_TEXTURE_2D, texture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, options.width, options.height, 0,
                 GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);

    bool saved = false;
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE)
    {
        glViewport(0, 0, options.width, options.height);
        glClearColor(theme.windowBackground.x, theme.windowBackground.y,
                     theme.windowBackground.z, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

        saved = SaveFramebufferRegionAsPng(path.c_str(), 0, 0, options.width, options.height,
                                           options.height);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteTextures(1, &texture);
    glDeleteFramebuffers(1, &framebuffer);

    io.DisplaySize = savedDisplaySize;
    io.DisplayFramebufferScale = savedFramebufferScale;
    style.FontScaleDpi = savedFontScale;

    return saved;
}

} // namespace giggle
