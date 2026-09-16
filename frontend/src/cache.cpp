#include "cache.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace cache {
namespace {

// Everything a game's directory holds that is NOT a ROM. Each of these is
// either irreplaceable until it reaches RomM, or so small that reclaiming it
// would cost more bookkeeping than it returns.
//
// A save state is 27 KB for a Game Boy and 6.5 MB for a DS, and they do not
// overwrite because the history is the point — so states CAN become the largest
// thing on a disk for somebody who saves often. They are still not deleted
// here: which ones are safe to drop depends on what has been uploaded, and this
// code does not know. docs/PROJECT.md records the rule that wants building.
//
// `.part` is a download that was interrupted. Not a ROM, not reusable, and
// removed by the downloader rather than by eviction.
bool isKeptFile(const char* name) {
    const char* dot = std::strrchr(name, '.');
    if (!dot) return false;
    static const char* kNotRoms[] = {".state", ".srm", ".sav", ".brm",
                                     ".rtc",   ".png", ".part"};
    for (const char* ext : kNotRoms)
        if (std::strcmp(dot, ext) == 0) return true;
    return false;
}

}  // namespace

int64_t freeBytes(const std::string& path) {
    struct statvfs vfs;
    if (::statvfs(path.c_str(), &vfs) != 0) return 0;
    // f_bavail, not f_bfree: the blocks available to us, excluding what the
    // filesystem reserves for root. Using f_bfree would promise space that a
    // non-root write cannot actually have.
    return static_cast<int64_t>(vfs.f_bavail) * static_cast<int64_t>(vfs.f_frsize);
}

std::vector<Entry> candidates(const std::string& cacheDir) {
    std::vector<Entry> out;
    DIR* top = ::opendir(cacheDir.c_str());
    if (!top) return out;

    while (struct dirent* gameDir = ::readdir(top)) {
        if (gameDir->d_name[0] == '.') continue;
        // A game's directory is its rom id. `saves/` sits alongside and is not
        // one, which is exactly why it is skipped here and never a candidate.
        char* end = nullptr;
        const long romId = std::strtol(gameDir->d_name, &end, 10);
        if (!end || *end != '\0' || romId <= 0) continue;

        const std::string dir = cacheDir + "/" + gameDir->d_name;
        DIR* inner = ::opendir(dir.c_str());
        if (!inner) continue;
        while (struct dirent* f = ::readdir(inner)) {
            if (f->d_name[0] == '.') continue;
            if (isKeptFile(f->d_name)) continue;
            const std::string path = dir + "/" + f->d_name;
            struct stat st;
            if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
            out.push_back({path, static_cast<int64_t>(st.st_size),
                           static_cast<int64_t>(st.st_mtime),
                           static_cast<int>(romId)});
        }
        ::closedir(inner);
    }
    ::closedir(top);

    // Oldest first, which is the entire eviction order.
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return a.lastUsed < b.lastUsed;
    });
    return out;
}

int64_t evictUntilFree(const std::string& cacheDir, int64_t needBytes,
                       int protectRomId) {
    const int64_t target =
        needBytes + static_cast<int64_t>(static_cast<double>(needBytes) * kMarginFraction);
    int64_t have = freeBytes(cacheDir);
    if (have >= target) return 0;

    // `have + freed` rather than re-measuring each time round, which is both
    // cheaper and — see the sync below — the only thing that would work.
    int64_t freed = 0;
    for (const Entry& e : candidates(cacheDir)) {
        if (have + freed >= target) break;
        // The running game. Its file is open, and deleting it would be the one
        // eviction a person could actually notice.
        if (protectRomId != 0 && e.romId == protectRomId) continue;
        if (::unlink(e.path.c_str()) != 0) continue;
        freed += e.bytes;
        std::fprintf(stderr, "[cache] evicted %s (%lld bytes)\n", e.path.c_str(),
                     static_cast<long long>(e.bytes));
    }
    if (freed > 0) {
        // WITHOUT THIS, EVERY EVICTION LOOKS LIKE A FAILURE. CabinetOS runs on
        // btrfs, where unlink returns immediately but the space stays invisible
        // to statvfs until a transaction commits — so a caller that deletes and
        // then measures sees exactly what it saw before and concludes there was
        // nothing to gain.
        //
        // Measured on the test machine rather than inferred from a symptom:
        // delete a 50 MB file and the free-space figure moves by 0 bytes
        // immediately, 0 after three seconds, and the full 50,003,968 the
        // moment a sync is forced. Waiting is not an option; committing is.
        //
        // syncfs rather than sync, so this is the one filesystem rather than
        // every mounted one. It happens once per eviction, on a path that is
        // about to move hundreds of megabytes over a network.
        if (const int fd = ::open(cacheDir.c_str(), O_RDONLY | O_DIRECTORY); fd >= 0) {
            ::syncfs(fd);
            ::close(fd);
        }
        std::fprintf(stderr, "[cache] freed %lld bytes to make room for %lld\n",
                     static_cast<long long>(freed), static_cast<long long>(needBytes));
    }
    return freed;
}

void touch(const std::string& romPath) {
    // The file's own mtime is the record, so there is no sidecar to keep in
    // step and nothing to migrate. utimensat with nullptr means "now".
    if (::utimensat(AT_FDCWD, romPath.c_str(), nullptr, 0) != 0) {
        // Not worth failing a launch over: the cost is that this game looks
        // older than it is and may be evicted sooner than it deserves.
        std::fprintf(stderr, "[cache] could not touch %s\n", romPath.c_str());
    }
}

}  // namespace cache
