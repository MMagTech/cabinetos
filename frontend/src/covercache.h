// Cover art kept on disk, so a console does not re-download its own artwork
// every time it is switched on.
//
// WHY THIS EXISTS. Nothing about a library survives a boot — the whole of what
// a console remembers is three files in config/ — so every start re-fetches
// every cover it draws. On a LAN that is a few megabytes nobody notices. Open
// question 29 established that RomM is not always on a LAN, and on a hosted
// server it is the same few megabytes over somebody's uplink, every time.
//
// IT IS NOT A SNAPSHOT OF THE LIBRARY, and that distinction is what keeps it
// clear of open question 22. Nothing here records which games exist, what they
// are called or which platform they belong to. It stores image BYTES under the
// server's own path for them, and a path this console was not told about by a
// live server is never read.
//
// --- THE PATH IS THE VERSION ------------------------------------------------
//
// RomM hands out cover paths with the art's own timestamp on them:
//
//   /assets/romm/resources/roms/15/569/cover/small.png?ts=2025-03-11 06:56:03
//
// So changed art is a changed path, which becomes a different file here, and
// the stale one is simply never asked for again. **That is why there is no TTL
// and no revalidation.** MMagTech raised the TTL question — art that sits on
// disk forever after a platform is deleted — and the answer is a sweep against
// the server's own list rather than a clock; see docs/PROJECT.md question 30.
// The sweep is NOT built yet, which is the honest state of this file.
//
// --- WHAT IS DELIBERATELY NOT CACHED ----------------------------------------
//
// Only rom cover art. An avatar comes from /api/users/<id>/avatar, which
// carries no version in its path and changes when somebody changes their
// picture, so caching it would show a stale face with no way to notice. Any
// key this does not recognise is passed through uncached rather than guessed
// at.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace covercache {

// Which server's art this is. The cache is keyed by it because pointing a
// console at a different RomM makes every path meaningless at once, and
// switching back should not mean downloading everything again.
//
// Until this is called, nothing is read and nothing is written: a cache with
// no server to attribute it to is not safe to serve from.
void setServer(const std::string& address);

// The bytes for a key, or empty if they are not here. Safe to call from the
// ImageCache's worker threads.
std::vector<uint8_t> read(const std::string& key);

// Stores bytes under a key. Best effort in every direction: a failure to write
// is not reported, because a console that cannot cache its art still draws it.
//
// IT REFUSES TO WRITE INTO THE SYSTEM RESERVE. cache.h's rule is that a disk
// full of games is a console that cannot update itself, and artwork is no more
// entitled to that space than a ROM is.
void write(const std::string& key, const std::vector<uint8_t>& bytes);

// Where it all lives, for the storage report and for anything that later
// sweeps it.
std::string dir();

// How many covers were served from disk and how many had to be fetched, since
// the process started. Printed once at shutdown beside the image cache's own
// figures: without it "the cover cache works" is an inference from a directory
// listing rather than something the console says.
struct Stats { int fromDisk = 0; int fetched = 0; int stored = 0; };
Stats stats();

}  // namespace covercache
