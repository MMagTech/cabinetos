// Emulators that are whole applications, played as games.
//
// WHAT THIS IS FOR. Switch (Eden), PS3 (RPCS3), Xbox (xemu) and Wii U (Cemu)
// are not libretro cores and cannot be loaded into this process the way the
// other systems are (PS2 included). They come from Flathub, pinned in
// flatpaks.list, and each one opens its own window and draws its own picture.
// So the console starts the emulator as a separate program, hands it the
// television, and takes the screen back when it ends. docs/PROJECT.md, open
// questions 21, 24 and 32; issue #169.
//
// ONE MECHANISM, ONE ROW PER EMULATOR. Everything here is the same for all of
// them; what differs (the Flatpak, the program's name, where its keys and
// settings go) is one row in the table in standalone.cpp and one `prepare`
// case. PS3 is meant to be a row, not a second copy of this file.
//
// WHERE ITS STATE LIVES. Each emulator gets `emulators/<core>/` under the
// storage root and runs with that as its working directory. Eden treats a
// `user` folder in its working directory as its whole home (keys, system
// memory, saves, settings, shader cache), so nothing lands in the Flatpak's
// hidden default under ~/.var/app, and no source patch is needed. Batocera
// patched Citron's source to move its keys; this does not have to.
//
// MEASURED ON THE A9, 2026-09-28, before any of this was written: Eden 0.2.1
// from Flathub played Retro City Rampage DX with RomM's v19 keys and no
// firmware; gamescope gave it the screen on its own; SIGTERM to Eden closed
// it cleanly in 0.8 s with `confirmStop=2`.

#pragma once

#include <cstdint>
#include <string>

namespace cab::standalone {

struct Emulator {
    // The name the catalog uses for it, the way a libretro row names its core.
    const char* core;
    // The Flatpak it comes from.
    const char* flatpak;
    // The program inside the sandbox, which is the process that takes signals.
    const char* program;
    // Its own log, under its home, and the two phrases it writes there when a
    // game will not load. That case is an error box on the television that
    // nobody can close, so the console reads the log and closes it instead.
    const char* log;
    const char* loadFailed[2];
    // And the ones among those failures that mean the keys on the console are
    // too old for this game: a newer prod.keys in RomM is the whole fix, so
    // the console says that rather than "Couldn't start".
    const char* keysTooOld[4];
    // What it writes when the game stops by itself, which leaves the
    // emulator's own window on the television rather than closing it. The
    // console closes it instead, as if the game had been exited.
    const char* gameEnded;
    // The game files it opens as they are, libretro's `valid_extensions`
    // format. The console never unpacks them.
    const char* extensions;
};

// The emulator for a catalog core name, or nullptr for an ordinary core.
const Emulator* find(const std::string& core);

// Whether this console has it. First boot installs it from Flathub
// (cabinetos-flatpak-setup), so on a new machine, or one with no internet,
// the answer can be no for a while or for good.
bool installed(const Emulator& e);

// Its whole home: `<storage root>/emulators/<core>`.
std::string home(const Emulator& e);

// Everything the emulator needs before a game starts: the folders, its keys
// out of `bios/` under the names it opens, and the settings the console
// decides for it (fullscreen, no prompts, docked).
//
// `saveDir` is this person's folder for this game (storage::savesDir), and
// the emulator writes the game's save inside it, so two people and two games
// never share one. `player` is the name a game shows for the person playing.
//
// False with `*missingKeys` when the keys it cannot run without are not on
// the console; false with an `*err` for anything else.
//
// `players` is how many virtual controllers (vpad.h) there are, and each
// player's controls are written for theirs, in the console's order.
bool prepare(const Emulator& e, const std::string& saveDir, const std::string& player,
             int players, bool* missingKeys, std::string* err);

// Where, inside `saveDir`, the game's own save folders are: the root of what
// travels to RomM as a zip, whose top-level folders are one per game. For
// Eden, `user/save/0000000000000000/<profile>`, each folder inside named by
// the game's title ID, which is why no title ID is ever asked for.
std::string saveRoot(const Emulator& e, const std::string& saveDir);

// One running game.
class Run {
public:
    // Starts `romPath` in `e`, able to write `saveDir`. False with a reason
    // when it could not start.
    bool start(const Emulator& e, const std::string& romPath, const std::string& saveDir,
               std::string* err);
    // Once a frame. True while the game is still going; false once it has
    // ended, after which `ended()` says how.
    bool poll();
    // Asks it to close the way its own window close would: SIGTERM to the
    // program. `poll` forces it after a deadline if it does not. A frozen
    // program is thawed first, or it never hears the request (measured: Eden
    // asked while frozen had to be forced after the full grace).
    void stop();
    // THE PAUSE MENU'S PAUSE (decision C): the whole program stops where it
    // is, sound and all, and carries on from the same instant when thawed.
    // Measured on the A9 2026-09-28: frozen for 10, 20 and 30 s, Eden's
    // picture held still and moved again the moment it was thawed.
    void freeze();
    void thaw();
    bool frozen() const { return frozen_; }

    bool active() const { return root_ > 0; }
    bool stopping() const { return stopAtMs_ > 0; }

    enum class End { None, Asked, Quit, Crashed, CouldNotLoad, KeysTooOld };
    End ended() const { return ended_; }

private:
    void kill9();

    const Emulator* emu_ = nullptr;
    int root_ = -1;          // what we started: the sandbox
    int program_ = -1;       // the emulator itself, once it is found
    int64_t startMs_ = 0;
    int64_t stopAtMs_ = 0;
    std::string logPath_;    // the emulator's own log, read for a failed load
    long logRead_ = 0;
    bool failedLoad_ = false;
    bool oldKeys_ = false;
    bool frozen_ = false;
    End ended_ = End::None;
};

}  // namespace cab::standalone
