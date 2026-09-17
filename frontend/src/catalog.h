// Which of a RomM library's platforms this console can actually play.
//
// A RomM server holds whatever its owner put in it. CabinetOS ships a fixed set
// of cores, so the two do not match and the gap is not small: the reference
// library has 35 platforms and six of them — Jaguar, ColecoVision, Switch, PS3,
// Vita, Wii — have no core in Cabinet's manifest at all.
//
// A console must not offer a game it cannot run. That is a promise it breaks in
// the worst possible place, after the person has chosen something and is waiting
// for it. But a library that silently drops 240 games is its own bug: someone
// who owns Switch games and sees none will reasonably conclude the scan failed.
//
// So the answer is neither "show everything" nor "hide quietly" — it is to KNOW,
// per platform, and to say so. This is the knowing part.
//
// Keyed on `slug`, with `fsSlug` breaking the tie, because those travel between
// servers whereas `id` does not — the opposite of identity WITHIN one server,
// where `id` is the only unique field. "Arcade" is the case that needs both:
// two platforms, one slug, different cores.

#pragma once

#include <map>
#include <string>

#include "romm.h"

namespace catalog {

// Where the core actually has to be for a game to start. Set once at startup;
// coverageFor answers from the manifest alone, and `installed` is the separate
// question of whether THIS console has that core built.
void setCoreDirectory(const char* dir);

enum class Support {
    // A core exists and ships. The game can be launched.
    Playable,
    // Nothing in the manifest serves this system. Switch, PS3, Vita and the
    // rest. Not a defect, just outside what this console is.
    NoCore,
    // The manifest has a core for this system, and this console does not have
    // it built. A different thing from NoCore and from Excluded: nothing is
    // wrong, the core simply has not been built yet. All twenty-one libretro
    // cores are built as of PPSSPP; the two rows that still answer this are
    // GameCube and PS2, whose emulators are not libretro cores at all.
    // Found by the hero offering an arcade game with no FBNeo on disk.
    NotInstalled,
    // A core exists but CabinetOS deliberately does not ship it. There is
    // always a reason, and `reason()` gives it, because a decision nobody can
    // recover is indistinguishable from a bug.
    Excluded,
    // The core is built and sitting on this disk, and the console still cannot
    // run it, because it renders through a graphics context rather than
    // handing back pixels and the host cannot serve the one it asks for.
    //
    // A FOURTH ANSWER RATHER THAN NotInstalled, because "we have not built it"
    // and "we cannot drive it" lead to different work, and because the file
    // being present would otherwise make the console offer a Dreamcast game it
    // cannot start. That is the exact bug NotInstalled was added for, one layer
    // further in.
    //
    // NOTHING ANSWERS THIS TODAY. It used to cover Flycast, Mupen64Plus and
    // PPSSPP wholesale; the host now hands a hardware-rendered core a
    // framebuffer in its own GLES context, and the first two are measured
    // running real games from the library. What remains is the narrower case
    // the value was always really about: a core that wants desktop GL or
    // Vulkan, which this context is not and which `core.cpp` turns down by
    // name. PPSSPP is not built yet and is the next one to find out about.
    NeedsHardwareRender,
};

struct Coverage {
    Support support = Support::NoCore;
    const char* core = nullptr;     // manifest core name, when there is one
    const char* reason = nullptr;   // why, when it is not simply playable
};

Coverage coverageFor(const romm::Platform& p);

// The name to put on a tile. RomM's own `name` is not always enough to tell two
// platforms apart: the reference library holds two called "Arcade", with the
// same slug, differing only in `fs_slug` and in which core they need. A person
// looking at two identical tiles has no way to choose, so the ambiguous ones
// are qualified — "Arcade (FinalBurn Neo)" and "Arcade (MAME 2003-Plus)" — and
// everything else is left exactly as the server named it.
std::string displayName(const romm::Platform& p);

// The same answer in a handful of words, for a place that has a handful of
// words' worth of room — a library tile's second line.
//
// `Coverage::reason` is a sentence, and a sentence truncated to "no core in
// the ..." tells a person strictly less than nothing: they can see the tile is
// dimmed, and the words that would explain it have been cut off. So the tile
// gets the short form and the launch screen, which has a column to itself, gets
// the sentence.
const char* shortReason(Support s);

// Deliberate core-option choices, keyed by the core's file or manifest name.
//
// EMPTY IS THE CORRECT STARTING POINT AND IT IS NOT THE OLD BEHAVIOUR. With no
// overrides at all, every option a core declares is still answered — with the
// core's own stated default. That alone fixes the thing that was actually
// broken: an unanswered option is not the default, it is the zero the C global
// was initialised to. See core.h.
//
// This is where a choice goes when CabinetOS wants something OTHER than what a
// core ships with. Cabinet hand-picks a subset per platform rather than dumping
// everything a core reports — see docs/CABINET.md, `NativeCoreOptions.swift` —
// and that list is the obvious thing to bring across, one platform at a time,
// with a reason recorded for each.
std::map<std::string, std::string> optionOverrides(const std::string& coreName);

// For the one platform whose save is a DIRECTORY rather than a file: where that
// directory sits under the save directory, or nullptr for everything else.
//
// PSP saves into a memory stick — `PSP/SAVEDATA/<GAMEID><TITLE>/` holding
// PARAM.SFO, DATA.BIN and icons — because that is what PPSSPP reads and writes
// on every platform, and there is no single-file PSP save anywhere. It travels
// to RomM as a zip; see dirsave.h for why zip and where the archive is rooted.
//
// Deliberately NOT the whole `PSP/` tree: NAND, PPSSPP_STATE and SYSTEM/CACHE
// sit beside SAVEDATA and are this machine's own state, save states and
// compiled shaders. Uploading them would put tens of megabytes of nothing on
// the server and mean nothing on the other end — the reference implementation
// says exactly that and it is right.
const char* directorySaveRoot(const char* core);

// The same question asked of a game. A ROM payload carries its own platform
// slug and fs_slug, so Home can decide whether the most recently played game is
// one this console can resume without fetching the platform list first.
Coverage coverageFor(const romm::Game& g);

inline bool playable(const romm::Platform& p) {
    return coverageFor(p).support == Support::Playable;
}

inline bool playable(const romm::Game& g) {
    return coverageFor(g).support == Support::Playable;
}

// The tag a save or state is filed under on RomM, for a given manifest core
// name. This is the compatibility marker: Cabinet greys out a state whose tag
// does not match the core about to run, which is what stops someone being
// offered a save that cannot load.
//
// **CabinetOS shares Cabinet's tags, and only because of work already done.**
// A Gambatte state is bit-identical between Cabinet's macOS arm64 build and a
// Linux x86-64 build AT THE SAME COMMIT; core-manifest.json pins that commit,
// build-core.sh asserts it, and CI proved the artifact reproducible across two
// machines. Those three together make a shared tag a fact rather than a hope,
// and they are why a state written on an Apple TV loads on this console.
//
// The rule for adding a core: share Cabinet's tag ONLY where the build is
// provably the same thing — same pinned commit AND the same build arguments.
// Where CabinetOS pulls a different lever, it must use a different tag, or
// Cabinet will offer someone a state that cannot load. Wrong in the safe
// direction costs a greyed-out entry; wrong the other way costs progress.
//
// Returns nullptr for a core whose tag has not been settled, which is a refusal
// to upload rather than a licence to guess.
const char* emulatorTag(const char* manifestCoreName);

}  // namespace catalog
