// GAMES ON SEVERAL DISCS (#162): one game, one save, a disc changed from
// the pause menu. Built once for every libretro disc system.
//
// THE RULES, as agreed on #162 (2026-10-10/11):
//   - A game's discs are one folder in RomM. Loose discs stay separate games.
//   - The console fetches every disc by its file id and writes ONE `.m3u`
//     per game, named after RomM's game, in the game's folder. The list is
//     what gives every disc one shared save: the cores name the save after
//     the content they were handed, and that is the list, whichever disc.
//   - Discs are ordered by a disc marker in the name, else by the part of
//     the names that differs, sorted naturally, else by name. A list the
//     library already has is read for order only, never run.
//   - The disc a person was last on is remembered per game and person, on
//     the console, by number: RomM keeps none, and its file ids change on a
//     rescan.
//
// What this file does not know: the core. Inserting and starting over are
// the app's (discs.cpp), through Core's disk control (core.h).

#pragma once

#include <string>
#include <vector>

#include "romm_types.h"
#include "storage.h"

namespace disc {

// WHAT A SYSTEM CAN DO WITH A GAME ON SEVERAL DISCS, from its core's source
// at our pins (#162, 2026-10-11).
enum class Change {
    None,        // not part of this work: the game is played as one file
    Insert,      // disk control: Insert and Start over, from the pause menu
    StartOver,   // no disk control in the core (3DO): Start over only
    Automatic,   // the game asks for its disc by name (GameCube): no menu,
                 // no remembered disc, no "Disc N of M" (Dolphin's port never
                 // says which disc it put in)
};

// HOW "START OVER ON DISC N" BOOTS DISC N.
enum class Boot {
    InitialImage,  // the core's own start-on-disc, before the load (PS1, Dreamcast)
    ListFirst,     // the list written with disc N first (Sega CD, GameCube)
    SwapFirst,     // disc N put in before the first frame (Saturn)
    File,          // disc N's own file, no list (3DO: its core reads none)
};

struct System {
    Change change = Change::None;
    Boot boot = Boot::ListFirst;
};
System systemFor(const std::string& platformSlug);
inline bool handles(const std::string& platformSlug) {
    return systemFor(platformSlug).change != Change::None;
}

struct Disc {
    // THE DISC'S OWN NUMBER, whatever the list order: the one its name gives
    // when every name numbers its disc from 1 ("(Disc 1)", "(Disc 3)" with
    // disc 2 missing are 1 and 3), else its place in the order, from 1.
    int number = 0;
    std::string file;    // the file's name in the game's folder
    std::string label;   // "Disc 2 (Soviet)"
};

// THE ORDER AND THE NAMES, from the discs' file names alone. `listed` is a
// list the library had (an .m3u's lines), used for the order when it names
// every disc. Labels are "Disc N", with whatever only that disc's name says
// in brackets after it ("(Allies)").
std::vector<Disc> fromNames(const std::vector<std::string>& names,
                            const std::vector<std::string>& listed = {});

// DISCS THE NAMES SAY ARE MISSING: every disc has a numbered marker, and a
// number below the highest is not there ("(Disc 1)", "(Disc 3)": 2). Said on
// the game page (#162); empty when the names do not number every disc.
std::vector<int> missing(const std::vector<std::string>& names);

// WHICH OF A GAME'S FILES ON ROMM TO FETCH, AND WHICH ARE ITS DISCS. Only
// the game's own files (top level, category "game"): no manuals, soundtracks
// or DLC. A sheet (.cue, .gdi, ...) is the disc when there is one, and its
// tracks are fetched beside it; otherwise each image is a disc. An .m3u is
// never fetched to the folder: its id is returned so its lines can be read.
struct Plan {
    std::vector<romm::RomFile> fetch;
    std::vector<std::string> discNames;   // as RomM names them, unordered
    int listFileId = 0;
    std::string listFileName;
};
Plan plan(const std::vector<romm::RomFile>& files);

// The lines of an .m3u, as file names: comments, blanks and directories
// dropped, a BOM and CRs stripped.
std::vector<std::string> readList(const std::string& text);

// THE CONSOLE'S LIST. Named after RomM's game, so the save a core names
// after it never changes. Plain relative names, LF, no blank lines, no
// labels, no BOM (#162: Genesis Plus GX turns a blank line into a disc).
// `first` puts that disc at the top (Boot::ListFirst); 0 keeps the order.
std::string listFileName(const std::string& fsName);
std::string listText(const std::vector<Disc>& discs, int first = 0);
// The order the list holds, as disc numbers: listOrder(discs, 3) for four
// discs is {3, 1, 2, 4}. The core's index i is disc order[i].
std::vector<int> listOrder(const std::vector<Disc>& discs, int first);

// THE DISCS IN A FOLDER, as the console last wrote them: `.discs` beside
// the list, one file name per line in disc order. What makes a game
// already on the drive playable with no server.
bool writeRecord(const std::string& entryDir, const std::vector<Disc>& discs);
std::vector<Disc> readRecord(const std::string& entryDir);

// THE DISC THIS PERSON WAS LAST ON (#162 item 8), and how many the game
// has, for the game page when the server is away. 0 when not known.
int lastDisc(const storage::User& u, int romId);
int knownCount(const storage::User& u, int romId);
void remember(const storage::User& u, int romId, int disc, int count);

// A SAVE STATE RECORDS ITS DISC in its name, "Panzer Dragoon (Disc 2)
// [2026-10-11 03-12-45-120].state", so it travels to every device with the
// state and needs no record beside it. Only on games with several discs.
std::string stateNamePart(int disc);
int discInStateName(const std::string& name);

// "Disc 2 of 4".
std::string ofText(int disc, int count);

// The disc numbered `n`, or nullptr; and the highest number, the "of M".
const Disc* byNumber(const std::vector<Disc>& discs, int n);
int highest(const std::vector<Disc>& discs);

// The rules above, checked with no core and no server: --disc-test.
int test();

}  // namespace disc
