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
// asked again only when that size changes. Not in the game's record
// (cache.h), which is kept only for a game on the console and is what the
// console shows with no server. A Windows arcade game is kept in RomM the
// same three ways, and windowsarcadeloader knows it by the name of the
// `<gameid>.windowsloader` file inside, so it is learned and kept the same
// way, in a file of its own.
//
// WHERE: `config/lindbergh-programs.json`, `{"<rom id>": {"size": <bytes>,
// "program": "hummer_Master.elf"}}`, and `config/windows-arcade-games.json`,
// `{"<rom id>": {"size": <bytes>, "game": "<gameid>"}}`; an empty name for a
// game whose files give none. Safe from any thread.

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

// The name kept for the game at RomM's size of it. False when none is kept
// at that size; true with an empty `name` when its files give none.
bool known(Kind kind, int romId, int64_t size, std::string* name);

// Asks RomM for the game's entry and keeps the name its files give
// (namedIn). False with `err` when RomM did not answer; nothing is kept then.
bool learn(romm::Client& client, Kind kind, int romId, int64_t size, std::string* name,
           std::string* err);

// Each of a folder's `games` not known at its size, learned in turn, until
// `over` says to stop. On the library read's worker.
void learnAll(romm::Client& client, Kind kind, const std::vector<romm::Game>& games,
              const std::function<bool()>& over);

// The name a game's files give, from RomM's list of them (each file's own
// name, and the last part of each name inside an archive): for Lindbergh the
// first of Linux Loader's program names, in its order
// (arcadematch::lindberghProgramNamed); for Windows the id of the first
// `.windowsloader` file (arcadematch::windowsIdNamed).
std::string namedIn(Kind kind, const std::vector<romm::RomFile>& files);

}  // namespace lindberghprograms
