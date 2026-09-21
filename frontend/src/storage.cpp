#include "storage.h"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <json-c/json.h>

#include "romm.h"

namespace storage {
namespace {

std::string gRoot;
User gUser;

// Read once, on the first call that needs it, and remembered. Resolving this
// repeatedly would mean a console that changed its mind about where it lives
// halfway through a launch.
bool gRootResolved = false;

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Whether we could actually write there, asked by writing rather than by
// reading a permission bit — a read-only mount and a directory owned by root
// both look fine to access(2) in ways that differ by filesystem.
bool writable(const std::string& dir) {
    const std::string probe = dir + "/.cabinetos-write-test";
    const int fd = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    ::close(fd);
    ::unlink(probe.c_str());
    return true;
}

void resolveRoot() {
    if (gRootResolved) return;
    gRootResolved = true;
    if (!gRoot.empty()) return;
    if (const char* env = std::getenv("CABINETOS_STORAGE"); env && *env) {
        gRoot = env;
        return;
    }
    // The console's own answer. It is tried rather than assumed, because the
    // development VM runs the frontend out of a home directory and must not
    // start scattering a person's saves into /var/lib on a machine where that
    // is nobody's idea of where they went.
    const char* kConsoleRoot = "/var/lib/cabinetos";
    if (makeDirs(kConsoleRoot) && writable(kConsoleRoot)) {
        gRoot = kConsoleRoot;
        return;
    }
    gRoot = ".";
}

std::string userCachePath() { return configDir() + "/user.json"; }

// The files a core needs that SHIP INSIDE THE IMAGE — PPSSPP's fonts and lookup
// tables, 13 MB of them, which came out of a core build rather than off the
// server. They belong in /usr/share/cabinetos: replaced wholesale on every
// update, never written to, and not the person's data.
//
// A core still wants them in its one system directory, so they are LINKED in
// rather than copied. A name already present as a real file or directory is
// left alone, which is what keeps a development machine working — there the
// assets sit in `bios/` where the core build put them and the image directory
// does not exist at all.
void linkImageAssets() {
    const std::string from = imageAssetsDir();
    const std::string to = biosDir();
    DIR* d = ::opendir(from.c_str());
    if (!d) return;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        const std::string dest = to + "/" + e->d_name;
        struct stat st;
        if (::lstat(dest.c_str(), &st) == 0) continue;
        if (::symlink((from + "/" + e->d_name).c_str(), dest.c_str()) == 0)
            std::fprintf(stderr, "[storage] linked %s into the system directory\n",
                         e->d_name);
    }
    ::closedir(d);
}

// Copies one file, byte for byte, preserving nothing but the contents and the
// mode. Only reached when a move crossed a filesystem, which the layout exists
// to prevent — see moveEntry.
bool copyFile(const std::string& from, const std::string& to, std::string* err) {
    FILE* in = std::fopen(from.c_str(), "rb");
    if (!in) { if (err) *err = "cannot read " + from; return false; }
    FILE* out = std::fopen(to.c_str(), "wb");
    if (!out) { std::fclose(in); if (err) *err = "cannot write " + to; return false; }
    char buf[1 << 16];
    size_t n;
    bool ok = true;
    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) {
        if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    }
    if (std::ferror(in)) ok = false;
    std::fclose(in);
    if (std::fclose(out) != 0) ok = false;
    if (ok) {
        struct stat st;
        if (::stat(from.c_str(), &st) == 0) ::chmod(to.c_str(), st.st_mode & 07777);
    } else {
        ::unlink(to.c_str());
        if (err) *err = "copy of " + from + " did not complete";
    }
    return ok;
}

bool copyTree(const std::string& from, const std::string& to, std::string* err) {
    struct stat st;
    if (::lstat(from.c_str(), &st) != 0) { if (err) *err = "cannot stat " + from; return false; }
    if (!S_ISDIR(st.st_mode)) return copyFile(from, to, err);
    if (!makeDirs(to)) { if (err) *err = "cannot create " + to; return false; }
    DIR* d = ::opendir(from.c_str());
    if (!d) { if (err) *err = "cannot open " + from; return false; }
    bool ok = true;
    while (struct dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        if (!copyTree(from + "/" + e->d_name, to + "/" + e->d_name, err)) { ok = false; break; }
    }
    ::closedir(d);
    return ok;
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

// The filesystem a path is on. Two paths on the same one are the same disk,
// whatever their names say, and a "drive" that is really a folder on the
// internal disk is not a drive.
dev_t deviceOf(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 ? st.st_dev : 0;
}

// Where a plugged-in drive turns up. Nothing here is a CabinetOS invention:
// `/run/media/<user>/` is where udisks mounts a USB stick on Fedora and
// therefore on Bazzite, and `/var/mnt/` is where an fstab-mounted second
// internal disk conventionally goes on an image-based system.
//
// $CABINETOS_DRIVES overrides the search with an explicit colon-separated list,
// which is how this gets tested on a machine where nobody can plug anything in.
std::vector<std::string> driveSearchPaths() {
    std::vector<std::string> out;
    if (const char* env = std::getenv("CABINETOS_DRIVES"); env && *env) {
        std::string s = env;
        size_t start = 0;
        while (start <= s.size()) {
            const size_t colon = s.find(':', start);
            const std::string one =
                s.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
            if (!one.empty()) out.push_back(one);
            if (colon == std::string::npos) break;
            start = colon + 1;
        }
        return out;
    }
    const char* user = std::getenv("USER");
    out.push_back(std::string("/run/media/") + (user ? user : "cabinet"));
    out.push_back("/var/mnt");
    return out;
}

// The one folder this console claims on somebody else's drive.
const char* kDriveFolder = "CabinetOS";

std::string drivesMemoPath() { return configDir() + "/drives.json"; }

std::vector<std::string> readRememberedDrives() {
    std::vector<std::string> out;
    FILE* f = std::fopen(drivesMemoPath().c_str(), "rb");
    if (!f) return out;
    std::string body;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    std::fclose(f);
    json_object* o = json_tokener_parse(body.c_str());
    if (o && json_object_get_type(o) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(o); ++i)
            if (const char* v = json_object_get_string(json_object_array_get_idx(o, i)))
                out.push_back(v);
    }
    if (o) json_object_put(o);
    return out;
}

void writeRememberedDrives(const std::vector<std::string>& drives) {
    makeDirs(configDir());
    json_object* a = json_object_new_array();
    for (const std::string& d : drives)
        json_object_array_add(a, json_object_new_string(d.c_str()));
    const std::string tmp = drivesMemoPath() + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(json_object_to_json_string_ext(a, JSON_C_TO_STRING_PRETTY), f);
        std::fputc('\n', f);
        std::fclose(f);
        ::rename(tmp.c_str(), drivesMemoPath().c_str());
    }
    json_object_put(a);
}

}  // namespace

void setRoot(const std::string& path) {
    gRoot = path;
    gRootResolved = true;
}

const std::string& root() {
    resolveRoot();
    return gRoot;
}

bool makeDirs(const std::string& path) {
    if (path.empty()) return false;
    if (isDir(path)) return true;
    std::string built;
    if (path[0] == '/') built = "/";
    size_t i = 0;
    while (i < path.size()) {
        const size_t slash = path.find('/', i);
        const std::string part =
            path.substr(i, slash == std::string::npos ? std::string::npos : slash - i);
        if (!part.empty()) {
            if (built.size() > 1 || (built.size() == 1 && built[0] != '/')) built += "/";
            built += part;
            if (::mkdir(built.c_str(), 0755) != 0 && errno != EEXIST) return false;
        }
        if (slash == std::string::npos) break;
        i = slash + 1;
    }
    return isDir(path);
}

bool exists(const std::string& path) {
    struct stat st;
    return ::lstat(path.c_str(), &st) == 0;
}

bool ensureTree(std::string* err) {
    const std::string r = root();
    if (!makeDirs(r)) {
        if (err) *err = "cannot create the storage root at " + r;
        return false;
    }
    if (!writable(r)) {
        if (err) *err = "the storage root at " + r + " is not writable";
        return false;
    }
    // Every location gets the same shape, which is the whole reason a demotion
    // is a rename. A location that is missing — an unplugged drive — is skipped
    // rather than being an error: docs/PROJECT.md, open question 14, "a missing
    // drive degrades; it never errors".
    for (const std::string& loc : locations()) {
        if (!isDir(loc)) continue;
        makeDirs(romsDir(loc));
        makeDirs(cacheDir(loc));
    }
    makeDirs(biosDir());
    makeDirs(configDir());
    makeDirs(logsDir());
    makeDirs(r + "/users");
    linkImageAssets();
    return true;
}

std::vector<std::string> locations() {
    std::vector<std::string> out{root()};
    const dev_t here = deviceOf(root());
    for (const std::string& where : driveSearchPaths()) {
        DIR* d = ::opendir(where.c_str());
        if (!d) continue;   // nothing mounted there. The ordinary case.
        std::vector<std::string> mounts;
        while (struct dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            mounts.push_back(where + "/" + e->d_name);
        }
        ::closedir(d);
        std::sort(mounts.begin(), mounts.end());
        for (const std::string& mount : mounts) {
            if (!isDir(mount)) continue;
            // A folder on the internal disk is not a second drive, whatever it
            // is called. This is the check that stops the console treating its
            // own storage as removable and then "losing" it.
            if (deviceOf(mount) == here) continue;
            const std::string claim = mount + "/" + kDriveFolder;
            // PLUG IT IN AND IT IS USED: the folder is made without asking,
            // because a drive plugged into a games console is a games drive and
            // a confirmation dialogue is the thing this is trying not to have.
            // Nothing outside this folder is ever touched.
            if (!isDir(claim) && !makeDirs(claim)) continue;
            if (!writable(claim)) continue;
            out.push_back(claim);
        }
    }
    return out;
}

const std::string& primaryLocation() { return root(); }

std::string keepLocation() {
    const std::vector<std::string> all = locations();
    // The first drive, if there is one. Kept games are what a drive is FOR —
    // they are deliberate, they are the bulk, and they are the only thing worth
    // carrying. The cache stays on the internal disk, which is always attached
    // and usually faster than USB.
    return all.size() > 1 ? all[1] : all.front();
}

std::string missingDriveToReport() {
    const std::vector<std::string> now = locations();
    std::vector<std::string> present(now.begin() + 1, now.end());
    const std::vector<std::string> before = readRememberedDrives();

    std::string gone;
    for (const std::string& d : before) {
        if (std::find(present.begin(), present.end(), d) == present.end()) {
            gone = d;
            break;
        }
    }
    // Whatever is here now is what gets remembered, so a drive that has just
    // been reported missing is forgotten in the same breath and is not
    // mentioned again.
    if (before != present) writeRememberedDrives(present);
    return gone;
}

std::string romsDir(const std::string& location) { return location + "/roms"; }
std::string cacheDir(const std::string& location) { return location + "/cache"; }
std::string biosDir() { return root() + "/bios"; }
std::string configDir() { return root() + "/config"; }
std::string logsDir() { return root() + "/logs"; }

std::string imageAssetsDir() {
    // Inside the bootc image, replaced wholesale on every update and never
    // written to. On a development machine it is simply absent, and the assets
    // sit in the system directory where the core build put them.
    //
    // `/system` AND NOT `/usr/share/cabinetos` ITSELF. That directory already
    // holds things that have nothing to do with a core — a DEVELOPMENT-IMAGE
    // marker and two package inventories — and the first version of this linked
    // all three into the console's system directory, where a core would go
    // looking for its fonts. Found by running it; the directory listing is what
    // said so.
    if (const char* env = std::getenv("CABINETOS_ASSETS"); env && *env) return env;
    return "/usr/share/cabinetos/system";
}

std::string safeSegment(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (char c : s) {
        const unsigned char u = static_cast<unsigned char>(c);
        // Everything above ASCII passes through: this is UTF-8 and those bytes
        // are the tail of a multi-byte character. Replacing them turns a
        // Japanese title into a row of underscores, which sanitisedStem in
        // main.cpp already learned the hard way.
        if (u >= 0x80 || std::isalnum(u) || std::strchr(" -_().,'!&+[]", c) != nullptr)
            out += c;
        else
            out += '_';
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    if (out.empty() || out == "." || out == "..") out = "_";
    return out;
}

std::string platformSegment(const std::string& fsSlug) {
    return fsSlug.empty() ? "unknown" : safeSegment(fsSlug);
}

std::string entryName(int romId, const std::string& title) {
    return std::to_string(romId) + " - " + safeSegment(title);
}

int romIdFromEntry(const std::string& entry) {
    // The number is the identity and the words are decoration, so this reads
    // the number and stops. It deliberately accepts "321 - anything" and
    // "321.chd" alike: a rename is cosmetic and must not lose a game.
    char* end = nullptr;
    const long id = std::strtol(entry.c_str(), &end, 10);
    if (id <= 0 || end == entry.c_str()) return 0;
    return static_cast<int>(id);
}

std::string User::dirName() const {
    return std::to_string(id) + " - " + safeSegment(name);
}

const User& currentUser() { return gUser; }

void setCurrentUser(const User& u) {
    gUser = u;
    if (!u.valid()) return;
    makeDirs(configDir());
    // Cached so a console with no network still knows whose saves it holds.
    // "We could not reach the server" is not a reason to start writing into
    // somebody else's directory.
    json_object* o = json_object_new_object();
    json_object_object_add(o, "id", json_object_new_int(u.id));
    json_object_object_add(o, "username", json_object_new_string(u.name.c_str()));
    // Cached with the rest of them, so a console that cannot reach its server
    // still draws the right face rather than falling back to a letter.
    if (!u.avatar.empty())
        json_object_object_add(o, "avatar", json_object_new_string(u.avatar.c_str()));
    const char* text = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    const std::string tmp = userCachePath() + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(text, f);
        std::fputc('\n', f);
        std::fclose(f);
        ::rename(tmp.c_str(), userCachePath().c_str());
    }
    json_object_put(o);
    makeDirs(userDir(u));
}

bool resolveCurrentUser(romm::Client& client, std::string* err) {
    romm::User me;
    std::string e;
    if (client.haveToken() && client.fetchCurrentUser(&me, &e) && me.id > 0) {
        User u;
        u.id = me.id;
        u.name = me.username;
        u.avatar = me.avatarPath;
        setCurrentUser(u);
        return true;
    }
    // The cached answer. A console that cannot reach its server still has to
    // put a save somewhere, and the right somewhere is where the last one went.
    if (FILE* f = std::fopen(userCachePath().c_str(), "rb")) {
        std::string body;
        char buf[512];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
        std::fclose(f);
        json_object* o = json_tokener_parse(body.c_str());
        if (o) {
            json_object* v = nullptr;
            User u;
            if (json_object_object_get_ex(o, "id", &v) && v)
                u.id = json_object_get_int(v);
            if (json_object_object_get_ex(o, "username", &v) && v)
                u.name = json_object_get_string(v);
            if (json_object_object_get_ex(o, "avatar", &v) && v)
                u.avatar = json_object_get_string(v);
            json_object_put(o);
            if (u.valid()) {
                gUser = u;
                std::fprintf(stderr, "[storage] server did not answer; using the "
                                     "last known user %s\n", u.dirName().c_str());
                return true;
            }
        }
    }
    if (err) *err = e.empty() ? "no RomM user, and none cached" : e;
    return false;
}

std::string userDir(const User& u) { return root() + "/users/" + u.dirName(); }

std::string savesDir(const User& u, const std::string& platform, int romId,
                     const std::string& core) {
    return userDir(u) + "/saves/" + platformSegment(platform) + "/" +
           std::to_string(romId) + "/" + safeSegment(core);
}

std::string statesDir(const User& u, const std::string& platform, int romId,
                      const std::string& core) {
    return userDir(u) + "/states/" + platformSegment(platform) + "/" +
           std::to_string(romId) + "/" + safeSegment(core);
}

std::string screenshotsDir(const User& u) { return userDir(u) + "/screenshots"; }
std::string userConfigDir(const User& u) { return userDir(u) + "/config"; }
std::string keepsDir(const User& u) { return userDir(u) + "/keeps"; }
std::string pendingDir(const User& u) { return userDir(u) + "/pending"; }

std::string scratchSavesDir(const std::string& core) {
    // `--core` is given a path, not a name. Taking the whole thing would make a
    // directory called `cores_build_gambatte_libretro.so`.
    const size_t slash = core.find_last_of('/');
    const std::string name =
        slash == std::string::npos ? core : core.substr(slash + 1);
    return root() + "/users/0 - local/saves/local/0/" +
           safeSegment(name.empty() ? "core" : name);
}

std::vector<User> knownUsers() {
    std::vector<User> out;
    const std::string dir = root() + "/users";
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        if (!isDir(dir + "/" + e->d_name)) continue;
        const std::string name = e->d_name;
        const int id = romIdFromEntry(name);   // the same convention, both times
        if (id <= 0) continue;
        User u;
        u.id = id;
        const size_t dash = name.find(" - ");
        u.name = dash == std::string::npos ? std::string() : name.substr(dash + 3);
        out.push_back(std::move(u));
    }
    ::closedir(d);
    std::sort(out.begin(), out.end(), [](const User& a, const User& b) { return a.id < b.id; });
    return out;
}

std::string locationFor(int romId) {
    for (const std::string& loc : locations()) {
        for (const std::string& half : {romsDir(loc), cacheDir(loc)}) {
            DIR* d = ::opendir(half.c_str());
            if (!d) continue;
            bool found = false;
            while (struct dirent* plat = ::readdir(d)) {
                if (plat->d_name[0] == '.') continue;
                const std::string pdir = half + "/" + plat->d_name;
                DIR* pd = ::opendir(pdir.c_str());
                if (!pd) continue;
                while (struct dirent* entry = ::readdir(pd)) {
                    if (entry->d_name[0] == '.') continue;
                    if (romIdFromEntry(entry->d_name) == romId) { found = true; break; }
                }
                ::closedir(pd);
                if (found) break;
            }
            ::closedir(d);
            if (found) return loc;
        }
    }
    return locations().front();
}

MoveResult moveEntry(const std::string& from, const std::string& to) {
    MoveResult r;
    if (!exists(from)) {
        r.error = from + " is not there";
        return r;
    }
    const size_t slash = to.find_last_of('/');
    if (slash != std::string::npos) makeDirs(to.substr(0, slash));

    // THE FAST PATH IS THE ONLY PATH THE CONSOLE SHOULD EVER TAKE. A rename is
    // instant whatever the game weighs, and it is why `roms/` and `cache/`
    // repeat on every location instead of living once at the root.
    if (::rename(from.c_str(), to.c_str()) == 0) {
        r.ok = true;
        return r;
    }
    if (errno != EXDEV) {
        r.error = std::string("could not move ") + from + ": " + std::strerror(errno);
        return r;
    }

    // EXDEV: the two ends are on different filesystems. Reported, not hidden —
    // if this ever fires in the product, the layout has a fault in it and
    // somebody should know rather than wonder why a demotion took four minutes.
    r.crossedFilesystem = true;
    std::string err;
    if (!copyTree(from, to, &err)) {
        removeTree(to);
        r.error = err;
        return r;
    }
    if (!removeTree(from)) {
        r.error = "copied to " + to + " but could not remove " + from;
        return r;
    }
    r.ok = true;
    return r;
}

int64_t treeBytes(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return 0;
    if (!S_ISDIR(st.st_mode)) return S_ISREG(st.st_mode) ? st.st_size : 0;
    int64_t total = 0;
    DIR* d = ::opendir(path.c_str());
    if (!d) return 0;
    while (struct dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        total += treeBytes(path + "/" + e->d_name);
    }
    ::closedir(d);
    return total;
}

}  // namespace storage
