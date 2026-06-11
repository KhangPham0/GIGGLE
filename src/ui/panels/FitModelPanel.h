#ifndef GIGGLE_UI_PANELS_FIT_MODEL_PANEL_H
#define GIGGLE_UI_PANELS_FIT_MODEL_PANEL_H

#include "core/FitModel.h"
#include "core/HistogramData.h"
#include "ui/Theme.h"

struct ImFont;

namespace giggle {

// What the user did in the Fit Model panel this frame.
struct FitPanelAction
{
    bool fitRequested = false;
    bool revertRequested = false;
};

// Right panel: builds the fit model. Edits the shared FitModel in place;
// the plot draws its overlay curves from the same object, so the two can
// never disagree.
class FitModelPanel
{
public:
    static constexpr const char* Title = "Fit Model";

    // The histogram is used to express peak heights in plot units; the
    // mono font is used for numeric fields. When it is null (no histogram
    // open) the panel only shows a hint.
    FitPanelAction Draw(FitModel& model, const HistogramData* histogram, bool fitRunning,
                        bool canRevert, const Theme& theme, ImFont* monoFont);

private:
    void DrawRangeSection(FitModel& model, const HistogramData& histogram, ImFont* monoFont);
    void DrawPeaksSection(FitModel& model, const HistogramData& histogram, ImFont* monoFont);
    void DrawBackgroundSection(FitModel& model, const HistogramData& histogram, ImFont* monoFont);
    void DrawStatisticSection(FitModel& model);

    // The amplitude parameter row, displayed in plot units (peak height /
    // background level in counts).
    void DrawAmplitudeRow(FitComponent& component, const HistogramData& histogram,
                          const char* label, ImFont* monoFont);

    // One labeled row with a draggable value and a "fix" checkbox.
    // Returns true when the value changed.
    bool DrawParameterRow(FitParameter& parameter, const char* id, ImFont* monoFont);
};

} // namespace giggle

#endif
