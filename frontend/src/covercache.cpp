#include "covercache.h"

#include <cstdio>
#include <mutex>
#include <string>

#include "cache.h"
#include "storage.h"

namespace covercache {
namespace {

std::mutex gMutex;          // guards gServer, the counters and directory creation
std::string gServer;        // sanitised, empty until setServer
Stats gStats;

// Pulls the two ids and the size out of a RomM cover path, and the version off
// the query string. Returns false for anything that is not one — an avatar, a
// platform logo, a stand-in library's file on disk — and those are then never
// cached rather than filed under a guess.
//
//   /assets/romm/resources/roms/15/569/cover/small.png?ts=2025-03-11 06:56:03
//                               ^pid ^rid       ^size      ^version
bool parse(const std::string& key, std::string* pid, std::string* rid,
           std::string* size, std::string* version) {
    const std::string marker = "/resources/roms/";
    const size_t at = key.find(marker);
    if (at == std::string::npos) return false;
    size_t i = at + marker.size();

    auto segment = [&](std::string* out) {
        const size_t start = i;
        while (i < key.size() && key[i] != '/') ++i;
        if (i == start || i >= key.size()) return false;
        out->assign(key, start, i - start);
        ++i;                       // step over the slash
        return true;
    };
    if (!segment(pid) || !segment(rid)) return false;

    // The next segment is "cover"; anything else is a different kind of asset
    // and is left alone.
    std::string kind;
    if (!segment(&kind) || kind != "cover") return false;

    const size_t q = key.find('?', i);
    const size_t end = (q == std::string::npos) ? key.size() : q;
    if (end <= i) return false;
    size->assign(key, i, end - i);            // "small.png"
    // Strip the extension; the file written here keeps its own.
    const size_t dot = size->rfind('.');
    if (dot != std::string::npos) size->erase(dot);

    // No timestamp is not a failure. It means the server did not version this
    // one, and it is filed under a constant so it is still found again.
    version->assign("none");
    if (q != std::string::npos) {
        const std::string ts = "ts=";
        const size_t t = key.find(ts, q);
        if (t != std::string::npos) *version = key.substr(t + ts.size());
    }
    return !pid->empty() && !rid->empty() && !size->empty();
}

// The file a key is stored at, or empty when the key is not cover art or no
// server has been set.
std::string pathFor(const std::string& key, std::string* dirOut) {
    std::string server;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        server = gServer;
    }
    if (server.empty()) return {};

    std::string pid, rid, size, version;
    if (!parse(key, &pid, &rid, &size, &version)) return {};

    // ORGANISED RATHER THAN HASHED, because somebody will look in here. A hash
    // of the whole key would be one flat directory of unreadable names, and the
    // first question anybody asks of a cache is "what is in it".
    const std::string d = dir() + "/" + server + "/" + storage::safeSegment(pid) +
                          "/" + storage::safeSegment(rid);
    if (dirOut) *dirOut = d;
    return d + "/" + storage::safeSegment(size) + "." +
           storage::safeSegment(version) + ".img";
}

}  // namespace

void setServer(const std::string& address) {
    std::lock_guard<std::mutex> lk(gMutex);
    gServer = storage::safeSegment(address);
}

std::string dir() { return storage::root() + "/covers"; }

std::vector<uint8_t> read(const std::string& key) {
    const std::string path = pathFor(key, nullptr);
    if (path.empty()) return {};
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::vector<uint8_t> out;
    uint8_t buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.insert(out.end(), buf, buf + n);
    std::fclose(f);
    // A truncated file is worse than no file: it decodes to nothing and the
    // cover silently never appears. An empty read is treated as a miss and the
    // server is asked.
    if (out.empty()) return {};
    {
        std::lock_guard<std::mutex> lk(gMutex);
        ++gStats.fromDisk;
    }
    return out;
}

Stats stats() {
    std::lock_guard<std::mutex> lk(gMutex);
    return gStats;
}

void write(const std::string& key, const std::vector<uint8_t>& bytes) {
    if (bytes.empty()) return;
    std::string d;
    const std::string path = pathFor(key, &d);
    if (path.empty()) return;
    {
        std::lock_guard<std::mutex> lk(gMutex);
        ++gStats.fetched;
    }

    // The reserve belongs to the system image, not to artwork. Checked before
    // every write rather than once at startup, because what fills a disk is
    // usually the thing that has been running for an hour.
    if (cache::freeBytes(storage::root()) <
        cache::kSystemReserveBytes + static_cast<int64_t>(bytes.size()))
        return;

    {
        std::lock_guard<std::mutex> lk(gMutex);
        if (!storage::makeDirs(d)) return;
    }

    // WRITTEN ASIDE AND RENAMED. Four ImageCache workers run concurrently and a
    // console loses power with no warning; a half-written cover that is found
    // later is indistinguishable from a real one, and rename is the only step
    // here that is atomic.
    const std::string tmp = path + ".part";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) return;
    const size_t put = std::fwrite(bytes.data(), 1, bytes.size(), f);
    const bool ok = (put == bytes.size()) && (std::fflush(f) == 0);
    std::fclose(f);
    if (!ok || std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return;
    }
    std::lock_guard<std::mutex> lk(gMutex);
    ++gStats.stored;
}

}  // namespace covercache
