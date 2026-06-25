#ifndef GIGGLE_UI_PANELS_RESULTS_PANEL_H
#define GIGGLE_UI_PANELS_RESULTS_PANEL_H

#include "core/FitModel.h"
#include "core/FitResult.h"
#include "core/HistogramData.h"
#include "ui/Theme.h"

struct ImFont;

namespace giggle {

// What the user did in the Results panel this frame.
struct ResultsAction
{
    bool saveJsonRequested = false;
    bool saveCsvRequested = false;
    bool copyCsvRequested = false;
};

// Bottom panel: the fit results table -- counts, centroids, FWHM -- with
// the raw data counts in range for comparison, warnings, the cross-check
// verdict, and export buttons.
class ResultsPanel
{
public:
    static constexpr const char* Title = "Results";

    ResultsAction Draw(const FitResult* result, const HistogramData* histogram,
                       const FitModel* model, const Theme& theme, ImFont* monoFont);

private:
    void DrawSummaryLine(const FitResult& result, const Theme& theme, ImFont* monoFont);
    void DrawComponentsTable(const FitResult& result, const FitModel& model, const Theme& theme,
                             ImFont* monoFont);
    void DrawWarnings(const FitResult& result, const Theme& theme);

    // For showing amplitudes in plot units; set each Draw.
    const HistogramData* m_histogramForUnits = nullptr;
};

} // namespace giggle

#endif
