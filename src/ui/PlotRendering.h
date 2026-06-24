#ifndef GIGGLE_UI_PLOT_RENDERING_H
#define GIGGLE_UI_PLOT_RENDERING_H

#include "core/FitModel.h"
#include "core/HistogramData.h"
#include "ui/Theme.h"

namespace giggle {

// The plot content shared by the screen panel and the offscreen exporter:
// both draw through these, so an exported figure is the screen plot, not a
// re-implementation of it. Call between BeginPlot and EndPlot.
//
// `emphasis` scales line weights, so high-resolution exports keep their
// visual weight instead of turning into hairlines.

void RenderHistogramStairs(const HistogramData& histogram, const Theme& theme,
                           float emphasis = 1.0f);

void RenderRangeShade(const FitRange& range, const Theme& theme);

void RenderModelCurves(const FitModel& model, const HistogramData& histogram,
                       const Theme& theme, float emphasis = 1.0f);

} // namespace giggle

#endif
