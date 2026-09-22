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
#include <vector>

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
    // name. PPSSPP has since been built and PSP plays through the same
    // framebuffer path, so all three of the original cores are measured
    // running real games and none of them answers this. Nothing is queued
    // behind them — the next core that wants desktop GL will be the first.
    NeedsHardwareRender,
};

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

// --- Saves the CORE writes as a file ---------------------------------------
//
// THE MAJORITY OF THE SAVES ON A REAL SERVER, which is the thing that made
// this worth building. The audit of 2026-09-17 measured 47 of the 81 saves on
// the reference server — 58% — as belonging to platforms whose core never
// exposes RETRO_MEMORY_SAVE_RAM at all. It reads like an edge case and it is
// not one.
//
// A core in this class keeps the game's progress in a FILE it opens itself: a
// Dreamcast VMU image, a Sega CD backup-RAM dump, an arcade board's NVRAM, a
// Neo Geo Pocket flash, a DS cartridge SRAM, a 3DO NVRAM. `[save] battery is 0
// bytes` at launch is this console correctly reporting that there is no
// battery to read — the save is on the disk, not in the core's memory.
//
// So the mechanism is the one PSP already uses and for the same reasons: put
// the file there BEFORE the core loads the game, because these cores read it
// once while the machine is being built and never look again; read it back
// AFTER `retro_unload_game`, because that is where a core flushes what it has
// been buffering. See filesave.h for the halves and docs/PROJECT.md, *The save
// audit*, for the per-platform evidence.
//
// This is the table. filesave.{h,cpp} is the mechanism.

// How to tell a save somebody made from the empty thing a core writes just by
// being switched on.
//
// WITHOUT THIS EVERY LAUNCH UPLOADS. A core formats its own storage the first
// time it runs, the result is identical for everyone who plays that game, and
// it would then be pulled back down onto somebody's other device as if it
// meant something. The reference implementation's `isUntouchedNVRAM` exists
// for exactly this and the rules below are its per-platform refinements.
enum class Untouched {
    // Nothing but 0x00 and 0xFF: no core wrote anything worth keeping.
    Blank,
    // Every byte identical, whatever the byte is. MAME's fresh NVRAM is all
    // 0x01 for the capbowl family and all 0x00 elsewhere, and a seeded
    // bootstrap image is the same for everyone who plays that board.
    Uniform,
    // Genesis Plus GX puts its 64-byte format block at the END of a .brm, and
    // freshly formatted backup RAM carries allocation marks in the first 16
    // bytes — a repeating `ff fa 00 02`. Counting either as data uploads
    // formatted-empty images. Same layout at a bigger size for the cartridge.
    SegaCDBackup,
    // Opera formats a fresh NVRAM with a filesystem header — the volume block
    // and root directory, all inside the first 176 bytes. Data past that is
    // what distinguishes a real save.
    ThreeDONvram,
    // A PlayStation 2 card that has never been written to. MEASURED against
    // the reference server 2026-09-20 and it is not a subtle test: PCSX2
    // creates the file as 8,650,752 bytes of 0xFF and only the console's own
    // BIOS writes the format header, so an untouched card carries no magic at
    // all. Three of the four PS2 cards on that server were exactly this.
    //
    // THE RULE IS THE MAGIC AND NOTHING MORE, ON PURPOSE. The one real card
    // available to measure — Burnout 3 — has its save data out at 8,384 KB
    // while its FAT and directory sit in the first 128 KB, and a threshold
    // invented from ONE sample is how a rule silently refuses somebody's save.
    // A false negative loses a save; a false positive uploads a formatted
    // empty card, which is what the old behaviour did anyway. The weaker rule
    // is the safe direction until there is a second card to measure.
    Ps2Format,
    // A GameCube card with nothing in its directory. Block 0 is the header,
    // blocks 1 and 2 are the directory and its backup: 127 entries of 64
    // bytes, and an entry's first four bytes are the game code, 0xFF when the
    // slot is free. Measured both ways on 2026-09-20 — the three cards on the
    // reference server had zero entries in use, and a card Ikaruga had saved
    // to carried `GIKE`/`70`/`ikaruga_save_data`.
    GameCubeDirectory,
    // A VMU's own directory, and the one guard this project did not inherit.
    // MEASURED, 2026-09-19: three of the thirteen Dreamcast cards on the
    // reference server — Cannon Spike, Re-Volt, San Francisco Rush 2049 — hold
    // no file at all. They are formatted cards the reference implementation
    // uploaded because its Dreamcast path applies no freshness rule. A quarter
    // of the rows for the platform with the most of them carry nothing.
    //
    // A VMU is 128 KB of 512-byte blocks. Block 255 is the root and starts
    // with sixteen 0x55 bytes; blocks 253 down to 241 are the directory,
    // sixteen 32-byte entries each, and an entry's first byte is 0x33 for a
    // data file, 0xCC for a game and 0x00 for a free slot. So "this card holds
    // a save" is a fact the bytes state outright.
    VmuDirectory,
};

// One file a core writes for itself.
struct SaveFile {
    // Relative to the core's SAVE directory, which is
    // `users/<id> - <name>/saves/<platform>/<romId>/<core>/` and holds this one
    // game's files. Already has the ROM's stem substituted where the core names
    // the file after what it loaded.
    std::string path;
    // A suffix to scan the directory for after the core has shut down, when
    // the name the core chose cannot be predicted from here. Empty means
    // `path` is exact in both directions.
    std::string captureSuffix;
    // A name that ends in `captureSuffix` and belongs to a DIFFERENT region.
    // Sega CD is the case: `4Mbit_cart.brm` also ends in `.brm`, and without
    // this the cartridge and the internal RAM overwrite each other.
    std::string captureExclude;
    // The extension this console uploads the row under, which is also how a
    // restore tells two regions of one game apart. "srm" for the game's own
    // save and "cart" for Sega CD's external RAM cartridge — the reference
    // implementation's convention, and the reason its Lunar has two rows.
    std::string region = "srm";
    Untouched untouched = Untouched::Blank;
    // What the row on the server calls the core, or empty when naming the game
    // is enough.
    //
    // ARCADE AND ONLY ARCADE: one game can legitimately have two of these, one
    // per emulator, and RomM matches a row for overwrite by FILENAME ALONE
    // with the emulator tag not included. So two arcade cores both uploading
    // `smashtv (Cabinet).srm` would quietly overwrite each other's high scores
    // on the server despite being tagged differently — a fault nobody would
    // see until the scores were gone.
    //
    // The strings are the reference implementation's, not this console's
    // manifest names, and they were read off the rows already on the server
    // rather than guessed: `smashtv (Cabinet fbneo).srm` and `lethalen
    // (Cabinet mame2003Plus).srm`. Matching them is the point — see
    // main.cpp's saveRowName.
    std::string coreRowName;
    // DREAMCAST ONLY. Flycast keeps the VMU in the libretro SYSTEM directory —
    // `bios/dc/` here — because libretro gives a core exactly one of those and
    // that is where the core looks. It is the one file in `bios/` that cannot
    // be fetched again, which is the last rough edge the folder layout left
    // open: see docs/PROJECT.md, open question 18. So the card is placed there
    // for the length of a session and taken back out afterwards, and what
    // lives in `bios/dc/` at rest is `dc_nvmem.bin`, the console's own clock
    // and language, which rebuilds itself if it is lost.
    bool inSystemDir = false;
    // NAMED THE WAY THE MAC NAMES IT, rather than this console's own
    // `<game> (Cabinet).<region>`.
    //
    // PlayStation 2 and GameCube are the only platforms where a save already
    // exists on the server written by a DIFFERENT implementation of this same
    // product, and RomM matches a row for overwrite by filename alone. Cabinet
    // for Mac calls a PS2 card `cabinet-604.ps2`; this console would call it
    // `Burnout 3 Takedown (Cabinet).srm`. Same bytes, same emulator tag, two
    // rows — and a restore that never finds the card the person actually made.
    //
    // So for these two the Mac's convention wins, because matching it is the
    // entire point. It is a deliberate exception to the naming rule above and
    // not a drift into one.
    bool macRowName = false;
};

// --- Firmware a core can actually find --------------------------------------
//
// THE PROBLEM, PLAINLY: a core looks its BIOS up by a FIXED filename, and RomM
// serves firmware under whatever name the person who uploaded it chose. When
// the two disagree the file is downloaded, sits in `bios/`, and the core says
// it cannot find a BIOS — which is exactly as unhelpful as it sounds.
//
// It is not hypothetical and it is not rare. On the reference server, Saturn's
// BIOS is `saturn_bios.bin` and Beetle Saturn opens `sega_101.bin`, so **no
// Saturn game could start at all** until 2026-09-19. 3DO is the same shape and
// only works because that server happens to use the one name `opera_bios` is
// answered with. Sega CD and TurboGrafx-CD happen to match. Four platforms,
// two of them broken by luck.
//
// THE ONLY SIGNAL AVAILABLE IS THE SIZE. RomM's firmware record carries a
// filename and a length and says nothing about region or purpose, so matching
// by size is not a shortcut — it is the whole of what there is to match on.
//
// AND THE COPY GOES UNDER EVERY NAME, not the one that looks right. Beetle
// Saturn and Genesis Plus GX both pick their CD BIOS from the DISC's region
// code at runtime, with no fallback if that one file is absent — so which name
// is needed is not knowable when the file is being placed. Putting the same
// bytes under both names lets whichever one the disc asks for resolve. That is
// the reference implementation's reasoning and it is right.
struct FirmwareAliases {
    // The exact byte count of the real file. A downloaded firmware file of any
    // other size is a different thing and is left alone.
    int64_t sizeBytes = 0;
    // Every name a core serving this platform might open, in no order.
    std::vector<const char*> names;
    // A subdirectory of `bios/` to place it in as well, or empty. Flycast is
    // the one that wants this: it looks for the Dreamcast boot ROM under
    // `dc/`, which libretro-super's own `flycast_libretro.info` documents and
    // which the reference implementation confirmed the hard way — a flat
    // placement produces a SILENT fallback to the core's built-in HLE BIOS
    // with no error at all, so the console appears to work while running
    // something other than the machine it says it is.
    const char* subDir = nullptr;
};

// What to copy where, for this platform, or nothing for the platforms whose
// cores need no firmware or already agree with RomM about the name.
FirmwareAliases firmwareAliases(const std::string& slug, const std::string& fsSlug);

// What this platform's core writes, or empty for the cores that expose a
// battery and need none of this.
//
// Keyed on the platform rather than the core, because one core serves several
// systems and only some of them are in this class: Genesis Plus GX writes a
// `.brm` for Sega CD and nothing at all for Genesis, Game Gear or Master
// System, whose cartridges have real batteries the core exposes. The audit's
// own table is the source; `stem` is the basename of the file the core was
// handed, without its extension, which is what these cores name the save
// after.
std::vector<SaveFile> saveFiles(const std::string& slug, const std::string& fsSlug,
                                const std::string& stem);

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

// The tag a SAVE travels under, which is not always the tag a STATE travels
// under — and five platforms' worth of saves depend on the difference.
//
// THE PLAIN VERSION: a save state is a photograph of the emulator's own
// insides, so two builds of a core can disagree about it and the tag is what
// stops somebody being handed one that will not load. A file save is not that.
// A Dreamcast VMU image is 128 KB in the VMU's own format, an arcade NVRAM is
// the board's own chip, a Sega CD `.brm` is the machine's backup RAM. Those
// formats are defined by the hardware being emulated and no build flag moves
// them — which the audit proved by reading the bytes: "every other core
// uploads the emulator's own bytes, so anything that can read a save for those
// platforms can read what is on this server."
//
// WHY IT MATTERS HERE. `emulatorTag` is deliberately silent for five of the
// cores in the file-writing class — Flycast, Opera, FBNeo, MAME 2003-Plus and
// Beetle NGP — and silence means "do not upload". Between them those five hold
// 35 of the 47 file saves on the reference server, including all thirteen
// Dreamcast cards. Applying the state rule to them would leave the majority of
// this feature dead on arrival, and it would be the wrong rule: nothing about
// a VMU image can be unloadable.
//
// So states keep the strict rule and saves get this one. Flycast is the case
// that shows the two apart: its pinned commit does not reproduce what the
// reference implementation ships, because that build carries unscripted edits
// in its working tree (docs/NEXT-SESSION.md, *Cabinet-side debts*). That is a
// real reason to refuse a save STATE and no reason at all to refuse a memory
// card, so `emulatorTag` still returns nullptr for it and this returns
// `flycast-native`.
//
// Returns nullptr when even a save should stay local.
const char* saveTag(const char* manifestCoreName);

}  // namespace catalog
