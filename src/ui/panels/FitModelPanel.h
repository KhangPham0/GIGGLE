#ifndef GIGGLE_UI_PANELS_FIT_MODEL_PANEL_H
#define GIGGLE_UI_PANELS_FIT_MODEL_PANEL_H

namespace giggle {

// Right panel: the fit model being built (peaks, background, fit controls).
class FitModelPanel
{
public:
    static constexpr const char* Title = "Fit Model";

    void Draw();
};

} // namespace giggle

#endif
