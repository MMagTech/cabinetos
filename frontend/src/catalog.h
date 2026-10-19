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
//
// What each core saves, the tags a save and a state travel under and the
// firmware a core can find are in savetable.h, and the core options this
// console sets on purpose in coreoptions.h, both included below; moved out of
// this file in the restructure (#398).

#pragma once

#include <map>
#include <string>
#include <vector>

#include "coreoptions.h"
#include "romm_types.h"
#include "savetable.h"

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
    // wrong, the core simply has not been built yet.
    //
    // NOTHING ANSWERS THIS ON A HEALTHY CONSOLE, checked against the reference
    // A9 2026-09-22: all 23 cores this table names are in
    // /usr/lib/cabinetos/cores, 22 libretro plus PCSX2 as cabinetos-ps2.so.
    // GameCube is dolphin_libretro.so and IS a libretro core; PPSSPP is built
    // and PSP plays. An earlier version of this comment said otherwise on both
    // counts and was believed for a fortnight, which is the argument for
    // checking the disk rather than the comment.
    //
    // SO THIS IS A FAULT DETECTOR NOW, not a normal state. Cores ship in the
    // image and the image is atomic, so the only ways here are a staging
    // failure or a name that drifted out from under coreFileName — which is
    // exactly what ci/stage-image-payload.sh checks for, and why it prints any
    // .so it finds that is not on the list. Keep the stat. It is one syscall
    // and it turns a tile that fails on tap into a tile that says why.
    // Found by the hero offering an arcade game with no FBNeo on disk.
    NotInstalled,
    // A core exists but CabinetOS deliberately does not ship it. There is
    // always a reason, and `reason()` gives it, because a decision nobody can
    // recover is indistinguishable from a bug.
    Excluded,
    // The console runs this system, and THIS GAME was made for a controller
    // nobody has paired. Wii: a game that takes neither a Classic Controller
    // nor a GameCube pad needs a real Wii Remote (wii.h; docs/PROJECT.md open
    // question 35). Wii U: one that takes neither a Pro nor a Classic
    // Controller needs a Wii Remote or the GamePad (wiiu.h; open question 36).
    // An answer about one game
    // rather than its whole platform, so a platform's grid shows it, greyed
    // and saying why, rather than leaving it out.
    NeedsController,
    // The console runs this system, and no emulator here runs THIS GAME: an
    // arcade set in a generic arcade folder that neither FinalBurn Neo's nor
    // MAME 2003-Plus's set list has (#310). Also about one game, so also
    // shown greyed.
    UnsupportedGame,
};

// Whether a platform's grid shows this game greyed, saying why, rather than
// leaving it out: the answers about one game, not its whole platform.
bool shownGreyed(Support s);

struct Coverage {
    Support support = Support::NoCore;
    const char* core = nullptr;     // manifest core name, when there is one
    const char* reason = nullptr;   // why, when it is not simply playable
};

// The file a core was built into, given the manifest's name for it.
//
// ONE RULE, ONE PLACE, and it is here because having it in two cost a launch.
// A manifest name that already ends in `_libretro` does not get a second one:
// `fbneo_libretro` is filed as `fbneo_libretro.so`, not
// `fbneo_libretro_libretro.so`. `cores/build-core.sh` applies the same rule
// when it names the artifact and says so in a comment; coverageFor applied it
// too, and the LAUNCH path did not — so every arcade game on this console
// reported the core as playable from one code path and then failed to open it
// from the other, with a message about a file nothing ever builds. Found by
// running an FBNeo game, 2026-09-19, and it had been true since the core
// landed.
//
// THERE IS A THIRD COPY NOW and it is deliberate: ci/stage-image-payload.sh
// derives the same name to check that every core reached the image. It cannot
// call this, being a shell script that runs before anything is compiled, so
// the comment is on all three. It also PRINTS any .so it finds that is not on
// the list, which is how a name that drifts shows up as a line of output
// rather than as a platform the console says it cannot play.
std::string coreFileName(const std::string& manifestCoreName);

// Every libretro core the table plays a system on, by its manifest name, with
// the RomM platform slugs it serves: what the option check (optcheck.h) walks,
// so a core with no list in cores/options/ fails it rather than going
// unchecked. Not PCSX2, whose file is not a libretro core's, nor an emulator
// run as a program.
std::map<std::string, std::vector<std::string>> libretroCores();

Coverage coverageFor(const romm::Platform& p);

// The name to put on a tile. RomM's own `name` is not always enough to tell two
// platforms apart: the reference library holds two called "Arcade", with the
// same slug, differing only in `fs_slug` and in which core they need. A person
// looking at two identical tiles has no way to choose, so the ambiguous ones
// are qualified — "Arcade (FinalBurn Neo)" and "Arcade (MAME 2003-Plus)" — and
// everything else is left exactly as the server named it.
std::string displayName(const romm::Platform& p);

// THE QUALIFIER, ON ITS OWN. "Arcade (FinalBurn Neo)" is one string where a
// title is wanted — a grid's heading, a launch screen — and two facts where a
// tile is: a NAME and the thing that tells it apart from the other tile with
// the same name.
//
// Keeping them joined cost more than tidiness. At the width a library tile
// gives a Title 3 name, "Arcade (FinalBurn Neo)" and "Arcade (MAME 2003-Plus)"
// both came out as "Arcade" over "(FinalBurn ..." and "(MAME 200..." — so the
// qualifier that exists SOLELY to tell two tiles apart was the part being cut
// off. Split, the name fits on one line and the qualifier goes to the line
// that has room for it.
//
// Empty for every platform that was never ambiguous, which is most of them.
std::string displayQualifier(const romm::Platform& p);

// The same answer in a handful of words, for a place that has a handful of
// words' worth of room — a library tile's second line.
//
// `Coverage::reason` is a sentence, and a sentence truncated to "no core in
// the ..." tells a person strictly less than nothing: they can see the tile is
// dimmed, and the words that would explain it have been cut off. So the tile
// gets the short form and the launch screen, which has a column to itself, gets
// the sentence.
const char* shortReason(Support s);
// The same, for a platform: an emulator that is its own application is not a
// core, so it is never "not built"; it is waiting for first boot to install it.
const char* shortReason(const Coverage& c);

// The same question asked of a game. A ROM payload carries its own platform
// slug and fs_slug, so Home can decide whether the most recently played game is
// one this console can resume without fetching the platform list first.
//
// AND IN A GENERIC ARCADE FOLDER IT PICKS THE CORE (#310). A folder whose name
// is an arcade emulator's (`FBNEO`, `fb-neo`, `MAME 2003-Plus`, ...: capitals,
// spaces, dashes, underscores, dots and `+` ignored) runs every game in that
// emulator, as before. Any other folder RomM files as arcade (`arcade`,
// `mame`, `cps`) picks per game, by the set name the zip is called: FinalBurn
// Neo if its set list has it, else MAME 2003-Plus if its list does, else
// UnsupportedGame. The lists are `<core>.sets` beside the cores, made from each
// core's own DAT at its pinned commit (cores/arcade-sets.sh).
Coverage coverageFor(const romm::Game& g);

inline bool playable(const romm::Platform& p) {
    return coverageFor(p).support == Support::Playable;
}

inline bool playable(const romm::Game& g) {
    return coverageFor(g).support == Support::Playable;
}

}  // namespace catalog
