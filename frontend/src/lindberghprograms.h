// What each Lindbergh game and each Windows arcade game in RomM holds, learned
// once from RomM and kept on the console, so a grid can tell a game played on
// a pad from a gun game before anything is downloaded (#620, #762;
// arcadematch.h): a Lindbergh game's program, a Windows game's id.
//
// WHY A STORE OF ITS OWN. A Lindbergh game is its hard drive's folder, kept in
// RomM as that folder or as a zip or 7z of it, and Linux Loader knows it by the
// program inside (arcadematch.h). RomM's lists send no file names, and a
// game's own entry, which does, can be large: Hummer Extreme's TeknoParrot
// copy holds 124,777 files, and its entry was 104 MB and 25 s on the
// write-test server (2026-11-26). So each game's entry is asked for once, the
// name its files give is kept here with the size RomM gave the game, and
// asked again only when that size changes.
//
// NOTHING IS WRITTEN WHEN RomM GAVE NOTHING TO READ IT FROM: a game that is
// one zip or 7z of which RomM listed no member. RomM gives up opening an
// archive after its SEVEN_ZIP_TIMEOUT (180 s by default) and records it as
// one file with no members (a 10 GB zip of 37,002 files, 2026-11-27), so a
// name kept as none on the disk would outlast the owner's rescan with a
// longer limit. Its answer, none, is held in memory instead, so the grid and
// the game's page say Not supported at once as for any game whose files give
// none; and every library load, and every Play, asks RomM again, until RomM
// lists its members and the name they give is kept on the disk. Not in the game's record
// (cache.h), which is kept only for a game on the console and is what the
// console shows with no server. A Windows arcade game is kept in RomM the
// same three ways, and windowsarcadeloader knows it by the name of the
// `<gameid>.windowsloader` file inside, so it is learned and kept the same
// way, in a file of its own.
//
// WHERE: `config/lindbergh-programs.json`, `{"<rom id>": {"size": <bytes>,
// "program": "hummer_Master.elf"}}`, and `config/windows-arcade-games.json`,
// `{"<rom id>": {"size": <bytes>, "game": "<gameid>"}}`; an empty name for a
// game whose listed files give none. Safe from any thread.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "romm.h"

namespace lindberghprograms {

// Which name is kept: a Lindbergh game's program, or a Windows arcade game's
// id.
enum class Kind { Lindbergh, Windows };

// The name known for the game at RomM's size of it: kept on the disk, or,
// for a game RomM listed nothing inside, none, held in memory since RomM was
// last asked in this run. False when neither is at that size; true with an
// empty `name` when its files give none.
bool known(Kind kind, int romId, int64_t size, std::string* name);

// The same, kept on the disk only: what is never asked of RomM again.
bool kept(Kind kind, int romId, int64_t size, std::string* name);

// RomM's list of a game's files, as Client::fetchRomFiles gives it. False
// with `err` when RomM did not answer.
using Fetch = std::function<bool(int romId, std::vector<romm::RomFile>* files, std::string* err)>;

// Asks RomM for the game's entry and keeps the name its files give
// (namedIn) on the disk; for a game RomM listed nothing inside (listed), it
// holds none in memory instead, and one kept on the disk later clears it.
// False with `err` when RomM did not answer; nothing is kept then.
bool learn(romm::Client& client, Kind kind, int romId, int64_t size, std::string* name,
           std::string* err);

// Each of a folder's `games` not kept on the disk at its size, learned in
// turn, until `over` says to stop: a game held only in memory is asked
// again. On the library read's worker. The second form takes RomM's answers
// from `fetch` (the self-test's).
void learnAll(romm::Client& client, Kind kind, const std::vector<romm::Game>& games,
              const std::function<bool()>& over);
void learnAll(const Fetch& fetch, Kind kind, const std::vector<romm::Game>& games,
              const std::function<bool()>& over);

// The name a game's files give, from RomM's list of them (each file's own
// name, and the last part of each name inside an archive): for Lindbergh the
// first of Linux Loader's program names, in its order
// (arcadematch::lindberghProgramNamed); for Windows the id of the first
// `.windowsloader` file (arcadematch::windowsIdNamed).
std::string namedIn(Kind kind, const std::vector<romm::RomFile>& files);

// Whether RomM's list of a game's files gives anything to read its name
// from: false when the game is one zip or 7z (arcadematch::isArchive) of
// which RomM listed no member, true for any other list, a folder's files
// and an archive whose members are listed among them, even when none of
// them is a program or a loader file.
bool listed(const std::vector<romm::RomFile>& files);

}  // namespace lindberghprograms
