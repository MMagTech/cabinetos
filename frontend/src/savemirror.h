// RomM's saves for the games on the drive, as last seen, so offline play
// starts from them (#88). MMagTech, 2026-10-05, the vacation case: a game
// played last on the Mac the night before a trip must not start on holiday
// from the console's older copy.
//
// WHAT IT IS: per person, per game, the list of save rows RomM returned and
// the bytes of the rows a launch would restore. Offline, a launch reads this
// exactly as it reads the server online (savesFor, in main.cpp), so the one
// rule stays the rule: the console's own copy wins only while its upload is
// still owed (cache::isPending); otherwise the server's newest does.
//
// WHY IT CAN NEVER BE OLDER THAN A SAVE THE CONSOLE SENT: an upload that lands
// forgets this game's copy before anything else, and only then asks the
// server again. Should that ask fail, there is no copy, and offline falls back
// to the console's own save, which is the one just sent.
//
// Where: `users/<id> - <name>/server-saves/<romId>/`: `saves.json` (the rows)
// and `<assetId>-<updated>.bin` (the bytes of a row at that version).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "romm.h"
#include "storage.h"

namespace savemirror {

// The rows RomM returned for this game. Bytes kept for rows no longer listed,
// or at another version, are deleted.
void putList(const storage::User& u, int romId, const std::vector<romm::Asset>& rows);

// False when there is no copy for this game (never seen, or forgotten).
bool getList(const storage::User& u, int romId, std::vector<romm::Asset>* rows);

void putBytes(const storage::User& u, int romId, const romm::Asset& row,
              const std::vector<uint8_t>& bytes);
// Empty when these exact bytes (this row at this version) are not kept.
std::vector<uint8_t> getBytes(const storage::User& u, int romId, const romm::Asset& row);
bool hasBytes(const storage::User& u, int romId, const romm::Asset& row);

// Deletes this game's copy entirely.
void forget(const storage::User& u, int romId);

// A FETCH THAT STARTED BEFORE A FORGET MUST NOT PUT ITS OLDER LIST BACK. The
// fetch takes a mark before asking the server, and skips a game whose copy was
// forgotten after it (an upload landed meanwhile).
uint64_t mark();
bool forgottenSince(int romId, uint64_t mark);

}  // namespace savemirror
