#ifndef GIGGLE_UI_PANELS_FIT_MODEL_PANEL_H
#define GIGGLE_UI_PANELS_FIT_MODEL_PANEL_H

#include "core/FitModel.h"

struct ImFont;

namespace giggle {

// Right panel: builds the fit model. Edits the shared FitModel in place;
// the plot draws its overlay curves from the same object, so the two can
// never disagree.
class FitModelPanel
{
public:
    static constexpr const char* Title = "Fit Model";

    // The mono font is used for numeric fields. When `enabled` is false
    // (no histogram open) the panel only shows a hint.
    void Draw(FitModel& model, bool enabled, ImFont* monoFont);

private:
    void DrawRangeSection(FitModel& model, ImFont* monoFont);
    void DrawPeaksSection(FitModel& model, ImFont* monoFont);
    void DrawBackgroundSection(FitModel& model, ImFont* monoFont);
    void DrawStatisticSection(FitModel& model);

    // One labeled row with a draggable value and a "fix" checkbox.
    // Returns true when the value changed.
    bool DrawParameterRow(FitParameter& parameter, const char* id, ImFont* monoFont);
};

} // namespace giggle

#endif
