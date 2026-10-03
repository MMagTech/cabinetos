// A system's own options in the pause menu (#73): the choices a person makes
// about how a system looks, changed in front of the game.
//
// TWO ROWS ON TWO SYSTEMS, narrowed with MMagTech on 2026-10-02
// (docs/SETTINGS.md, "In the pause menu, per system"): Virtual Boy's screen,
// and the original Game Boy's colours. Choices are Cabinet's
// (NativeCoreOptions.swift), checked against the values the cores at our pins
// declare.
//
// ONE ROW CAN SET MORE THAN ONE CORE OPTION. Virtual Boy's glasses and its
// screen colour were two rows, and they are one choice: with glasses on, the
// core draws in the glasses' two colours and ignores the screen colour
// (beetle-vb libretro.cpp, SettingChanged). Two rows let a person set a colour
// that did nothing, and made the menu seven buttons long (MMagTech on the TV).
//
// PER SYSTEM, NOT PER GAME, and the console's, not a person's: one choice in
// `config/settings.json` as "pause_option.<system>.<row>", read at every
// launch and changed live while the game runs.

#pragma once

#include <map>
#include <string>
#include <utility>
#include <vector>

namespace sysopts {

struct Choice {
    const char* id;      // what is stored
    const char* label;   // on screen
    // The core options this choice sets, as the core's own values.
    std::vector<std::pair<const char*, const char*>> sets;
};

struct Option {
    const char* id;      // "screen"
    const char* label;   // "Screen"
    std::vector<Choice> choices;
    const char* defaultId;
};

// The rows for a system, by RomM platform slug; empty for most.
const std::vector<Option>& forSystem(const std::string& platformSlug);

// The stored choice's index into `o.choices`, or the default's.
int chosen(const std::string& platformSlug, const Option& o);
void choose(const std::string& platformSlug, const Option& o, int index);

// Every row's current core options, for the launch's option overrides.
std::map<std::string, std::string> overrides(const std::string& platformSlug);

}  // namespace sysopts
