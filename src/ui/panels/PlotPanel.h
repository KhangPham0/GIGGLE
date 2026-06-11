#ifndef GIGGLE_UI_PANELS_PLOT_PANEL_H
#define GIGGLE_UI_PANELS_PLOT_PANEL_H

#include <string>

#include "core/HistogramData.h"
#include "ui/Theme.h"

struct ImFont;

namespace giggle {

// Center panel: the spectrum plot.
class PlotPanel
{
public:
    static constexpr const char* Title = "Plot";

    // Draws the histogram (or an empty plot when null). The mono font is
    // used for axis numbers.
    void Draw(const HistogramData* histogram, const Theme& theme, ImFont* monoFont);

private:
    std::string m_lastDrawnName; // to refit the axes when the histogram changes
};

} // namespace giggle

#endif
