// Where everything on this console lives, and who it belongs to.
//
// WHAT THIS IS FOR, IN ONE SENTENCE: somebody who plugs a drive in or logs in
// over SFTP should be able to find their saves without being told where they
// are, and the console should never be able to delete one person's game because
// somebody else pressed a button.
//
// The layout before this was never designed — it accumulated. There were two
// save directories and a game's save landed in whichever one the launch path
// happened to use; `system/` held replaceable BIOS next to an irreplaceable
// Dreamcast flash; every core in the machine shared one flat pile of saves, so
// working out which game wrote a file meant comparing timestamps; and the
// directory holding kept games and everybody's saves was called `romcache`.
//
// docs/PROJECT.md, open question 18, decided the shape. This is it:
//
//   <location>/
//   ├── roms/<platform>/<romId> - <name>      kept games                shared
//   ├── cache/<platform>/<romId> - <name>     pulled games              shared
//   ├── bios/                                 firmware from RomM        shared
//   ├── users/
//   │   └── <id> - <name>/
//   │       ├── saves/<platform>/<romId>/<core>/
//   │       ├── states/<platform>/<romId>/<core>/
//   │       ├── screenshots/
//   │       ├── keeps/                        which games this person pinned
//   │       ├── pending/                      what has not reached RomM yet
//   │       └── config/
//   ├── config/
//   └── logs/
//
// The vocabulary is RetroArch's and RetroBat's — `roms`, `saves`, `states`,
// `bios`, `config` — because somebody who has used either already knows where
// to look, and that is most of what this question was asking for. The one name
// neither of them needs is `cache/`, because both assume the games are yours
// and permanent. This console pulls on demand, so it has two categories they do
// not: a game that is here because somebody played it, and a game that is here
// because somebody asked for it.
//
// THE PAYOFF IS A RULE YOU CAN CHECK BY LOOKING: eviction only ever deletes
// inside `cache/`. Not "eviction deletes things that are not marked kept" —
// which is a sentence about code — but a directory you can list.
//
// ONE CONVENTION, TWICE: THE NUMBER IDENTIFIES, THE WORDS ARE FOR YOU.
// `users/1 - MMagTech/` and `cache/Sony Playstation/321 - Crash Bandicoot.chd`.
// Everything after " - " is decoration and may be re-derived at any time, so
// renaming a user in RomM is cosmetic here rather than destructive. That
// failure is not hypothetical: a name-keyed console would quietly create an
// empty folder and start again, with every save still on the disk and nothing
// looking for it.
//
// AND ONE SPELLING OF A PLATFORM, EVERYWHERE — RomM's `fs_slug`. The short
// `slug` was written down first and reads better in a path, but it is NOT
// UNIQUE: the reference server has two Arcade platforms sharing `arcade`, 223
// games between them, needing different cores. Under the short name those games
// share one folder while their saves correctly split into `FBNEO/` and
// `MAME2003/` — one console, filed two ways, which is the thing this whole
// question exists to stop. `fs_slug` is also what RomM itself uses for both its
// roms and its assets, so the two trees are the same shape all the way down.
//
// TWO RomM INSTANCES BOTH HAVE A User:1. The real key is (server, user) and
// this uses the user alone, which is a documented limit rather than a mechanism
// — open question 14 established that RomM exposes no instance identity, and
// nothing here can invent one.

#pragma once

#include <string>
#include <vector>

namespace romm { class Client; }

namespace storage {

// --- Where the root is ------------------------------------------------------
//
// `/var/lib/cabinetos` on a console, and the directory the binary was started
// in on a development machine, because the test VM runs the frontend out of a
// home directory and has no business writing to /var/lib.
//
// Resolution order: what `--storage-root` said, then $CABINETOS_STORAGE, then
// /var/lib/cabinetos if it can be created and written, then the working
// directory. Whatever wins is printed once at startup, because a console
// writing somewhere nobody expected is a bug that otherwise takes an afternoon.
void setRoot(const std::string& path);
const std::string& root();

// Creates the tree, and returns false only if the root itself is unusable —
// which is the one storage fault that is not survivable.
bool ensureTree(std::string* err);

// --- Storage locations ------------------------------------------------------
//
// `roms/` and `cache/` repeat on EVERY location rather than living once at the
// root, and that is not tidiness. Releasing the last keep on a game demotes it
// to the cache instead of deleting it, and a demotion has to be a rename inside
// one filesystem; with kept games on a big drive and one cache at the root, it
// would mean physically copying gigabytes between disks because somebody
// changed their mind. A rename cannot cross a filesystem — the kernel returns
// EXDEV — so this is a fact the layout has to carry, not a preference.
//
// PLUG IT IN AND IT IS USED. There is no setup screen and no confirmation,
// which is the Nintendo Switch's answer and the one open question 14 settled on:
// put a card in a Switch and it becomes where downloads go, full stop. So this
// LOOKS for drives every time it is asked rather than being told about them
// once.
//
// AND IT NEVER TAKES OVER A DRIVE. One folder named `CabinetOS/` on it, and
// nothing outside that folder is read, written or counted. A drive with
// somebody's films on it works as a games drive too, and unplugging it takes
// their films back untouched.
//
// THE FIRST ENTRY IS ALWAYS THE INTERNAL ROOT and is always present. Everything
// after it is a drive somebody plugged in, and any of them may vanish between
// one call and the next — which is fine, because nothing here remembers.
std::vector<std::string> locations();

// Where the cache lives, and everything that is not a game: saves, keeps,
// firmware, configuration. The internal disk, always.
const std::string& primaryLocation();

// Where a game being KEPT should go: the games drive when one is plugged in,
// and the internal disk otherwise.
//
// This decides where a download LANDS and nothing else. Keeping a game already
// on the disk never moves bytes between drives — it changes which half of its
// own location it sits in, which is a rename. See cache::keep.
std::string keepLocation();

// A drive that was here the last time the console looked and is not here now,
// or empty.
//
// THE ONE THING THAT IS REMEMBERED, and only so the console can say a true
// sentence out loud. It is never used to decide where a file is — that is
// always read off the disk — because a remembered list that has gone stale is
// exactly the fault this design exists to avoid: pull the card out of a Steam
// Deck and it still shows the games as installed, with a Play button that does
// nothing.
//
// It reports a given drive ONCE. Having said it, the console forgets that drive
// and stops mentioning it, because a machine that complains about a drive you
// removed on purpose every time it boots is worse than one that says nothing.
std::string missingDriveToReport();

// The location a game with this id is stored under, or the primary location
// when it is not here yet.
std::string locationFor(int romId);

// --- The shared half --------------------------------------------------------

std::string romsDir(const std::string& location);    // <loc>/roms
std::string cacheDir(const std::string& location);   // <loc>/cache

// The libretro system directory: BIOS fetched from RomM, and the assets a core
// ships with.
//
// KNOWN GAP, AND IT IS WRITTEN UP IN docs/PROJECT.md RATHER THAN PAPERED OVER:
// libretro gives a core exactly ONE system directory, and cores write into it —
// Flycast keeps the Dreamcast's own flash there, which is the one file in it
// that cannot be re-fetched. So `bios/` is not purely "replaceable firmware"
// the way the name promises. What it no longer holds is the 13 MB of PSP system
// files that ship inside the image; those live in /usr/share/cabinetos/system
// and are linked in rather than written to.
std::string biosDir();

std::string configDir();
std::string logsDir();

// Where the image puts the files a core needs and nobody may edit — today that
// is PPSSPP's fonts and lookup tables. Read-only on a console by construction:
// it is inside the bootc image. `ensureTree` links what it finds there into the
// system directory, and leaves alone any name that is already a real file.
std::string imageAssetsDir();

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
// below then refuses rather than guessing.
bool resolveCurrentUser(romm::Client& client, std::string* err);

std::string userDir(const User& u);
std::string savesDir(const User& u, const std::string& platform, int romId,
                     const std::string& core);
std::string statesDir(const User& u, const std::string& platform, int romId,
                      const std::string& core);
std::string screenshotsDir(const User& u);
std::string userConfigDir(const User& u);
std::string keepsDir(const User& u);
std::string pendingDir(const User& u);

// Every user directory on this machine, whether or not anybody has logged in as
// them recently. `keepers()` in cache.h walks this, which is what makes one
// person's release safe for everybody else.
std::vector<User> knownUsers();

// Where a save goes when there is no game and no person — `--core <so> <rom>`,
// the developer entry point that plays a file off the disk with no RomM behind
// it, and the `--core-options` audit, which loads every core in turn.
//
// It exists so that THERE IS STILL ONLY ONE SAVE DIRECTORY. The fault open
// question 18 opened with was two of them: `romcache/saves` when a game came
// from the library and `saves/` when it came from `--core`, with PSP save
// folders found in both and neither seeing the other's. This keeps the
// developer path inside the same tree, under a user id of 0 that cannot be
// mistaken for a person — `knownUsers()` skips it, so nothing can be kept or
// attributed to it.
std::string scratchSavesDir(const std::string& core);

// --- Naming -----------------------------------------------------------------

// "321 - Crash Bandicoot". The entry a game occupies under a platform, which is
// a FILE when the game is one file and a DIRECTORY when its archive unpacked
// into several. Both are renamed the same way, so promotion and demotion do not
// care which it is.
std::string entryName(int romId, const std::string& title);

// The rom id at the front of an entry name, or 0. This is the half of the
// convention that identifies; everything after " - " is ignored on purpose.
int romIdFromEntry(const std::string& entryName);

// The platform segment, and it is RomM's `fs_slug` everywhere — "Sony
// Playstation", "Game Boy", "FBNEO". The server uses it for both its own roms
// and its assets, so this tree and that one are the same shape all the way
// down and a fault is visible by eye. See the top of this file for why the
// shorter `slug` lost.
std::string platformSegment(const std::string& fsSlug);

// Anything that came off a server and is about to become a path. Slashes, NULs
// and the two dot-entries, which is the whole of what can escape a directory.
std::string safeSegment(const std::string& s);

// Makes a directory and every parent of it.
bool makeDirs(const std::string& path);

// Moves a file or a whole directory, and says whether it had to fall back to
// copying because the two ends were on different filesystems. The console
// should never need the copy — see `locations()` — so it is reported rather
// than done silently.
struct MoveResult {
    bool ok = false;
    bool crossedFilesystem = false;
    std::string error;
};
MoveResult moveEntry(const std::string& from, const std::string& to);

// Total bytes under a path, following nothing. A file answers its own size.
int64_t treeBytes(const std::string& path);

// True when the path exists at all, file or directory.
bool exists(const std::string& path);

}  // namespace storage
