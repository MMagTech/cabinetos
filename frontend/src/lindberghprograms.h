// Which program each Lindbergh game in RomM holds, learned once from RomM and
// kept on the console, so a grid can tell a game played on a pad from a gun
// game before anything is downloaded (#620; arcadematch.h).
//
// WHY A STORE OF ITS OWN. A Lindbergh game is its hard drive's folder, kept in
// RomM as that folder or as a zip or 7z of it, and Linux Loader knows it by the
// program inside (arcadematch.h). RomM's lists send no file names, and a
// game's own entry, which does, can be large: Hummer Extreme's TeknoParrot
// copy holds 124,777 files, and its entry was 104 MB and 25 s on the
// write-test server (2026-11-26). So each game's entry is asked for once, the
// program its files name is kept here with the size RomM gave the game, and
// asked again only when that size changes. Not in the game's record
// (cache.h), which is kept only for a game on the console and is what the
// console shows with no server.
//
// WHERE: `config/lindbergh-programs.json`, `{"<rom id>": {"size": <bytes>,
// "program": "hummer_Master.elf"}}`, an empty program for a game whose files
// name none of the loader's programs. Safe from any thread.

#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "romm.h"

namespace lindberghprograms {

// The program kept for the game at RomM's size of it. False when none is
// kept at that size; true with an empty `program` when its files name none.
bool known(int romId, int64_t size, std::string* program);

// Asks RomM for the game's entry and keeps the program its files name: the
// first of Linux Loader's names, in its order, among the folder's files or
// what RomM lists inside its zip or 7z (arcadematch::lindberghProgramNamed).
// False with `err` when RomM did not answer; nothing is kept then.
bool learn(romm::Client& client, int romId, int64_t size, std::string* program,
           std::string* err);

// Each of a Lindbergh folder's `games` not known at its size, learned in
// turn, until `over` says to stop. On the library read's worker.
void learnAll(romm::Client& client, const std::vector<romm::Game>& games,
              const std::function<bool()>& over);

// The program a game's files name, from RomM's list of them: each file's own
// name, and the last part of each name inside an archive.
std::string namedIn(const std::vector<romm::RomFile>& files);

}  // namespace lindberghprograms
