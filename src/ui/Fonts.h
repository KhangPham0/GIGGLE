#ifndef GIGGLE_UI_FONTS_H
#define GIGGLE_UI_FONTS_H

#include "imgui.h"

namespace giggle {

// The fonts GIGGLE uses, embedded in the binary so no files are needed at
// runtime. Sizes are dynamic: pass a size to ImGui::PushFont when a
// different size is needed.
//
// GIGGLE uses one typeface throughout: Cascadia Code Bold. It is
// monospaced, so it serves both the interface text and the numeric
// columns (counts, parameters, axis labels) where digits must align.
struct Fonts
{
    ImFont* ui = nullptr;   // interface text (the default font)
    ImFont* mono = nullptr; // numbers and tables (same face, aligned digits)
};

// Loads the embedded fonts into the ImGui font atlas. Call once at startup,
// after ImGui::CreateContext.
Fonts LoadFonts();

} // namespace giggle

#endif
