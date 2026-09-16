#include "cache.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
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
bool isNotARom(const char* name) {
    const char* dot = std::strrchr(name, '.');
    if (!dot) return false;
    static const char* kNotRoms[] = {".state", ".srm", ".sav", ".brm",
                                     ".rtc",   ".png", ".part"};
    for (const char* ext : kNotRoms)
        if (std::strcmp(dot, ext) == 0) return true;
    return false;
}

// Where a keep is recorded, and where an unsent upload is recorded. Both sit
// BESIDE the game directories rather than inside them, so neither can be
// mistaken for a game and swept up by a walk over the cache.
std::string keptDir(const std::string& cacheDir) { return cacheDir + "/kept"; }
std::string keptPath(const std::string& cacheDir, int romId) {
    return keptDir(cacheDir) + "/" + std::to_string(romId) + ".json";
}
std::string pendingDir(const std::string& cacheDir) { return cacheDir + "/pending"; }

// One directory level, created if it is not there. No recursion needed: every
// path here is one level under a cache directory the caller already made.
void ensureDir(const std::string& path) { ::mkdir(path.c_str(), 0755); }

// A file name that cannot escape its directory or collide with a marker from
// another game. Upload file names come from game titles, which contain slashes,
// colons and worse.
std::string markerName(int romId, const std::string& fileName) {
    std::string s = std::to_string(romId) + "-";
    for (unsigned char c : fileName) {
        s += (std::isalnum(c) || c == '.' || c == '-' || c == '_')
                 ? static_cast<char>(c) : '_';
    }
    return s + ".pending";
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

        // A kept game is not a candidate, whatever its mtime says. This is the
        // whole of what keeping buys, and it is enforced here so that no future
        // caller of candidates() can forget it. Asked BEFORE the directory is
        // opened, so skipping one does not leak the handle.
        if (isKept(cacheDir, static_cast<int>(romId))) continue;

        const std::string dir = cacheDir + "/" + gameDir->d_name;
        DIR* inner = ::opendir(dir.c_str());
        if (!inner) continue;

        while (struct dirent* f = ::readdir(inner)) {
            if (f->d_name[0] == '.') continue;
            if (isNotARom(f->d_name)) continue;
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

int64_t saveFloorBytes(const std::string& path) {
    struct statvfs vfs;
    if (::statvfs(path.c_str(), &vfs) != 0) {
        // An unreadable disk looks full elsewhere in this file, and the same
        // pessimism applies: take the whole 2 GB rather than a percentage of a
        // size we do not know.
        return 2LL << 30;
    }
    const int64_t total =
        static_cast<int64_t>(vfs.f_blocks) * static_cast<int64_t>(vfs.f_frsize);
    return std::min<int64_t>(2LL << 30, total / 20);   // 5%
}

bool isKept(const std::string& cacheDir, int romId) {
    struct stat st;
    return ::stat(keptPath(cacheDir, romId).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<int> keptRoms(const std::string& cacheDir) {
    std::vector<int> out;
    DIR* d = ::opendir(keptDir(cacheDir).c_str());
    if (!d) return out;   // nothing kept yet is the ordinary case, not a fault
    while (struct dirent* f = ::readdir(d)) {
        if (f->d_name[0] == '.') continue;
        char* end = nullptr;
        const long id = std::strtol(f->d_name, &end, 10);
        if (id > 0 && end && std::strcmp(end, ".json") == 0)
            out.push_back(static_cast<int>(id));
    }
    ::closedir(d);
    return out;
}

// What this game already has on the disk. Keeping a game that is fully
// downloaded costs nothing today; keeping one that is not is a promise to fetch
// the rest and then never let go of it.
static int64_t bytesOnDiskFor(const std::string& cacheDir, int romId) {
    const std::string dir = cacheDir + "/" + std::to_string(romId);
    DIR* d = ::opendir(dir.c_str());
    if (!d) return 0;
    int64_t total = 0;
    while (struct dirent* f = ::readdir(d)) {
        if (f->d_name[0] == '.') continue;
        if (isNotARom(f->d_name)) continue;
        struct stat st;
        if (::stat((dir + "/" + f->d_name).c_str(), &st) == 0 && S_ISREG(st.st_mode))
            total += st.st_size;
    }
    ::closedir(d);
    return total;
}

KeepVerdict mayKeep(const std::string& cacheDir, int romId, int64_t gameBytes) {
    KeepVerdict v;
    v.floorBytes = saveFloorBytes(cacheDir) + kSystemReserveBytes;

    // Everything the console could still delete if it had to, WITH this game
    // already excluded — candidates() skips kept games, and this game is about
    // to be one, so it is subtracted by hand rather than by keeping first and
    // asking afterwards.
    int64_t reclaimable = freeBytes(cacheDir);
    for (const Entry& e : candidates(cacheDir)) {
        if (e.romId == romId) continue;
        reclaimable += e.bytes;
    }

    // The part of this game still to fetch comes OUT of that, because keeping
    // an undownloaded game is a promise to spend the space.
    const int64_t onDisk = bytesOnDiskFor(cacheDir, romId);
    const int64_t stillToFetch = std::max<int64_t>(0, gameBytes - onDisk);
    reclaimable -= stillToFetch;

    // An upload that has not reached the server is the one thing here that
    // cannot be re-fetched, so its bytes are spoken for and are not reclaimable
    // by anybody.
    reclaimable -= pendingBytes(cacheDir);

    v.reclaimableBytes = reclaimable;
    v.allowed = reclaimable >= v.floorBytes;
    v.shortfallBytes = v.allowed ? 0 : (v.floorBytes - reclaimable);
    return v;
}

bool keep(const std::string& cacheDir, int romId, const std::string& record,
          std::string* err) {
    ensureDir(cacheDir);
    ensureDir(keptDir(cacheDir));
    const std::string path = keptPath(cacheDir, romId);
    // Written through a temporary and renamed, so a keep is either recorded or
    // it is not. A half-written record is a kept game that cannot be read back,
    // which is the worst of both: the space is protected and the game is not
    // launchable offline.
    const std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        if (err) *err = "could not write the keep record";
        return false;
    }
    const bool ok = std::fwrite(record.data(), 1, record.size(), f) == record.size();
    std::fclose(f);
    if (!ok || ::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        if (err) *err = "could not write the keep record";
        return false;
    }
    return true;
}

bool unkeep(const std::string& cacheDir, int romId) {
    // The ROM itself stays exactly where it is. Un-keeping returns a game to
    // the cache; it does not delete it. It becomes evictable, and it may well
    // sit there for months before anything needs the room.
    return ::unlink(keptPath(cacheDir, romId).c_str()) == 0;
}

void markPending(const std::string& cacheDir, int romId, const std::string& fileName,
                 int64_t bytes) {
    ensureDir(cacheDir);
    ensureDir(pendingDir(cacheDir));
    const std::string path = pendingDir(cacheDir) + "/" + markerName(romId, fileName);
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "%lld\n", static_cast<long long>(bytes));
        std::fclose(f);
    }
}

void clearPending(const std::string& cacheDir, int romId, const std::string& fileName) {
    ::unlink((pendingDir(cacheDir) + "/" + markerName(romId, fileName)).c_str());
}

int64_t pendingBytes(const std::string& cacheDir) {
    DIR* d = ::opendir(pendingDir(cacheDir).c_str());
    if (!d) return 0;
    int64_t total = 0;
    while (struct dirent* f = ::readdir(d)) {
        if (f->d_name[0] == '.') continue;
        if (FILE* fh = std::fopen((pendingDir(cacheDir) + "/" + f->d_name).c_str(), "rb")) {
            long long n = 0;
            if (std::fscanf(fh, "%lld", &n) == 1 && n > 0) total += n;
            std::fclose(fh);
        }
    }
    ::closedir(d);
    return total;
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
