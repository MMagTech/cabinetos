// Picture quality: Performance, Balanced or Quality, and what each emulator is
// told at each level. Issue #63, phase 1; the decisions and the measurements
// behind every value are in docs/PROJECT.md, "the automatic quality design"
// and the #63 decisions of 2026-10-02.
//
// ONE PLACE ANSWERS. Every emulator asks this file for its picture settings
// at launch and nothing else knows the levels exist. Settings are written
// fresh at every launch and nothing is synced, so the whole feature comes out
// in one ordinary update if it has to.
//
// THE DIAL MOVES TWO THINGS: the internal resolution, and on the emulators
// that offer it, whether shaders are built in the background (Balanced,
// Quality) or when first needed, with a short stutter (Performance).
// Everything else that is free is fixed at its best value for every level
// (anisotropic filtering 16x, 3DO's high resolution, sharp vector lines), and
// nothing here needs a file from anywhere.
//
// THE LEVEL FOR A GAME is the game's own choice if the player made one in its
// pause menu, otherwise the console's. A game's choice survives a change of
// the console's (MMagTech, 2026-10-02).

#pragma once

#include <map>
#include <optional>
#include <string>
#include <vector>

namespace quality {

enum class Level { Performance, Balanced, Quality };
constexpr int kLevelCount = 3;

// "Performance", for the screen.
const char* levelName(Level l);
// "performance", for the settings file: a word survives retuning.
const char* levelWord(Level l);
bool levelFromWord(const std::string& word, Level* out);

// The console's level. PERFORMANCE UNTIL PHASE 2: nothing tells machines
// apart yet, and the design's rule for unknown hardware is Performance.
Level console();
void setConsole(Level l);

// A game's own choice from its pause menu, if it has one. Kept on this
// console only, never synced.
std::optional<Level> gameChoice(int romId);
void setGameChoice(int romId, std::optional<Level> l);

// What a game runs at: its own choice, else the console's.
Level forGame(int romId);

// --- What each emulator is told ----------------------------------------------

// Libretro option values for `core` (the manifest name, e.g. "dolphin") on
// `platformSlug` (GameCube and Wii share Dolphin and differ at Quality).
// Applied on top of catalog::optionOverrides.
std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, Level l);

struct Ps2 {
    float upscale = 1.0f;
    int anisotropy = 0;
};
Ps2 ps2(Level l);

// One `[section] key=value` for an ini-style file.
struct Setting {
    std::string section, key, value;
};
// Eden's qt-config.ini.
std::vector<Setting> eden(Level l);
// RPCS3's config.yml: a whole `Video:` block, appended to the console's own.
std::string rpcs3(Level l);
// xemu's xemu.toml: lines for its `[display.quality]` table.
std::string xemu(Level l);
// Xenia Edge's command line.
std::vector<std::string> xenia(Level l);

// One line on stderr saying what was applied, so a log shows the level a game
// actually ran at.
void logApplied(const std::string& who, int romId, Level l);

}  // namespace quality
