#include "standalone.h"

#include "proc.h"
#include "storage.h"
#include "vpad.h"

#include <archive.h>
#include <archive_entry.h>
#include <dirent.h>
#include <dlfcn.h>
#include <signal.h>
#include <sys/stat.h>
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

// Eden is pinned in system_files/usr/share/cabinetos/flatpaks.list, and xemu
// is installed from the same list and becomes a row here when its issue is
// built (#172). RPCS3 is in the image (build_files/install-rpcs3.sh).
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
     "Eden |", 5,
     "nsp|xci", "keys", false, false, false},

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
     nullptr, 0,
     "iso|pkg", "firmware", true, true, true},
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
                 int players, bool* missingKeys, std::string* err) {
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
        if (endsWithNoCase(name, ".pkg") && ::stat(p.c_str(), &st) == 0 && S_ISREG(st.st_mode))
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
    for (const std::string& name : listDir(entryPath))
        if (isFile(entryPath + "/" + name + "/PARAM.SFO"))
            ::rename((entryPath + "/" + name).c_str(), (games + "/" + name).c_str());

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
    // hard drive, which are not the game.
    for (const std::string& name : listDir(games))
        if (isFile(games + "/" + name + "/PARAM.SFO"))
            ::rename((games + "/" + name).c_str(), (entryPath + "/" + name).c_str());
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
                  const std::string& player, int players, bool* missing, std::string* err) {
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
    if (!writeFile(config + "/vfs.yml", vfs) || !writeFile(config + "/config.yml", kRpcs3Settings) ||
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

}  // namespace

const Emulator* find(const std::string& core) {
    for (const Emulator& e : kEmulators)
        if (core == e.core) return &e;
    return nullptr;
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
             const std::string& player, int players, bool* missingKeys, std::string* err) {
    *missingKeys = false;
    storage::makeDirs(saveDir);
    if (std::strcmp(e.core, "eden") == 0)
        return prepareEden(e, saveDir, player, players, missingKeys, err);
    if (std::strcmp(e.core, "rpcs3") == 0)
        return prepareRpcs3(e, entryPath, saveDir, player, players, missingKeys, err);
    *err = std::string("nothing prepares ") + e.core;
    return false;
}

std::string saveRoot(const Emulator& e, const std::string& saveDir) {
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

bool Run::start(const Emulator& e, const std::string& romPath, const std::string& entryPath,
                const std::string& saveDir, std::string* err) {
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
    logCheckMs_ = 0;
    const std::string dir = home(e);
    // A fresh log each start, so a failure from the last game is not read as
    // this one's.
    logPath_ = (e.cacheInGame ? entryPath : dir) + "/" + e.log;
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
    const std::string out = storage::logsDir() + "/" + e.core + ".log";
    storage::makeDirs(storage::logsDir());

    // A PROGRAM IN THE IMAGE has no sandbox and no `flatpak run` to carry its
    // settings, so `env` does, and then becomes it: the pid started is the
    // emulator's own. Its settings are in its home (XDG_CONFIG_HOME); what it
    // builds per game goes in the game's folder (XDG_CACHE_HOME, when
    // `cacheInGame`), which is how a removed game takes its cache with it.
    if (e.binary) {
        const std::vector<std::string> args = {
            "env",
            "XDG_CONFIG_HOME=" + dir,
            "XDG_CACHE_HOME=" + (e.cacheInGame ? entryPath : dir),
            // gamescope's Xwayland, as for Eden below.
            "QT_QPA_PLATFORM=xcb",
            "SDL_JOYSTICK_HIDAPI=0",
            binaryOf(e), "--no-gui", "--fullscreen", romPath,
        };
        root_ = proc::spawn(args, out);
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
