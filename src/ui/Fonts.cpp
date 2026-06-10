#include "Fonts.h"

// Font data generated with imgui's binary_to_compressed_c tool (-base85).
#include "fonts/inter_regular.h"
#include "fonts/jetbrains_mono_regular.h"

namespace giggle {

Fonts LoadFonts()
{
    ImGuiIO& io = ImGui::GetIO();

    // The default rendered size; PushFont can override it per use.
    ImGui::GetStyle().FontSizeBase = 16.0f;

    Fonts fonts;
    fonts.ui = io.Fonts->AddFontFromMemoryCompressedBase85TTF(InterRegular_compressed_data_base85);
    fonts.mono = io.Fonts->AddFontFromMemoryCompressedBase85TTF(JetBrainsMonoRegular_compressed_data_base85);

    return fonts;
}

} // namespace giggle
