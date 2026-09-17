// Making room for a download by deleting games nobody has played for a while.
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
// Four things about that are worth repeating here, because each one is a
// decision somebody could reasonably make the other way:
//
// NEVER ON A TIMER. A cached game on a half-empty disk costs nothing, and
// deleting it only buys a re-download. Eviction happens when a download needs
// the room and at no other moment.
//
// THE UNIT IS A FILE, NOT A GAME. A game's directory holds its save states and
// battery save beside the ROM. Deleting the directory would take the only
// irreplaceable things with it to reclaim the one thing that always comes back.
// So this deletes ROM payloads and leaves everything else where it is.
//
// OLDEST FIRST, AND THAT IS THE WHOLE ORDER. No size tiers, no exemptions for
// small systems. Both were considered and are recorded as deferred.
//
// AND IT IS INVISIBLE. Nothing tells the person this happened. They press play,
// and the game either starts or downloads. See the policy for why: the feedback
// that matters already exists, at the only moment it is useful.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cache {

// Bytes free on the filesystem holding `path`. Zero when it cannot be read,
// which is deliberately the pessimistic answer: an unreadable disk should look
// full rather than infinite.
int64_t freeBytes(const std::string& path);

// One ROM payload sitting in the cache, and when it was last used.
//
// `lastUsed` is the file's own mtime, which needs no new bookkeeping: the
// download sets it, and a launch touches it. A game downloaded but never played
// therefore sorts oldest, which is right — nobody has come back to it.
//
// "Last used" is a fact about THIS console's copy, deliberately not RomM's play
// history. That history belongs to the household and spans every device, so a
// game played on a phone this morning is recent to the server and has never
// been on this disk at all.
struct Entry {
    std::string path;
    int64_t bytes = 0;
    int64_t lastUsed = 0;   // seconds since the epoch
    int romId = 0;
};

// Every ROM payload under `cacheDir` that the console is allowed to delete,
// oldest first.
//
// Save states, battery saves and partial downloads are not included, and
// neither are the ROMs of games marked KEEP. This returns candidates, and
// everything it leaves out is thereby safe — which is the property that makes
// keeping mean anything, so it is enforced HERE rather than at each caller.
std::vector<Entry> candidates(const std::string& cacheDir);

// Deletes least-recently-used ROMs until `needBytes` are free, plus a margin so
// the next few launches cost nothing. Returns the number of bytes freed.
//
// `protectRomId` is the game currently running, which is never a candidate
// however old its file looks. Pass 0 when nothing is running.
//
// It stops when it runs out of candidates rather than failing: the caller
// checks whether there is now enough room, because "not enough space" is a
// different answer from "nothing left to delete" only in what it says to the
// person.
int64_t evictUntilFree(const std::string& cacheDir, int64_t needBytes,
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

// Marks a ROM as used now, so that oldest-first means anything at all.
void touch(const std::string& romPath);

// --- Keeping, and the two floors that guard it ------------------------------
//
// A KEPT game is the one thing on the disk that is deliberate. Everything else
// here is a copy of RomM that the console may reclaim whenever it needs the
// room; a kept game is a promise that it will not. docs/PROJECT.md: "Nothing
// that is running, nothing you marked as keep, and nothing still waiting to
// reach RomM is ever touched."
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
KeepVerdict mayKeep(const std::string& cacheDir, int romId, int64_t gameBytes);

// Marks a game kept. `record` is the game's whole library entry as JSON, not a
// subset: Cabinet's KeptGame embeds the entire Rom captured at keep time so a
// kept game can be browsed and launched with no network at all, and a subset is
// how that promise gets broken later by a field nobody thought of.
bool keep(const std::string& cacheDir, int romId, const std::string& record,
          std::string* err);
bool unkeep(const std::string& cacheDir, int romId);
bool isKept(const std::string& cacheDir, int romId);
std::vector<int> keptRoms(const std::string& cacheDir);

// --- What has not reached RomM yet ------------------------------------------
//
// The one genuinely irreplaceable thing on the machine. A ROM comes back, a
// state that has been uploaded comes back, and a state written two minutes ago
// on a console that cannot see its server comes back from nowhere.
//
// Eviction never takes save data, so this is not protecting files from the
// evictor — it is making "unsynced" a FACT ON DISK rather than something only
// the running process knows. A marker is written before an upload is attempted
// and removed when it succeeds, so a queue that was interrupted by a crash or a
// power cut is still visible on the next boot, and its bytes still count
// against the save floor.
void markPending(const std::string& cacheDir, int romId, const std::string& fileName,
                 int64_t bytes);
void clearPending(const std::string& cacheDir, int romId, const std::string& fileName);
int64_t pendingBytes(const std::string& cacheDir);

}  // namespace cache
