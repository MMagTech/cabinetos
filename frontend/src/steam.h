// The Steam entry: one tile that hands the screen to Steam's own Big Picture
// session and comes back when Steam closes. Issue #223; docs/SETTINGS.md,
// Library, Storage and System; docs/PROJECT.md, "Steam".
//
// WHAT THE CONSOLE OWNS IS SMALL: the tile, its first-pick screen, Steam's
// slice of the main drive (a fixed-size file, /usr/libexec/cabinetos-steam)
// and the handover. Steam itself, its login, its games, Proton and its
// settings pages are Steam's, and Steam updates itself from Valve while it is
// open. Nothing of Steam runs, or is even mounted, while the console does.
//
// THE HANDOVER: the frontend writes `steam` to $XDG_RUNTIME_DIR/cabinetos-next
// and quits; the session script (cabinetos-session, steam_step) attaches the
// slice, runs Steam's session in the console's own login session, stops
// everything left of Steam when it closes, detaches the slice, writes
// $XDG_RUNTIME_DIR/cabinetos-from-steam and starts the console again, which
// lands on the tile.
//
// Every call that runs the root helper BLOCKS until it answers; call them off
// the frame thread.

#pragma once

#include <cstdint>
#include <string>

namespace steam {

// The image carries Steam and its session. A console built without them
// shows no tile at all.
bool available();

// Steam is set up: its slice exists. Cheap (one stat).
bool isSetUp();

// The slice's size in bytes, 0 when not set up. Cheap.
int64_t sliceBytes();

// True when Steam's slice lives on the same filesystem as `location`, so
// storage::spaceOf leaves it out of that location's size: the console's part
// of the main drive is the drive minus Steam's slice (MMagTech, 2026-10-03).
bool sliceOn(const std::string& location);

// Hide Steam (first-pick screen) and show it again (Settings, System, only
// while hidden). The console's, not a person's: it hides the tile for
// everyone.
bool hidden();
void setHidden(bool hide);

// The sizes the first-pick screen and the grow panel step through, in
// decimal GB as Storage counts, each a whole MiB for the helper.
inline constexpr int64_t kStepBytes = 25'000'000'000LL;     // 25 GB a press
inline constexpr int64_t kMinBytes = 100'000'000'000LL;     // 100 GB, the floor
inline constexpr int64_t kDefaultCapBytes = 500'000'000'000LL;  // the default's cap
// What setting Steam up or growing it always leaves the console for games it
// only plays (the cache), on top of the cache's own floors. A starting value
// (the audit's 50 GB, #223); one line.
inline constexpr int64_t kCacheHeadroomBytes = 50'000'000'000LL;

// What the main drive could give Steam, in total, without touching a kept
// game: its free space plus every cached game, less the console's floors and
// the cache's headroom, plus the slice it already has. Walks the cache, so
// not on the frame thread.
int64_t roomBytes();

// The default size, MMagTech 2026-10-02/03: 25% of the main drive, at least
// 100 GB, at most 500 GB, on a 25 GB step; never more than roomBytes(). 0
// when even 100 GB does not fit, and then nothing is set up.
int64_t defaultBytes(int64_t room);

// Makes the slice, after clearing cached games (oldest first, once) to make
// the room. Kept games are never touched. False with a reason on screen.
bool create(int64_t bytes, std::string* why);

// Makes it bigger, the same way. Grow only (MMagTech, 2026-10-03).
bool grow(int64_t bytes, std::string* why);

// Remove Steam: the slice with every game in it, and Steam's own files in the
// home folder (its links, its login, the screen modes its session saved).
// Other drives are never touched: a Steam library someone put on one stays.
bool remove(std::string* why);

// Asks the session to hand the screen to Steam once the frontend quits.
bool requestHandover();

// True once, at the console's first start after Steam closed, so it lands on
// the tile.
bool takeReturned();

}  // namespace steam
