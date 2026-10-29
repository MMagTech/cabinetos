// Which emulator plays an arcade game, from the files in its zip
// (docs/decisions/2026-10-28-arcade-emulator-from-the-zip.md).
//
// THE ZIP'S NAME IS THE SET'S NAME, AND ITS FILES CONFIRM IT. Every arcade
// emulator finds a game by the zip's name, so the name finds the set in each
// emulator's catalogue, and the files (each by CRC32 and size) say whether the
// zip is that emulator's version of it. Emulators are tried in a fixed order,
// Flycast, FinalBurn Neo, MAME 2003-Plus, and the first whose catalogue has
// the set, marks it working, and whose files the zip holds plays it. A set
// with a disc needs its CHD too, at the top of the game's folder
// (docs/decisions/2026-10-28-arcade-discs-one-folder.md), with the SHA1 the
// catalogue gives where it gives one. MAME 2003-Plus cannot open a 7z and is
// skipped for one. A folder's name never chooses.
//
// TWO QUESTIONS, ASKED AT DIFFERENT TIMES. A grid asks by the name alone
// (byName), since a list carries no file's contents: a set no emulator here
// has, or every one that has it marks not working, is greyed, and so is one
// any list marks as made for anything but a controller (a touchscreen, a
// mahjong panel, a gambling machine...), and a light-gun game until light
// guns are supported. Play asks by the files (byFiles), from
// RomM's list of what a loose zip holds, or from the archive itself for one
// inside a folder game; a set whose files do not fit says so then.
//
// THE CATALOGUES are `<core file stem>.catalogue` beside the cores, made in CI
// from each emulator's own list at its pin (cores/arcade-sets.sh, which says
// what a line holds). Read once, the first time a game asks. A missing one is
// said once in the log and reads as empty: that emulator plays nothing.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "romm_types.h"

namespace arcadematch {

// The RomM platform slugs whose games are arcade games, all picked here:
// `arcade` (RomM's own for MAME, FinalBurn Neo, Naomi and Atomiswave folders),
// `cps1`, `cps2`, `cps3`, `neogeomvs`, `neogeoaes`, `stv`, `model2`, `model3`.
// Dreamcast's folder is not one, though a zip in it is asked about Flycast.
bool isArcadeSlug(const std::string& slug);

// Whether a Dreamcast folder's game is asked here: a zip or a 7z, which
// Flycast would take for a Naomi board (its libretro.cpp treats every zip as
// one), so it goes to Flycast only when it is a Flycast set.
bool asksDreamcast(const std::string& fileName);

// Where the catalogues are: the core directory (catalog::setCoreDirectory).
void setDirectory(const std::string& dir);

// One file an archive holds, as RomM's `archive_members` gives it.
using Member = romm::ArchiveMember;

// A CHD at the top of the game's folder: its name without `.chd`, and the
// SHA1 its header gives (chdSha1).
struct Disc {
    std::string name;
    std::string sha1;
};

enum class Verdict {
    Plays,          // `core` plays it
    NotSupported,   // no emulator here has it working, or it is no controller's game
    NeedsGun,       // a light-gun game (until #575)
    FilesDiffer,    // the zip does not hold the files of the set it is named for
    DiscMissing,    // its set has a disc and no CHD of that name is beside it
    DiscDiffers,    // the CHD is there and is not the one the set lists
};

struct Answer {
    Verdict verdict = Verdict::NotSupported;
    // The emulator (a manifest core name): the one that plays it, or, when it
    // does not play, the first that has the set working.
    const char* core = nullptr;
    // The words a game's page shows in place of Play, when it does not play.
    std::string why;
};

// By the set's name alone (`fileName` is RomM's fs_name, `sf2.zip`).
Answer byName(const std::string& fileName, bool dreamcast);

// By the files: what the archive holds, and the CHDs beside it.
Answer byFiles(const std::string& fileName, const std::vector<Member>& members,
               const std::vector<Disc>& discs, bool dreamcast);

// What an archive on the disk holds: a zip's own directory, or for a 7z
// every file decompressed once (libarchive gives a 7z's names and sizes, not
// its CRCs). False for anything else, or a damaged archive.
bool readMembers(const std::string& path, std::vector<Member>* out, std::string* err);

// The SHA1 a CHD's header gives, as lower-case hex: the one MAME's lists
// name, for versions 3, 4 and 5. Empty when the file is not a CHD.
std::string chdSha1(const std::string& path);

// Whether a firmware file is one of the BIOS zips Flycast reads from
// `bios/dc/` (its `!bios` line): `naomi.zip`, `awbios.zip` and the rest.
bool flycastBios(const std::string& fileName);

// --arcade-match-test: the rule, every case, on catalogues written in the
// test. 0 when all pass.
int test();

}  // namespace arcadematch
