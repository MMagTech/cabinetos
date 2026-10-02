#include "standalone.h"

#include "gpu.h"
#include "proc.h"
#include "storage.h"
#include "vpad.h"
#include "x360profile.h"
#include "xboxhdd.h"

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <type_traits>
#include <utility>
#include <vector>

namespace cab::standalone {
namespace {

// Eden and xemu are pinned in system_files/usr/share/cabinetos/flatpaks.list.
// RPCS3 is in the image (build_files/install-rpcs3.sh).
const Emulator kEmulators[] = {
    // Eden's words for a game it cannot load (yuzu/main_window.cpp, before its
    // "Error while loading ROM!" box).
    {"eden", "dev.eden_emu.eden", nullptr, "eden", "user/log/eden_log.txt",
     {"Failed to load ROM", "Failed to obtain loader"},
     // Eden's loader codes (core/loader/loader.h) for a key the file needs and
     // the keys do not hold: 21 is a master key newer than the file has, which
     // is what Metroid Prime 4 Beyond gave against RomM's v19 keys on
     // 2026-09-28; 18 and 19 are its title key and title key key.
     {"(Error 21)", "(Error 18)", "(Error 19)", "(Error 13)"},
     nullptr, nullptr,
     // "Eden | <version> | <compiler>" idle, and the game's name, its version
     // and the GPU after that while one runs (main_window.cpp,
     // UpdateWindowTitle).
     "Eden |", 5, nullptr,
     "nsp|xci", "keys", false, false, false, false, false, false},

    // PLAYSTATION 3. Its log sits beside its cache, in the game's folder
    // (XDG_CACHE_HOME, rpcs3/ appended: Utilities/File.cpp, get_cache_dir).
    // A load that fails is a FATAL line, `·F ` in its log, and RPCS3 then
    // opens its error box as a second process (rpcs3.cpp,
    // report_fatal_error). The licence line is Crypto/unself.cpp's. No title
    // check: started with --no-gui, RPCS3 quits by itself when the game
    // ends (gui_application.cpp, try_to_quit).
    {"rpcs3", nullptr, "/usr/lib/cabinetos/rpcs3/usr/bin/rpcs3", "rpcs3", "rpcs3/RPCS3.log",
     {"\xC2\xB7" "F ", nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     "Failed to locate the game license file",
     "Emulation has been frozen",
     nullptr, 0, nullptr,
     "iso|pkg", "firmware", true, true, true, false, false, false},

    // ORIGINAL XBOX. xemu keeps no log of its own; what it prints is it. A
    // game is a folder, as a PS3 game is, because its hard drive lives beside
    // the disc (`installs`: fetched file by file, then installGame). No title
    // check: xemu's window title never names the game (ui/xemu.c). SIGTERM
    // closes it cleanly: qemu drains and closes the drive first
    // (system/runstate.c, qemu_cleanup). Issue #172.
    {"xemu", "app.xemu.xemu", nullptr, "xemu", nullptr,
     {nullptr, nullptr},
     {nullptr, nullptr, nullptr, nullptr},
     nullptr, nullptr,
     nullptr, 0, nullptr,
     "iso|xiso", "BIOS", true, false, false, true, true, false},

    // XBOX 360. Xenia Edge's own Linux build, in the image
    // (build_files/install-xenia.sh). Its log is where the console tells it
    // (`--log_file`); a load that fails, and any fatal error later, is an
    // `x>` line there, then an error box nobody can press (base/logging.cc,
    // FatalError). SIGTERM does nothing: SDL takes it and Edge never reads
    // SDL's quit (measured on the A9, 2026-09-29), so it is closed by its
    // window, which exits 0 at once. A game that quits to the dashboard
    // leaves Edge open on its own list, and its window title loses the
    // `| [TITLEID vVER] Name` part: `Xenia-edge (...) <x64>` idle,
    // `Xenia-edge (...) | [4D530AA4 v0.0.0.12] Forza Horizon 2 <...>` in a
    // game (emulator_window.cc). Issue #192; open question 34.
    {"xenia", nullptr, "/usr/lib/cabinetos/xenia/usr/bin/xenia_edge", "xenia_edge", "xenia.log",
     {"Failed to launch target", "x> "},
     {nullptr, nullptr, nullptr, nullptr},
     nullptr,
     "x> ",
     "Xenia-edge", 2, nullptr,
     "iso|xex|zar", "", false, false, true, false, false, true},

    // WII U. Cemu main, built from source at a pin (cores/build-cemu.sh) and in
    // the image at /usr/lib/cabinetos/cemu. Its log is under its home
    // (XDG_DATA_HOME/Cemu). Started with `-g`, it builds no game list and
    // CLOSES ITSELF when the game ends, returning the game's own exit status
    // (OnRequestGameExit, CemuApp::OnExit), so no title watch. A game it
    // cannot open is a desktop box titled "Error" over its empty window, and
    // only some of those reach the log, so both are watched. SIGTERM is
    // `_Exit(0)`, no clean shutdown; the window close saves and stops the
    // game (measured on the A9, 2026-10-01: exit 0, save intact). Mid-game,
    // an unrecoverable Vulkan error is logged and thrown. Issue #174; open
    // question 36.
    {"cemu", nullptr, "/usr/lib/cabinetos/cemu/bin/Cemu", "Cemu", "Cemu/log.txt",
     {"Mounting failed", "Unable to find RPX executable"},
     {nullptr, nullptr, nullptr, nullptr},
     nullptr,
     "Unrecoverable error in Vulkan",
     nullptr, 0, "Error",
     "wua", "", false, false, true, false, false, true},
};

// How long a program that was asked to close gets before it is made to.
// Eden took 0.8 s on the A9; ten is room for a slow disk flushing a save.
constexpr int64_t kStopGraceMs = 10 * 1000;
// A failed load shows the emulator's own error box and waits for a click
// nobody can make, so its log is watched for this long after a start.
constexpr int64_t kLoadWatchMs = 60 * 1000;

// WHERE AN EMULATOR IN THE IMAGE IS, or, for development only,
// `CABINETOS_BINARY_<core>` (CABINETOS_BINARY_rpcs3=...), so a build of the
// frontend can be tried against an emulator before any image carries it. The
// console's session sets none.
std::string binaryOf(const Emulator& e) {
    const std::string var = std::string("CABINETOS_BINARY_") + e.core;
    if (const char* v = std::getenv(var.c_str()); v && *v) return v;
    return e.binary ? e.binary : "";
}

int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string readFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

bool writeFile(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out) return false;
        out << data;
        if (!out) return false;
    }
    return std::rename(tmp.c_str(), path.c_str()) == 0;
}

// The file in `bios/` that RomM serves under `prefix` + anything + `.keys`,
// with the highest version in its name: `prod-v19.keys` over `prod-v17.keys`,
// and over a bare `prod.keys`, which says nothing about its version.
std::string newestKeys(const std::string& biosDir, const std::string& prefix) {
    auto version = [](const std::string& name) {
        long best = -1;
        for (size_t i = 0; i < name.size();) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) { ++i; continue; }
            size_t j = i;
            while (j < name.size() && std::isdigit(static_cast<unsigned char>(name[j]))) ++j;
            best = std::max(best, std::strtol(name.substr(i, j - i).c_str(), nullptr, 10));
            i = j;
        }
        return best;
    };
    std::string pick;
    long pickVersion = -2;
    DIR* d = opendir(biosDir.c_str());
    if (!d) return pick;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() < prefix.size() + 5) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - 5, 5, ".keys") != 0) continue;
        struct stat st;
        const std::string path = biosDir + "/" + name;
        if (::stat(path.c_str(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_size == 0) continue;
        const long v = version(name);
        if (v > pickVersion || (v == pickVersion && path > pick)) {
            pick = path;
            pickVersion = v;
        }
    }
    closedir(d);
    return pick;
}

// The Switch firmware zip RomM serves, if any: a `.zip` in `bios/` with
// "firmware" in its name, the highest version when there are several. Named
// rather than any zip because `bios/` is shared by every system, and arcade
// BIOS sets are zips too.
std::string newestFirmwareZip(const std::string& biosDir) {
    std::string pick;
    long pickVersion = -2;
    DIR* d = opendir(biosDir.c_str());
    if (!d) return pick;
    while (dirent* e = readdir(d)) {
        std::string lower = e->d_name;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lower.size() < 4 || lower.compare(lower.size() - 4, 4, ".zip") != 0) continue;
        if (lower.find("firmware") == std::string::npos) continue;
        long v = -1;
        for (size_t i = 0; i < lower.size();) {
            if (!std::isdigit(static_cast<unsigned char>(lower[i]))) { ++i; continue; }
            size_t j = i;
            while (j < lower.size() && std::isdigit(static_cast<unsigned char>(lower[j]))) ++j;
            v = std::max(v, std::strtol(lower.substr(i, j - i).c_str(), nullptr, 10));
            break;   // the first number is the major version
        }
        const std::string path = biosDir + "/" + e->d_name;
        if (v > pickVersion || (v == pickVersion && path > pick)) {
            pick = path;
            pickVersion = v;
        }
    }
    closedir(d);
    return pick;
}

// FIRMWARE IS USED WHEN RomM HAS IT (decision A), installed the way Eden's
// own "Install Firmware" does it (qt_common/util/content.cpp): every `.nca` in
// the source, at any depth, copied by name into nand/system/Contents/
// registered after that folder is emptied. From the zip directly, so it does
// not matter whether the files are at its top or inside a folder.
//
// Once per zip: a note of which zip and its size is kept beside it, and the
// install runs again only when the zip in RomM changes. Mario Kart 8 Deluxe
// is why: without firmware its Mii screen crashed the game (2026-09-28).
bool installFirmware(const std::string& homeDir, const std::string& zip) {
    struct stat st;
    if (::stat(zip.c_str(), &st) != 0) return false;
    const std::string contents = homeDir + "/user/nand/system/Contents";
    const std::string registered = contents + "/registered";
    const std::string note = contents + "/cabinetos-firmware.txt";
    const std::string stamp =
        zip.substr(zip.find_last_of('/') + 1) + " " + std::to_string(st.st_size) + "\n";
    if (readFile(note) == stamp) return true;

    storage::makeDirs(registered);
    if (DIR* d = opendir(registered.c_str())) {
        while (dirent* e = readdir(d)) {
            const std::string n = e->d_name;
            if (n == "." || n == "..") continue;
            const std::string p = registered + "/" + n;
            struct stat es;
            if (::lstat(p.c_str(), &es) == 0 && S_ISREG(es.st_mode)) ::unlink(p.c_str());
        }
        closedir(d);
    }

    archive* a = archive_read_new();
    archive_read_support_format_zip(a);
    int placed = 0;
    bool ok = archive_read_open_filename(a, zip.c_str(), 1 << 16) == ARCHIVE_OK;
    archive_entry* entry = nullptr;
    while (ok && archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (archive_entry_filetype(entry) != AE_IFREG) continue;
        std::string name = archive_entry_pathname(entry);
        name = name.substr(name.find_last_of('/') + 1);
        if (name.size() < 4 || name.compare(name.size() - 4, 4, ".nca") != 0) continue;
        const std::string dest = registered + "/" + name;
        FILE* out = std::fopen(dest.c_str(), "wb");
        if (!out) { ok = false; break; }
        const void* buf;
        size_t size;
        la_int64_t offset;
        int r;
        while ((r = archive_read_data_block(a, &buf, &size, &offset)) == ARCHIVE_OK)
            if (std::fwrite(buf, 1, size, out) != size) { r = ARCHIVE_FATAL; break; }
        std::fclose(out);
        if (r != ARCHIVE_EOF) { ok = false; break; }
        ++placed;
    }
    archive_read_free(a);
    if (!ok || placed == 0) {
        std::fprintf(stderr, "[standalone] firmware from %s: %s\n", zip.c_str(),
                     placed == 0 ? "no .nca files in it" : "could not unpack it");
        return false;
    }
    writeFile(note, stamp);
    std::fprintf(stderr, "[standalone] firmware installed from %s (%d files)\n", zip.c_str(),
                 placed);
    return true;
}

// Copies `from` to `to` unless `to` already holds the same bytes.
bool placeFile(const std::string& from, const std::string& to) {
    const std::string bytes = readFile(from);
    if (bytes.empty()) return false;
    if (readFile(to) == bytes) return true;
    return writeFile(to, bytes);
}

// Sets `key=value` in `[section]` of an INI file, keeping everything else the
// emulator wrote. Eden rewrites the whole file on exit, so the console's
// choices are laid over whatever is there before every start rather than
// written once.
//
// AND FOR EDEN EACH ONE NEEDS A `key\default=false` BESIDE IT. Eden reads a
// setting's value only when that flag says it is not the default; a value on
// its own is silently ignored (frontend_common/config.cpp,
// ReadSettingGeneric). `withDefaults` off for anything else (RPCS3's Qt
// settings file).
struct IniSet {
    const char* section;
    const char* key;
    const char* value;
};

std::string applyIni(const std::string& text, const std::vector<IniSet>& sets,
                     bool withDefaults = true) {
    std::vector<std::string> lines;
    {
        std::istringstream in(text);
        for (std::string l; std::getline(in, l);) {
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines.push_back(l);
        }
    }
    auto sectionOf = [&](size_t upto) {
        std::string s;
        for (size_t i = 0; i < upto; ++i)
            if (!lines[i].empty() && lines[i].front() == '[' && lines[i].back() == ']')
                s = lines[i].substr(1, lines[i].size() - 2);
        return s;
    };
    auto setOne = [&](const std::string& section, const std::string& key,
                      const std::string& value) {
        const std::string prefix = key + "=";
        for (size_t i = 0; i < lines.size(); ++i) {
            if (lines[i].compare(0, prefix.size(), prefix) != 0) continue;
            if (sectionOf(i) != section) continue;
            lines[i] = prefix + value;
            return;
        }
        // Not there: at the end of its section, or a new section at the end.
        const std::string header = "[" + section + "]";
        auto at = std::find(lines.begin(), lines.end(), header);
        if (at == lines.end()) {
            if (!lines.empty() && !lines.back().empty()) lines.push_back("");
            lines.push_back(header);
            lines.push_back(prefix + value);
            return;
        }
        auto end = at + 1;
        while (end != lines.end() && !(!end->empty() && end->front() == '[')) ++end;
        while (end != at + 1 && (end - 1)->empty()) --end;
        lines.insert(end, prefix + value);
    };
    for (const IniSet& s : sets) {
        if (withDefaults) setOne(s.section, std::string(s.key) + "\\default", "false");
        setOne(s.section, s.key, s.value);
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

// THE EMULATOR'S WINDOW TITLE, read from the X server the console and the
// emulator share (gamescope's Xwayland). libX11 is opened at run time, as
// overlaywin.cpp does, so a console without it only loses this check.
struct XTitles {
    using Display = void;
    using Window = unsigned long;
    using Atom = unsigned long;
    void* lib = nullptr;
    Display* dpy = nullptr;
    Display* (*OpenDisplay)(const char*) = nullptr;
    int (*CloseDisplay)(Display*) = nullptr;
    Window (*DefaultRootWindow)(Display*) = nullptr;
    int (*QueryTree)(Display*, Window, Window*, Window*, Window**, unsigned*) = nullptr;
    Atom (*InternAtom)(Display*, const char*, int) = nullptr;
    int (*GetWindowProperty)(Display*, Window, Atom, long, long, int, Atom, Atom*, int*,
                             unsigned long*, unsigned long*, unsigned char**) = nullptr;
    int (*Free)(void*) = nullptr;
    int (*SendEvent)(Display*, Window, int, long, void*) = nullptr;
    int (*Flush)(Display*) = nullptr;

    bool open() {
        if (dpy) return true;
        if (!lib) lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
        if (!lib) return false;
        auto sym = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
            return fn != nullptr;
        };
        if (!(sym(OpenDisplay, "XOpenDisplay") && sym(CloseDisplay, "XCloseDisplay") &&
              sym(DefaultRootWindow, "XDefaultRootWindow") && sym(QueryTree, "XQueryTree") &&
              sym(InternAtom, "XInternAtom") && sym(GetWindowProperty, "XGetWindowProperty") &&
              sym(Free, "XFree") && sym(SendEvent, "XSendEvent") && sym(Flush, "XFlush")))
            return false;
        dpy = OpenDisplay(nullptr);
        return dpy != nullptr;
    }
    void close() {
        if (dpy) CloseDisplay(dpy);
        dpy = nullptr;
    }
    // The longest title among the top-level windows that start with `prefix`,
    // counted in " | " parts; 0 when there is none.
    int parts(const char* prefix) {
        if (!open()) return 0;
        const Atom netName = InternAtom(dpy, "_NET_WM_NAME", 0);
        const Atom utf8 = InternAtom(dpy, "UTF8_STRING", 0);
        Window rootRet = 0, parent = 0;
        Window* kids = nullptr;
        unsigned n = 0;
        if (!QueryTree(dpy, DefaultRootWindow(dpy), &rootRet, &parent, &kids, &n)) return 0;
        int best = 0;
        for (unsigned i = 0; i < n; ++i) {
            Atom type = 0;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            if (GetWindowProperty(dpy, kids[i], netName, 0, 1024, 0, utf8, &type, &format,
                                  &count, &after, &data) != 0 || !data)
                continue;
            const std::string title(reinterpret_cast<char*>(data), count);
            Free(data);
            if (title.compare(0, std::strlen(prefix), prefix) != 0) continue;
            int p = 1;
            for (size_t at = title.find(" | "); at != std::string::npos;
                 at = title.find(" | ", at + 3))
                ++p;
            best = std::max(best, p);
        }
        if (kids) Free(kids);
        return best;
    }
    // THE CLOSE A PERSON WOULD MAKE: WM_DELETE_WINDOW to every top-level
    // window of process `pid` (by _NET_WM_PID, which Qt sets). The number of
    // windows asked, so a caller can fall back when there were none.
    int closeWindowsOf(int pid) {
        if (!open()) return 0;
        const Atom wmPid = InternAtom(dpy, "_NET_WM_PID", 0);
        const Atom protocols = InternAtom(dpy, "WM_PROTOCOLS", 0);
        const Atom del = InternAtom(dpy, "WM_DELETE_WINDOW", 0);
        const Atom cardinal = 6;   // XA_CARDINAL
        Window rootRet = 0, parent = 0;
        Window* kids = nullptr;
        unsigned n = 0;
        if (!QueryTree(dpy, DefaultRootWindow(dpy), &rootRet, &parent, &kids, &n)) return 0;
        int asked = 0;
        for (unsigned i = 0; i < n; ++i) {
            Atom type = 0;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            if (GetWindowProperty(dpy, kids[i], wmPid, 0, 1, 0, cardinal, &type, &format, &count,
                                  &after, &data) != 0 || !data)
                continue;
            const long owner = count == 1 ? *reinterpret_cast<long*>(data) : -1;
            Free(data);
            if (owner != pid) continue;
            // XClientMessageEvent, laid out as Xlib declares it, inside the
            // 24 longs every XEvent is.
            struct {
                int type;
                unsigned long serial;
                int sendEvent;
                Display* display;
                Window window;
                Atom messageType;
                int format;
                long l[5];
            } msg{};
            long ev[24] = {};
            static_assert(sizeof msg <= sizeof ev, "XEvent is 24 longs");
            msg.type = 33;   // ClientMessage
            msg.sendEvent = 1;
            msg.display = dpy;
            msg.window = kids[i];
            msg.messageType = protocols;
            msg.format = 32;
            msg.l[0] = static_cast<long>(del);
            std::memcpy(ev, &msg, sizeof msg);
            if (SendEvent(dpy, kids[i], 0, 0, ev)) ++asked;
        }
        if (kids) Free(kids);
        Flush(dpy);
        return asked;
    }
    // Whether process `pid` has a top-level window called exactly `title`:
    // the emulator's own error box (`errorBox`).
    bool hasWindowTitled(int pid, const char* title) {
        if (!open()) return false;
        const Atom wmPid = InternAtom(dpy, "_NET_WM_PID", 0);
        const Atom netName = InternAtom(dpy, "_NET_WM_NAME", 0);
        const Atom utf8 = InternAtom(dpy, "UTF8_STRING", 0);
        const Atom cardinal = 6;   // XA_CARDINAL
        Window rootRet = 0, parent = 0;
        Window* kids = nullptr;
        unsigned n = 0;
        if (!QueryTree(dpy, DefaultRootWindow(dpy), &rootRet, &parent, &kids, &n)) return false;
        bool found = false;
        for (unsigned i = 0; i < n && !found; ++i) {
            Atom type = 0;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            if (GetWindowProperty(dpy, kids[i], wmPid, 0, 1, 0, cardinal, &type, &format, &count,
                                  &after, &data) != 0 || !data)
                continue;
            const long owner = count == 1 ? *reinterpret_cast<long*>(data) : -1;
            Free(data);
            if (owner != pid) continue;
            data = nullptr;
            if (GetWindowProperty(dpy, kids[i], netName, 0, 1024, 0, utf8, &type, &format,
                                  &count, &after, &data) != 0 || !data)
                continue;
            found = std::string(reinterpret_cast<char*>(data), count) == title;
            Free(data);
        }
        if (kids) Free(kids);
        return found;
    }
};
XTitles gTitles;

// EDEN'S PROFILE, which names the folder every save sits in:
// `user/save/0000000000000000/<profile>/<title ID>/`. Eden makes one with a
// RANDOM ID the first time it starts, so on a new console the folder a save
// must be restored into would not exist until the game was already running.
// So the console makes it first, with a fixed ID, and on a console where Eden
// already made one it is read and kept.
//
// The file is Eden's ProfileDataRaw (acc/profile_manager.cpp): 0x10 bytes of
// padding, then eight 0xC8-byte users, each a 16-byte ID, the same ID again,
// an 8-byte timestamp, a 32-byte name and 0x80 bytes of extra data. The
// folder is the ID's bytes REVERSED, in capitals: checked against two
// profiles Eden made on the A9, 2026-09-28.
//
// The name is set to whoever is playing each time, because some games show
// it. The ID never changes, so neither does the folder.
constexpr const char* kEdenProfiles =
    "/user/nand/system/save/8000000000000010/su/avators/profiles.dat";
constexpr size_t kProfilesSize = 0x650;
constexpr size_t kUserSize = 0xC8;
constexpr size_t kUserNameAt = 0x28;   // after two IDs and the timestamp
constexpr size_t kUserNameSize = 0x20;
// "CabinetOS player", which reads back from the folder name in a hex dump.
constexpr uint8_t kProfileId[16] = {'C', 'a', 'b', 'i', 'n', 'e', 't', 'O',
                                    'S', ' ', 'p', 'l', 'a', 'y', 'e', 'r'};

std::string profileFolder(const uint8_t* id) {
    static const char* hex = "0123456789ABCDEF";
    std::string out;
    for (int i = 15; i >= 0; --i) {
        out += hex[id[i] >> 4];
        out += hex[id[i] & 0xF];
    }
    return out;
}

// The first profile's folder, making the profile if there is none. Empty
// only when the file cannot be written.
std::string edenProfile(const std::string& homeDir, const std::string& player) {
    const std::string path = homeDir + kEdenProfiles;
    std::string data = readFile(path);
    if (data.size() != kProfilesSize) data.assign(kProfilesSize, '\0');

    size_t user = 0x10;
    bool found = false;
    for (size_t at = 0x10; at + kUserSize <= kProfilesSize; at += kUserSize) {
        bool zero = true;
        for (size_t i = 0; i < 16; ++i)
            if (data[at + i] != '\0') { zero = false; break; }
        if (!zero) { user = at; found = true; break; }
    }
    if (!found) {
        std::memcpy(&data[user], kProfileId, 16);
        std::memcpy(&data[user + 16], kProfileId, 16);
    }

    // The name, cut at a whole character: 32 bytes, zero-padded.
    std::string name = player.empty() ? std::string("Player") : player;
    if (name.size() > kUserNameSize) {
        size_t cut = kUserNameSize;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
        name.resize(cut);
    }
    std::string field(kUserNameSize, '\0');
    std::memcpy(&field[0], name.data(), name.size());
    const bool rename = data.compare(user + kUserNameAt, kUserNameSize, field) != 0;
    if (rename) std::memcpy(&data[user + kUserNameAt], field.data(), kUserNameSize);

    if (!found || rename) {
        storage::makeDirs(path.substr(0, path.find_last_of('/')));
        if (!writeFile(path, data)) return {};
    }
    return profileFolder(reinterpret_cast<const uint8_t*>(&data[user]));
}

// EACH PLAYER'S CONTROLS, on their virtual controller (vpad.h), laid out as a
// Pro Controller the way Eden lays out any SDL pad itself: by POSITION, so
// the button on the right of the diamond is A on every pad, as it is on a
// Switch (sdl_driver.cpp, GetDefaultButtonBinding). Players past `players` are
// unplugged, or a game would count four controllers and offer four seats.
std::vector<std::string> edenControls(int players) {
    const std::string guid = cab::vpad::edenGuid();
    std::vector<std::string> lines;   // "key=value" for [Controls]
    auto btn = [&](int p, SDL_GamepadButton b) {
        return "\"engine:sdl,port:" + std::to_string(p) + ",guid:" + guid +
               ",button:" + std::to_string(cab::vpad::buttonIndex(b)) + "\"";
    };
    auto hat = [&](int p, const char* dir) {
        return "\"engine:sdl,port:" + std::to_string(p) + ",guid:" + guid +
               ",hat:0,direction:" + dir + "\"";
    };
    auto trig = [&](int p, SDL_GamepadAxis a) {
        return "\"engine:sdl,port:" + std::to_string(p) + ",guid:" + guid +
               ",axis:" + std::to_string(cab::vpad::axisIndex(a)) +
               ",threshold:0.5,invert:+\"";
    };
    auto stick = [&](int p, SDL_GamepadAxis x, SDL_GamepadAxis y) {
        return "\"engine:sdl,port:" + std::to_string(p) + ",guid:" + guid +
               ",axis_x:" + std::to_string(cab::vpad::axisIndex(x)) +
               ",axis_y:" + std::to_string(cab::vpad::axisIndex(y)) +
               ",offset_x:0,offset_y:0,invert_x:+,invert_y:+\"";
    };
    for (int p = 0; p < 8; ++p) {
        const std::string k = "player_" + std::to_string(p) + "_";
        lines.push_back(k + "connected=" + (p < players ? "true" : "false"));
        if (p >= cab::vpad::kMaxPlayers) continue;
        lines.push_back(k + "type=0");   // Pro Controller
        lines.push_back(k + "button_a=" + btn(p, SDL_GAMEPAD_BUTTON_EAST));
        lines.push_back(k + "button_b=" + btn(p, SDL_GAMEPAD_BUTTON_SOUTH));
        lines.push_back(k + "button_x=" + btn(p, SDL_GAMEPAD_BUTTON_NORTH));
        lines.push_back(k + "button_y=" + btn(p, SDL_GAMEPAD_BUTTON_WEST));
        lines.push_back(k + "button_lstick=" + btn(p, SDL_GAMEPAD_BUTTON_LEFT_STICK));
        lines.push_back(k + "button_rstick=" + btn(p, SDL_GAMEPAD_BUTTON_RIGHT_STICK));
        lines.push_back(k + "button_l=" + btn(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        lines.push_back(k + "button_r=" + btn(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        lines.push_back(k + "button_zl=" + trig(p, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
        lines.push_back(k + "button_zr=" + trig(p, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
        lines.push_back(k + "button_plus=" + btn(p, SDL_GAMEPAD_BUTTON_START));
        lines.push_back(k + "button_minus=" + btn(p, SDL_GAMEPAD_BUTTON_BACK));
        lines.push_back(k + "button_dleft=" + hat(p, "left"));
        lines.push_back(k + "button_dup=" + hat(p, "up"));
        lines.push_back(k + "button_dright=" + hat(p, "right"));
        lines.push_back(k + "button_ddown=" + hat(p, "down"));
        lines.push_back(k + "button_slleft=" + btn(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        lines.push_back(k + "button_srleft=" + btn(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        lines.push_back(k + "button_slright=" + btn(p, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER));
        lines.push_back(k + "button_srright=" + btn(p, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER));
        lines.push_back(k + "button_home=" + btn(p, SDL_GAMEPAD_BUTTON_GUIDE));
        lines.push_back(k + "button_screenshot=\"\"");
        lines.push_back(k + "lstick=" + stick(p, SDL_GAMEPAD_AXIS_LEFTX, SDL_GAMEPAD_AXIS_LEFTY));
        lines.push_back(k + "rstick=" + stick(p, SDL_GAMEPAD_AXIS_RIGHTX, SDL_GAMEPAD_AXIS_RIGHTY));
    }
    return lines;
}

bool prepareEden(const Emulator& e, const std::string& saveDir, const std::string& player,
                 int players, quality::Level level, bool* missingKeys, std::string* err) {
    const std::string user = home(e) + "/user";
    storage::makeDirs(user + "/keys");
    storage::makeDirs(user + "/config");

    // KEYS ARE REQUIRED, FIRMWARE IS NOT (decision A). Without prod.keys Eden
    // opens its own "Install them now?" box over the game, so the console
    // refuses first and says why. title.keys is optional: most games carry
    // their own ticket.
    const std::string prod = newestKeys(storage::biosDir(), "prod");
    if (prod.empty()) {
        *missingKeys = true;
        return false;
    }
    if (!placeFile(prod, user + "/keys/prod.keys")) {
        *err = "could not place " + prod;
        return false;
    }
    std::fprintf(stderr, "[standalone] keys: %s\n", prod.c_str());
    const std::string title = newestKeys(storage::biosDir(), "title");
    if (!title.empty() && !placeFile(title, user + "/keys/title.keys"))
        std::fprintf(stderr, "[standalone] could not place %s\n", title.c_str());
    // Not fatal either way: most games need none (decision A).
    if (const std::string fw = newestFirmwareZip(storage::biosDir()); !fw.empty())
        installFirmware(home(e), fw);

    // THE CONSOLE'S SETTINGS, laid over Eden's own file before every start.
    // Every one of these is something that would otherwise put a box or a
    // choice on the television. Batocera's generator and the community Eden
    // add-on write the same ones.
    const std::string ini = user + "/config/qt-config.ini";
    std::vector<IniSet> sets = {
        {"UI", "fullscreen", "true"},
        // No welcome on the first start.
        {"UI", "firstStart", "false"},
        // 2 is Ask_Never: no "Are you sure?" when the console closes it.
        // Without this, SIGTERM opens a question instead of closing.
        {"UI", "confirmStop", "2"},
        // Compiled out of the Flathub build, and off in any other.
        {"UI", "check_for_updates", "false"},
        // "Wayland Detected!" at start.
        {"UI", "gui_hide_backend_warning", "true"},
        // A game asking for a controller layout opens Eden's own dialog.
        {"UI", "disableControllerApplet", "true"},
        // Docked, always: the television mode (decision B). 1 is Docked.
        {"System", "use_docked_mode", "1"},
        // THIS PERSON'S SAVES FOR THIS GAME, a different folder every launch,
        // so nothing of anybody else's is in reach of the game and what
        // changed while it ran is exactly what it saved.
        {"Data%20Storage", "save_directory", saveDir.c_str()},
        // Eden's own drivers for Nintendo pads open the real ones directly,
        // which the virtual controllers exist to prevent (vpad.h).
        {"Controls", "enable_joycon_driver", "false"},
        {"Controls", "enable_procon_driver", "false"},
        // NO INTERNET, as far as a game can tell. Nintendo's online services do
        // not answer an emulator, so nothing is lost. It did NOT save
        // Shredder's Revenge, whose Epic online layer quits on a call Eden
        // only stubs (bsd EventFd); nor did cutting the sandbox off the
        // network, which made it hang instead and was taken out
        // (2026-09-28).
        {"Network", "airplane_mode", "true"},
        // NO VSYNC, ITS OWN LIMITER (#209, MMagTech 2026-10-01): gamescope
        // already lines every frame up with the screen, so Eden's own wait
        // (Fifo by default) only adds one more. 0 is Immediate; the speed
        // limit, on at 100% by default, keeps the game at its own speed.
        {"Renderer", "use_vsync", "0"},
    };
    const std::vector<std::string> controls = edenControls(players);
    std::vector<std::string> keys, values;
    keys.reserve(controls.size());
    values.reserve(controls.size());
    for (const std::string& l : controls) {
        const size_t eq = l.find('=');
        keys.push_back(l.substr(0, eq));
        values.push_back(l.substr(eq + 1));
    }
    for (size_t i = 0; i < controls.size(); ++i)
        sets.push_back({"Controls", keys[i].c_str(), values[i].c_str()});
    // PICTURE QUALITY (#63): resolution, filtering and shader building.
    const std::vector<quality::Setting> picture = quality::eden(level);
    for (const quality::Setting& q : picture)
        sets.push_back({q.section.c_str(), q.key.c_str(), q.value.c_str()});
    if (!writeFile(ini, applyIni(readFile(ini), sets))) {
        *err = "could not write " + ini;
        return false;
    }
    if (edenProfile(home(e), player).empty()) {
        *err = "could not write Eden's profile";
        return false;
    }
    return true;
}

// --- RPCS3 -----------------------------------------------------------------
//
// WHERE EVERYTHING GOES, decided by MMagTech 2026-09-28 (docs/PROJECT.md, open
// question 19) and measured on the A9 the same day with the build the image
// pins:
//
//   emulators/rpcs3/rpcs3/        its settings and the PS3's system software
//                                 (dev_flash, 189 MB, once per console)
//   <the game's folder>/          the installed game (<title ID>/USRDIR/...),
//                                 its `.rap`, and rpcs3/, its compile cache
//                                 and log: removed with the game
//   <this person's save folder>/  hdd0/, the PS3's hard drive as the game
//                                 sees it: saves, the licence, trophies
//
// RPCS3 starts an installed game from OUTSIDE its hard drive by itself: it
// mounts the game's folder at /dev_hdd0/game/<title ID> (Emu/System.cpp, the
// HG category). Measured: Super Stardust HD booted from its own folder,
// decrypted with its `.rap`, and wrote its save into the save folder.

// RPCS3's one user, as it names the folder. The console's people are told
// apart by whose save folder `hdd0` is, not by PS3 users.
constexpr const char* kPs3User = "00000001";
// Where a game's folder records what was installed into it and deleted.
constexpr const char* kInstalledNote = ".cabinetos-installed";
// And where an install is built before it is moved into place.
constexpr const char* kStaging = ".install";

bool isFile(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool removeTree(const std::string& path) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) return false;
    if (!S_ISDIR(st.st_mode)) return ::unlink(path.c_str()) == 0;
    if (DIR* d = opendir(path.c_str())) {
        while (dirent* e = readdir(d)) {
            if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
            removeTree(path + "/" + e->d_name);
        }
        closedir(d);
    }
    return ::rmdir(path.c_str()) == 0;
}

std::vector<std::string> listDir(const std::string& dir) {
    std::vector<std::string> names;
    if (DIR* d = opendir(dir.c_str())) {
        while (dirent* e = readdir(d)) {
            if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
            names.push_back(e->d_name);
        }
        closedir(d);
    }
    std::sort(names.begin(), names.end());
    return names;
}

bool endsWithNoCase(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i)
        if (std::tolower(static_cast<unsigned char>(s[s.size() - n + i])) !=
            std::tolower(static_cast<unsigned char>(suffix[i])))
            return false;
    return true;
}

// A YAML scalar that means exactly this string: a path can hold ": " or
// start with something YAML reads as syntax.
std::string yamlQuote(const std::string& v) {
    std::string out = "\"";
    for (const char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

std::string rpcs3Config(const Emulator& e) { return home(e) + "/rpcs3"; }
std::string rpcs3Flash(const Emulator& e) { return rpcs3Config(e) + "/dev_flash"; }
std::string firmwareNote(const Emulator& e) { return rpcs3Flash(e) + "/.cabinetos-firmware"; }

// The PS3 system software RomM serves: `PS3UPDAT*.PUP` in `bios/`, the highest
// version in its name when there are several (4.96 over 4.93).
std::string newestPup(const std::string& biosDir) {
    std::string pick;
    long pickVersion = -1;
    for (const std::string& name : listDir(biosDir)) {
        std::string lower = name;
        for (char& ch : lower) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
        if (lower.compare(0, 8, "ps3updat") != 0 || !endsWithNoCase(lower, ".pup")) continue;
        long v = 0;
        for (const char ch : lower)
            if (std::isdigit(static_cast<unsigned char>(ch))) v = v * 10 + (ch - '0');
        if (v > pickVersion) {
            pick = biosDir + "/" + name;
            pickVersion = v;
        }
    }
    return pick;
}

// RPCS3 WITHOUT A WINDOW, to install something, and its log afterwards.
// `configHome` is a scratch folder of its own, so an install never touches the
// settings a game runs with, and the whole of it is deleted after.
//
// IT EXITS 134 AFTER A SUCCESSFUL INSTALL: an abort in a static destructor,
// after it has logged "Successfully installed" (lessons, the PS3 notes;
// measured again 2026-09-28 on the pinned build). So the log line decides,
// never the exit code. `prlimit --core=0` because every one of those aborts
// would otherwise leave a core file of a process with gigabytes mapped.
std::string rpcs3Headless(const Emulator& e, const std::string& configHome,
                          const std::vector<std::string>& what, const std::atomic<bool>& cancel) {
    std::vector<std::string> args = {
        "prlimit", "--core=0", "--",
        "env", "XDG_CONFIG_HOME=" + configHome, "XDG_CACHE_HOME=" + configHome,
        binaryOf(e), "--headless",
    };
    args.insert(args.end(), what.begin(), what.end());
    // Half an hour: 20 GB went in at 161 MB/s on the VM's slow disk; a USB
    // hard drive could be a fifth of that.
    const proc::Result r = proc::run(args, 30 * 60, &cancel);
    std::fprintf(stderr, "[standalone] rpcs3 %s: exit %d%s\n", what.front().c_str(), r.status,
                 r.timedOut ? " (stopped)" : "");
    return readFile(configHome + "/rpcs3/RPCS3.log");
}

// THE PS3'S SYSTEM SOFTWARE, once per console and again only when RomM's
// file changes, the way Eden's firmware is (installFirmware above). 1.1 s and
// 189 MB on the A9.
bool installPs3Firmware(const Emulator& e, const std::atomic<bool>& cancel,
                        std::string* message) {
    const std::string pup = newestPup(storage::biosDir());
    const std::string note = firmwareNote(e);
    const std::string had = readFile(note);
    if (pup.empty()) {
        // Nothing on the server, or no server: whatever is installed stands.
        if (!had.empty()) return true;
        *message = "No PlayStation 3 firmware on your server";
        return false;
    }
    struct stat st;
    ::stat(pup.c_str(), &st);
    const std::string stamp =
        pup.substr(pup.find_last_of('/') + 1) + " " + std::to_string(st.st_size) + "\n";
    if (had == stamp) return true;

    const std::string scratch = home(e) + "/" + kStaging;
    removeTree(scratch);
    storage::makeDirs(scratch + "/rpcs3");
    storage::makeDirs(rpcs3Flash(e));
    writeFile(scratch + "/rpcs3/vfs.yml",
              "/dev_flash/: " + yamlQuote(rpcs3Flash(e) + "/") + "\n" +
              "/dev_hdd0/: " + yamlQuote(scratch + "/hdd0/") + "\n");
    const std::string log = rpcs3Headless(e, scratch, {"--installfw", pup}, cancel);
    removeTree(scratch);
    if (log.find("Successfully installed PS3 firmware") == std::string::npos) {
        std::fprintf(stderr, "[standalone] PS3 firmware from %s did not install\n", pup.c_str());
        *message = "Couldn't install the PlayStation 3 firmware";
        return false;
    }
    writeFile(note, stamp);
    std::fprintf(stderr, "[standalone] PS3 firmware installed from %s\n", pup.c_str());
    return true;
}

// A PS3 PACKAGE, BY WHAT IT IS rather than by its name: every PKG starts
// 7F 'P' 'K' 'G'. Found 2026-09-28: Zombie Apocalypse's PKG on the reference
// server has no extension at all, and a name test would have skipped it.
bool isPkg(const std::string& path) {
    char magic[4] = {};
    std::ifstream in(path, std::ios::binary);
    return in.read(magic, 4) && std::memcmp(magic, "\x7FPKG", 4) == 0;
}

// EVERY PACKAGE IN THE GAME'S FOLDER, installed into it, largest first (the
// game before an update or add-on for it), then deleted. RPCS3 installs into
// its hard drive's game/<title ID>; that drive is pointed at a staging folder
// inside the game's own, so the result is on the same disk and moves into
// place with a rename, never a copy.
//
// A GAME ALREADY INSTALLED goes back into staging first, so a later update
// lands on top of it rather than beside it. AN INTERRUPTED INSTALL leaves its
// staging behind, and nothing else would clean it (lessons, the PS3 notes),
// so it is deleted before starting.
bool installPackages(const Emulator& e, const std::string& entryPath,
                     const std::atomic<bool>& cancel, std::string* message) {
    std::vector<std::pair<int64_t, std::string>> pkgs;
    for (const std::string& name : listDir(entryPath)) {
        struct stat st;
        const std::string p = entryPath + "/" + name;
        if (name[0] != '.' && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) && isPkg(p))
            pkgs.emplace_back(st.st_size, name);
    }
    if (pkgs.empty()) return true;
    std::sort(pkgs.begin(), pkgs.end(), [](const auto& a, const auto& b) { return a.first > b.first; });

    const std::string staging = entryPath + "/" + kStaging;
    const std::string games = staging + "/hdd0/game";
    removeTree(staging);
    storage::makeDirs(staging + "/rpcs3");
    storage::makeDirs(games);
    writeFile(staging + "/rpcs3/vfs.yml",
              "/dev_hdd0/: " + yamlQuote(staging + "/hdd0/") + "\n" +
              "/dev_flash/: " + yamlQuote(rpcs3Flash(e) + "/") + "\n");
    std::vector<std::string> hadBefore;
    for (const std::string& name : listDir(entryPath))
        if (isFile(entryPath + "/" + name + "/PARAM.SFO") &&
            ::rename((entryPath + "/" + name).c_str(), (games + "/" + name).c_str()) == 0)
            hadBefore.push_back(name);

    std::string note = readFile(entryPath + "/" + kInstalledNote);
    bool ok = true;
    for (const auto& [bytes, name] : pkgs) {
        const std::string pkg = entryPath + "/" + name;
        const std::string log = rpcs3Headless(e, staging, {"--installpkg", pkg}, cancel);
        // "Successfully installed <path> (title_id=NPEA00014, title=..." on
        // its own line (rpcs3qt/main_window.cpp).
        const size_t at = log.find("Successfully installed " + pkg);
        if (at == std::string::npos || cancel.load()) {
            std::fprintf(stderr, "[standalone] %s did not install\n", pkg.c_str());
            ok = false;
            break;
        }
        note += std::to_string(bytes) + " " + name + "\n";
        std::fprintf(stderr, "[standalone] installed %s (%lld bytes)\n", name.c_str(),
                     static_cast<long long>(bytes));
    }

    // WHAT CAME OUT, back into the game's folder: every title directory with
    // its PARAM.SFO. RPCS3 also makes TEST12345 and a lock folder on any new
    // hard drive, which are not the game. AFTER A FAILURE only what was
    // installed before goes back: a half-installed new title in the folder
    // would be found by bootPath and started, offline, as if it were whole.
    for (const std::string& name : listDir(games)) {
        const bool keep = ok || std::find(hadBefore.begin(), hadBefore.end(), name) !=
                                    hadBefore.end();
        if (keep && isFile(games + "/" + name + "/PARAM.SFO"))
            ::rename((games + "/" + name).c_str(), (entryPath + "/" + name).c_str());
    }
    removeTree(staging);
    if (!ok) {
        *message = "Couldn't install this game";
        return false;
    }
    // Only now: a PKG is deleted once the game it became is in place.
    writeFile(entryPath + "/" + kInstalledNote, note);
    for (const auto& pkg : pkgs) ::unlink((entryPath + "/" + pkg.second).c_str());
    return true;
}

// THE CONSOLE'S SETTINGS FOR RPCS3, written whole before every start: every
// one is a box, a pause or a hint that would otherwise land on the
// television. Names from Emu/system_config.h. Everything else is RPCS3's
// default, which is what RetroArch-style "it just works" means for it.
constexpr const char* kRpcs3Settings =
    "Miscellaneous:\n"
    "  Automatically start games after boot: true\n"
    "  Exit RPCS3 when process finishes: true\n"
    "  Pause emulation on RPCS3 focus loss: false\n"
    "  Start games in fullscreen mode: true\n"
    "  Prevent display sleep while running games: true\n"
    "  Show trophy popups: false\n"
    "  Show RPCN popups: false\n"
    "  Show shader compilation hint: false\n"
    "  Show PPU compilation hint: false\n"
    "  Show autosave/autoload hint: false\n"
    "  Show pressure intensity toggle hint: false\n"
    "  Show analog limiter toggle hint: false\n"
    "  Show mouse and keyboard toggle hint: false\n"
    "  Show capture hints: false\n";

// EACH PLAYER'S CONTROLS, on their virtual controller (vpad.h), found by its
// NAME: RPCS3's evdev handler matches a device by the name it reports, so
// there are no numbers to get wrong. The buttons are RPCS3's names for the
// evdev codes the virtual controller sends (Input/evdev_joystick_handler.h),
// laid out by POSITION as a DualShock 3 is.
//
// SQUARE IS "Y" AND TRIANGLE IS "X", and that is not a slip. The virtual
// controller sends the west button as BTN_WEST and the north as BTN_NORTH,
// which are the same codes Linux also calls BTN_Y and BTN_X, and RPCS3 names
// the codes by those older words. Its own default (Square "X") suits pads
// whose drivers send by label rather than position. Players past `players`
// are unplugged, or a game would see four pads.
std::string rpcs3Controls(int players) {
    std::string out;
    for (int p = 1; p <= 7; ++p) {
        out += "Player " + std::to_string(p) + " Input:\n";
        if (p > players || p > cab::vpad::kMaxPlayers) {
            out += "  Handler: \"Null\"\n  Device: \"Null\"\n";
            continue;
        }
        out += "  Handler: Evdev\n";
        out += "  Device: " + yamlQuote("CabinetOS player " + std::to_string(p)) + "\n";
        out += "  Config:\n";
        const char* map[][2] = {
            {"Left Stick Left", "LX-"},   {"Left Stick Down", "LY+"},
            {"Left Stick Right", "LX+"},  {"Left Stick Up", "LY-"},
            {"Right Stick Left", "RX-"},  {"Right Stick Down", "RY+"},
            {"Right Stick Right", "RX+"}, {"Right Stick Up", "RY-"},
            {"Start", "Start"},           {"Select", "Select"},
            {"PS Button", "Mode"},
            {"Square", "Y"},              {"Cross", "A"},
            {"Circle", "B"},              {"Triangle", "X"},
            {"Left", "Hat0 X-"},          {"Down", "Hat0 Y+"},
            {"Right", "Hat0 X+"},         {"Up", "Hat0 Y-"},
            {"R1", "TR"},                 {"R2", "RZ+"},
            {"R3", "Thumb R"},            {"L1", "TL"},
            {"L2", "LZ+"},                {"L3", "Thumb L"},
        };
        for (const auto& m : map) out += std::string("    ") + m[0] + ": " + yamlQuote(m[1]) + "\n";
    }
    return out;
}

bool prepareRpcs3(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                  const std::string& player, int players, quality::Level level, bool* missing,
                  std::string* err) {
    // The download installs it; without it RPCS3 opens its own "no firmware"
    // box over the game, so the console refuses first.
    if (readFile(firmwareNote(e)).empty()) {
        *missing = true;
        return false;
    }
    const std::string config = rpcs3Config(e);
    storage::makeDirs(config + "/GuiConfigs");
    storage::makeDirs(config + "/input_configs/global");

    // THIS PERSON'S HARD DRIVE FOR THIS GAME, as Eden's save folder is: the
    // game's saves, its licence and its trophies, and nobody else's.
    const std::string hdd0 = saveDir + "/hdd0";
    const std::string user = hdd0 + "/home/" + kPs3User;
    storage::makeDirs(user + "/savedata");
    storage::makeDirs(user + "/exdata");
    // The name some games show, cut to the PS3's sixteen bytes at a whole
    // character.
    std::string name = player.empty() ? std::string("Player") : player;
    if (name.size() > 16) {
        size_t cut = 16;
        while (cut > 0 && (static_cast<unsigned char>(name[cut]) & 0xC0) == 0x80) --cut;
        name.resize(cut);
    }
    writeFile(user + "/localusername", name);
    // THE LICENCE, where RPCS3 says it must be: exdata, lower-case `.rap`
    // (its own error, Crypto/unself.cpp). It stays in the game's folder too,
    // so a second person gets it.
    for (const std::string& f : listDir(entryPath))
        if (endsWithNoCase(f, ".rap"))
            placeFile(entryPath + "/" + f,
                      user + "/exdata/" + f.substr(0, f.size() - 4) + ".rap");

    const std::string vfs = "/dev_hdd0/: " + yamlQuote(hdd0 + "/") + "\n" +
                            "/dev_flash/: " + yamlQuote(rpcs3Flash(e) + "/") + "\n";
    const std::string gui = config + "/GuiConfigs/CurrentSettings.ini";
    const std::string gui_text = applyIni(readFile(gui), {
        // "Exit Game?" on the close the console sends, and the welcome.
        {"main_window", "confirmationBoxExitGame", "false"},
        {"main_window", "confirmationBoxBootGame", "false"},
        {"main_window", "infoBoxEnabledWelcome", "false"},
        {"main_window", "infoBoxEnabledInstallPKG", "false"},
        {"main_window", "infoBoxEnabledInstallPUP", "false"},
        {"main_window", "confirmationObsoleteCfg", "false"},
    }, /*withDefaults=*/false);
    if (!writeFile(config + "/vfs.yml", vfs) || !writeFile(config + "/config.yml",
                   std::string(kRpcs3Settings) + quality::rpcs3(level)) ||
        !writeFile(gui, gui_text) ||
        !writeFile(config + "/input_configs/global/Default.yml", rpcs3Controls(players))) {
        *err = "could not write RPCS3's settings in " + config;
        return false;
    }
    // Its list of games held outside its hard drive, one line per game it
    // has started; every game here is, so it would only ever grow.
    ::unlink((config + "/games.yml").c_str());
    return true;
}

// --- Xbox (xemu) --------------------------------------------------------------
//
// Decided with MMagTech 2026-09-29, issue #172: xemu from Flathub as it ships;
// the BIOS and MCPX boot ROM from RomM; xemu's own blank drive and one EEPROM
// for every console, both in the image; the drive in the game's folder; saves
// carried on and off it by the console (xboxhdd.h).

constexpr const char* kXboxDrive = "xbox_hdd.qcow2";

// THE DASHBOARD'S TITLE ID. xemu-dashboard's (FFFF0002, read from
// C:\xboxdash.xbe on the drive install-xemu-drive.sh pins), and Microsoft's
// (FFFE0000) should a drive ever carry that instead.
bool isXboxDashboard(uint32_t title) { return title == 0xFFFF0002 || title == 0xFFFE0000; }

// The title ID in an XBE's header, as it sits in the Xbox's memory at
// 0x10000: `XBEH`, the base address at 0x104 and the certificate's at 0x118,
// the title ID eight bytes into the certificate (xemu-xbe.h). Zero when it is
// not there yet.
uint32_t xbeTitle(const std::string& h) {
    auto le = [&](size_t at) {
        return static_cast<uint32_t>(static_cast<uint8_t>(h[at])) |
               static_cast<uint32_t>(static_cast<uint8_t>(h[at + 1])) << 8 |
               static_cast<uint32_t>(static_cast<uint8_t>(h[at + 2])) << 16 |
               static_cast<uint32_t>(static_cast<uint8_t>(h[at + 3])) << 24;
    };
    if (h.size() < 0x120 || h.compare(0, 4, "XBEH") != 0) return 0;
    const uint32_t base = le(0x104), cert = le(0x118);
    if (cert < base || cert - base + 12 > h.size()) return 0;
    return le(cert - base + 8);
}

// One QMP command and its answer: lines until one says "return" or "error"
// (events can come first). False on no answer or an error.
bool qmpCommand(int fd, const std::string& cmd) {
    const std::string line = cmd + "\n";
    if (::send(fd, line.data(), line.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(line.size()))
        return false;
    std::string got;
    char buf[4096];
    for (int reads = 0; reads < 16; ++reads) {
        const ssize_t n = ::recv(fd, buf, sizeof buf, 0);
        if (n <= 0) return false;
        got.append(buf, static_cast<size_t>(n));
        size_t nl;
        while ((nl = got.find('\n')) != std::string::npos) {
            const std::string one = got.substr(0, nl);
            got.erase(0, nl + 1);
            if (one.find("\"return\"") != std::string::npos) return true;
            if (one.find("\"error\"") != std::string::npos) return false;
        }
    }
    return false;
}
constexpr const char* kPlayingNote = ".cabinetos-playing";

// What the image carries for xemu: `xbox_hdd.qcow2`, xemu's own blank drive
// (xemu-project/xemu-dashboard, pinned by build_files/install-xemu-assets.sh),
// and `eeprom.bin`, the one EEPROM every CabinetOS console uses. For
// development, `CABINETOS_XEMU_ASSETS` points somewhere else.
std::string xemuAssets() {
    if (const char* v = std::getenv("CABINETOS_XEMU_ASSETS"); v && *v) return v;
    return "/usr/share/cabinetos/xemu";
}

// THE XBOX'S RESET CODE, which begins every MCPX boot ROM (`33 C0`, xor ax,ax)
// and ends it (`02 EE`), and which a flash image carries in its top 512 bytes
// too. Read from both files on RomM, 2026-09-29, and from xemu's docs
// (required-files.md) for the ROM. It is how both are recognised: by what is
// in them, never by a name (RomM's is `bios_retail_4627.bin`, xemu's docs say
// Complex 4627, and nothing else in `bios/` looks like this).
bool xboxResetCode(const std::string& d, size_t at) {
    return d.size() >= at + 512 && static_cast<uint8_t>(d[at]) == 0x33 &&
           static_cast<uint8_t>(d[at + 1]) == 0xC0 && static_cast<uint8_t>(d[at + 510]) == 0x02 &&
           static_cast<uint8_t>(d[at + 511]) == 0xEE;
}

std::string findXboxFile(bool bootRom) {
    const std::string bios = storage::biosDir();
    for (const std::string& name : listDir(bios)) {
        const std::string p = bios + "/" + name;
        struct stat st;
        if (::stat(p.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        const int64_t n = st.st_size;
        if (bootRom ? n != 512 : (n != 256 * 1024 && n != 512 * 1024 && n != 1024 * 1024)) continue;
        const std::string d = readFile(p);
        if (xboxResetCode(d, d.size() - 512)) return p;
    }
    return {};
}

// A TOML basic string: a game's folder can hold an apostrophe.
std::string tomlQuote(const std::string& v) {
    std::string out = "\"";
    for (const char c : v) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

bool prepareXemu(const Emulator& e, const std::string& entryPath, int players,
                 quality::Level level, bool* missing, std::string* err) {
    const std::string mcpx = findXboxFile(true);
    const std::string flash = findXboxFile(false);
    if (mcpx.empty() || flash.empty()) {
        std::fprintf(stderr, "[xbox] boot ROM %s, BIOS %s\n",
                     mcpx.empty() ? "missing" : mcpx.c_str(),
                     flash.empty() ? "missing" : flash.c_str());
        *missing = true;
        return false;
    }
    const std::string dir = home(e);
    storage::makeDirs(dir);
    // Copied into its home, where the sandbox is given a path it can read.
    // THE EEPROM IS PUT BACK EVERY TIME: some games lock their saves to its
    // key, so it must be the same on every console and never drift.
    const std::string assets = xemuAssets();
    if (!placeFile(mcpx, dir + "/mcpx.bin") || !placeFile(flash, dir + "/flash.bin") ||
        !placeFile(assets + "/eeprom.bin", dir + "/eeprom.bin")) {
        *err = "could not put xemu's BIOS and EEPROM in " + dir;
        return false;
    }
    // THE GAME'S DRIVE, from the blank one the first time.
    const std::string drive = entryPath + "/" + kXboxDrive;
    if (!isFile(drive) && !placeFile(assets + "/" + kXboxDrive, drive)) {
        *err = "could not make a drive from " + assets + "/" + kXboxDrive;
        return false;
    }
    // EACH PLAYER'S CONTROLLER, BY NAME, in the console's order, and nothing
    // else: auto-binding would hand xemu the real pads as well.
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    if (!writeFile(dir + "/gamecontrollerdb.txt", db)) {
        *err = "could not write xemu's controller list";
        return false;
    }
    // THE CONSOLE'S SETTINGS, written whole before every start; xemu writes
    // back only what differs from its defaults on exit (xemu-settings.cc), so
    // nothing it wrote last time survives this. Names from config_spec.yml.
    std::string t;
    t += "[general]\nshow_welcome = false\nskip_boot_anim = true\n";
    t += "[general.updates]\ncheck = false\n";
    t += "[sys.files]\n";
    t += "bootrom_path = " + tomlQuote(dir + "/mcpx.bin") + "\n";
    t += "flashrom_path = " + tomlQuote(dir + "/flash.bin") + "\n";
    t += "eeprom_path = " + tomlQuote(dir + "/eeprom.bin") + "\n";
    t += "hdd_path = " + tomlQuote(drive) + "\n";
    // VULKAN, not xemu's OpenGL default: the console is built for it, and PS3
    // and Switch already use it (MMagTech, 2026-09-29). On the GPU the console
    // itself chose, never llvmpipe, which also offers Vulkan (gpu.cpp).
    t += "[display]\nrenderer = \"VULKAN\"\n";
    const std::string gpu = cab::gpu::vulkan().deviceName;
    if (!gpu.empty())
        t += "[display.vulkan]\npreferred_physical_device = " + tomlQuote(gpu) + "\n";
    // PICTURE QUALITY (#63): xemu's internal resolution.
    t += "[display.quality]\n" + quality::xemu(level);
    // VSYNC STAYS ON, xemu's default, the one exception to #209's rule.
    // Measured on the A9 2026-10-02 (FlatOut, frames.py): with it off the
    // game ran at its right speed, timed by the emulated Xbox, but xemu
    // redrew the screen 631 times a second, all wasted on the graphics chip.
    // Its own limiter paces the game, not the picture.
    t += "[display.window]\nfullscreen_on_startup = true\n";
    t += "[display.ui]\nshow_menubar = false\nshow_notifications = false\nhide_cursor = true\n";
    t += "[input]\nauto_bind = false\n";
    t += "gamecontrollerdb_path = " + tomlQuote(dir + "/gamecontrollerdb.txt") + "\n";
    t += "[input.bindings]\n";
    for (int p = 0; p < 4; ++p)
        t += "port" + std::to_string(p + 1) + " = " +
             tomlQuote(p < players ? cab::vpad::sdlGuid(p) : std::string()) + "\n";
    if (!writeFile(dir + "/xemu.toml", t)) {
        *err = "could not write xemu's settings";
        return false;
    }
    return true;
}

// A FULL DISC DUMP (redump) has the game partition 387 MiB in, where xemu
// cannot boot it; an XISO has it at the start. Both carry the Xbox's volume
// descriptor, `MICROSOFT*XBOX*MEDIA`, 64 KiB into the game partition
// (extract-xiso.c). Checked on the four games on RomM, 2026-09-29: all XISO.
constexpr uint64_t kRedumpGameOffset = 0x18300000;
bool xboxMediaAt(const std::string& path, uint64_t at) {
    std::ifstream in(path, std::ios::binary);
    char m[20] = {};
    in.seekg(static_cast<std::streamoff>(at + 0x10000));
    return in.read(m, sizeof m) && std::memcmp(m, "MICROSOFT*XBOX*MEDIA", 20) == 0;
}

// Cuts a full dump down to its game partition, in place, and notes the file
// as installed at its original size so it is not fetched again.
bool trimRedump(const std::string& entryPath, const std::string& name, std::string* message) {
    const std::string path = entryPath + "/" + name;
    struct stat st;
    if (::stat(path.c_str(), &st) != 0) return false;
    const std::string cut = path + ".cut";
    std::FILE* in = std::fopen(path.c_str(), "rb");
    std::FILE* out = std::fopen(cut.c_str(), "wb");
    bool ok = in && out && fseeko(in, static_cast<off_t>(kRedumpGameOffset), SEEK_SET) == 0;
    std::vector<char> buf(4 << 20);
    while (ok) {
        const size_t n = std::fread(buf.data(), 1, buf.size(), in);
        if (n == 0) break;
        ok = std::fwrite(buf.data(), 1, n, out) == n;
    }
    if (in) std::fclose(in);
    if (out && std::fclose(out) != 0) ok = false;
    if (!ok || std::rename(cut.c_str(), path.c_str()) != 0) {
        ::unlink(cut.c_str());
        *message = "Couldn't open this game's file";
        return false;
    }
    std::string note = readFile(entryPath + "/" + kInstalledNote);
    note += std::to_string(static_cast<long long>(st.st_size)) + " " + name + "\n";
    writeFile(entryPath + "/" + kInstalledNote, note);
    std::fprintf(stderr, "[xbox] %s was a full disc dump; kept its game partition\n",
                 name.c_str());
    return true;
}

bool installXbox(const std::string& entryPath, std::string* romPath, std::string* message) {
    for (const std::string& name : listDir(entryPath)) {
        if (name[0] == '.' || name == kXboxDrive) continue;
        const std::string p = entryPath + "/" + name;
        if (!isFile(p) || xboxMediaAt(p, 0)) continue;
        if (xboxMediaAt(p, kRedumpGameOffset) && !trimRedump(entryPath, name, message))
            return false;
    }
    std::string pick;
    int64_t pickSize = -1;
    for (const std::string& name : listDir(entryPath)) {
        struct stat st;
        const std::string p = entryPath + "/" + name;
        if (name[0] == '.' || name == kXboxDrive || ::stat(p.c_str(), &st) != 0 ||
            !S_ISREG(st.st_mode) || !xboxMediaAt(p, 0))
            continue;
        if (st.st_size > pickSize) {
            pick = p;
            pickSize = st.st_size;
        }
    }
    if (pick.empty()) {
        std::fprintf(stderr, "[xbox] no Xbox disc image in %s\n", entryPath.c_str());
        *message = "Couldn't open this game's file";
        return false;
    }
    *romPath = pick;
    return true;
}

// --- Xbox 360 (Xenia Edge) --------------------------------------------------
//
// Decided with MMagTech 2026-09-29, issue #192, open question 34: Edge as it
// ships, never patched; one profile ID on every console for good
// (x360profile.h), named for the person on Home; the person's saves written
// straight into their own folder for the game, as Eden's are.
//
// WHERE THINGS ARE. Edge's storage root is its home here, shared by every
// game and person: its shader cache and the game-scratch `cache` folders,
// none of which is progress. Its CONTENT ROOT, where the profile and the
// saves live, is `<saveDir>/content`: this person's folder for this game.
// So two people and two games never share one, nothing is copied in or out
// around a game, and the zip that travels is Xenia's own layout,
// `<XUID>/...` and `0000000000000000/...`, which drops into any Xenia.
//
// WHY THE WHOLE PROFILE FOLDER TRAVELS. A game's own GPD (achievements, and
// the settings some games keep there) is rewritten empty at start unless the
// profile's dashboard GPD, FFFE07D1.gpd, lists that game (user_profile.cc);
// measured on the A9: 41,780 bytes became 35,208. Both sit in the profile
// folder, which is in the content root, so they come down together.

std::string xeniaContent(const std::string& saveDir) { return saveDir + "/content"; }

bool prepareXenia(const Emulator& e, const std::string& saveDir, std::string* err) {
    const std::string dir = home(e);
    storage::makeDirs(dir);
    storage::makeDirs(xeniaContent(saveDir));
    // EACH PLAYER'S CONTROLLER, BY NAME, as for xemu: Edge binds slots to
    // these SDL GUIDs (`--slot_bindings_passthrough`) and never sees a real
    // pad (Run::start).
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    if (!writeFile(dir + "/gamecontrollerdb.txt", db)) {
        *err = "could not write Xenia's controller list";
        return false;
    }
    // THE CONSOLE'S SETTINGS ARE ALL ON THE COMMAND LINE (Run::start), so the
    // settings file Edge writes back is thrown away before every start and
    // nothing it kept from last time can differ.
    ::unlink((dir + "/xenia-edge.config.toml").c_str());
    return true;
}

// --- Wii U (Cemu) -------------------------------------------------------------
//
// Decided with MMagTech 2026-10-01, issue #174, open question 36: Cemu main
// at a pin, never patched; no graphic packs (#207); one account ID for
// everyone, `80000001`, which Cemu makes by itself when there is none; the
// person's saves written straight into their own folder for the game.
//
// WHERE THINGS ARE. Its home holds its settings and controller profiles
// (XDG_CONFIG_HOME), its log (XDG_DATA_HOME) and its shader cache
// (XDG_CACHE_HOME), so the cache is one per console, as Dolphin's is. Its
// mlc, the Wii U's own storage where the saves and the account are, is
// `<saveDir>/mlc`: this person's folder for this game. What travels is
// `mlc/usr/save/00050000/`, Cemu's own layout, which drops into any Cemu.

std::string cemuMlc(const std::string& saveDir) { return saveDir + "/mlc"; }

std::string xmlEscape(const std::string& v) {
    std::string out;
    for (const char c : v) {
        switch (c) {
            case '&': out += "&amp;"; break;
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '"': out += "&quot;"; break;
            default: out += c;
        }
    }
    return out;
}

// Cemu's numbers for a Pro Controller's inputs (input/emulated/ProController.h,
// ButtonId, from 1) and for an SDL gamepad's (input/api/Controller.h: SDL's
// button numbers, then ZL 32, ZR 33, the d-pad 34 to 37, and the axes from 38:
// left stick X+ Y+, right X+ Y+, triggers L R, then the same negative). By
// position, as Switch is here: Nintendo's A is the pad's right face button.
// The Pro Controller's HOME is left out: Guide is the console's pause menu.
// The same table as Batocera's.
constexpr int kCemuPro[][2] = {
    {1, SDL_GAMEPAD_BUTTON_EAST},          {2, SDL_GAMEPAD_BUTTON_SOUTH},
    {3, SDL_GAMEPAD_BUTTON_NORTH},         {4, SDL_GAMEPAD_BUTTON_WEST},
    {5, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER}, {6, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER},
    {7, 42},                               {8, 43},
    {9, SDL_GAMEPAD_BUTTON_START},         {10, SDL_GAMEPAD_BUTTON_BACK},
    {12, SDL_GAMEPAD_BUTTON_DPAD_UP},      {13, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {14, SDL_GAMEPAD_BUTTON_DPAD_LEFT},    {15, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {16, SDL_GAMEPAD_BUTTON_LEFT_STICK},   {17, SDL_GAMEPAD_BUTTON_RIGHT_STICK},
    {18, 45}, {19, 39}, {20, 44}, {21, 38},   // left stick up, down, left, right
    {22, 47}, {23, 41}, {24, 46}, {25, 40},   // right stick
};

std::string cemuProfile(int player) {
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<emulated_controller>\n";
    x += "  <type>Wii U Pro Controller</type>\n  <controller>\n";
    x += "    <api>SDLController</api>\n";
    // `<n>_<GUID>`: the n-th pad with that GUID. Each player's virtual
    // controller has a GUID of its own (vpad.h), so always the first.
    x += "    <uuid>0_" + cab::vpad::sdlGuid(player) + "</uuid>\n";
    x += "    <display_name>CabinetOS player " + std::to_string(player + 1) + "</display_name>\n";
    x += "    <rumble>1</rumble>\n";
    for (const char* a : {"axis", "rotation", "trigger"})
        x += std::string("    <") + a + "><deadzone>0.25</deadzone><range>1</range></" + a + ">\n";
    x += "    <mappings>\n";
    for (const auto& m : kCemuPro)
        x += "      <entry><mapping>" + std::to_string(m[0]) + "</mapping><button>" +
             std::to_string(m[1]) + "</button></entry>\n";
    x += "    </mappings>\n  </controller>\n</emulated_controller>\n";
    return x;
}

bool prepareCemu(const Emulator& e, const std::string& saveDir, int players, std::string* err) {
    const std::string config = home(e) + "/Cemu";
    storage::makeDirs(config + "/controllerProfiles");
    storage::makeDirs(cemuMlc(saveDir));
    // THE CONSOLE'S SETTINGS, written whole before every start; Cemu writes
    // the file back on exit, so nothing it kept can differ. Without the file
    // Cemu opens its first-run wizard. Names from config/CemuConfig.cpp and
    // gui/wxgui/wxCemuConfig.cpp at the pin.
    std::string x = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<content>\n";
    x += "  <mlc_path>" + xmlEscape(cemuMlc(saveDir)) + "</mlc_path>\n";
    // English, as the console's own words are.
    x += "  <console_language>1</console_language>\n";
    x += "  <check_update>false</check_update>\n";
    x += "  <use_discord_presence>false</use_discord_presence>\n";
    x += "  <fullscreen>true</fullscreen>\n";
    x += "  <fullscreen_menubar>false</fullscreen_menubar>\n";
    // "Already shown": its Vulkan notice and its offer to download graphic
    // packs, which the console never takes (#207).
    x += "  <vk_warning>true</vk_warning>\n";
    x += "  <gp_download>true</gp_download>\n";
    x += "  <open_pad>false</open_pad>\n";
    x += "  <Graphic>\n";
    // VULKAN on the GPU the console chose, never llvmpipe (gpu.cpp).
    x += "    <api>1</api>\n";
    const std::string uuid = cab::gpu::vulkan().deviceUuid;
    if (uuid.size() == 32) x += "    <vkDevice>" + uuid + "</vkDevice>\n";
    // NO VSYNC (#209). 1 was copied from Batocera, where the emulator
    // presents straight to the screen; here gamescope already lines frames
    // up with it. Cemu's own timer at 60 x 1.002 Hz keeps the game at speed
    // whatever the present mode (LatteTiming.cpp). Never 3, "match display":
    // on Linux release builds its thread is an empty stub.
    x += "    <VSync>0</VSync>\n";
    x += "    <AsyncCompile>true</AsyncCompile>\n";
    x += "    <Overlay><FPS>false</FPS><DrawCalls>false</DrawCalls><CPUUsage>false</CPUUsage>"
         "<CPUPerCoreUsage>false</CPUPerCoreUsage><RAMUsage>false</RAMUsage>"
         "<VRAMUsage>false</VRAMUsage></Overlay>\n";
    x += "    <Notification><ControllerProfiles>false</ControllerProfiles>"
         "<ControllerBattery>false</ControllerBattery><ShaderCompiling>false</ShaderCompiling>"
         "<FriendService>false</FriendService></Notification>\n";
    x += "  </Graphic>\n";
    // SOUND THROUGH CUBEB TO THE SYSTEM'S OUTPUT. An empty TVDevice is no TV
    // sound at all (audio/IAudioAPI.cpp, CreateDeviceFromConfig), which the
    // log does not mention; `default` is Cemu's own "Default Device". Its
    // volume defaults to 20.
    x += "  <Audio>\n    <api>3</api>\n    <TVChannels>1</TVChannels>\n";
    x += "    <TVVolume>100</TVVolume>\n    <TVDevice>default</TVDevice>\n  </Audio>\n";
    // ITS KEYBOARD SHORTCUTS OFF: Escape leaves fullscreen, F11 and Alt+Enter
    // toggle it, F12 saves a screenshot, all on by default, and a keyboard
    // plugged into the console would reach them. `0 -1` is no key and no
    // controller button (wxCemuConfig.h, sHotkeyCfg).
    x += "  <Hotkeys>\n";
    for (const char* k : {"ExitFullscreen", "ToggleFullscreen", "ToggleFullscreenAlt",
                          "TakeScreenshot", "ToggleFastForward", "ExitApplication"})
        x += std::string("    <") + k + ">0 -1</" + k + ">\n";
    x += "  </Hotkeys>\n";
    x += "</content>\n";
    if (!writeFile(config + "/settings.xml", x)) {
        *err = "could not write Cemu's settings in " + config;
        return false;
    }
    // EACH PLAYER A PRO CONTROLLER, on their own virtual controller, in the
    // console's order; and no profile for a seat nobody is in.
    for (int p = 0; p < 8; ++p) {
        const std::string file = config + "/controllerProfiles/controller" + std::to_string(p) + ".xml";
        if (p >= players) {
            ::unlink(file.c_str());
        } else if (!writeFile(file, cemuProfile(p))) {
            *err = "could not write Cemu's controller profile " + file;
            return false;
        }
    }
    return true;
}

}  // namespace

const Emulator* find(const std::string& core) {
    for (const Emulator& e : kEmulators)
        if (core == e.core) return &e;
    return nullptr;
}

std::vector<const Emulator*> all() {
    std::vector<const Emulator*> out;
    for (const Emulator& e : kEmulators) out.push_back(&e);
    return out;
}

bool installed(const Emulator& e) {
    if (e.binary) return ::access(binaryOf(e).c_str(), X_OK) == 0;
    // The system installation, where cabinetos-flatpak-setup puts it.
    return isDir(std::string("/var/lib/flatpak/app/") + e.flatpak + "/current/active/files");
}

std::string home(const Emulator& e) { return storage::emulatorsDir() + "/" + e.core; }

bool installedFile(const std::string& entryPath, const std::string& fileName,
                   int64_t sizeBytes) {
    const std::string line = std::to_string(sizeBytes) + " " + fileName;
    std::istringstream in(readFile(entryPath + "/" + kInstalledNote));
    for (std::string l; std::getline(in, l);)
        if (l == line) return true;
    return false;
}

std::string bootPath(const std::string& entryPath) {
    // An installed game is a title directory with USRDIR/EBOOT.BIN; anything
    // else in the folder is the emulator's (rpcs3/) or the console's (dot).
    for (const std::string& name : listDir(entryPath)) {
        if (name[0] == '.') continue;
        const std::string eboot = entryPath + "/" + name + "/USRDIR/EBOOT.BIN";
        if (isFile(eboot)) return eboot;
    }
    // A stamped decrypted disc image, which RPCS3 opens as it is (open
    // question 19). The largest, should there ever be more than one.
    std::string pick;
    int64_t pickSize = -1;
    for (const std::string& name : listDir(entryPath)) {
        struct stat st;
        const std::string p = entryPath + "/" + name;
        if (endsWithNoCase(name, ".iso") && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
            st.st_size > pickSize) {
            pick = p;
            pickSize = st.st_size;
        }
    }
    return pick;
}

bool installGame(const Emulator& e, const std::string& entryPath,
                 const std::atomic<bool>& cancel, std::string* romPath,
                 std::string* message) {
    if (std::strcmp(e.core, "xemu") == 0) return installXbox(entryPath, romPath, message);
    if (std::strcmp(e.core, "rpcs3") != 0) {
        *message = "Couldn't install this game";
        return false;
    }
    if (!installPs3Firmware(e, cancel, message)) return false;
    if (!installPackages(e, entryPath, cancel, message)) return false;
    *romPath = bootPath(entryPath);
    if (romPath->empty()) {
        std::fprintf(stderr, "[standalone] nothing to start in %s\n", entryPath.c_str());
        *message = "Couldn't open this game's file";
        return false;
    }
    return true;
}

bool prepare(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
             const std::string& player, int players, quality::Level level, bool* missingKeys,
             std::string* err) {
    *missingKeys = false;
    storage::makeDirs(saveDir);
    if (std::strcmp(e.core, "eden") == 0)
        return prepareEden(e, saveDir, player, players, level, missingKeys, err);
    if (std::strcmp(e.core, "rpcs3") == 0)
        return prepareRpcs3(e, entryPath, saveDir, player, players, level, missingKeys, err);
    if (std::strcmp(e.core, "xemu") == 0)
        return prepareXemu(e, entryPath, players, level, missingKeys, err);
    if (std::strcmp(e.core, "xenia") == 0) return prepareXenia(e, saveDir, err);
    if (std::strcmp(e.core, "cemu") == 0) return prepareCemu(e, saveDir, players, err);
    *err = std::string("nothing prepares ") + e.core;
    return false;
}

std::string saveRoot(const Emulator& e, const std::string& saveDir) {
    // Xbox: a copy of the drive's E: as far as saves go, `UDATA/<title ID>/`
    // and `TDATA/<title ID>/`, so the zip drops straight onto any xemu drive.
    if (std::strcmp(e.core, "xemu") == 0) return saveDir + "/E";
    // Xbox 360: Edge's content root, `<XUID>/` and `0000000000000000/`.
    if (std::strcmp(e.core, "xenia") == 0) return xeniaContent(saveDir);
    // Wii U: the games' save folders in this person's mlc, one per title,
    // `<title low>/user/` and `<title low>/meta/`.
    if (std::strcmp(e.core, "cemu") == 0) return cemuMlc(saveDir) + "/usr/save/00050000";
    if (std::strcmp(e.core, "rpcs3") == 0)
        return saveDir + "/hdd0/home/" + kPs3User + "/savedata";
    if (std::strcmp(e.core, "eden") == 0) {
        const std::string data = readFile(home(e) + kEdenProfiles);
        for (size_t at = 0x10; data.size() == kProfilesSize && at + kUserSize <= kProfilesSize;
             at += kUserSize) {
            bool zero = true;
            for (size_t i = 0; i < 16; ++i)
                if (data[at + i] != '\0') { zero = false; break; }
            if (!zero)
                return saveDir + "/user/save/0000000000000000/" +
                       profileFolder(reinterpret_cast<const uint8_t*>(&data[at]));
        }
    }
    return {};
}

uint32_t Run::pollXboxTitle() {
    const std::string dir = home(*emu_);
    if (qmpFd_ < 0) {
        const std::string path = dir + "/qmp.sock";
        const int fd = ::socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_un addr{};
        addr.sun_family = AF_UNIX;
        if (fd < 0 || path.size() >= sizeof addr.sun_path) {
            if (fd >= 0) ::close(fd);
            return 0;
        }
        std::memcpy(addr.sun_path, path.c_str(), path.size() + 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof addr) != 0) {
            ::close(fd);
            return 0;   // not listening yet
        }
        timeval tv{0, 300 * 1000};
        ::setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        qmpFd_ = fd;
        if (!qmpCommand(qmpFd_, "{\"execute\":\"qmp_capabilities\"}")) {
            ::close(qmpFd_);
            qmpFd_ = -1;
            return 0;
        }
    }
    const std::string dump = dir + "/xbe-header.bin";
    ::unlink(dump.c_str());
    if (!qmpCommand(qmpFd_, "{\"execute\":\"memsave\",\"arguments\":{\"val\":65536,"
                            "\"size\":4096,\"filename\":" + yamlQuote(dump) + "}}"))
        return 0;
    return xbeTitle(readFile(dump));
}

bool beforeStart(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                 const std::string& player, const std::string& note, std::string* err) {
    // The note first, in the person's folder: from here until the save is
    // zipped, the emulator writes there and nothing has been sent.
    if (e.notesPlaying && !writeFile(saveDir + "/" + kPlayingNote, note)) {
        *err = "could not mark " + saveDir + " as playing";
        return false;
    }
    if (std::strcmp(e.core, "xenia") == 0) {
        // AFTER the save came down, which may hold a profile file of its own
        // from another console or an older name: this one is who is here.
        const std::string tag = x360profile::gamertag(player);
        if (!x360profile::writeAccount(x360profile::accountPath(xeniaContent(saveDir)), tag,
                                       err)) {
            ::unlink((saveDir + "/" + kPlayingNote).c_str());
            return false;
        }
        std::fprintf(stderr, "[x360] signed in as %s (%s)\n", tag.c_str(), x360profile::kXuid);
        return true;
    }
    if (std::strcmp(e.core, "xemu") != 0) return true;
    // The note first: from here until afterEnd, this folder holds somebody's
    // saves that are nowhere else once the game writes to the drive.
    if (!writeFile(entryPath + "/" + kPlayingNote, note)) {
        *err = "could not mark " + entryPath + " as playing";
        return false;
    }
    const std::string drive = entryPath + "/" + kXboxDrive;
    std::string why;
    if (!xboxhdd::writeSaves(drive, saveRoot(e, saveDir), &why)) {
        // A drive this cannot write is thrown away for a blank one: it holds
        // nothing the save zip does not.
        std::fprintf(stderr, "[xbox] %s: %s; starting from a blank drive\n", drive.c_str(),
                     why.c_str());
        ::unlink(drive.c_str());
        if (!placeFile(xemuAssets() + "/" + kXboxDrive, drive) ||
            !xboxhdd::writeSaves(drive, saveRoot(e, saveDir), &why)) {
            *err = "could not put the saves on " + drive + ": " + why;
            ::unlink((entryPath + "/" + kPlayingNote).c_str());
            return false;
        }
    }
    return true;
}

bool afterEnd(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
              std::string* err) {
    if (std::strcmp(e.core, "xemu") != 0) return true;
    const std::string drive = entryPath + "/" + kXboxDrive;
    if (!xboxhdd::readSaves(drive, saveRoot(e, saveDir), err)) {
        // The note stays: the saves are still only on the drive.
        return false;
    }
    ::unlink((entryPath + "/" + kPlayingNote).c_str());
    return true;
}

void finished(const Emulator& e, const std::string& saveDir) {
    if (e.notesPlaying) ::unlink((saveDir + "/" + kPlayingNote).c_str());
}

std::string playingNote(const std::string& entryPath) {
    return readFile(entryPath + "/" + kPlayingNote);
}

bool hasPlayingNote(const std::string& entryPath) {
    return isFile(entryPath + "/" + kPlayingNote);
}

bool Run::start(const Emulator& e, const std::string& romPath, const std::string& entryPath,
                const std::string& saveDir, quality::Level level, std::string* err) {
    emu_ = &e;
    ended_ = End::None;
    program_ = -1;
    stopAtMs_ = 0;
    failedLoad_ = false;
    oldKeys_ = false;
    noLicence_ = false;
    froze_ = false;
    frozen_ = false;
    sawGameTitle_ = false;
    titleCheckMs_ = 0;
    errorCheckMs_ = 0;
    logCheckMs_ = 0;
    if (qmpFd_ >= 0) ::close(qmpFd_);
    qmpFd_ = -1;
    qmpCheckMs_ = 0;
    xboxTitle_ = 0;
    const std::string dir = home(e);
    // A fresh log each start, so a failure from the last game is not read as
    // this one's.
    const std::string out = storage::logsDir() + "/" + e.core + ".log";
    logPath_ = e.log ? (e.cacheInGame ? entryPath : dir) + "/" + e.log : out;
    storage::makeDirs(logPath_.substr(0, logPath_.find_last_of('/')));
    ::unlink(logPath_.c_str());
    logRead_ = 0;

    // THE SANDBOX SEES THREE THINGS OF OURS: the emulator's own home, this
    // person's save folder for this game, and the folder the game is in,
    // read-only. Nothing else of the console's and nobody else's saves. The
    // game may be on an external drive, which is why its folder is named per
    // launch rather than granted once.
    std::string gameDir = romPath;
    if (const size_t slash = gameDir.find_last_of('/'); slash != std::string::npos)
        gameDir.erase(slash);
    storage::makeDirs(storage::logsDir());

    // A PROGRAM IN THE IMAGE has no sandbox and no `flatpak run` to carry its
    // settings, so `env` does, and then becomes it: the pid started is the
    // emulator's own. Its settings are in its home (XDG_CONFIG_HOME); what it
    // builds per game goes in the game's folder (XDG_CACHE_HOME, when
    // `cacheInGame`), which is how a removed game takes its cache with it.
    if (e.binary) {
        // THE GRAPHICS DRIVER'S MEMORY STAYS SHARED, where the console's own
        // is, even though RPCS3's cache moves into the game's folder: Mesa
        // would otherwise follow XDG_CACHE_HOME there too. The first thing
        // RPCS3 does on every start is build its shader interpreter's
        // pipelines, which are the same for every game: 3 min 25 s on the
        // A9 the first time (Super Stardust HD, 2026-09-28), all 24 threads.
        // Shared, that is paid once per console (and again when an update
        // brings a new Mesa), not once per game. Mesa caps the folder (1 GB,
        // its default) and ages out the oldest itself. MMagTech, 2026-09-28.
        const char* xdgCache = std::getenv("XDG_CACHE_HOME");
        const char* userHome = std::getenv("HOME");
        const std::string mesaCache = xdgCache && *xdgCache
                                          ? std::string(xdgCache)
                                          : std::string(userHome ? userHome : "") + "/.cache";
        const std::vector<std::string> args = {
            "env",
            "XDG_CONFIG_HOME=" + dir,
            "XDG_CACHE_HOME=" + (e.cacheInGame ? entryPath : dir),
            // Mesa adds mesa_shader_cache/ itself (util/disk_cache_os.c).
            "MESA_SHADER_CACHE_DIR=" + mesaCache,
            // gamescope's Xwayland, as for Eden below.
            "QT_QPA_PLATFORM=xcb",
            "SDL_JOYSTICK_HIDAPI=0",
            binaryOf(e),
        };
        std::vector<std::string> full = args;
        if (std::strcmp(e.core, "xenia") == 0) {
            // ONLY THE VIRTUAL CONTROLLERS, as for xemu: Edge's menus read
            // what is bound to a slot, and its slots auto-bind whatever SDL
            // shows it. SDL's own hint, no patch. GTK on gamescope's Xwayland.
            full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0");
            full.insert(full.end() - 1, "GDK_BACKEND=x11");
            std::string slots;
            for (int p = 0; p < 4; ++p) {
                if (p) slots += ';';
                if (p < cab::vpad::count()) slots += cab::vpad::sdlGuid(p);
            }
            const int gpu = cab::gpu::vulkan().deviceIndex;
            // The settings the A9 measurement ran with (Batocera's), the
            // integration's own, and nothing else. Names are Edge's cvars.
            for (const std::string& a : std::vector<std::string>{
                     "--storage_root=" + dir,
                     "--content_root=" + xeniaContent(saveDir),
                     "--log_file=" + logPath_,
                     "--log_to_stdout=false",
                     "--logged_profile_slot_0_xuid=" + std::string(x360profile::kXuid),
                     "--fullscreen=true",
                     // Vulkan on the GPU the console chose, never llvmpipe.
                     "--gpu=vulkan",
                     "--vulkan_device=" + std::to_string(gpu),
                     "--discord=false",
                     // Guide is the console's pause menu; no mouse menu.
                     "--guide_button=false",
                     "--disable_game_window_mouse=true",
                     // Arcade games as the full game, not the trial.
                     "--license_mask=1",
                     "--mappings_file=" + dir + "/gamecontrollerdb.txt",
                     "--slot_bindings_passthrough=" + slots,
                     "--render_target_path=performance",
                     "--occlusion_query=fast",
                     "--async_shader_compilation=true",
                     "--mount_scratch=true",
                     "--protect_zero=false",
                 })
                full.push_back(a);
            // PICTURE QUALITY (#63), and the game last.
            for (const std::string& a : quality::xenia(level)) full.push_back(a);
            full.push_back(romPath);
        } else if (std::strcmp(e.core, "cemu") == 0) {
            // ITS LOG under its home (XDG_DATA_HOME/Cemu); its settings and
            // shader cache are there already, by the two above. ONLY THE
            // VIRTUAL CONTROLLERS, as for xemu and Edge, with SDL told what
            // each one is: Cemu (SDL 3) asks for gamepads, and its profiles
            // bind them by GUID (prepareCemu). GTK on gamescope's Xwayland.
            std::string db;
            for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
            full.insert(full.end() - 1, "XDG_DATA_HOME=" + dir);
            full.insert(full.end() - 1, "SDL_GAMECONTROLLERCONFIG=" + db);
            full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0");
            full.insert(full.end() - 1, "GDK_BACKEND=x11");
            // `-g` starts the game with no game list and closes Cemu when it
            // ends; `-f` is fullscreen.
            full.insert(full.end(), {"-g", romPath, "-f"});
        } else {
            full.insert(full.end(), {"--no-gui", "--fullscreen", romPath});
        }
        root_ = proc::spawn(full, out);
        if (root_ <= 0) {
            *err = "could not start " + binaryOf(e);
            root_ = -1;
            return false;
        }
        program_ = root_;
        startMs_ = nowMs();
        std::fprintf(stderr, "[standalone] started %s (pid %d) on %s\n", e.program, root_,
                     romPath.c_str());
        return true;
    }

    // XBOX. Its home is its XDG data folder, where xemu keeps anything it
    // writes by itself; the game's folder is writable because the drive is
    // in it (and a disc on a read-only mount cannot take qemu's lock: xemu
    // #2958). X11 only: the Flatpak asks SDL for Wayland first.
    if (std::strcmp(e.core, "xemu") == 0) {
        const char* xdgCache = std::getenv("XDG_CACHE_HOME");
        const char* userHome = std::getenv("HOME");
        const std::string mesaCache = (xdgCache && *xdgCache
                                           ? std::string(xdgCache)
                                           : std::string(userHome ? userHome : "") + "/.cache");
        storage::makeDirs(mesaCache + "/mesa_shader_cache");
        const std::vector<std::string> args = {
            "flatpak", "run",
            "--cwd=" + dir,
            "--filesystem=" + dir,
            "--filesystem=" + gameDir,
            // THE DRIVER'S SHADER MEMORY IS SHARED with the console's, as
            // RPCS3's is (see below), so its first-run wait is paid once.
            "--filesystem=" + mesaCache + "/mesa_shader_cache",
            "--env=MESA_SHADER_CACHE_DIR=" + mesaCache,
            "--env=XDG_DATA_HOME=" + dir,
            "--env=SDL_VIDEODRIVER=x11",
            "--nosocket=wayland",
            "--env=SDL_JOYSTICK_HIDAPI=0",
            // ONLY THE VIRTUAL CONTROLLERS, not the real pads too. xemu's own
            // menu reads every gamepad SDL shows it, bound to a port or not
            // (ui/xui/input-manager.cc), so a real pad's Guide, or Back and
            // Start together, opened it over the game (FlatOut, 2026-09-29).
            // The ID is vpad.cpp's; SDL's own hint, no patch.
            "--env=SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=0x1209/0xCAB0",
            "--env=LC_NUMERIC=C",
            std::string("--command=") + e.program,
            e.flatpak,
            "-config_path", dir + "/xemu.toml",
            "-dvd_path", romPath,
            // qemu's control socket, for the dashboard watch.
            "-qmp", "unix:" + dir + "/qmp.sock,server=on,wait=off",
        };
        ::unlink((dir + "/qmp.sock").c_str());
        root_ = proc::spawn(args, out);
        if (root_ <= 0) {
            *err = "could not start flatpak";
            root_ = -1;
            return false;
        }
        startMs_ = nowMs();
        std::fprintf(stderr, "[standalone] started %s (pid %d) on %s\n", e.flatpak, root_,
                     romPath.c_str());
        return true;
    }

    const std::vector<std::string> args = {
        "flatpak", "run",
        "--cwd=" + dir,
        "--filesystem=" + dir,
        "--filesystem=" + saveDir,
        "--filesystem=" + gameDir + ":ro",
        // gamescope's Xwayland. Eden also offers Wayland, and there it puts up
        // a warning box first; the console runs on X11 anyway (lessons,
        // "THE CONSOLE RUNS ON X11").
        "--env=QT_QPA_PLATFORM=xcb",
        // Its SDL keeps its hands off the real pads' HID devices. It is given
        // virtual controllers instead (vpad.h), and two programs driving one
        // Switch or PlayStation pad's HID protocol at once fight over it.
        "--env=SDL_JOYSTICK_HIDAPI=0",
        std::string("--command=") + e.program,
        e.flatpak,
        "-f", "-g", romPath,
    };
    root_ = proc::spawn(args, out);
    if (root_ <= 0) {
        *err = "could not start flatpak";
        root_ = -1;
        return false;
    }
    startMs_ = nowMs();
    std::fprintf(stderr, "[standalone] started %s (pid %d) on %s\n", e.flatpak, root_,
                 romPath.c_str());
    return true;
}

void Run::freeze() {
    if (!active() || frozen_ || stopping()) return;
    if (program_ <= 0) program_ = proc::findDescendant(root_, emu_->program);
    if (program_ <= 0) return;
    ::kill(program_, SIGSTOP);
    frozen_ = true;
    std::fprintf(stderr, "[standalone] %s frozen\n", emu_->program);
}

void Run::thaw() {
    if (!frozen_) return;
    if (program_ > 0) ::kill(program_, SIGCONT);
    frozen_ = false;
    std::fprintf(stderr, "[standalone] %s thawed\n", emu_->program);
}

void Run::stop() {
    if (!active() || stopping()) return;
    thaw();
    stopAtMs_ = nowMs();
    if (program_ <= 0) program_ = proc::findDescendant(root_, emu_->program);
    if (program_ > 0 && emu_->closesByWindow && gTitles.closeWindowsOf(program_) > 0) {
        std::fprintf(stderr, "[standalone] asked %s's window (pid %d) to close\n",
                     emu_->program, program_);
    } else if (program_ > 0) {
        ::kill(program_, SIGTERM);
        std::fprintf(stderr, "[standalone] asked %s (pid %d) to close\n", emu_->program,
                     program_);
    } else {
        // Not up yet, so there is nothing that would close politely.
        std::fprintf(stderr, "[standalone] %s is not up yet; stopping the sandbox\n",
                     emu_->program);
        kill9();
    }
}

void Run::kill9() {
    if (program_ <= 0) program_ = proc::findDescendant(root_, emu_->program);
    if (program_ > 0) ::kill(program_, SIGKILL);
    ::kill(root_, SIGKILL);
    // And whatever it started: RPCS3 shows a fatal error as a second RPCS3
    // (`--error`), in the session spawn gave it, which would otherwise stay
    // on the television.
    if (emu_->binary) ::kill(-root_, SIGKILL);
}

bool Run::poll() {
    if (!active()) return false;

    int status = 0;
    const pid_t got = waitpid(root_, &status, WNOHANG);
    if (got == root_) {
        const int64_t ran = nowMs() - startMs_;
        // Anything it left behind, its error box included (kill9).
        if (emu_->binary) ::kill(-root_, SIGKILL);
        if (failedLoad_ && oldKeys_) {
            ended_ = End::KeysTooOld;
        } else if (failedLoad_ && noLicence_) {
            ended_ = End::NoLicence;
        } else if (froze_) {
            ended_ = End::Crashed;
        } else if (failedLoad_) {
            ended_ = End::CouldNotLoad;
        } else if (stopping()) {
            ended_ = End::Asked;
        } else if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
            ended_ = End::Quit;
        } else {
            ended_ = End::Crashed;
        }
        std::fprintf(stderr, "[standalone] %s ended after %.1f s (%s %d)\n", emu_->program,
                     ran / 1000.0, WIFEXITED(status) ? "exit" : "signal",
                     WIFEXITED(status) ? WEXITSTATUS(status) : WTERMSIG(status));
        root_ = -1;
        program_ = -1;
        stopAtMs_ = 0;
        frozen_ = false;
        gTitles.close();
        if (qmpFd_ >= 0) ::close(qmpFd_);
        qmpFd_ = -1;
        return false;
    }

    const int64_t now = nowMs();
    if (program_ <= 0 && now - startMs_ > 200) program_ = proc::findDescendant(root_, emu_->program);
    const bool watchLoad = !failedLoad_ && now - startMs_ < kLoadWatchMs;

    if (stopping() && now - stopAtMs_ > kStopGraceMs) {
        std::fprintf(stderr, "[standalone] %s did not close in %lld s; forcing it\n",
                     emu_->program, static_cast<long long>(kStopGraceMs / 1000));
        kill9();
        stopAtMs_ = now;   // and again after another grace, should even that not take
    }

    // A LOAD THAT FAILED is an error box nobody can close, so the emulator's
    // log is read, as it grows, for its words for that in the first minute,
    // and the console closes it instead. Errors are written at once, unlike
    // the rest of the log.
    //
    // AND ONE THAT FROZE, for as long as the game runs (`froze`): RPCS3 holds
    // the last picture with its window up and waits for somebody to close it,
    // which is a crash to the person holding the controller. Read twice a
    // second, only what was added since the last read.
    const bool watchFreeze = emu_->froze && !froze_ && now - logCheckMs_ >= 500;
    if (!stopping() && (watchLoad || watchFreeze)) {
        logCheckMs_ = now;
        if (FILE* f = std::fopen(logPath_.c_str(), "r")) {
            std::fseek(f, logRead_, SEEK_SET);
            char line[1024];
            while (std::fgets(line, sizeof line, f)) {
                if (watchLoad && ((emu_->loadFailed[0] && std::strstr(line, emu_->loadFailed[0])) ||
                                  (emu_->loadFailed[1] && std::strstr(line, emu_->loadFailed[1])))) {
                    std::fprintf(stderr, "[standalone] %s could not load the game: %s",
                                 emu_->program, line);
                    failedLoad_ = true;
                    for (const char* k : emu_->keysTooOld)
                        if (k && std::strstr(line, k)) oldKeys_ = true;
                    if (emu_->noLicence && std::strstr(line, emu_->noLicence)) noLicence_ = true;
                    break;
                }
                if (emu_->froze && std::strstr(line, emu_->froze)) {
                    std::fprintf(stderr, "[standalone] %s froze: %s", emu_->program, line);
                    froze_ = true;
                    break;
                }
            }
            logRead_ = std::ftell(f);
            std::fclose(f);
        }
        // Not asked to close: its error box is modal and holds the close
        // back (measured on Contra, which waited out the whole grace), and a
        // game that never started has nothing to write.
        if (failedLoad_ || froze_) {
            stopAtMs_ = now;
            kill9();
        }
    }

    // ITS OWN ERROR BOX for a game it could not open (`errorBox`), which only
    // some of its failures put in the log. Twice a second, in the same first
    // minute.
    if (!stopping() && watchLoad && emu_->errorBox && program_ > 0 &&
        now - errorCheckMs_ >= 500) {
        errorCheckMs_ = now;
        if (gTitles.hasWindowTitled(program_, emu_->errorBox)) {
            std::fprintf(stderr, "[standalone] %s could not open the game: its \"%s\" box is up\n",
                         emu_->program, emu_->errorBox);
            failedLoad_ = true;
            stopAtMs_ = now;
            kill9();
        }
    }

    // THE XBOX'S DASHBOARD (watchesDashboard): the game quit, or never ran.
    if (emu_->watchesDashboard && !stopping() && !frozen_ && now - qmpCheckMs_ >= 1000) {
        qmpCheckMs_ = now;
        const uint32_t title = pollXboxTitle();
        if (title != 0 && !isXboxDashboard(title) && title != xboxTitle_) {
            xboxTitle_ = title;
            std::fprintf(stderr, "[xbox] running title %08X\n", title);
        } else if (title != 0 && isXboxDashboard(title)) {
            if (xboxTitle_ != 0) {
                std::fprintf(stderr, "[xbox] the game went to the dashboard; closing xemu\n");
                stop();
            } else {
                std::fprintf(stderr, "[xbox] the dashboard came up, not the game\n");
                failedLoad_ = true;
                stopAtMs_ = now;
                kill9();
            }
        }
    }

    // A GAME THAT STOPPED, by itself or through the emulator's own exit
    // shortcut, leaves the emulator's window up: its title loses the game.
    // Twice a second is plenty, and a frozen emulator's title does not move.
    if (!stopping() && !frozen_ && emu_->titlePrefix && now - titleCheckMs_ >= 500) {
        titleCheckMs_ = now;
        const int parts = gTitles.parts(emu_->titlePrefix);
        if (parts >= emu_->titleParts) {
            sawGameTitle_ = true;
        } else if (sawGameTitle_ && parts > 0) {
            std::fprintf(stderr, "[standalone] emulation stopped; closing %s\n",
                         emu_->program);
            stop();
        }
    }
    return true;
}

}  // namespace cab::standalone
