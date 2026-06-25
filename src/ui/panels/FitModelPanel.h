#ifndef GIGGLE_UI_PANELS_FIT_MODEL_PANEL_H
#define GIGGLE_UI_PANELS_FIT_MODEL_PANEL_H

#include <string>

#include "core/FitModel.h"
#include "core/FormulaValidator.h"
#include "core/HistogramData.h"
#include "ui/Theme.h"

struct ImFont;

namespace giggle {

// What the user did in the Fit Model panel this frame.
struct FitPanelAction
{
    bool fitRequested = false;
    bool revertRequested = false;
    bool savePresetRequested = false;
    bool loadPresetRequested = false;
};

// Right panel: builds the fit model. Edits the shared FitModel in place;
// the plot draws its overlay curves from the same object, so the two can
// never disagree.
class FitModelPanel
{
public:
    static constexpr const char* Title = "Fit Model";

    // The histogram is used to express peak heights in plot units; the
    // mono font is used for numeric fields; the validator checks custom
    // formulas. When the histogram is null (no file open) the panel only
    // shows a hint.
    FitPanelAction Draw(FitModel& model, const HistogramData* histogram, bool fitRunning,
                        bool canRevert, const Theme& theme, ImFont* monoFont,
                        const FormulaValidator& validator);

private:
    void DrawRangeSection(FitModel& model, const HistogramData& histogram, const Theme& theme,
                          ImFont* monoFont);
    void DrawPeaksSection(FitModel& model, const HistogramData& histogram, ImFont* monoFont,
                          const Theme& theme, const FormulaValidator& validator);
    void DrawBackgroundSection(FitModel& model, const HistogramData& histogram, ImFont* monoFont,
                               const Theme& theme, const FormulaValidator& validator);
    void DrawStatisticSection(FitModel& model, const Theme& theme);

    // The formula field with its Apply button; shown for custom components.
    void DrawFormulaEditor(FitComponent& component, const Theme& theme,
                           const FormulaValidator& validator);

    // The amplitude parameter row, displayed in plot units (peak height /
    // background level in counts).
    void DrawAmplitudeRow(FitComponent& component, const HistogramData& histogram,
                          const char* label, ImFont* monoFont);

    // One labeled row with a draggable value and a "fix" checkbox.
    // Returns true when the value changed.
    bool DrawParameterRow(FitParameter& parameter, const char* id, ImFont* monoFont);

    // Feedback from the last formula Apply, shown under that component.
    const void* m_formulaMessageOwner = nullptr;
    bool m_formulaMessageIsError = false;
    std::string m_formulaMessage;
};

} // namespace giggle

#endif
