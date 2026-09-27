// Rumble: a game's vibration reaching the pad in each player's hand (#149).
//
// MATCHES CABINET: one switch for the whole console, on by default
// (Cabinet's `rumbleEnabled`), and every system that has motors gets them.
// There is nothing per game and nothing per pad.
//
// WHERE THE MOTORS COME FROM. A libretro core asks for the rumble interface
// and calls it with a port, a motor (strong or weak) and a strength; that is
// how RetroArch drives rumble for every core, and core.cpp answers the same
// way. PlayStation 2 is not a libretro core: its bridge reports what PCSX2
// asked of each DualShock 2 (ps2.h). Both land in `set`.
//
// WHERE THEY GO. SDL's gamepad rumble, strong motor as SDL's low-frequency
// one and weak as its high-frequency one, as RetroArch's SDL driver does.
// Pads without motors ignore it.
//
// THEY NEVER RUN ON. A motor left on after its game stopped caring is the
// failure worth designing against, so every send is short and renewed while
// it holds (a dead process stops a pad within a quarter second), and
// `update(false)` stops everything the moment the game is not being played:
// the pause menu, a state loading, leaving the game.

#pragma once

#include <cstdint>

namespace rumble {

// The Settings switch. Saved as "rumble" in settings.json, "on" by default.
bool enabled();
void setEnabled(bool on);

// What the game asks of player `port`'s motor: 0 strong, 1 weak, 0 to 65535.
// Safe from any thread; a core may call it from its own.
void set(unsigned port, unsigned motor, uint16_t strength);

// Once a frame, from the main thread. `live` is "the game is being played
// right now"; false stops every motor at once.
void update(bool live);

// Forgets what the last game asked for and stops every motor. Called when a
// game starts and when it ends, so nothing carries over.
void reset();

}  // namespace rumble
