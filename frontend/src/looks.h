// The looks a system is offered in the pause menu's Look row, and the one each
// system has chosen (#122). The shaders that draw them are screenfx.h's, which
// includes this; moved out of it in the restructure (#398).

#pragma once

#include <string>
#include <vector>

namespace screenfx {

// One choice in the pause menu's Look row.
struct Look {
    const char* id;      // what is stored
    const char* label;   // on screen
    // The preset, relative to the shader directory; nullptr is the console's
    // own picture with no shader at all.
    const char* preset;
};

// The looks a system is offered, by RomM platform slug; empty for a system
// that has none (PS2 and newer, DS, PSP, Virtual Boy, Vectrex). Consoles never
// see the handheld looks, and handhelds never see the TV looks.
const std::vector<Look>& looksFor(const std::string& platformSlug);

// The stored choice's index into looksFor, or 0.
int chosen(const std::string& platformSlug);
void choose(const std::string& platformSlug, int index);

// The preset path for the stored choice, or "" for none.
std::string presetFor(const std::string& platformSlug);

// Where the shader files are: $CABINETOS_SHADERS, else the image's copy.
std::string shaderDir();

}  // namespace screenfx
