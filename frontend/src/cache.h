// Which games are here because somebody asked for them, and which are here
// because somebody played them — and making room for the next one.
//
// A library of 1644 games does not fit on a console. Until this existed the
// disk simply filled and stayed full, which is the state docs/PROJECT.md
// described as the biggest hole in the product.
//
// THE POLICY IS IN docs/PROJECT.md, Phase 4, and the whole of it is:
//
//   The games you have played on this console are on the disk. They stay until
//   the disk needs the room, and then the ones you have not played for longest
//   go first. Nothing that is running, nothing you marked as keep, and nothing
//   still waiting to reach RomM is ever touched.
//
// NEVER ON A TIMER. A cached game on a half-empty disk costs nothing, and
// deleting it only buys a re-download. Eviction happens when a download needs
// the room and at no other moment.
//
// OLDEST FIRST, AND THAT IS THE WHOLE ORDER. No size tiers, no exemptions for
// small systems. Both were considered and are recorded as deferred.
//
// AND IT IS INVISIBLE. Nothing tells the person this happened. They press play,
// and the game either starts or downloads. See the policy for why: the feedback
// that matters already exists, at the only moment it is useful.
//
// --- WHAT THE FOLDER LAYOUT CHANGED HERE, AND IT IS MOST OF THIS FILE --------
//
// This used to be a walk over one directory called `romcache/` that held kept
// games, cached games, everybody's saves and the upload queue, with a `kept/`
// folder of marker files deciding which of them eviction was allowed to touch.
// Two rules had to be enforced in code because the disk could not express them:
// "do not delete a kept game" and "do not delete a save".
//
// Now the disk expresses both. A kept game is in `roms/` and a cached one is in
// `cache/`, on the same filesystem so that moving between them is a rename;
// saves are somewhere else entirely, under the person they belong to.
//
//   EVICTION ONLY EVER DELETES INSIDE `cache/`.
//
// That is a rule you can check by listing a directory rather than by reading
// this file, and it is the whole point of the split. Two consequences worth
// saying out loud:
//
//   THE UNIT IS NOW THE WHOLE GAME, not "the files in its directory that do not
//   look like saves". The old code carried a list of extensions it must not
//   delete — `.srm`, `.state`, `.brm` — because a game's directory held its
//   saves beside the ROM, and one wrong entry in that list would have taken the
//   only irreplaceable thing on the machine. There is nothing irreplaceable
//   inside `cache/` any more, so that list is gone.
//
//   KEEPING IS A SET OF PEOPLE, NOT A FLAG. One kept game is one file however
//   many people play it, so releasing must not take it from somebody else — and
//   releasing the LAST keep DEMOTES the game to the cache rather than deleting
//   it. Un-keep is a safe button, which matters when it sits one press away on
//   a game's own screen.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "storage.h"

namespace cache {

// Bytes free on the filesystem holding `path`. Zero when it cannot be read,
// which is deliberately the pessimistic answer: an unreadable disk should look
// full rather than infinite.
int64_t freeBytes(const std::string& path);

// --- Where a game is ---------------------------------------------------------

// One game on the disk, on one storage location.
//
// `entryPath` is a FILE when the game is a single payload and a DIRECTORY when
// its archive unpacked into several — a `.cue` needs its `.bin` beside it, and
// an arcade set is a folder. Both are named `<romId> - <title>`, both are
// renamed the same way, and nothing outside this file has to care which it is.
struct Placement {
    bool present = false;
    bool kept = false;          // it is under roms/ rather than cache/
    std::string location;       // the storage location it sits on
    std::string platform;       // the platform segment it sits under
    std::string entryPath;
    bool isDirectory = false;
};

// Finds a game wherever it is, on any location. Cheap enough to call per
// launch: it is a readdir over a few platform folders.
Placement find(int romId);

// Where a game SHOULD go. `kept` picks `roms/` over `cache/`; everything else
// follows the naming convention — the number identifies, the words are for you.
std::string entryPathFor(const std::string& location, const std::string& platform,
                         int romId, const std::string& title, bool kept);

// --- Eviction ----------------------------------------------------------------

// One cached game, and when it was last used.
//
// `lastUsed` is the entry's own mtime, which needs no new bookkeeping: the
// download sets it and a launch touches it. A game downloaded but never played
// therefore sorts oldest, which is right — nobody has come back to it.
//
// "Last used" is a fact about THIS CONSOLE'S COPY, deliberately not RomM's play
// history, which belongs to the household and spans every device. It is also
// THE MACHINE'S rather than any one person's: reading it per user would evict a
// game because *you* have not touched it while somebody else plays it daily.
struct Entry {
    std::string path;
    int64_t bytes = 0;
    int64_t lastUsed = 0;   // seconds since the epoch
    int romId = 0;
    std::string platform;
};

// Everything in `cache/` on this location, oldest first. There is nothing to
// exclude and no rule to remember: if it is in there, it may go.
std::vector<Entry> candidates(const std::string& location);

// Deletes least-recently-used games until `needBytes` are free, plus a margin so
// the next few launches cost nothing. Returns the number of bytes freed.
//
// `protectRomId` is the game currently running, which is never a candidate
// however old its entry looks. Pass 0 when nothing is running.
//
// It stops when it runs out of candidates rather than failing: the caller
// checks whether there is now enough room, because "not enough space" is a
// different answer from "nothing left to delete" only in what it says to the
// person.
int64_t evictUntilFree(const std::string& location, int64_t needBytes,
                       int protectRomId = 0);

// The margin above, as a fraction. Freeing exactly enough means evicting on
// every single launch once a disk sits near full, until the cache holds nothing
// but the running game — the person never sees it happen and never benefits
// from the cache again.
inline constexpr double kMarginFraction = 0.10;

// Slack on top of a download's declared size. Landing on exactly zero free
// bytes is where filesystems begin to fail in interesting ways, and staying off
// it costs nothing.
inline constexpr double kOverheadFraction = 0.05;

// Marks a game as used now, so that oldest-first means anything at all.
void touch(const std::string& entryPath);

// --- Keeping, and the two floors that guard it ------------------------------
//
// A KEPT game is the one thing on the disk that is deliberate. Everything else
// here is a copy of RomM that the console may reclaim whenever it needs the
// room; a kept game is a promise that it will not.
//
// THE PROMISE IS WHY KEEPING CAN BE REFUSED, and it is the only place in this
// whole subsystem where the console says no to anything. Kept games are never
// evicted, so without a check a person can keep enough of them to leave the
// machine with nothing it is permitted to delete — at which point it can
// neither write a save nor update itself, and the only way out is to go and
// un-keep something. Two floors, and the console refuses a keep that would
// cross either:
//
//   THE SAVE FLOOR protects the upload queue and room to write one more state.
//   2 GB, or 5% of the disk, whichever is smaller — a fixed size rather than a
//   percentage because saves do not scale with the disk. The same person plays
//   the same games on a 4 TB drive as on a 32 GB one.
//
//   THE SYSTEM RESERVE protects the machine's ability to update itself, which
//   needs room for a whole image. 5 GB, and the larger of the two. The cache is
//   the system's to take, so this is checked when KEEPING and never against the
//   cache.
//
// Neither floor can protect data from itself: somebody offline for a week fills
// the disk with their own upload queue and no reserve helps. That is recorded
// in docs/PROJECT.md as a conversation rather than a number.

// Room for a whole system image. The reserve is a constant because an image is
// a constant; it does not grow with the disk it sits on.
inline constexpr int64_t kSystemReserveBytes = 5LL << 30;

// 2 GB, or 5% of this filesystem, whichever is smaller.
int64_t saveFloorBytes(const std::string& path);

// What a keep would cost and whether the console can afford it.
//
// `gameBytes` is what the kept game will occupy IN TOTAL, whether or not any of
// it is on the disk yet — keeping a game nobody has downloaded is the case this
// has to get right, because that is a promise to fetch it and hold it.
struct KeepVerdict {
    bool allowed = false;
    // What the console would be short by. Zero when allowed, and the number the
    // refusal should say out loud: "the disk is full of things you asked me to
    // keep" is only useful with an amount beside it.
    int64_t shortfallBytes = 0;
    // What was measured, so a caller can explain the answer rather than just
    // report it.
    int64_t reclaimableBytes = 0;
    int64_t floorBytes = 0;
};

// The question is NOT "is there room right now" — a kept game may already be on
// the disk, in which case keeping it consumes nothing at all today. It is
// whether, AFTER this game stops being evictable, the console can still free
// its way down to both floors. So it counts what it could still reclaim if it
// deleted every evictable thing it has, and compares that with the floors.
//
// ALREADY KEPT BY SOMEBODY ELSE COSTS NOTHING, which is the whole of what the
// per-user split buys: the second person to keep a game is asking for a promise
// the machine has already made.
KeepVerdict mayKeep(const std::string& location, int romId, int64_t gameBytes);

// Marks a game kept BY THIS PERSON, and promotes it out of the cache.
//
// `record` is the game's whole library entry as JSON, not a subset: Cabinet's
// KeptGame embeds the entire Rom captured at keep time so a kept game can be
// browsed and launched with no network at all, and a subset is how that promise
// gets broken later by a field nobody thought of.
bool keep(const storage::User& u, int romId, const std::string& record,
          std::string* err);

// Releases THIS PERSON's keep. If nobody else is keeping it, the game is
// demoted to the cache — a rename, not a copy, and never a delete.
//
// Returns false only when the record could not be removed. A demotion that
// could not happen is reported on stderr and leaves the game kept on disk,
// which is the safe direction to fail in.
bool unkeep(const storage::User& u, int romId);

bool isKeptBy(const storage::User& u, int romId);
bool isKeptByAnyone(int romId);

// The user ids keeping this game. Walks every user directory on the machine,
// which is what makes one person's release safe for everybody else.
std::vector<int> keepers(int romId);

// The games this person has kept.
std::vector<int> keptRoms(const storage::User& u);

// Every game kept by anybody, which is what the machine's storage report and
// the floors care about.
std::vector<int> allKeptRoms();

// --- What has not reached RomM yet ------------------------------------------
//
// The one genuinely irreplaceable thing on the machine. A ROM comes back, a
// state that has been uploaded comes back, and a state written two minutes ago
// on a console that cannot see its server comes back from nowhere.
//
// Eviction never takes save data — it cannot, it only walks `cache/` — so this
// is not protecting files from the evictor. It is making "unsynced" a FACT ON
// DISK rather than something only the running process knows. A marker is
// written before an upload is attempted and removed when it succeeds, so a
// queue interrupted by a crash or a power cut is still visible on the next
// boot, and its bytes still count against the save floor.
//
// Per user, because the thing it is tracking is a save.
void markPending(const storage::User& u, int romId, const std::string& fileName,
                 int64_t bytes);
void clearPending(const storage::User& u, int romId, const std::string& fileName);

// Across everybody on the machine: the floor is a fact about the disk, and the
// disk does not care whose upload is queued on it.
int64_t pendingBytes();

}  // namespace cache
