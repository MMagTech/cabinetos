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
#include <set>

namespace cache {
namespace {

std::string keepPath(const storage::User& u, int romId) {
    return storage::keepsDir(u) + "/" + std::to_string(romId) + ".json";
}

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

// Walks one half of one location — `roms/` or `cache/` — calling `fn` with the
// platform segment and the entry name. Both halves have exactly the same shape,
// which is the property that makes a demotion a rename.
template <typename F>
void walkHalf(const std::string& half, F fn) {
    DIR* top = ::opendir(half.c_str());
    if (!top) return;   // nothing kept yet, or nothing cached. Both ordinary.
    while (struct dirent* plat = ::readdir(top)) {
        if (plat->d_name[0] == '.') continue;
        const std::string platform = plat->d_name;
        const std::string pdir = half + "/" + platform;
        DIR* pd = ::opendir(pdir.c_str());
        if (!pd) continue;
        while (struct dirent* e = ::readdir(pd)) {
            if (e->d_name[0] == '.') continue;
            fn(platform, std::string(e->d_name));
        }
        ::closedir(pd);
    }
    ::closedir(top);
}

int64_t newestMtime(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return 0;
    return static_cast<int64_t>(st.st_mtime);
}

bool removeTree(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return ::unlink(path.c_str()) == 0;
    DIR* d = ::opendir(path.c_str());
    if (!d) return false;
    while (struct dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        removeTree(path + "/" + e->d_name);
    }
    ::closedir(d);
    return ::rmdir(path.c_str()) == 0;
}

// Moves a game between `roms/` and `cache/` on the location it already sits on.
//
// THIS IS WHY THE TWO DIRECTORIES REPEAT ON EVERY LOCATION. With one `cache/`
// at the root and kept games on a second drive, this would be a copy of
// gigabytes because somebody changed their mind about a game. moveEntry reports
// a crossed filesystem rather than hiding it, so if that ever happens here it
// is a fault in the layout and it says so.
bool relocate(const Placement& p, bool toKept) {
    if (!p.present || p.kept == toKept) return true;
    // THE NAME DOES NOT CHANGE — only which half it is under. Rebuilding it
    // from the game's title here would rename a game as a side effect of
    // keeping it, and the name on disk is what a person recognises.
    const std::string entryName = p.entryPath.substr(p.entryPath.find_last_of('/') + 1);
    const std::string target = (toKept ? storage::romsDir(p.location)
                                       : storage::cacheDir(p.location)) +
                               "/" + p.platform + "/" + entryName;
    if (target == p.entryPath) return true;
    const storage::MoveResult r = storage::moveEntry(p.entryPath, target);
    if (!r.ok) {
        std::fprintf(stderr, "[keep] could not %s %s: %s\n",
                     toKept ? "promote" : "demote", p.entryPath.c_str(), r.error.c_str());
        return false;
    }
    if (r.crossedFilesystem) {
        std::fprintf(stderr,
                     "[keep] %s CROSSED A FILESYSTEM and had to be copied: %s -> %s. "
                     "roms/ and cache/ are supposed to repeat per location so this "
                     "is a rename; something is wrong with the layout.\n",
                     toKept ? "promotion" : "demotion", p.entryPath.c_str(),
                     target.c_str());
    }
    std::fprintf(stderr, "[keep] %s %s\n", toKept ? "promoted" : "demoted to the cache",
                 target.c_str());
    return true;
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

std::vector<Placement> findAll(int romId) {
    std::vector<Placement> out;
    for (const std::string& loc : storage::locations()) {
        // `roms/` before `cache/`, so that where there IS only one answer it is
        // the kept one.
        const std::pair<std::string, bool> halves[2] = {
            {storage::romsDir(loc), true},
            {storage::cacheDir(loc), false},
        };
        for (const auto& [half, kept] : halves) {
            walkHalf(half, [&](const std::string& platform, const std::string& entry) {
                if (storage::romIdFromEntry(entry) != romId) return;
                Placement p;
                p.present = true;
                p.kept = kept;
                p.location = loc;
                p.platform = platform;
                p.entryPath = half + "/" + platform + "/" + entry;
                struct stat st;
                p.isDirectory = ::lstat(p.entryPath.c_str(), &st) == 0 &&
                                S_ISDIR(st.st_mode);
                out.push_back(std::move(p));
            });
        }
    }
    return out;
}

Placement find(int romId) {
    const std::vector<Placement> all = findAll(romId);
    return all.empty() ? Placement{} : all.front();
}

namespace {

// Does this entry hold the payload RomM describes?
//
// A single-file entry answers with its own size. A directory entry cannot —
// it holds the archive that came down AND whatever was unpacked out of it, so
// its total is always larger — so the question becomes whether any one file
// inside it is exactly the size the server reports. That file is the download,
// and its presence at the right size is the same test the launch path already
// trusts to decide a game need not be fetched again.
bool holdsPayloadOfSize(const std::string& entryPath, int64_t expectedBytes) {
    if (expectedBytes <= 0) return false;
    struct stat st;
    if (::lstat(entryPath.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return st.st_size == expectedBytes;
    DIR* d = ::opendir(entryPath.c_str());
    if (!d) return false;
    bool found = false;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        struct stat f;
        if (::stat((entryPath + "/" + e->d_name).c_str(), &f) == 0 && S_ISREG(f.st_mode) &&
            f.st_size == expectedBytes) {
            found = true;
            break;
        }
    }
    ::closedir(d);
    return found;
}

}  // namespace

Placement dedupe(int romId, int64_t expectedBytes) {
    std::vector<Placement> all = findAll(romId);
    if (all.size() <= 1) return all.empty() ? Placement{} : all.front();

    // Where this game BELONGS, which is a fact about the game and not about any
    // disk: kept games live on the games drive, and the cache lives on the
    // internal one. The keep record never leaves the internal disk, so this
    // answer is available even when the drive is not.
    const bool kept = isKeptByAnyone(romId);
    const std::string home = kept ? storage::keepLocation() : storage::primaryLocation();

    std::vector<const Placement*> usable;
    for (const Placement& p : all)
        if (holdsPayloadOfSize(p.entryPath, expectedBytes)) usable.push_back(&p);

    if (usable.empty()) {
        // NOTHING IS DELETED. Every copy is the wrong size, so there is no
        // known-good one to fall back on and picking a survivor would be a
        // guess — the one move here that could actually cost something. The
        // launch re-fetches, which fixes it.
        std::fprintf(stderr,
                     "[storage] rom %d is here %zu times and NONE is %lld bytes — "
                     "leaving all of them alone and fetching a clean copy\n",
                     romId, all.size(), static_cast<long long>(expectedBytes));
        return all.front();
    }

    const Placement* winner = usable.front();
    for (const Placement* p : usable)
        if (p->location == home) { winner = p; break; }

    for (const Placement& p : all) {
        if (&p == winner) continue;
        if (removeTree(p.entryPath)) {
            std::fprintf(stderr,
                         "[storage] rom %d was here twice; kept %s and removed %s\n",
                         romId, winner->entryPath.c_str(), p.entryPath.c_str());
        }
    }
    return *winner;
}

std::string entryPathFor(const std::string& location, const std::string& platform,
                         int romId, const std::string& title, bool kept) {
    const std::string half = kept ? storage::romsDir(location) : storage::cacheDir(location);
    return half + "/" + storage::platformSegment(platform) + "/" +
           storage::entryName(romId, title);
}

std::vector<Entry> candidates(const std::string& location) {
    std::vector<Entry> out;
    walkHalf(storage::cacheDir(location),
             [&](const std::string& platform, const std::string& entry) {
                 const int romId = storage::romIdFromEntry(entry);
                 if (romId <= 0) return;
                 const std::string path =
                     storage::cacheDir(location) + "/" + platform + "/" + entry;
                 out.push_back({path, storage::treeBytes(path), newestMtime(path), romId,
                                platform});
             });
    // Oldest first, which is the entire eviction order.
    std::sort(out.begin(), out.end(), [](const Entry& a, const Entry& b) {
        return a.lastUsed < b.lastUsed;
    });
    return out;
}

int64_t evictUntilFree(const std::string& location, int64_t needBytes,
                       int protectRomId) {
    const int64_t target =
        needBytes + static_cast<int64_t>(static_cast<double>(needBytes) * kMarginFraction);
    int64_t have = freeBytes(location);
    if (have >= target) return 0;

    // `have + freed` rather than re-measuring each time round, which is both
    // cheaper and — see the sync below — the only thing that would work.
    int64_t freed = 0;
    for (const Entry& e : candidates(location)) {
        if (have + freed >= target) break;
        // The running game. Its file is open, and deleting it would be the one
        // eviction a person could actually notice.
        if (protectRomId != 0 && e.romId == protectRomId) continue;
        if (!removeTree(e.path)) continue;
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
        if (const int fd = ::open(location.c_str(), O_RDONLY | O_DIRECTORY); fd >= 0) {
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

bool isKeptBy(const storage::User& u, int romId) {
    if (!u.valid()) return false;
    struct stat st;
    return ::stat(keepPath(u, romId).c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

std::vector<int> keepers(int romId) {
    std::vector<int> out;
    for (const storage::User& u : storage::knownUsers())
        if (isKeptBy(u, romId)) out.push_back(u.id);
    return out;
}

bool isKeptByAnyone(int romId) { return !keepers(romId).empty(); }

std::vector<int> keptRoms(const storage::User& u) {
    std::vector<int> out;
    if (!u.valid()) return out;
    DIR* d = ::opendir(storage::keepsDir(u).c_str());
    if (!d) return out;   // nothing kept yet is the ordinary case, not a fault
    while (struct dirent* f = ::readdir(d)) {
        if (f->d_name[0] == '.') continue;
        char* end = nullptr;
        const long id = std::strtol(f->d_name, &end, 10);
        if (id > 0 && end && std::strcmp(end, ".json") == 0)
            out.push_back(static_cast<int>(id));
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

std::vector<int> allKeptRoms() {
    std::set<int> ids;
    for (const storage::User& u : storage::knownUsers())
        for (int id : keptRoms(u)) ids.insert(id);
    return std::vector<int>(ids.begin(), ids.end());
}

KeepVerdict mayKeep(const std::string& location, int romId, int64_t gameBytes) {
    KeepVerdict v;
    v.floorBytes = saveFloorBytes(location) + kSystemReserveBytes;

    // Everything the console could still delete if it had to, with this game
    // excluded — it is about to stop being evictable.
    int64_t reclaimable = freeBytes(location);
    for (const Entry& e : candidates(location)) {
        if (e.romId == romId) continue;
        reclaimable += e.bytes;
    }

    // The part of this game still to fetch comes OUT of that, because keeping
    // an undownloaded game is a promise to spend the space.
    //
    // A game SOMEBODY ELSE ALREADY KEEPS costs nothing at all: the bytes are on
    // the machine and are already not evictable, so the second keep changes
    // nothing about the disk. That is the whole of what "one kept game, two
    // people" means here.
    const Placement p = find(romId);
    const int64_t onDisk = p.present ? storage::treeBytes(p.entryPath) : 0;
    const int64_t stillToFetch = p.kept ? 0 : std::max<int64_t>(0, gameBytes - onDisk);
    reclaimable -= stillToFetch;

    // An upload that has not reached the server is the one thing here that
    // cannot be re-fetched, so its bytes are spoken for and are not reclaimable
    // by anybody.
    reclaimable -= pendingBytes();

    v.reclaimableBytes = reclaimable;
    v.allowed = reclaimable >= v.floorBytes;
    v.shortfallBytes = v.allowed ? 0 : (v.floorBytes - reclaimable);
    return v;
}

bool keep(const storage::User& u, int romId, const std::string& record,
          std::string* err) {
    if (!u.valid()) {
        if (err) *err = "no user, so there is nobody to keep this for";
        return false;
    }
    storage::makeDirs(storage::keepsDir(u));
    const std::string path = keepPath(u, romId);
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
    // And out of the cache, if it is here. A game that is not here yet is
    // promoted by the download, which writes it straight into `roms/`.
    relocate(find(romId), /*toKept=*/true);
    return true;
}

bool unkeep(const storage::User& u, int romId, bool keepTheBytes, Release* out) {
    Release scratch;
    Release& r = out ? *out : scratch;
    r = Release{};

    if (!u.valid()) return false;
    const std::string path = keepPath(u, romId);
    struct stat st;
    const bool had = ::stat(path.c_str(), &st) == 0;
    if (had && ::unlink(path.c_str()) != 0) return false;

    // SOMEBODY ELSE MAY STILL BE KEEPING IT, and then nothing happens to the
    // file at all. This is the whole reason keeping stopped being a boolean.
    const std::vector<int> rest = keepers(romId);
    if (!rest.empty()) {
        std::fprintf(stderr, "[keep] %d released by user %d, still kept by %zu other(s)\n",
                     romId, u.id, rest.size());
        r.what = Release::What::StillKept;
        r.otherKeepers = static_cast<int>(rest.size());
        return true;
    }

    const Placement p = find(romId);
    if (!p.present) return true;

    if (keepTheBytes) {
        // See cache.h: the game is being played, or a keep failed. Neither is
        // somebody asking for their space back, so the game drops into the
        // cache and eviction takes it in the ordinary way.
        relocate(p, /*toKept=*/false);
        std::fprintf(stderr, "[keep] %d released to the cache\n", romId);
        r.what = Release::What::Demoted;
        return true;
    }

    // THE LAST KEEP DELETES, because the row says "Remove download" and
    // reclaiming the space is why anybody presses it.
    const int64_t bytes = storage::treeBytes(p.entryPath);
    if (!removeTree(p.entryPath)) {
        std::fprintf(stderr, "[keep] %d released but %s could not be removed\n", romId,
                     p.entryPath.c_str());
        r.what = Release::What::DeleteFailed;
        return true;
    }
    // WITHOUT THIS THE SPACE DOES NOT APPEAR TO COME BACK, which is the exact
    // complaint that changed this behaviour. btrfs unlinks immediately and
    // leaves statvfs reporting the old figure until a transaction commits, so a
    // Storage screen refreshed a second later would show no change at all. Same
    // reason evictUntilFree does it.
    if (const int fd = ::open(p.location.c_str(), O_RDONLY | O_DIRECTORY); fd >= 0) {
        ::syncfs(fd);
        ::close(fd);
    }
    std::fprintf(stderr, "[keep] %d released and removed, %lld bytes back\n", romId,
                 static_cast<long long>(bytes));
    r.what = Release::What::Deleted;
    r.bytesFreed = bytes;
    return true;
}

void markPending(const storage::User& u, int romId, const std::string& fileName,
                 int64_t bytes) {
    if (!u.valid()) return;
    storage::makeDirs(storage::pendingDir(u));
    const std::string path = storage::pendingDir(u) + "/" + markerName(romId, fileName);
    if (FILE* f = std::fopen(path.c_str(), "wb")) {
        std::fprintf(f, "%lld\n", static_cast<long long>(bytes));
        std::fclose(f);
    }
}

void clearPending(const storage::User& u, int romId, const std::string& fileName) {
    if (!u.valid()) return;
    ::unlink((storage::pendingDir(u) + "/" + markerName(romId, fileName)).c_str());
}

bool isPending(const storage::User& u, int romId, const std::string& fileName) {
    if (!u.valid()) return false;
    struct stat st;
    const std::string path = storage::pendingDir(u) + "/" + markerName(romId, fileName);
    return ::stat(path.c_str(), &st) == 0;
}

int64_t pendingBytes() {
    int64_t total = 0;
    for (const storage::User& u : storage::knownUsers()) {
        const std::string dir = storage::pendingDir(u);
        DIR* d = ::opendir(dir.c_str());
        if (!d) continue;
        while (struct dirent* f = ::readdir(d)) {
            if (f->d_name[0] == '.') continue;
            if (FILE* fh = std::fopen((dir + "/" + f->d_name).c_str(), "rb")) {
                long long n = 0;
                if (std::fscanf(fh, "%lld", &n) == 1 && n > 0) total += n;
                std::fclose(fh);
            }
        }
        ::closedir(d);
    }
    return total;
}

void touch(const std::string& entryPath) {
    // The entry's own mtime is the record, so there is no sidecar to keep in
    // step and nothing to migrate. utimensat with nullptr means "now", and it
    // works on a directory entry exactly as it does on a file one.
    if (::utimensat(AT_FDCWD, entryPath.c_str(), nullptr, 0) != 0) {
        // Not worth failing a launch over: the cost is that this game looks
        // older than it is and may be evicted sooner than it deserves.
        std::fprintf(stderr, "[cache] could not touch %s\n", entryPath.c_str());
    }
}

}  // namespace cache
