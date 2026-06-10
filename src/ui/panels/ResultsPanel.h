#ifndef GIGGLE_UI_PANELS_RESULTS_PANEL_H
#define GIGGLE_UI_PANELS_RESULTS_PANEL_H

namespace giggle {

// Bottom panel: fit results (counts, uncertainties, fit quality).
class ResultsPanel
{
public:
    static constexpr const char* Title = "Results";

    void Draw();
};

} // namespace giggle

#endif
