#ifndef GIGGLE_UI_PANELS_RESULTS_PANEL_H
#define GIGGLE_UI_PANELS_RESULTS_PANEL_H

#include "core/FitModel.h"
#include "core/FitResult.h"
#include "core/HistogramData.h"
#include "ui/Theme.h"

struct ImFont;

namespace giggle {

// Bottom panel: fit results, including the raw data counts in the fit
// range so the model total can be checked against the physical number.
// The full results table comes next.
class ResultsPanel
{
public:
    static constexpr const char* Title = "Results";

    void Draw(const FitResult* result, const HistogramData* histogram, const FitModel* model,
              const Theme& theme, ImFont* monoFont);
};

} // namespace giggle

#endif
