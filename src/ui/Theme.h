#ifndef GIGGLE_UI_THEME_H
#define GIGGLE_UI_THEME_H

#include <vector>

#include "imgui.h"

namespace giggle {

// Every color that defines GIGGLE's appearance, kept as plain data so that
// alternative themes (light, university colors, ...) can be added later
// without touching any drawing code.
struct Theme
{
    const char* name = "";

    // Base surfaces
    ImVec4 windowBackground;
    ImVec4 panelBackground;
    ImVec4 childBackground;
    ImVec4 popupBackground;

    // The accent color, used sparingly: selection, primary actions, focus.
    ImVec4 accent;
    ImVec4 accentHover;
    ImVec4 accentActive;

    // A second highlight, distinct from the accent, marking the focused
    // panel -- so "which panel is active" reads apart from the teal headers.
    ImVec4 highlight;

    // Text
    ImVec4 textPrimary;
    ImVec4 textDisabled;

    // Widget surfaces (inputs, buttons, headers)
    ImVec4 frame;
    ImVec4 frameHover;
    ImVec4 frameActive;

    // Borders and separators
    ImVec4 border;

    // Status colors
    ImVec4 statusGood;
    ImVec4 statusWarning;
    ImVec4 statusError;

    // Plot area
    ImVec4 plotBackground;
    ImVec4 plotGrid;
    ImVec4 histogramLine;
    ImVec4 histogramFill;
    ImVec4 fitCurve;

    // Colors for individual model components (cycled when there are more
    // components than colors).
    std::vector<ImVec4> componentColors;

    ImVec4 ComponentColor(size_t index) const
    {
        return componentColors.empty() ? fitCurve
                                       : componentColors[index % componentColors.size()];
    }
};

// The default GIGGLE theme.
Theme DarkTheme();

// Writes a theme into the global ImGui and ImPlot styles.
void ApplyTheme(const Theme& theme);

} // namespace giggle

#endif
