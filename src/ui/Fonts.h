#ifndef GIGGLE_UI_FONTS_H
#define GIGGLE_UI_FONTS_H

#include "imgui.h"

namespace giggle {

// The fonts GIGGLE uses, embedded in the binary so no files are needed at
// runtime. Sizes are dynamic: pass a size to ImGui::PushFont when a
// different size is needed.
struct Fonts
{
    ImFont* ui = nullptr;   // Inter: all interface text (the default font)
    ImFont* mono = nullptr; // JetBrains Mono: numbers, tables, axis labels
};

// Loads the embedded fonts into the ImGui font atlas. Call once at startup,
// after ImGui::CreateContext.
Fonts LoadFonts();

} // namespace giggle

#endif
