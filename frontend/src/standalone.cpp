#include "standalone.h"

#include "proc.h"
#include "storage.h"
#include "vpad.h"

#include <dirent.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <sstream>
#include <utility>
#include <vector>

namespace cab::standalone {
namespace {

// Pinned in system_files/usr/share/cabinetos/flatpaks.list. RPCS3 and xemu are
// already installed from the same list and become rows here when their own
// issues are built (#171, #172).
const Emulator kEmulators[] = {
    // Eden's words for a game it cannot load (yuzu/main_window.cpp, before its
    // "Error while loading ROM!" box).
    {"eden", "dev.eden_emu.eden", "eden", "user/log/eden_log.txt",
     {"Failed to load ROM", "Failed to obtain loader"}, "nsp|xci"},
};

// How long a program that was asked to close gets before it is made to.
// Eden took 0.8 s on the A9; ten is room for a slow disk flushing a save.
constexpr int64_t kStopGraceMs = 10 * 1000;
// A failed load shows the emulator's own error box and waits for a click
// nobody can make, so its log is watched for this long after a start.
constexpr int64_t kLoadWatchMs = 60 * 1000;

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
// AND EACH ONE NEEDS A `key\default=false` BESIDE IT. Eden reads a setting's
// value only when that flag says it is not the default; a value on its own is
// silently ignored (frontend_common/config.cpp, ReadSettingGeneric).
struct IniSet {
    const char* section;
    const char* key;
    const char* value;
};

std::string applyIni(const std::string& text, const std::vector<IniSet>& sets) {
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
        setOne(s.section, std::string(s.key) + "\\default", "false");
        setOne(s.section, s.key, s.value);
    }
    std::string out;
    for (const std::string& l : lines) out += l + "\n";
    return out;
}

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

}  // namespace

const Emulator* find(const std::string& core) {
    for (const Emulator& e : kEmulators)
        if (core == e.core) return &e;
    return nullptr;
}

bool installed(const Emulator& e) {
    // The system installation, where cabinetos-flatpak-setup puts it.
    return isDir(std::string("/var/lib/flatpak/app/") + e.flatpak + "/current/active/files");
}

std::string home(const Emulator& e) { return storage::emulatorsDir() + "/" + e.core; }

bool prepare(const Emulator& e, const std::string& saveDir, const std::string& player,
             int players, bool* missingKeys, std::string* err) {
    *missingKeys = false;
    storage::makeDirs(saveDir);
    if (std::strcmp(e.core, "eden") == 0)
        return prepareEden(e, saveDir, player, players, missingKeys, err);
    *err = std::string("nothing prepares ") + e.core;
    return false;
}

std::string saveRoot(const Emulator& e, const std::string& saveDir) {
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

bool Run::start(const Emulator& e, const std::string& romPath, const std::string& saveDir,
                std::string* err) {
    emu_ = &e;
    ended_ = End::None;
    program_ = -1;
    stopAtMs_ = 0;
    failedLoad_ = false;
    const std::string dir = home(e);
    // A fresh log each start, so a failure from the last game is not read as
    // this one's.
    logPath_ = dir + "/" + e.log;
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
    const std::string out = storage::logsDir() + "/" + e.core + ".log";
    storage::makeDirs(storage::logsDir());
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

void Run::stop() {
    if (!active() || stopping()) return;
    stopAtMs_ = nowMs();
    if (program_ <= 0) program_ = proc::findDescendant(root_, emu_->program);
    if (program_ > 0) {
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
}

bool Run::poll() {
    if (!active()) return false;

    int status = 0;
    const pid_t got = waitpid(root_, &status, WNOHANG);
    if (got == root_) {
        const int64_t ran = nowMs() - startMs_;
        if (failedLoad_) {
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
        return false;
    }

    const int64_t now = nowMs();
    if (program_ <= 0 && now - startMs_ > 200) program_ = proc::findDescendant(root_, emu_->program);

    if (stopping() && now - stopAtMs_ > kStopGraceMs) {
        std::fprintf(stderr, "[standalone] %s did not close in %lld s; forcing it\n",
                     emu_->program, static_cast<long long>(kStopGraceMs / 1000));
        kill9();
        stopAtMs_ = now;   // and again after another grace, should even that not take
    }

    // A LOAD THAT FAILED IS AN ERROR BOX NOBODY CAN CLOSE, so the log is read
    // for Eden's own words for it and the console closes it instead.
    if (!failedLoad_ && !stopping() && now - startMs_ < kLoadWatchMs) {
        if (FILE* f = std::fopen(logPath_.c_str(), "r")) {
            std::fseek(f, logRead_, SEEK_SET);
            char line[1024];
            while (std::fgets(line, sizeof line, f)) {
                if (std::strstr(line, emu_->loadFailed[0]) ||
                    std::strstr(line, emu_->loadFailed[1])) {
                    std::fprintf(stderr, "[standalone] %s could not load the game: %s",
                                 emu_->program, line);
                    failedLoad_ = true;
                    break;
                }
            }
            logRead_ = std::ftell(f);
            std::fclose(f);
        }
        // Not asked to close: its error box is modal and holds the close
        // back (measured on Contra, which waited out the whole grace), and a
        // game that never started has nothing to write.
        if (failedLoad_) {
            stopAtMs_ = now;
            kill9();
        }
    }
    return true;
}

}  // namespace cab::standalone
