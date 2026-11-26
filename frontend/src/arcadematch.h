// Which emulator plays an arcade game, from the files in its zip
// (docs/decisions/2026-10-28-arcade-emulator-from-the-zip.md).
//
// THE ZIP'S NAME IS THE SET'S NAME, AND ITS FILES CONFIRM IT. Every arcade
// emulator finds a game by the zip's name, so the name finds the set in each
// emulator's catalogue, and the files (each by CRC32 and size) say whether the
// zip is that emulator's version of it. Emulators are tried in a fixed order,
// Flycast, FinalBurn Neo, MAME 2003-Plus, sm2-emu, Supermodel, xemu-chihiro,
// MAME 0.289, and the first whose catalogue has the set, marks it working, and
// whose files the zip holds plays it: sm2-emu, made for Model 2, plays every
// Model 2 game its list marks working, Supermodel every Model 3 game, and MAME
// 0.289 gets only what nothing else plays.
//
// A CHIHIRO GAME IS ONE CONVERTED IMAGE, `<set>.bin`, made once by the
// library's owner from the game's disc and key (chihiro-netboot): its name is
// the set, and the game's own ID inside it, at boot.id 0x30 where the emulator
// reads it, confirms it (byImage, chihiroId). Only xemu-chihiro is asked about
// a `.bin`, and it is asked about nothing else. A set with a disc needs its CHD too, at
// the top of the game's folder
// (docs/decisions/2026-10-28-arcade-discs-one-folder.md), with the SHA1 the
// catalogue gives where it gives one. MAME 2003-Plus and Supermodel cannot
// open a 7z and are skipped for one. A folder's name never chooses.
//
// A LINDBERGH GAME IS ITS HARD DRIVE'S FOLDER, played by Linux Loader, and
// known as the loader knows it: by its program inside, never by the folder's
// name. A grid asks by the program's name (byProgram), the first of the
// loader's own names, in its order, that the game's files hold (the game's
// record keeps it, lindberghProgramNamed); Play confirms by the loader's
// fingerprint of the program on the disk (lindberghProgram,
// lindberghFingerprint, byFingerprint). Only Linux Loader is asked about a
// Lindbergh game, and it is asked about nothing else.
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
// THE CATALOGUES are `<core file stem>.catalogue` beside the cores, or for a
// program of its own `<its name>.catalogue` (sm2emu.catalogue,
// supermodel.catalogue, xemu-chihiro.catalogue, linuxloader.catalogue), made in CI
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
// `arcade`, `cps1`, `cps2`, `cps3`, `neogeomvs`, `neogeoaes`, `stv`, `model2`,
// `model3`, and the labels RomM gives each emulator's folder from
// rommapp/romm#5273 on: `mame`, `mame-advmame`, `fbneo`, `fba`, `naomi`,
// `naomi2`, `naomigd` and `atomiswave` (RomM 5.4 called them all `arcade`),
// `gaelco`, whose games FinalBurn Neo and MAME play, `chihiro`, whose
// games xemu-chihiro plays, and `lindbergh` (RomM 5.5.0-alpha.1 on;
// rommapp/romm#5273), whose games Linux Loader plays. RomM's other new boards
// (Triforce, Hikaru and the like) have no emulator here, so get no tile.
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
//
// THE SHA1 TELLS WHICH MAME A CHD WAS MADE FOR: MAME 2003-Plus and MAME
// 0.289 list different ones for the same disc, so a disc game that both have
// goes to the one whose disc it is.
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

// By a converted Chihiro image's ID (chihiroId): the set its name gives,
// which plays when the image carries that set's ID, and otherwise "<file>
// doesn't match its set".
Answer byImage(const std::string& fileName, const std::string& id);

// The ID a converted Chihiro image's boot.id carries, four letters or digits
// (`SBFZ`), or empty when the file is no such image.
std::string chihiroId(const std::string& path);

// A Lindbergh game by its program's name (`hummer_Master.elf`): plays in
// Linux Loader when the loader's table gives that program a game played on a
// controller (driving, flying, fighting, sports); "Needs a light gun" for a
// gun game's; "Not supported" for a mahjong or quiz panel's, or a name the
// loader does not start.
Answer byProgram(const std::string& programName);
// The same by the loader's fingerprint of the program (lindberghFingerprint),
// against its table of game versions (gameData.c); "Not supported" for one it
// does not know, a test menu, a server box or Segaboot.
Answer byFingerprint(uint32_t crc);
// The program Linux Loader starts in a game's folder on the disk, as it finds
// it: the first of its names, in its order, at the top of the folder, then in
// each folder one down by name (TeknoParrot's copies keep it in `elf/` or a
// folder named for the game), then two down (a zip or 7z that held the game's
// own folder), that is a program. Empty when there is none.
std::string lindberghProgram(const std::string& folder);
// The first of the loader's program names, in its order, among a game's file
// names (RomM's list of its files). Empty when none.
std::string lindberghProgramNamed(const std::vector<std::string>& fileNames);
// The loader's fingerprint of a program on the disk (mainL.c): CRC32 of
// 0x4000 bytes from 10 into its third program header's segment, when that is
// its code. False for a file that is no such program.
bool lindberghFingerprint(const std::string& path, uint32_t* crc);

// The name an emulator gives a set's saves, where its catalogue says one:
// xemu-chihiro's, its parent's for a revision (`outr2st` for `outr2sto`).
// Empty otherwise.
std::string gameName(const char* core, const std::string& fileName);

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

// How many buttons a set's player 1 has, by an emulator's catalogue, or 0
// when it does not say. Only MAME 0.289's, sm2-emu's and Supermodel's say:
// none names a game's buttons to the console itself, so the console declares
// them (fightstick::mameButtons, fightstick::programButtons).
int buttons(const char* core, const std::string& fileName);

// --arcade-match-test: the rule, every case, on catalogues written in the
// test, a Lindbergh folder and program made in the test among them. 0 when all
// pass.
int test();

// --arcade-changes <old dir> <new dir>: every set whose emulator, by its
// name, differs between two folders of catalogues; every set whose name gives
// the same one but whose files are tried on other emulators at Play, so a zip
// or a disc that does not fit the first may land elsewhere; and every set
// whose files changed in the emulator that plays it, so a zip that fitted may
// not now.
// CI runs it when a catalogue changes (ci/list-arcade-changes.sh): a game
// that changes emulator loses its saves and states (docs/decisions/
// 2026-10-28-emulator-updates-and-saves.md). Prints, and returns 0.
int changes(const std::string& oldDir, const std::string& newDir);

}  // namespace arcadematch
