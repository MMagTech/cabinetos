// PlayStation 2, which is an emulator rather than a libretro core.
//
// WHAT THIS IS AND WHY IT IS NOT IN core.cpp. Twenty-one of this console's
// emulators are libretro cores: the frontend loads a `.so`, calls `retro_run`
// once per frame, and is handed back a picture and some samples. PCSX2 is a
// whole emulator. It runs its own machine on its own thread and does not
// return until the game stops, which is the same shape Cabinet found on the
// Mac — `CabinetPS2::Run` "blocks for the entire life of the game and must be
// given its own thread".
//
// So the frame loop cannot drive it. What it can do is take the finished
// picture and the finished sound, hand it the pad, and stop it — which is
// exactly what this header is, and it is deliberately the same four things
// `Core` already does for everything else. `core.cpp` routes to here when the
// platform is `ps2`; nothing above that layer learns a new concept.
//
// THE EMULATOR ARRIVES AS A `.so` AND IS `dlopen`ED, like every core. That is
// not incidental: PCSX2 is GPLv3 and this repository is MIT, and
// docs/LICENCES.md rests its position on exactly that distinction. See
// frontend/ps2/CabinetPS2Bridge.cpp.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ps2 {

// Opens cabinetos-ps2.so. Returns false with a reason on `error()` — a console
// missing it should say so once, plainly, rather than failing later in a way
// that reads as a broken game.
bool load(const std::string& soPath);
void unload();
bool loaded();

const std::string& error();

// The PCSX2 revision the shared object was built from. Empty when nothing is
// loaded. The same question `retro_get_system_info` answers for a core, for
// the same reason: an emulator that cannot say which revision it is cannot be
// audited, and a save state's compatibility is a fact about the build.
std::string version();

// Starts a game.
//
// The folders are given separately rather than derived from one root, because
// on this console they live in three different places for three different
// reasons: the firmware comes from RomM and is shared, the card belongs to one
// person and one game, and the scratch is throwaway. Deriving them from a
// single root put PCSX2's card and log under the BIOS directory, which is
// root-owned, and it failed to create either.
//
// `memoryCard` is a bare filename inside `memcardsDir`. The caller names it the
// way catalog::saveFiles already says a PlayStation 2 card is named, so the
// whole existing save machinery works on it unchanged.
bool startGame(const std::string& discPath, const std::string& biosDir,
               const std::string& memcardsDir, const std::string& memoryCard,
               const std::string& scratchDir, const std::string& resourcesDir,
               float upscale);

// Asks the game to stop and WAITS. Blocking is the point: PCSX2 flushes the
// memory card during shutdown, so a card captured before this returns is the
// card as it was when the game started.
void stopGame();

// Whether the emulated machine is running, as opposed to still being built.
bool running();

// Freezes the emulated machine, for the in-game overlay. Not optional the way
// it is for a libretro core: those stop because the frame loop stops stepping
// them, and this one runs on a thread of its own.
void setPaused(bool paused);

void setPad(int port, uint32_t buttons, float leftX, float leftY, float rightX, float rightY,
            float leftTrigger, float rightTrigger);

// Takes the newest finished frame, if there is one. The pixels stay valid
// until the next call.
bool takeFrame(const uint32_t** pixels, unsigned& width, unsigned& height);

// Drains sound as interleaved 16-bit stereo, appending to `out` — the same
// contract as `Core::drainAudio`.
void drainAudio(std::vector<int16_t>& out);
unsigned sampleRate();

// Live performance. `readbackUs` is what reading one finished frame out of the
// GS costs, and the whole picture path rests on it staying small.
void metrics(float& fps, float& speed, double& readbackUs);

} // namespace ps2
