// Virtual controllers: the pads an emulator of its own is actually given.
//
// WHY THE REAL PADS ARE NOT HANDED OVER. Eden, like every emulator built on
// SDL, maps a pad by its RAW button numbers, and those come from the SDL inside
// its Flatpak: 3.2.30 under sdl2-compat, measured 2026-09-28, against 3.4.16
// in the console itself. Two SDLs two versions apart can number the same pad
// differently, and a map written from one and read by the other scrambles the
// buttons with no error anywhere. So the emulator never sees the real pads.
// It sees one of these per player, whose identity and layout are fixed here,
// and the console passes each real pad's presses to its player's virtual one.
// That is what Steam Input does for the same reason.
//
// WHAT IT BUYS BEYOND THAT. The same four pads serve PS3, Xbox and Wii U; the
// player order is the console's (players.h), not the emulator's guess; and the
// pause menu takes the controllers simply by passing nothing on.
//
// WHAT IT COSTS, SAID PLAINLY: motion (gyro) does not come through. Rumble
// comes back the other way: each virtual controller takes the emulator's
// force-feedback the way a real pad does, and pump() hands it to rumble.h,
// which drives the player's real pad under the console's Rumble switch.
//
// THE LAYOUT IS SDL'S OWN RULE FOR AN evdev DEVICE: buttons numbered in the
// order of their key codes, axes in the order of theirs, the d-pad as hat 0.
// That rule is in SDL's Linux joystick code and has not changed across the
// versions in question, which is the whole reason for choosing it.

#pragma once

#include <SDL3/SDL.h>

#include <cstdint>
#include <string>

namespace cab::vpad {

constexpr int kMaxPlayers = 4;

// Creates `players` virtual controllers, one per player, and keeps them until
// close(). False when /dev/uinput cannot be opened, which leaves an emulator
// of its own with no controllers at all; the caller says so.
bool open(int players);
void close();
int count();

// A real pad's change, passed to its player's virtual controller. Player
// numbers are players.h's, from 0. Anything a virtual controller does not
// have (paddles, a touchpad) is dropped.
void button(int player, SDL_GamepadButton b, bool down);
void axis(int player, SDL_GamepadAxis a, int16_t value);
// Everything let go and centred: the pause menu opening, a pad unplugged.
void releaseAll();

// Once a frame while an emulator runs: answers its force-feedback requests
// and passes each player's rumble to rumble.h, and stops a rumble whose time
// is up. `nowMs` is any steady clock in milliseconds.
void pump(int64_t nowMs);

// The identity an emulator knows them by, as Eden writes it: SDL's GUID for
// the device, with the two bytes of name checksum cleared (Eden's GetGUID).
std::string edenGuid();

// Where each input lands in SDL's numbering of a virtual controller.
int buttonIndex(SDL_GamepadButton b);   // -1 when it has none
int axisIndex(SDL_GamepadAxis a);       // -1 when it has none

}  // namespace cab::vpad
