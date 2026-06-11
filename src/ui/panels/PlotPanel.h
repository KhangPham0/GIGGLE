#ifndef GIGGLE_UI_PANELS_PLOT_PANEL_H
#define GIGGLE_UI_PANELS_PLOT_PANEL_H

#include <optional>
#include <string>

#include "core/FitModel.h"
#include "core/HistogramData.h"
#include "ui/Theme.h"

struct ImFont;

namespace giggle {

// What the user did in the plot this frame.
struct PlotAction
{
    std::optional<double> addPeakAt; // x position for a new peak
    bool fitRequested = false;
};

// Center panel: the spectrum plot with the model overlay and the direct
// manipulation tools -- drag handles on peaks and background, draggable
// fit range edges, click-to-add, and the right-click menu.
class PlotPanel
{
public:
    static constexpr const char* Title = "Plot";

    // The model is edited in place by the drag handles. The mono font is
    // used for axis numbers.
    PlotAction Draw(const HistogramData* histogram, FitModel* model,
                    const Theme& theme, ImFont* monoFont);

private:
    void DrawHistogram(const HistogramData& histogram, const Theme& theme);
    void DrawRangeTools(FitModel& model, const HistogramData& histogram, const Theme& theme);
    void DrawModelCurves(const FitModel& model, const HistogramData& histogram, const Theme& theme);
    void DrawPeakHandles(FitModel& model, const HistogramData& histogram, const Theme& theme);
    void DrawBackgroundHandles(FitModel& model, const HistogramData& histogram, const Theme& theme);
    void HandleAddPeakClick(PlotAction& action);
    void DrawContextMenu(FitModel& model, const HistogramData& histogram, PlotAction& action);

    bool m_addPeakMode = false;
    bool m_logScaleY = false;
    bool m_requestAxesFit = false;
    bool m_openContextMenu = false;
    double m_contextMenuX = 0.0; // plot x where the context menu was opened
    std::string m_lastDrawnName; // to refit the axes when the histogram changes
};

} // namespace giggle

#endif
