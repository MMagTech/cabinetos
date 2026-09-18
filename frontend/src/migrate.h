// Moving what is already on a console into the layout storage.h describes.
//
// This exists because the layout changed after there were real files on real
// machines, and one category of them cannot be fetched again. A ROM comes back
// from RomM; a save state written on a console that could not see its server
// comes back from nowhere. So this is written to the standard that category
// deserves rather than the one a cache deserves:
//
//   NOTHING IS DELETED. Every step is a rename, and a rename that cannot be
//   made is reported rather than worked around.
//
//   EVERY STEP IS RECORDED BEFORE THE NEXT ONE STARTS. The manifest is appended
//   and flushed per move, so a migration interrupted by a power cut is still
//   completely reversible — the record is on the disk, not in this process.
//
//   IT IS VERIFIED BY READING THE FILES BACK. Save data is hashed before the
//   move and hashed again at the destination, and a mismatch stops the run.
//   "The function returned true" is not evidence that a save survived.
//
//   WHAT CANNOT BE ATTRIBUTED IS SET ASIDE, NOT GUESSED AT. The old layout gave
//   every core one flat save directory, so a file in it — `scd_U.brm`,
//   `pcsx-card2.mcd` — genuinely does not say which game wrote it. Those move
//   somewhere obvious and are listed by name; a wrong attribution would be a
//   save filed against the wrong game, which is worse than one filed nowhere.
//
// Run it with `--migrate --dry-run` first: that prints the whole plan and
// touches nothing.

#pragma once

#include <string>
#include <vector>

#include "romm.h"

namespace migrate {

// One rename, and whether its contents have to survive it provably.
struct Move {
    std::string from;
    std::string to;
    // Hash before, hash after. True for everything that is save data and false
    // for game payloads, which are a copy of something RomM still holds.
    bool verify = false;
    std::string note;
};

struct Plan {
    std::vector<Move> moves;
    // Things deliberately left where they are, each with the reason. Printed
    // whether or not the run goes ahead, because "it did not move X" is the
    // half of a migration report that people actually need.
    std::vector<std::string> leftAlone;
    // True when there is nothing of the old layout here at all, which is the
    // ordinary case on a fresh console and is not a fault.
    bool nothingToDo() const { return moves.empty(); }
};

// Reads the old layout — `romcache/`, `saves/`, `system/` relative to
// `oldRoot` — and works out where everything belongs.
//
// `library` is used only to turn a rom id into a platform and a title. A game
// the server no longer lists still moves; it lands under `unknown/` with its
// id, which keeps it findable and keeps the migration lossless.
Plan build(const std::string& oldRoot, const std::vector<romm::Game>& library);

void print(const Plan& p);

// Carries it out. `manifestPath` receives the file that `undo` needs.
bool run(const Plan& p, std::string* manifestPath, std::string* err);

// Puts everything back where the manifest says it came from, newest move
// first. Verifies on the way back for the same moves it verified on the way
// out.
bool undo(const std::string& manifestPath, std::string* err);

}  // namespace migrate
