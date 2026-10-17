// Who this console is acting as (currentUser) and everybody with a folder on
// it (knownUsers). storage.h includes this above its per-user paths, which
// take a User; moved out of it in the restructure (#398).

#pragma once

#include <string>
#include <vector>

namespace romm { class Client; }

namespace storage {

// --- The per-user half ------------------------------------------------------
//
// WHAT IS NOT PER USER MATTERS AS MUCH AS WHAT IS. Two people on one console
// must not download the same game twice or hold two copies of the PS2 BIOS, and
// must never see each other's saves.
//
//   per user              shared by the machine
//   saves, save states    the downloaded game files
//   screenshots           BIOS and firmware
//   preferences           cores
//   the decision to keep  shader caches
struct User {
    int id = 0;
    std::string name;
    // Where the person's picture is, for the account chip on Home. Empty when
    // they have not set one, which is a normal state: the chip falls back to a
    // lettered disc, the way the reference implementation falls back to a
    // person glyph.
    std::string avatar;
    // "1 - MMagTech". The directory name, and the only thing on disk that
    // encodes both halves of the convention.
    std::string dirName() const;
    bool valid() const { return id > 0; }
};

// Who the console is acting as. One user today; account switching is a session
// of its own and is deliberately not started here.
//
// The id comes from RomM's `/api/users/me` and nowhere else. It is asked for
// once, at startup, and cached on disk — because a console with no network must
// still know whose saves it is holding, and "we could not reach the server" is
// not a reason to start writing into somebody else's directory.
const User& currentUser();
void setCurrentUser(const User& u);

// Asks the server who we are, falling back to the cached answer. Returns false
// when neither is available, which leaves the console with no user — everything
// in storage.h's per-user half then refuses rather than guessing.
bool resolveCurrentUser(romm::Client& client, std::string* err);

// Every user directory on this machine, whether or not anybody has logged in as
// them recently. `keepers()` in cache.h walks this, which is what makes one
// person's release safe for everybody else.
std::vector<User> knownUsers();

}  // namespace storage
