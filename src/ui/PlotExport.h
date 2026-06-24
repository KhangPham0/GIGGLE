#ifndef GIGGLE_UI_PLOT_EXPORT_H
#define GIGGLE_UI_PLOT_EXPORT_H

#include <string>

#include "core/FitModel.h"
#include "core/HistogramData.h"
#include "ui/Theme.h"

struct ImFont;

namespace giggle {

struct PlotExportOptions
{
    int width = 1920;
    int height = 1080;
    // Multiplies fonts and line weights. 0 = automatic (height / 720, so
    // the figure keeps the screen's proportions at any resolution).
    float emphasis = 0.0f;

    // The view to frame: axis limits (xmin, xmax, ymin, ymax) and scale,
    // normally taken from the on-screen plot.
    double viewLimits[4] = { 0.0, 1.0, 0.0, 1.0 };
    bool logScaleY = false;
};

// Renders the plot offscreen at the requested resolution and writes a PNG.
// Runs a synthetic ImGui frame into a framebuffer, drawing through the
// same functions as the screen plot. Must be called between frames (not
// inside ImGui::NewFrame/Render). Returns false on failure.
bool ExportPlotOffscreen(const std::string& path, const PlotExportOptions& options,
                         const HistogramData& histogram, const FitModel* model,
                         const Theme& theme, ImFont* monoFont);

} // namespace giggle

#endif
