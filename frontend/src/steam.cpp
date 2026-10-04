#include "steam.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <unistd.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <thread>
#include <vector>

#include "cache.h"
#include "prefs.h"
#include "storage.h"
#include "unit.h"

namespace steam {

namespace {

constexpr const char* kSlice = "/var/lib/cabinetos-steam/steam.img";
constexpr const char* kState = "/run/cabinetos-steam.state";
// How long the helper may take. A grow checks the whole filesystem first,
// which with hundreds of GB of games takes a while; nothing else comes close.
constexpr int kHelperSeconds = 15 * 60;

std::string runtimeDir() {
    const char* d = std::getenv("XDG_RUNTIME_DIR");
    if (d && *d) return d;
    return "/run/user/" + std::to_string(::getuid());
}

std::string home() {
    const char* h = std::getenv("HOME");
    return h && *h ? h : "/var/home/cabinet";
}

std::string mainLocation() {
    const std::vector<std::string> all = storage::locations();
    return all.empty() ? storage::root() : all.front();
}

// A whole MiB, which the helper insists on: decimal GB steps are not.
int64_t wholeMiB(int64_t b) { return b - b % (1024 * 1024); }

// The helper's answer for `op`, written after `since`: "done", "failed" (with
// `why`), or "" while it is still working.
std::string answer(const std::string& op, int64_t since, std::string* why) {
    std::ifstream in(kState);
    std::string line, state, gotOp, reason;
    int64_t at = 0;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "state") state = v;
        else if (k == "op") gotOp = v;
        else if (k == "reason") reason = v;
        else if (k == "at") at = std::strtoll(v.c_str(), nullptr, 10);
    }
    if (gotOp != op || at < since) return "";
    if (state == "failed" && why) *why = reason;
    return state == "working" ? "" : state;
}

// Starts cabinetos-steam@<op> and waits for its answer.
bool runHelper(const std::string& op, std::string* why) {
    const int64_t since = static_cast<int64_t>(::time(nullptr));
    const std::string unitName = "cabinetos-steam@" + op + ".service";
    std::string err;
    if (!unit::start(unitName.c_str(), &err)) {
        if (why) *why = "Couldn't reach the system";
        std::fprintf(stderr, "[steam] %s refused: %s\n", op.c_str(), err.c_str());
        return false;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(kHelperSeconds);
    while (std::chrono::steady_clock::now() < deadline) {
        std::string reason;
        const std::string a = answer(op, since, &reason);
        if (a == "done") {
            std::fprintf(stderr, "[steam] %s done\n", op.c_str());
            return true;
        }
        if (a == "failed") {
            std::fprintf(stderr, "[steam] %s failed: %s\n", op.c_str(), reason.c_str());
            if (why) *why = reason;
            return false;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    std::fprintf(stderr, "[steam] %s: no answer in %d s\n", op.c_str(), kHelperSeconds);
    if (why) *why = "No answer from the system";
    return false;
}

// Deletes a file or a whole folder WITHOUT FOLLOWING LINKS: ~/.steam is a
// folder of links into Steam's own, and a link is removed, never its target.
void removeTree(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return;
    if (S_ISDIR(st.st_mode)) {
        if (DIR* d = ::opendir(path.c_str())) {
            while (struct dirent* e = ::readdir(d)) {
                if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
                removeTree(path + "/" + e->d_name);
            }
            ::closedir(d);
        }
        ::rmdir(path.c_str());
    } else {
        ::unlink(path.c_str());
    }
}

// Clears cached games, oldest first and once, so the main drive has `bytes`
// free beyond the console's floors and the cache's headroom.
void makeRoom(int64_t bytes) {
    const std::string main = mainLocation();
    const int64_t need =
        bytes + cache::saveFloorBytes(main) + cache::kSystemReserveBytes + kCacheHeadroomBytes;
    const int64_t freed = cache::evictUntilFree(main, need);
    std::fprintf(stderr, "[steam] made room for %lld bytes (%lld freed from the cache)\n",
                 static_cast<long long>(bytes), static_cast<long long>(freed));
}

}  // namespace

bool available() {
    return ::access("/usr/bin/steam", X_OK) == 0 &&
           ::access("/usr/share/gamescope-session-plus/gamescope-session-plus", X_OK) == 0 &&
           ::access("/usr/libexec/cabinetos-steam", X_OK) == 0;
}

bool isSetUp() {
    struct stat st;
    return ::stat(kSlice, &st) == 0 && S_ISREG(st.st_mode);
}

int64_t sliceBytes() {
    struct stat st;
    if (::stat(kSlice, &st) != 0 || !S_ISREG(st.st_mode)) return 0;
    return static_cast<int64_t>(st.st_size);
}

bool sliceOn(const std::string& location) {
    struct stat a, b;
    return ::stat(kSlice, &a) == 0 && S_ISREG(a.st_mode) &&
           ::stat(location.c_str(), &b) == 0 && a.st_dev == b.st_dev;
}

bool hidden() { return prefs::get("steam", "shown") == "hidden"; }

void setHidden(bool hide) { prefs::set("steam", hide ? "hidden" : "shown"); }

int64_t roomBytes() {
    const std::string main = mainLocation();
    int64_t room = cache::freeBytes(main);
    for (const cache::Entry& e : cache::candidates(main)) room += e.bytes;
    room -= cache::saveFloorBytes(main) + cache::kSystemReserveBytes + kCacheHeadroomBytes;
    room += sliceBytes();
    return std::max<int64_t>(0, room);
}

int64_t defaultBytes(int64_t room) {
    // The whole drive, Steam's slice or not: the default is a share of the
    // drive the box says, not of what is left.
    struct statvfs v;
    if (::statvfs(mainLocation().c_str(), &v) != 0) return 0;
    const int64_t drive = static_cast<int64_t>(v.f_blocks) * static_cast<int64_t>(v.f_frsize);
    int64_t d = std::clamp<int64_t>(drive / 4, kMinBytes, kDefaultCapBytes);
    d -= d % kStepBytes;
    if (d > room) d = room - room % kStepBytes;
    return d < kMinBytes ? 0 : d;
}

bool create(int64_t bytes, std::string* why) {
    bytes = wholeMiB(bytes);
    if (isSetUp()) return true;
    if (bytes < wholeMiB(kMinBytes) || bytes > roomBytes()) {
        if (why) *why = "Not enough space";
        return false;
    }
    makeRoom(bytes);
    return runHelper("create-" + std::to_string(bytes), why);
}

bool grow(int64_t bytes, std::string* why) {
    bytes = wholeMiB(bytes);
    const int64_t now = sliceBytes();
    if (bytes <= now) return true;
    if (bytes > roomBytes()) {
        if (why) *why = "Not enough space";
        return false;
    }
    makeRoom(bytes - now);
    return runHelper("grow-" + std::to_string(bytes), why);
}

bool remove(std::string* why) {
    if (!runHelper("remove", why)) return false;
    // Steam's own files outside its folder, as the console's user: the links
    // it keeps in ~/.steam, its pid and path files, the screen modes its
    // session saved, an autostart entry, and the shortcuts it makes for games
    // (desktop files that run steam://). Nothing else in the home folder is
    // touched.
    const std::string h = home();
    for (const char* p : {"/.steam", "/.steampath", "/.steampid", "/.config/gamescope",
                          "/.config/autostart/steam.desktop"})
        removeTree(h + p);
    const std::string apps = h + "/.local/share/applications";
    if (DIR* d = ::opendir(apps.c_str())) {
        while (struct dirent* e = ::readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() < 9 || name.compare(name.size() - 8, 8, ".desktop") != 0) continue;
            std::ifstream in(apps + "/" + name);
            const std::string body((std::istreambuf_iterator<char>(in)),
                                   std::istreambuf_iterator<char>());
            if (body.find("steam://") != std::string::npos) ::unlink((apps + "/" + name).c_str());
        }
        ::closedir(d);
    }
    std::fprintf(stderr, "[steam] removed\n");
    return true;
}

bool requestHandover() {
    const std::string path = runtimeDir() + "/cabinetos-next";
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::trunc);
        out << "steam\n";
        if (!out) return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) return false;
    std::fprintf(stderr, "[steam] asked the session to hand over to Steam\n");
    return true;
}

bool takeReturned() {
    const std::string path = runtimeDir() + "/cabinetos-from-steam";
    if (::unlink(path.c_str()) != 0) return false;
    std::fprintf(stderr, "[steam] back from Steam\n");
    return true;
}

}  // namespace steam
