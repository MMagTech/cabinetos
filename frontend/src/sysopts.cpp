#include "sysopts.h"

#include "prefs.h"

namespace sysopts {

namespace {

const std::vector<Option> kNone;

// Virtual Boy (Beetle VB): the flat screen in one of eight colours, the
// original black and red first and by default, then real depth for each pair
// of glasses the core knows, since which pair somebody owns is not something
// the console can guess (Cabinet's reasoning, kept).
const std::vector<Option> kVirtualBoy = {
    {"screen",
     "Screen",
     {
         {"red", "Red", {{"vb_anaglyph_preset", "disabled"}, {"vb_color_mode", "black & red"}}},
         {"white", "White", {{"vb_anaglyph_preset", "disabled"}, {"vb_color_mode", "black & white"}}},
         {"blue", "Blue", {{"vb_anaglyph_preset", "disabled"}, {"vb_color_mode", "black & blue"}}},
         {"cyan", "Cyan", {{"vb_anaglyph_preset", "disabled"}, {"vb_color_mode", "black & cyan"}}},
         {"electric-cyan", "Electric cyan",
          {{"vb_anaglyph_preset", "disabled"}, {"vb_color_mode", "black & electric cyan"}}},
         {"green", "Green", {{"vb_anaglyph_preset", "disabled"}, {"vb_color_mode", "black & green"}}},
         {"magenta", "Magenta",
          {{"vb_anaglyph_preset", "disabled"}, {"vb_color_mode", "black & magenta"}}},
         {"yellow", "Yellow", {{"vb_anaglyph_preset", "disabled"}, {"vb_color_mode", "black & yellow"}}},
         {"3d-red-blue", "3D red/blue", {{"vb_anaglyph_preset", "red & blue"}}},
         {"3d-red-cyan", "3D red/cyan", {{"vb_anaglyph_preset", "red & cyan"}}},
         {"3d-red-electric-cyan", "3D red/electric cyan",
          {{"vb_anaglyph_preset", "red & electric cyan"}}},
         {"3d-green-magenta", "3D green/magenta", {{"vb_anaglyph_preset", "green & magenta"}}},
         {"3d-yellow-blue", "3D yellow/blue", {{"vb_anaglyph_preset", "yellow & blue"}}},
     },
     "red"},
};

// Original Game Boy (Gambatte). Not for Game Boy Color games: Gambatte skips
// colourisation whenever the game is a GBC one (libretro.cpp, isCgb), so the
// row would visibly do nothing there.
const std::vector<Option> kGameBoy = {
    {"colors",
     "Colors",
     {
         {"off", "Off", {{"gambatte_gb_colorization", "disabled"}}},
         {"auto", "Auto", {{"gambatte_gb_colorization", "auto"}}},
         {"gbc", "Game Boy Color", {{"gambatte_gb_colorization", "GBC"}}},
         {"sgb", "Super Game Boy", {{"gambatte_gb_colorization", "SGB"}}},
     },
     "off"},
};

std::string prefKey(const std::string& platformSlug, const Option& o) {
    return "pause_option." + platformSlug + "." + o.id;
}

}  // namespace

const std::vector<Option>& forSystem(const std::string& platformSlug) {
    if (platformSlug == "virtualboy") return kVirtualBoy;
    if (platformSlug == "gb") return kGameBoy;
    return kNone;
}

int chosen(const std::string& platformSlug, const Option& o) {
    const std::string v = prefs::get(prefKey(platformSlug, o), o.defaultId);
    for (size_t i = 0; i < o.choices.size(); ++i)
        if (v == o.choices[i].id) return static_cast<int>(i);
    for (size_t i = 0; i < o.choices.size(); ++i)
        if (std::string(o.defaultId) == o.choices[i].id) return static_cast<int>(i);
    return 0;
}

void choose(const std::string& platformSlug, const Option& o, int index) {
    if (index < 0 || index >= static_cast<int>(o.choices.size())) return;
    prefs::set(prefKey(platformSlug, o), o.choices[index].id);
}

std::map<std::string, std::string> overrides(const std::string& platformSlug) {
    std::map<std::string, std::string> out;
    for (const Option& o : forSystem(platformSlug))
        for (const auto& kv : o.choices[chosen(platformSlug, o)].sets) out[kv.first] = kv.second;
    return out;
}

}  // namespace sysopts
