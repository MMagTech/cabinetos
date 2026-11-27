// Windows arcade boards, through windowsarcadeloader in the image: its root
// and its settings layer written before every start (prepareWindows), the
// game's folder and the Windows setup each under a layer of this person's
// own (windowsBeforeStart, windowsArgs), the Windows setup built once from
// the admin's file (windowsSetup), and what of those layers is the save
// (windowsSaveFile).

#include "standalone_internal.h"

#include "arcadematch.h"
#include "proc.h"
#include "quality.h"
#include "storage.h"
#include "vpad.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/xattr.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>
#include <vector>

namespace cab::standalone {
namespace detail {

// --- Windows arcade boards (windowsarcadeloader) ---------------------------------
//
// Decided with MMagTech 2026-11-26 (#762, "Windows Arcade: games from
// Windows-based arcade boards, in windowsarcadeloader"; docs/decisions/
// 2026-11-26-windows-arcade-in-windowsarcadeloader.md): the loader built at a
// pin with GE-Proton and winetricks beside it
// (cores/build-windowsarcadeloader.sh,
// build_files/install-windowsarcadeloader.sh), never patched, run with no
// network like every emulator. No save states and no RetroAchievements, as
// for every program of its own. Coins, as every arcade game here: Coin on
// Back, and the cabinet's Service and Test switches on the stick clicks, the
// loader's own layout, which is how a game's own operator settings are
// reached. The light-gun games are greyed until #575 (arcadematch.h).
//
// HOW IT RUNS A GAME. The loader is given the game's `<gameid>.windowsloader`
// file, whose name is the game and whose first line is its program
// (arcadematch.h). It builds a run folder for the game inside the Windows
// setup (`drive_c/wal/<system>/<gameid>`: links into the game's folder, its
// own DLLs beside the program), starts the game under Wine in a process
// group of its own, and feeds it the controllers. Wine's own server and
// services run beside the game and belong to nobody's process group. So the
// sandbox is a process space of its own (`ownPidSpace`, standalone.h): a
// pause and a kill reach every process in it, and nothing of Wine's outlives
// it.
//
// ITS ROOT IS A FOLDER IN ITS HOME, `root/`, with links to the image's
// `dist`, `systemprofiles` and `wine-runners` and real `wine-prefix/` and
// `userprofiles/` beside them, so every path a profile gives from the root
// (the runner, the payloads, a game's own Windows setup) is where the loader
// looks, and nothing is written into the image. The console's choices are one
// layer the loader reads last (`--profile`), written whole before every
// start, the same for every game but for its picture options (below).
//
// THREE PLACES A GAME WRITES, AND NONE OF THEM SHARED (the trial of
// 2026-11-26): its own folder (a board's memory in `sv/`, an Unreal game's
// `Saved/`), its run folder in the Windows setup (a program that writes
// beside itself), and Wine's user folders. The game's folder on the drive is
// the one every person plays, and the Windows setup is one for the console.
// So neither is ever written. Each is run under a layer of this person's own
// for this game, in their save folder: what the game writes lands in the
// layer, and what it reads comes from the layer first and then from what is
// underneath.
//   - THE GAME'S FOLDER through fuse-overlayfs, mounted for the length of
//     the game at `game/` in the loader's home (windowsBeforeStart,
//     windowsAfterEnd). Not the kernel's own layering: a games drive can be
//     exFAT, which the kernel refuses to layer over (measured on the test VM,
//     2026-11-27: bwrap's overlay over an exFAT folder fails with "Invalid
//     argument"; fuse-overlayfs works).
//   - THE WINDOWS SETUP through the kernel's own layering, inside the sandbox
//     only (bwrap's --overlay, windowsArgs): it is always on the console's
//     own drive, and Wine opens thousands of its files at every start.
// The layers are `save/game` and `save/prefix` in this person's folder for
// the game, and what travels to RomM is the save root, `save/`: everything in
// the game's layer, and the run folder and Wine's user folders out of the
// setup's layer, less what is no save (windowsSaveFile). The rest of the
// setup's layer is what Wine and the loader write at every start (the
// registry, links, the loader's DLLs): it is emptied before each start, so a
// game always starts on the shared setup as it is now. THE GAME'S OWN
// REGISTRY KEYS are the one thing taken out of it first (keepRegistry): a
// Unity game keeps its screen size there, and without it TMNT drew in a
// quarter of the screen at every start (21 of 22 pictures on the A9,
// 2026-11-27; full screen in 6 of 6 with its keys put back). They travel as
// one file among Wine's user folders and are laid over the shared setup's
// registry before each start (putRegistry).
//
// PROVED ON THE A9, 2026-11-27, with this file's steps run by hand on the
// trial's games: the game's folder and the shared setup unchanged by
// checksum after every run; a stop in under 0.6 s with nothing left; the
// pause and the kill reaching every process; Scarlet Dawn's first picture
// 19.6 s through the mount and 19.6 s without it.
//
// THE QUIT is SIGTERM to the launcher, which stops Wine's server and with it
// the game and exits 0 (under 0.7 s for every game of the trial). It exits 0
// when a game ends by itself too, and an arcade game has no quit of its own,
// so an end nobody asked for is a failed start or a crash (`neverQuits`).
//
// EVERY KEY THE LAYER WRITES IS ONE THE LOADER READS at the pin, asserted in
// build_files/install-windowsarcadeloader.sh against its source.

namespace {

// The loader's own files in the image, from the launcher's path in them
// (`dist/arcade-launcher`).
std::string imageDir(const Emulator& e) {
    std::string dir = binaryOf(e);
    for (int up = 0; up < 2; ++up) dir = dir.substr(0, dir.find_last_of('/'));
    return dir;
}

std::string rootDir(const Emulator& e) { return home(e) + "/root"; }
// Where the game's folder is mounted under this person's layer.
std::string gameMount(const Emulator& e) { return home(e) + "/game"; }
std::string layerFile(const Emulator& e) { return home(e) + "/layer.yaml"; }
// The game's own registry keys, in this person's save: beside Wine's user
// folders, under no user's name, so nothing rests on what Wine calls its user.
const char* const kRegistryKept = "/prefix/drive_c/users/cabinetos-registry.reg";

// A game's Windows setup, by the name its profile gives it (`full`, the one
// nearly every game shares).
std::string prefixDir(const Emulator& e, const std::string& name) {
    return rootDir(e) + "/wine-prefix/" + name;
}

std::string lowered(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

bool endsWith(const std::string& s, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return s.size() >= n && s.compare(s.size() - n, n, suffix) == 0;
}

// THE ROOT, made or mended: three links into the image and two real folders.
bool makeRoot(const Emulator& e, std::string* err) {
    const std::string root = rootDir(e);
    if (!storage::makeDirs(root + "/wine-prefix") || !storage::makeDirs(root + "/userprofiles")) {
        *err = "could not make " + root;
        return false;
    }
    for (const char* part : {"dist", "systemprofiles", "wine-runners"}) {
        const std::string target = imageDir(e) + "/" + part;
        char now[4096];
        const ssize_t n = ::readlink((root + "/" + part).c_str(), now, sizeof now - 1);
        if (n > 0 && std::string(now, static_cast<size_t>(n)) == target) continue;
        if (!placeLink(target, root + "/" + part)) {
            *err = "could not link " + root + "/" + part;
            return false;
        }
    }
    return true;
}

// Whether something is mounted at `dir`: its device is not its parent's.
bool mounted(const std::string& dir) {
    struct stat here, above;
    return ::stat(dir.c_str(), &here) == 0 &&
           ::stat(dir.substr(0, dir.find_last_of('/')).c_str(), &above) == 0 &&
           here.st_dev != above.st_dev;
}

// The game's folder let go of. A mount left by a console that went down
// mid-game is let go of the same way before the next one.
void unmountGame(const Emulator& e) {
    const std::string mnt = gameMount(e);
    if (!mounted(mnt)) return;
    proc::Result r = proc::run({"fusermount3", "-u", mnt}, 10);
    if (mounted(mnt)) r = proc::run({"fusermount3", "-u", "-z", mnt}, 10);
    if (mounted(mnt))
        std::fprintf(stderr, "[windows] could not let go of %s: %s\n", mnt.c_str(),
                     proc::trimmed(r.err).c_str());
}

// A game's picture options at a level (arcadematch::WindowsOption, read from
// the loader's own profile for the game): what the game starts with at
// Performance, the loader's own default. A size (`1920x1080`): at Balanced
// the largest up to 1080 lines, at Quality the largest up to 2160. A scale
// (a list of numbers, or a range): at Quality the most, at Balanced the one
// nearest halfway from the default to the most. Measured on the A9,
// 2026-11-27: Scarlet Dawn's scale at 60, 85 and 100 all held 61.8 frames a
// second at 4K with the graphics chip at most 41% busy. A game that declares
// none is drawn at its own size and the screen scales it.
std::string optionAt(const arcadematch::WindowsOption& o, quality::Level level) {
    if (level == quality::Level::Performance) return o.def;
    const double def = std::atof(o.def.c_str());
    if (o.choices.empty()) {
        if (o.max <= def) return o.def;
        return std::to_string(level == quality::Level::Quality
                                  ? o.max
                                  : static_cast<int>(def + (o.max - def) / 2));
    }
    const bool sizes = o.def.find('x') != std::string::npos;
    if (sizes) {
        const int cap = level == quality::Level::Quality ? 2160 : 1080;
        std::string best = o.def;
        int bestLines = 0;
        for (const std::string& c : o.choices) {
            const size_t x = c.find('x');
            const int lines = x == std::string::npos ? 0 : std::atoi(c.c_str() + x + 1);
            if (lines > bestLines && lines <= cap) {
                best = c;
                bestLines = lines;
            }
        }
        return best;
    }
    double most = def;
    for (const std::string& c : o.choices) most = std::max(most, std::atof(c.c_str()));
    const double want = level == quality::Level::Quality ? most : def + (most - def) / 2;
    std::string best = o.def;
    double bestGap = -1;
    for (const std::string& c : o.choices) {
        const double v = std::atof(c.c_str());
        if (v < def) continue;
        const double gap = v > want ? v - want : want - v;
        if (bestGap < 0 || gap < bestGap) {
            best = c;
            bestGap = gap;
        }
    }
    return best;
}

// THE CONSOLE'S LAYER, whole. The loader's own defaults stand but for these:
//   - GE-Proton's own FFmpeg ahead of the image's. The loader puts the
//     system's first, and Wine's movie code was built with the runner's: with
//     the image's, Let's Go Island's movies were black and silent and a coin
//     froze it (the trial's fault 1).
//   - No way out but the console's: the loader's own Back with Start, Escape
//     and its keyboard layout are off, so Coin with Start never ends a game.
//   - Every window managed, so gamescope sees it: a profile that leaves its
//     window unmanaged (Elevator Action Invasion's) drew a game gamescope
//     could neither time nor photograph (fault 5).
//   - 60 frames a second for every Direct3D version through DXVK, as the
//     boards ran: the loader's own limit covers Direct3D 8 and 9 only, and an
//     Unreal game ran uncapped with the graphics chip at 100% (fault 5). By
//     DXVK's own setting, given in its environment variable: Elevator Action
//     Invasion 59.8 frames a second with it (the A9, 2026-11-27).
//   - Coins, as every arcade game here.
std::string layer(const Emulator& e, const arcadematch::WindowsGame& game,
                  quality::Level level) {
    const std::string ffmpeg =
        rootDir(e) + "/wine-runners/" + game.runner + "/lib/x86_64-linux-gnu";
    std::string t =
        "# Written by CabinetOS before every start (standalone_windows.cpp).\n"
        "lib64_dirs: [" + yamlQuote(ffmpeg) + ", \"/usr/lib64\"]\n"
        "input:\n"
        "  exit_combo: []\n"
        "  exit_keys: []\n"
        "  keyboard_enabled: false\n"
        "env:\n"
        "  WAL_DESKTOP_UNMANAGED: \"0\"\n"
        "  DXVK_CONFIG: \"dxvk.maxFrameRate=60\"\n"
        "  WAL_FREEPLAY: \"0\"\n";
    for (const arcadematch::WindowsOption& o : game.options)
        t += "  " + o.key + ": " + yamlQuote(optionAt(o, level)) + "\n";
    return t;
}

// THE SETUP'S LAYER EMPTIED OF EVERYTHING THAT IS NO SAVE, before a start:
// what Wine and the loader wrote into it last time (the registry, links, the
// loader's DLLs, a deleted file's marker) would otherwise stand over the
// shared setup for good, an older registry over a newer setup's among them.
// A folder the layer replaced whole loses that mark too, so the setup's own
// files under it show again.
void emptyOfChurn(const std::string& layerRoot, const std::string& rel) {
    const std::string dir = rel.empty() ? layerRoot : layerRoot + "/" + rel;
    for (const std::string& name : listDir(dir)) {
        const std::string childRel = rel.empty() ? name : rel + "/" + name;
        const std::string child = layerRoot + "/" + childRel;
        struct stat st;
        if (::lstat(child.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            ::removexattr(child.c_str(), "user.overlay.opaque");
            emptyOfChurn(layerRoot, childRel);
            ::rmdir(child.c_str());   // only when nothing of the save is in it
        } else if (!S_ISREG(st.st_mode) || !windowsSaveFile("prefix/" + childRel)) {
            ::unlink(child.c_str());
        }
    }
}

std::string gameIdOf(const std::string& romPath) {
    return arcadematch::windowsGameId(romPath);
}

// One key of Wine's registry file with its values: from its `[...]` line to
// the next.
std::vector<std::string> registrySections(const std::string& text) {
    std::vector<std::string> out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        end = end == std::string::npos ? text.size() : end + 1;
        const std::string line = text.substr(at, end - at);
        if (line[0] == '[') out.push_back(line);
        else if (!out.empty()) out.back() += line;
        at = end;
    }
    return out;
}

// THE GAME'S OWN REGISTRY KEYS, out of what Wine left in the setup's layer
// after a game: every key of the person's registry under `Software\` that
// the shared setup's registry does not have at all, with a value in it, as
// Wine wrote it (a key's path has its backslashes doubled there), and not
// Wine's, Microsoft's, the classes' or the policies'. A key the shared setup
// has is the setup's or the runner's own, whatever it holds now: the runner
// rewrites a few of its own at every start (Valve's, a sound one of Intel's),
// and kept, they made a save out of every start of a game that keeps nothing
// (Contra, through the console on the A9, 2026-11-27). Written among Wine's
// user folders, where the save's rule takes it; none kept, none written.
void keepRegistry(const std::string& sharedPrefix, const std::string& saveDir) {
    const std::string kept = windowsSaves(saveDir) + kRegistryKept;
    auto keyOf = [](const std::string& section) {
        return lowered(section.substr(0, section.find(']') + 1));
    };
    std::set<std::string> shared;
    for (const std::string& s : registrySections(readFile(sharedPrefix + "/user.reg")))
        shared.insert(keyOf(s));
    std::string out;
    for (const std::string& s : registrySections(readFile(windowsSaves(saveDir) + "/prefix/user.reg"))) {
        const std::string head = keyOf(s);
        if (head.compare(0, 11, "[software\\\\") != 0 || shared.count(head)) continue;
        bool ours = true;
        for (const char* skip : {"wine", "microsoft", "classes", "policies"}) {
            const size_t n = std::strlen(skip);
            if (head.compare(11, n, skip) == 0 && !std::isalnum(static_cast<unsigned char>(head[11 + n])))
                ours = false;
        }
        if (!ours) continue;
        // WITHOUT WINE'S TIMES, the number after the key's name and its
        // `#time=` line, which are new at every start, AND WITHOUT THE UNITY
        // ENGINE'S OWN COUNTERS, the values it names `unity.` (how many times
        // the game has started, an id for each start, an id for its usage
        // statistics), which it rewrites at every start of every Unity game:
        // with either, every sitting made a new save out of a game nobody
        // had played (TMNT through the console on the A9, 2026-11-27). A
        // value's line begins with its name in quotes, or `@` for the key's
        // own, and a long one goes on over lines that end in a backslash.
        std::string kept1;
        bool hasValue = false, first = true, skipping = false;
        size_t at = 0;
        while (at < s.size()) {
            size_t end = s.find('\n', at);
            end = end == std::string::npos ? s.size() : end + 1;
            std::string line = s.substr(at, end - at);
            at = end;
            const bool goesOn = line.size() > 1 && line[line.size() - 2] == '\\';
            if (first) {
                line = line.substr(0, line.find(']') + 1) + "\n";
                first = false;
            } else if (skipping) {
                skipping = goesOn;
                continue;
            } else if (line.compare(0, 6, "#time=") == 0) {
                continue;
            } else if (lowered(line.substr(0, 7)) == "\"unity.") {
                skipping = goesOn;
                continue;
            } else if (line[0] == '"' || line.compare(0, 2, "@=") == 0) {
                hasValue = true;
            }
            kept1 += line;
        }
        if (!hasValue) continue;
        out += kept1;
        while (out.size() > 1 && out[out.size() - 1] == '\n' && out[out.size() - 2] == '\n')
            out.pop_back();
        out += "\n";
    }
    if (out.empty()) {
        ::unlink(kept.c_str());
        return;
    }
    storage::makeDirs(kept.substr(0, kept.find_last_of('/')));
    if (!writeFile(kept, out))
        std::fprintf(stderr, "[windows] could not keep the game's registry keys in %s\n",
                     kept.c_str());
}

// AND LAID BACK BEFORE A START, over the shared setup's registry as it is
// now: the layer's own copy of the person's registry is the shared one with
// the kept keys after it, and Wine reads a key named twice as one, the later
// values winning. Nothing kept, nothing written, and the game reads the
// shared registry itself.
void putRegistry(const std::string& sharedPrefix, const std::string& saveDir) {
    const std::string kept = readFile(windowsSaves(saveDir) + kRegistryKept);
    if (kept.empty()) return;
    std::string base = readFile(sharedPrefix + "/user.reg");
    if (base.empty()) return;
    while (!base.empty() && base.back() == '\n') base.pop_back();
    if (!writeFile(windowsSaves(saveDir) + "/prefix/user.reg", base + "\n\n" + kept))
        std::fprintf(stderr, "[windows] could not lay the game's registry keys back\n");
}

// A folder the kernel's layering used as its work folder, gone: it leaves a
// folder nobody may open (mode 000) with a file in it, which removeTree
// cannot enter until it is opened up.
void removeWork(const std::string& dir) {
    ::chmod((dir + "/work").c_str(), 0700);
    removeTree(dir);
}

}  // namespace

std::string windowsSaves(const std::string& saveDir) { return saveDir + "/save"; }

bool prepareWindows(const Emulator& e, const std::string& romPath, const std::string& saveDir,
                    int players, quality::Level level, std::string* err) {
    const std::string id = gameIdOf(romPath);
    const arcadematch::WindowsGame game = arcadematch::windowsGame(id);
    if (!game.known) {
        *err = "windowsarcadeloader has no game called " + id;
        return false;
    }
    if (!makeRoot(e, err)) return false;
    const std::string saves = windowsSaves(saveDir);
    for (const char* part : {"/game", "/prefix"}) storage::makeDirs(saves + part);
    removeWork(saveDir + "/layers/prefix");
    for (const char* part : {"/layers/game", "/layers/prefix"}) storage::makeDirs(saveDir + part);
    storage::makeDirs(gameMount(e));
    if (!writeFile(layerFile(e), layer(e, game, level))) {
        *err = "could not write " + layerFile(e);
        return false;
    }
    std::fprintf(stderr, "[windows] %s in the %s setup, layer for %d player(s) at %s\n",
                 id.c_str(), game.prefix.c_str(), players, quality::levelWord(level));
    return true;
}

bool windowsBeforeStart(const Emulator& e, const std::string& entryPath,
                        const std::string& saveDir, std::string* err) {
    unmountGame(e);
    // THE SAVE IS IN PLACE BY NOW (startStandalone restores it before this),
    // so what is left in the setup's layer that is no save goes, and the
    // game's own registry keys are laid over the shared setup's.
    emptyOfChurn(windowsSaves(saveDir) + "/prefix", "");
    {
        const arcadematch::WindowsGame game = arcadematch::windowsGame(
            arcadematch::windowsGameId(arcadematch::windowsLoaderFile(entryPath)));
        if (game.known) putRegistry(prefixDir(e, game.prefix), saveDir);
    }
    // THE THREE FOLDERS BY LINKS WITH PLAIN NAMES: fuse-overlayfs takes them
    // in one option string, where a comma or a colon in a game's or a
    // person's name would end a path early.
    const std::string links = home(e) + "/layers";
    storage::makeDirs(links);
    if (!placeLink(entryPath, links + "/lower") ||
        !placeLink(windowsSaves(saveDir) + "/game", links + "/upper") ||
        !placeLink(saveDir + "/layers/game", links + "/work")) {
        *err = "could not link the game's layers in " + links;
        return false;
    }
    const std::string mnt = gameMount(e);
    const proc::Result r = proc::run(
        {"fuse-overlayfs", "-o",
         "lowerdir=" + links + "/lower,upperdir=" + links + "/upper,workdir=" + links + "/work",
         mnt},
        30);
    if (!r.ok() || !mounted(mnt)) {
        *err = "could not lay this person's folder over " + entryPath + ": " +
               proc::trimmed(r.err);
        return false;
    }
    return true;
}

void windowsAfterEnd(const Emulator& e, const std::string& entryPath,
                     const std::string& saveDir) {
    unmountGame(e);
    const arcadematch::WindowsGame game = arcadematch::windowsGame(
        arcadematch::windowsGameId(arcadematch::windowsLoaderFile(entryPath)));
    if (game.known) keepRegistry(prefixDir(e, game.prefix), saveDir);
    removeWork(saveDir + "/layers/prefix");
}

void windowsArgs(std::vector<std::string>& full, const Emulator& e, const std::string& romPath,
                 const std::string& entryPath, const std::string& saveDir) {
    const arcadematch::WindowsGame game = arcadematch::windowsGame(gameIdOf(romPath));
    const std::string prefix = prefixDir(e, game.prefix);
    // A PROCESS SPACE OF ITS OWN, with its own /proc (Wine reads it), and
    // this person's layer over the Windows setup, before bwrap's "--".
    for (size_t i = 0; i < full.size(); ++i) {
        if (full[i] != "--") continue;
        full.insert(full.begin() + static_cast<long>(i),
                    {"--proc", "/proc", "--unshare-pid", "--overlay-src", prefix, "--overlay",
                     windowsSaves(saveDir) + "/prefix", saveDir + "/layers/prefix", prefix});
        break;
    }
    // ONLY THE VIRTUAL CONTROLLERS, as for every program, named for the
    // loader's SDL, which knows none of them by itself; Wine's own SDL
    // inherits the same, so a game that reads a controller itself sees no
    // real pad either.
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=" + cab::vpad::sdlIds());
    full.insert(full.end() - 1, "SDL_GAMECONTROLLERCONFIG=" + db);
    // THE GAME AS THIS PERSON'S LAYER SHOWS IT: the same file, under the mount.
    const std::string inGame = romPath.compare(0, entryPath.size(), entryPath) == 0
                                   ? romPath.substr(entryPath.size())
                                   : romPath.substr(romPath.find_last_of('/'));
    full.insert(full.end(), {"run", gameMount(e) + inGame, "--root", rootDir(e), "--profile",
                             layerFile(e)});
}

}  // namespace detail

// What of a person's two layers is the game's save, by its path under the
// save root (`game/...`, `prefix/drive_c/...`): everything the game wrote into
// its own folder, its run folder and Wine's user folders, less what is no
// save. One rule for every game: logs, crash dumps and the crash reporter's
// folders, caches (DXVK's and Unity's among them), a usage-statistics queue,
// temporary files, a program or a library (the loader copies its own DLLs and
// some games' programs into the run folder), and the loader's own notes.
// Links and the kernel's marker for a deleted file are no files and never get
// here (cab::listTree). fuse-overlayfs marks a folder the game made over one
// of its own with an empty file, `.wh..wh..opq`, which does travel: put back
// on another console, it hides the same files there.
//
// MEASURED ON THE A9, 2026-11-27, on seven games, each played once and
// started twice more with nothing done: no save was dropped (a board's
// memory in `sv/`, Scarlet Dawn's `AMFS/` and `AppData/`, Senxin Aleste's
// `save.dat` and rankings, TMNT's scores and operator settings, Elevator
// Action's ranking and settings), and what was new at every start with
// nothing saved was dropped: DXVK's cache in every game, Unreal's crash
// reporter's folder, TMNT's Unity usage statistics, Senxin Aleste's `Log.txt`.
bool windowsSaveFile(const std::string& relPath) {
    const bool inGame = relPath.compare(0, 5, "game/") == 0;
    const bool inSetup = relPath.compare(0, 21, "prefix/drive_c/users/") == 0 ||
                         relPath.compare(0, 19, "prefix/drive_c/wal/") == 0;
    if (!inGame && !inSetup) return false;
    const std::string rest = detail::lowered(relPath.substr(relPath.find('/') + 1));
    size_t at = 0;
    for (;;) {
        const size_t slash = rest.find('/', at);
        if (slash == std::string::npos) break;
        const std::string part = rest.substr(at, slash - at);
        if (part == "temp" || part == "tmp" || part == "log" || part == "logs" ||
            part == "crashes" || part == "crashreportclient" || part == "dxvk" ||
            part == "analytics" || part.find("cache") != std::string::npos ||
            part.find("shader") != std::string::npos)
            return false;
        at = slash + 1;
    }
    const std::string name = rest.substr(at);
    if (name == "log.txt") return false;
    for (const char* ending : {".log", ".dmp", ".tmp", ".dxvk-cache", ".exe", ".dll"})
        if (detail::endsWith(name, ending)) return false;
    return name.compare(0, 5, ".wal-") != 0;
}

namespace {

using namespace cab::standalone::detail;

// The sandbox the setup is built in: no network, a process space of its own
// (Wine's server goes with it), its own /tmp and a runtime folder in it, and
// no screen or session bus of the console's. The command follows what is
// returned.
std::vector<std::string> setupSandbox() {
    std::vector<std::string> args = noNetworkInstall();
    args.insert(std::find(args.begin(), args.end(), "--"),
                {"--proc", "/proc", "--unshare-pid", "--tmpfs", "/tmp", "--perms", "0700",
                 "--dir", "/tmp/xdg"});
    args.insert(args.end(), {"env", "-u", "DISPLAY", "-u", "WAYLAND_DISPLAY",
                             "XDG_RUNTIME_DIR=/tmp/xdg",
                             // No session bus in there, and none started for it.
                             "DBUS_SESSION_BUS_ADDRESS=unix:path=/tmp/xdg/no-session-bus"});
    return args;
}

// THE WINDOWS SETUP, BUILT ONCE (BUILT AGAIN ONLY FOR WHAT IS NEW). A game's
// setup is ready when the loader's own record in it, `.wal-tricks`, names
// every winetricks verb the catalogue gives the game. Otherwise the loader's
// own `prepare` builds it, exactly as it would on any computer, with one
// difference: no network. winetricks takes each installer from its cache when
// the file there has the checksum it expects, and the cache is filled from
// the admin's file (`windows-arcade-files.tar` in RomM's firmware for the
// TeknoParrot platform, made by tools/windows-arcade-admin.sh; docs/decisions/
// 2026-11-26-windows-files-from-the-admin.md), so it downloads nothing. The
// installers open windows, so it runs under a gamescope with no screen of its
// own. The cache is `winetricks/` under a cache folder of the run's own,
// which is where the loader looks for the one installer it runs itself
// (.NET 1.1) and where winetricks unpacks one installer out of another.
// Built this way on the A9 with no network, 2026-11-27: 2 min 28 s, all 40
// of a game's verbs in, 1.4 GB.
// The verbs of a game's setup the loader's record does not name yet.
std::vector<std::string> missingVerbs(const Emulator& e, const arcadematch::WindowsGame& game) {
    const std::string done = readFile(prefixDir(e, game.prefix) + "/.wal-tricks");
    std::vector<std::string> out;
    for (const std::string& v : game.verbs)
        if (!noteHas(done, v)) out.push_back(v);
    return out;
}

std::string packPath() { return storage::biosDir() + "/windows-arcade-files.tar"; }

// WHETHER THE SETUP'S BUILD GOES AGAIN after a pass that left verbs out: yes
// while a pass leaves fewer missing than it found, since the loader's record
// keeps what went in and the next pass goes on from there; no at the first
// pass that adds none, which would only fail the same way again, and no
// when none is missing. So at most one pass per verb.
bool setupGoesAgain(size_t missingBefore, size_t missingAfter) {
    return missingAfter > 0 && missingAfter < missingBefore;
}

// WHETHER A GAME'S SETUP IS THERE, OR CAN BE BUILT FROM WHAT THE CONSOLE HAS:
// Ready with `*built` when the loader's record names every verb; Ready
// without it when the admin's file holds every file the verbs still to go in
// download, by the list made from winetricks at the pin
// (arcadematch::windowsFiles); else what is missing. Builds nothing.
WindowsSetup checkSetup(const Emulator& e, const arcadematch::WindowsGame& game, bool* built,
                        std::string* detail) {
    const std::vector<std::string> todo = missingVerbs(e, game);
    *built = todo.empty() && isFile(prefixDir(e, game.prefix) + "/system.reg");
    if (*built) return WindowsSetup::Ready;
    const std::string pack = packPath();
    if (!isFile(pack)) {
        *detail = pack + " is not there";
        return WindowsSetup::NoFiles;
    }
    const proc::Result listed = proc::run({"tar", "-xOf", pack, "MANIFEST"}, 60);
    std::set<std::string> held;   // "<cache folder>/<file> <sha256>"
    for (const std::string& line : proc::lines(listed.out)) {
        const size_t a = line.find(' ');
        const size_t b = a == std::string::npos ? a : line.find(' ', a + 1);
        const size_t c = b == std::string::npos ? b : line.find(' ', b + 1);
        if (c != std::string::npos) held.insert(line.substr(a + 1, c - a - 1));
    }
    std::string lacking;
    for (const arcadematch::WindowsFile& f : arcadematch::windowsFiles()) {
        if (std::find(todo.begin(), todo.end(), f.verb) == todo.end()) continue;
        if (!held.count(f.path + " " + f.sha256)) lacking += (lacking.empty() ? "" : ", ") + f.path;
    }
    if (!lacking.empty()) {
        *detail = "the file on the server lacks " + lacking;
        return WindowsSetup::FilesMissing;
    }
    return WindowsSetup::Ready;
}

WindowsSetup buildSetup(const Emulator& e, const arcadematch::WindowsGame& game,
                        const std::string& gameId, const std::atomic<bool>& cancel,
                        std::string* detail) {
    bool built = false;
    const WindowsSetup can = checkSetup(e, game, &built, detail);
    if (can != WindowsSetup::Ready || built) return can;
    const std::string prefix = prefixDir(e, game.prefix);
    const std::string pack = packPath();
    std::vector<std::string> todo = missingVerbs(e, game);

    const std::string cacheHome = home(e) + "/setup-cache";
    const std::string cache = cacheHome + "/winetricks";
    removeTree(cacheHome);
    storage::makeDirs(cache);
    if (!proc::run({"tar", "-xf", pack, "-C", cache}, 10 * 60, &cancel).ok()) {
        removeTree(cacheHome);
        *detail = "could not unpack " + pack;
        return WindowsSetup::Failed;
    }
    std::vector<std::string> args = setupSandbox();
    args.insert(args.end(), {
        "XDG_CACHE_HOME=" + cacheHome, "W_CACHE=" + cache,
        "gamescope", "--backend", "headless", "-W", "1920", "-H", "1080", "--",
        binaryOf(e), "prepare", gameId, "--root", rootDir(e),
    });
    std::fprintf(stderr, "[windows] building the %s setup for %s: %zu verb(s) to go in\n",
                 game.prefix.c_str(), gameId.c_str(), todo.size());
    // An hour a pass: two and a half minutes on the reference console; a slow
    // drive has room. AGAIN WHILE A PASS MENDS SOMETHING (setupGoesAgain):
    // winetricks stopped at its start in 3 of 163 of its runs over four fresh
    // builds on the A9, about one in 55, at a different verb each time, Wine
    // having answered an empty %ProgramFiles%; winetricks' own check then
    // exits before it installs anything, so no verb is left half in, and the
    // loader's prepare stops at that verb. The next pass goes on from the
    // loader's record and never redoes a verb (2026-11-27). EVERY PASS IS IN
    // THE SETUP'S LOG, under a line saying which pass and how many verbs were
    // missing before and after it, so a pass that failed and was mended shows
    // in a report; the log starts afresh with each build. The status a pass
    // ends with is the headless gamescope's, not the loader's, so it is not
    // told: the verbs missing are.
    const std::string logPath = storage::logsDir() + "/windowsarcadeloader-setup.log";
    storage::makeDirs(storage::logsDir());
    std::string logText;
    proc::Result ran;
    size_t before = todo.size();
    for (int pass = 1;; ++pass) {
        ran = proc::run(args, 60 * 60, &cancel);
        const size_t after = missingVerbs(e, game).size();
        char head[160];
        std::snprintf(head, sizeof head, "=== pass %d: %zu verb(s) missing before, %zu after%s ===\n",
                      pass, before, after, ran.timedOut ? ", stopped at its hour" : "");
        logText += head + ran.out + ran.err + "\n";
        writeFile(logPath, logText);
        std::fprintf(stderr, "[windows] the setup's pass %d: %zu verb(s) missing before, %zu after\n",
                     pass, before, after);
        if (ran.timedOut || cancel.load() || !setupGoesAgain(before, after)) break;
        before = after;
    }
    removeTree(cacheHome);
    todo = missingVerbs(e, game);
    if (!todo.empty() || !isFile(prefix + "/system.reg")) {
        std::string left;
        for (const std::string& v : todo) left += (left.empty() ? "" : ", ") + v;
        *detail = ran.timedOut ? "stopped before it was done"
                                 : "not in: " + (left.empty() ? std::string("the setup itself") : left);
        return WindowsSetup::Failed;
    }
    std::fprintf(stderr, "[windows] the %s setup is ready\n", game.prefix.c_str());
    return WindowsSetup::Ready;
}

}  // namespace

WindowsSetup windowsSetup(const std::string& gameId, const std::atomic<bool>& cancel,
                          std::string* detail) {
    const Emulator* e = find("windowsarcadeloader");
    const arcadematch::WindowsGame game = arcadematch::windowsGame(gameId);
    if (!e || !game.known) {
        *detail = "no game called " + gameId;
        return WindowsSetup::Failed;
    }
    std::string err;
    if (!makeRoot(*e, &err)) {
        *detail = err;
        return WindowsSetup::Failed;
    }
    return buildSetup(*e, game, gameId, cancel, detail);
}

int windowsSetupTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };
    check(setupGoesAgain(40, 12), "a pass that leaves fewer verbs missing than it found goes again");
    check(setupGoesAgain(12, 11) && setupGoesAgain(2, 1), "by one verb too, and again after that");
    check(!setupGoesAgain(12, 12), "one that adds none stops");
    check(!setupGoesAgain(12, 13), "and one that leaves more missing");
    check(!setupGoesAgain(40, 0) && !setupGoesAgain(0, 0), "none missing is done");
    // The passes a build makes, each pass putting in `mended` verbs, until it
    // stops.
    auto passes = [](size_t missing, const std::vector<size_t>& mended) {
        int n = 0;
        for (size_t i = 0;; ++i) {
            ++n;
            const size_t after = missing - std::min(missing, i < mended.size() ? mended[i] : 0);
            if (!setupGoesAgain(missing, after)) return n;
            missing = after;
        }
    };
    check(passes(40, {28, 11, 1}) == 3, "two stops in a 40-verb build: three passes, the last whole");
    check(passes(40, {40}) == 1, "a build that goes through at once is one pass");
    check(passes(3, {1, 1, 1}) == 3, "at most one pass per verb");
    check(passes(40, {10, 0}) == 2, "a pass that adds none ends it");
    std::printf("\n%s\n", failures ? "FAILED" : "all passed");
    return failures ? 1 : 0;
}

WindowsSetup windowsSetupReady(const std::string& gameId, std::string* detail) {
    const Emulator* e = find("windowsarcadeloader");
    const arcadematch::WindowsGame game = arcadematch::windowsGame(gameId);
    if (!e || !game.known) {
        *detail = "no game called " + gameId;
        return WindowsSetup::Failed;
    }
    bool built = false;
    return checkSetup(*e, game, &built, detail);
}

}  // namespace cab::standalone
