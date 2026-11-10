// A seated pad's tilt: what its motion sensors, the accelerometer and the
// gyroscope, read now (#670).
//
// WHO ASKS. A game made for motion: a Game Boy Advance cartridge with a tilt
// or gyro sensor in it, a 3DS game's gyro aiming. Its emulator asks for a
// player's sensors, and whoever answers for that emulator calls here:
// core_input.cpp for the libretro cores (mGBA, Azahar). Nothing in this file
// knows a core or an emulator, so the PS3 (#683) and Switch, Wii U and Vita
// (#669) can read the same.
//
// IN SDL'S UNITS AND AXES, as SDL hands them out, never converted here. For a
// pad held in front of you: +x to the right, +y up out of its face, +z toward
// you. The accelerometer is in metres per second squared with gravity
// included (a pad lying flat reads about +9.81 on y, SDL_STANDARD_GRAVITY);
// the gyroscope is in radians per second.
//
// BY SEAT, NOT BY PAD. What is asked is kept for a player's seat (players.h)
// and applied to whichever pad sits there when it is read, so a pad that
// takes the seat later (a swap, a reconnect) is turned on as it is read, with
// nothing asked again. A pad with no such sensor reads nothing, and its
// player's game behaves as it did before #670. `reset` forgets every request;
// every game's start and end call it (core.cpp).
//
// THE FRAME THREAD ONLY, which is where a core runs and where SDL's events
// are pumped: players.h's seats are not guarded for any other thread.

#pragma once

namespace tilt {

enum class Sensor { Accel, Gyro };

// Turns `sensor` on or off for player `player` (0-based). On answers whether
// the pad in that seat now has the sensor and SDL turned it on; the request
// is kept either way. Off always succeeds.
bool set(int player, Sensor sensor, bool on);

// What the pad in seat `player` reads now, in SDL's units and axes, into
// out[0..2] (x, y, z). False, with zeros, when nothing was asked for that
// seat, the seat is empty, or its pad has no such sensor.
bool read(int player, Sensor sensor, float out[3]);

// Forgets every request and turns every open pad's sensors off.
void reset();

// --tilt-test: the rules above on pads made inside the test (SDL's virtual
// joysticks, one with both sensors and one with none), no real pad, no
// screen. 0 when all pass.
int test();

}  // namespace tilt
