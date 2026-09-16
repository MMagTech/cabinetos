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

// Every ROM payload under `cacheDir`, oldest first.
//
// Save states, battery saves and partial downloads are not included, so they
// can never be selected: this returns candidates, and everything it leaves out
// is thereby safe.
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

}  // namespace cache
