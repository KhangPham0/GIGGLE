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
    bool savePlotRequested = false;
};

// A screen-space rectangle (top-left origin, window coordinates).
struct PanelRect
{
    float x = 0.0f;
    float y = 0.0f;
    float width = 0.0f;
    float height = 0.0f;
};

// Center panel: the spectrum plot with the model overlay and the direct
// manipulation tools -- drag handles on peaks and background, draggable
// fit range edges, click-to-add, and the right-click menu.
class PlotPanel
{
public:
    static constexpr const char* Title = "Plot";

    // The model is edited in place by the drag handles. `showFit` gates the
    // whole fit overlay (range markers, handles, curves) and is toggled
    // from the plot's context menu. The mono font is used for axis numbers.
    PlotAction Draw(const HistogramData* histogram, FitModel* model, bool& showFit,
                    const Theme& theme, ImFont* monoFont);

    // Where the plot window was drawn this frame, for the PNG capture.
    const PanelRect& WindowRect() const { return m_windowRect; }

    // The axis ranges currently framed on screen (xmin, xmax, ymin, ymax)
    // and the y-axis scale, so an export shows the same view.
    const double* ViewLimits() const { return m_viewLimits; }
    bool LogScaleY() const { return m_logScaleY; }

private:
    void DrawHistogram(const HistogramData& histogram, const Theme& theme);
    void DrawFitToolsButton(bool& showFit, const Theme& theme);
    void DrawBinInspector(const HistogramData& histogram, const Theme& theme, ImFont* monoFont);
    void DrawRangeTools(FitModel& model, const HistogramData& histogram, const Theme& theme);
    void DrawModelCurves(const FitModel& model, const HistogramData& histogram, const Theme& theme);
    void DrawPeakHandles(FitModel& model, const HistogramData& histogram, const Theme& theme);
    // The apex + half-max handles for one peak-like component (a peak, or a
    // gaussian background). `baseId` must be unique per component.
    void DrawPeakControls(FitComponent& component, const FitRange& range,
                          const HistogramData& histogram, const ImVec4& color, int baseId);
    void DrawBackgroundHandles(FitModel& model, const HistogramData& histogram, const Theme& theme);
    // Edge (and, for the quadratic, middle) control points for the curve
    // backgrounds: dragging one re-solves the curve through it.
    void DrawBackgroundCurvePoints(FitComponent& background, const FitRange& range,
                                   const HistogramData& histogram, const ImVec4& color, int baseId);
    void HandleAddPeakClick(PlotAction& action);
    void DrawContextMenu(FitModel& model, const HistogramData& histogram, bool& showFit,
                         PlotAction& action);

    bool m_addPeakMode = false;
    bool m_binInspector = false;
    bool m_logScaleY = false;
    double m_dataYMin = 0.0; // data extent, cached per histogram; bounds the
    double m_dataYMax = 0.0; // y-axis so panning can't drift off the data
    bool m_requestAxesFit = false;
    bool m_openContextMenu = false;
    double m_contextMenuX = 0.0; // plot x where the context menu was opened
    std::string m_lastDrawnName; // to refit the axes when the histogram changes
    PanelRect m_windowRect;
    double m_viewLimits[4] = { 0.0, 1.0, 0.0, 1.0 };
};

} // namespace giggle

#endif
