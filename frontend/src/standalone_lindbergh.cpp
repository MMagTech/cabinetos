// Sega Lindbergh, through Linux Loader in the image: its settings and its
// controls written whole in this person's folder for the game before every
// start (prepareLindbergh), and its command line and sandbox (lindberghArgs).

#include "standalone_internal.h"

#include "quality.h"
#include "storage.h"
#include "vpad.h"

#include <cstdio>
#include <string>
#include <vector>

namespace cab::standalone::detail {

// --- Sega Lindbergh (Linux Loader) ----------------------------------------------
//
// Decided with MMagTech 2026-11-26 (#620, "Sega Lindbergh: The House of the
// Dead 4 and more with Linux Loader"; docs/decisions/
// 2026-11-26-lindbergh-in-linux-loader.md): Linux Loader v3.0.15, its own
// AppImage unpacked into the image with every library the image has taken
// out, its own Mesa among them, so the image's 32-bit Mesa draws
// (build_files/install-linuxloader.sh), drawing with OpenGL as measured, never
// patched, and run with no network like every emulator (Run::start's sandbox),
// none of its network settings on besides. No save states, as for every
// program of its own. No free play, Test and Service off, region Export, as
// every arcade game here. The light-gun games are greyed until #575
// (arcadematch.h), so nothing here aims.
//
// HOW IT RUNS A GAME. The loader is given the folder holding the game's
// program (`-g`), finds the program there by its own list of names, and
// starts it through the shell as a child of its own, with its library loaded
// into it, which plays the board around the game. So a pause, a quit and a
// kill reach the game only through the loader's whole process group
// (standalone.h, `gameIsChild`), and the quit is the loader's own (below).
//
// WHERE THINGS ARE, inside the sandbox, at fixed paths with no space in them:
// the loader refuses a settings or controls file whose path has one, and
// warns of trouble for a game folder that does (mainShared.c, hasSpaces),
// while the console's folders can hold one (a drive's name, a person's, a
// game's). So the sandbox's /var/tmp is the run's own, empty (where The House
// of the Dead 4 writes its empty `mwlogo`), with the game's folder at
// kGameAt and this person's folder for the game at kPersonAt inside it.
// The loader's settings, its controls and the controller list are this
// person's (`-c`, `-o`, `-d`). The board's `eeprom.bin` (its settings, coin
// counts and later a gun's calibration) and `sram.bin` (scores and
// rankings), and a player's cards, go in `saves/` there (EEPROM_PATH,
// SRAM_PATH, ID_CARDFOLDER, CARDFILE_01 and 02), which travels to RomM in
// its arcade form (saveRoot; catalog::SaveForm::aloneAsItIs). The loader
// writes both of the board's files through stdio with no flush (eeprom.c,
// baseBoard.c), so only its own clean exit keeps the last write: hence the
// quit below, never SIGTERM, which it has no handler for.
//
// WHAT ELSE IT WRITES stays in the game's folder and goes with it when the
// cache evicts it (cache.h): the shaders it converts on a first start
// (`fs/compiledmesa`), a game's own files under `tmp/` where it redirects
// them, and Let's Go Jungle Special's empty `warning`.
//
// THE CONSOLE'S QUIT IS THE LOADER'S OWN: its ExitGame, bound here to the
// Guide button of player 1's virtual controller alone, which the console
// presses to close it (Run::stop). The loader then exits cleanly with the
// board's memory written (sdlQuit, exit 0; 0.3 s in the trial). A player's
// Guide never reaches a game (it opens the pause menu), so no press of
// theirs can end one, and Start with Back no longer does.
//
// EVERY KEY BELOW IS ONE THE LOADER READS at the pin (config.c,
// applyIniConfig; sdlInput.c's action names), asserted in
// build_files/install-linuxloader.sh against the program itself.

namespace {

constexpr const char* kGameAt = "/var/tmp/cabinetos-game";
constexpr const char* kPersonAt = "/var/tmp/cabinetos-person";

struct Line {
    const char* section;
    const char* key;
    std::string value;
};

std::string quoted(const std::string& v) { return "\"" + v + "\""; }

// The settings file, whole: the loader's own defaults (its `--create
// config`) but for the values chosen for a console at home, the same for
// every game; no game gets a branch of its own.
std::vector<Line> settings(quality::Level level) {
    const int lines = quality::lindberghLines(level);
    const std::string saves = std::string(kPersonAt) + "/saves";
    return {
        // THE GAME'S OWN SHAPE at every level: the height by the level and
        // the width worked out by the loader from the game's own size, so a
        // 4:3 game stays 4:3 and Hummer its 5:3 (config.c, applyIniConfig);
        // the game's own size at Performance. Full screen, the render boost
        // drawing the 3D inside higher where the loader knows how.
        {"Display", "WIDTH", "AUTO"},
        {"Display", "HEIGHT", lines > 0 ? std::to_string(lines) : std::string("AUTO")},
        {"Display", "BOOST_RENDER_RES", "true"},
        {"Display", "FULLSCREEN", "true"},
        {"Display", "DISPLAY_INDEX", "0"},
        {"Display", "BORDER_ENABLED", "false"},
        {"Display", "WHITE_BORDER_PERCENTAGE", "2.0"},
        {"Display", "BLACK_BORDER_PERCENTAGE", "0.0"},
        {"Display", "KEEP_ASPECT_RATIO", "true"},
        {"Display", "SCALING_FILTER", "LINEAR"},
        {"Display", "HIDE_CURSOR", "true"},
        {"Display", "FPS_LIMITER_ENABLED", "true"},
        {"Display", "FPS_TARGET", "60.0"},
        {"Display", "FPS_OVERLAY_ENABLED", "false"},
        {"Display", "FPS_OVERLAY_POSITION", "0"},
        // SDL's controllers, the virtual ones (lindberghArgs), not evdev.
        {"Input", "INPUT_MODE", "1"},
        // THE CABINET as every arcade game here: Export, coins, the board's
        // own free-play setting left as it is; its boards played by the
        // loader where a game has one.
        {"Emulation", "REGION", "EX"},
        {"Emulation", "FREEPLAY", "none"},
        {"Emulation", "EMULATE_JVS", "true"},
        {"Emulation", "EMULATE_RIDEBOARD", "AUTO"},
        {"Emulation", "EMULATE_DRIVEBOARD", "AUTO"},
        {"Emulation", "EMULATE_MOTIONBOARD", "AUTO"},
        {"Emulation", "EMULATE_HW210_CARDREADER", "AUTO"},
        // The name the loader reads; its `--create` writes
        // EMULATE_ID_CARD_READER, which nothing reads.
        {"Emulation", "EMULATE_ID_CARDREADER", "AUTO"},
        {"Emulation", "EMULATE_TOUCHSCREEN", "AUTO"},
        // A PLAYER'S CARDS, this person's.
        {"Cards", "ID_CARDFILE_AUTOLOAD", "true"},
        {"Cards", "CARDFILE_01", quoted(saves + "/Card_01.crd")},
        {"Cards", "CARDFILE_02", quoted(saves + "/Card_02.crd")},
        {"Cards", "ID_CARDFOLDER", quoted(saves)},
        // THE BOARD'S MEMORY, this person's. The serial ports as the loader
        // has them: a board it does not play is one no console has.
        {"Paths", "JVS_PATH", quoted("/dev/ttyUSB0")},
        {"Paths", "SERIAL_1_PATH", quoted("/dev/ttyS0")},
        {"Paths", "SERIAL_2_PATH", quoted("/dev/ttyS1")},
        {"Paths", "SRAM_PATH", quoted(saves + "/sram.bin")},
        {"Paths", "EEPROM_PATH", quoted(saves + "/eeprom.bin")},
        {"Paths", "LIBCG_PATH", quoted("")},
        {"Graphics", "HUMMER_FLICKER_FIX", "false"},
        {"Graphics", "LGJ_RENDER_WITH_MESA", "true"},
        {"Graphics", "DISABLE_BUILTIN_FONT", "false"},
        {"Graphics", "DISABLE_BUILTIN_LOGOS", "false"},
        {"Cursor", "CUSTOM_CURSOR_ENABLED", "false"},
        {"Cursor", "CUSTOM_CURSOR", quoted("")},
        {"Cursor", "CUSTOM_CURSOR_WIDTH", "32"},
        {"Cursor", "CUSTOM_CURSOR_HEIGHT", "32"},
        {"Cursor", "TOUCH_CURSOR", quoted("")},
        {"Cursor", "TOUCH_CURSOR_WIDTH", "32"},
        {"Cursor", "TOUCH_CURSOR_HEIGHT", "32"},
        // OutRun 2 SP SDX's tower and drive board, which no console has:
        // only OutRun reads it.
        {"GameSpecific", "PRIMEVAL_HUNT_SCREEN_MODE", "2"},
        {"GameSpecific", "PRIMEVAL_HUNT_TEST_SCREEN_SINGLE", "true"},
        {"GameSpecific", "SKIP_OUTRUN_CABINET_CHECK", "true"},
        {"GameSpecific", "MJ4_ENABLED_ALL_THE_TIME", "false"},
        {"GameSpecific", "CPU_FREQ_GHZ", "0.0"},
        {"GameSpecific", "RAMBO_GUNS_SWITCH", "false"},
        {"GameSpecific", "HARLEY_ANALOG_REMAP", "false"},
        {"GameSpecific", "ID5_CHINESE_LANGUAGE", "false"},
        {"GameSpecific", "ID_STEERING_REDUCTION_PERCENTAGE", "0.0"},
        {"CrossHairs", "ENABLE_CROSSHAIRS", "false"},
        {"CrossHairs", "P1_CROSSHAIR_PATH", quoted("")},
        {"CrossHairs", "P2_CROSSHAIR_PATH", quoted("")},
        {"CrossHairs", "CUSTOM_CROSSHAIRS_WIDTH", "64"},
        {"CrossHairs", "CUSTOM_CROSSHAIRS_HEIGHT", "64"},
        {"CrossHairs", "GSEVO_CROSSHAIR_ALWAYS_ON", "false"},
        {"CrossHairs", "GSEVO_CROSSHAIR_ALWAYS_OFF", "false"},
        {"System", "DEBUG_MSGS", "false"},
        {"System", "LINDBERGH_COLOUR", "YELLOW"},
        // NO NETWORK OF ITS OWN: none of its patches for linked cabinets, which
        // Batocera turns off too (Harley-Davidson does not boot with them),
        // inside a sandbox with no network anyway. The addresses are the
        // loader's own defaults, read only by those patches.
        {"Network", "ENABLE_NETWORK_PATCHES", "false"},
        {"Network", "NIC_NAME", quoted("enp0s1")},
        {"Network", "ID_IP_SEAT_1", quoted("192.168.1.2")},
        {"Network", "ID_IP_SEAT_2", quoted("192.168.1.3")},
        {"Network", "OR2_IPADDRESS", quoted("192.168.1.2")},
        {"Network", "OR2_NETMASK", quoted("255.255.255.0")},
        {"Network", "IP_CAB1", quoted("192.168.1.2")},
        {"Network", "IP_CAB2", quoted("192.168.1.3")},
        {"Network", "IP_CAB3", quoted("192.168.1.4")},
        {"Network", "IP_CAB4", quoted("192.168.1.5")},
        {"Network", "2SPICY_IP_CAB1", quoted("192.168.1.2")},
        {"Network", "2SPICY_IP_CAB2", quoted("192.168.1.3")},
        {"Network", "SRTV_IPADDRESS", quoted("192.168.1.2")},
    };
}

// A virtual controller as the loader records it: SDL's GUID with the name's
// checksum cleared (sdlInput.c, initSdlInput), so all four read the same and
// player 1 is the first, which the console makes first.
std::string loaderGuid(int player) {
    std::string g = cab::vpad::sdlGuid(player);
    if (g.size() >= 8) g.replace(4, 4, "0000");
    return g;
}

// The controls file, whole: the loader's own layout for each kind of game
// (its `--create controls`: Driving, Flying, Digital, Shooting, Mahjong),
// the same for every game, with four changes. Coin on Back, as every arcade
// game here (the loader puts Service there). Test and Service on nothing.
// ExitGame on player 1's Guide alone, which only the console presses (above):
// not Escape, not Start with Back. And the card slot's Guide taken off, which
// a player's Guide never reaches. Each player is a virtual controller, by its
// GUID, so the loader asks nothing of the real pads and writes no controls
// file of its own into the game's folder.
std::string controls() {
    std::string t =
        "# Written by CabinetOS before every start (standalone_lindbergh.cpp).\n"
        "[Config]\n"
        "Steer_DeadZone = 500\n"
        "Gas_DeadZone = 500\n"
        "Brake_DeadZone = 500\n"
        "FLYING_X_DeadZone = 500\n"
        "FLYING_Y_DeadZone = 500\n"
        "Throttle_DeadZone = 500\n"
        "GunX_Centering = 0\n"
        "GunY_Centering = 0\n"
        "ShakeIncreaseRate = 1.0\n"
        "ShakeDecayRate = 0.95\n"
        "ShakeMinScreenFraction = 0.15\n"
        "CardInsert_Toggle = 1\n"
        "Card1Insert_Toggle = 1\n"
        "Card2Insert_Toggle = 1\n"
        "\n[ControllerGUIDs]\n";
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p)
        t += "P" + std::to_string(p + 1) + "_GUID = " + loaderGuid(p) + "\n";
    t +=
        "\n[Common]\n"
        "Test =\n"
        "ExitGame = GC0_BUTTON_GUIDE\n"
        "P1_Coin = KEY_5, GC0_BUTTON_BACK\n"
        "P2_Coin = KEY_6\n"
        "P1_Start = KEY_1, JOY0_BUTTON_11, GC0_BUTTON_START\n"
        "P2_Start = KEY_2\n"
        "P1_Service =\n"
        "\n[Digital]\n"
        "P1_Up = KEY_Up, GC0_BUTTON_DPUP, GC0_AXIS_LEFTY_NEGATIVE, JOY0_HAT0_UP, JOY0_AXIS_1_NEGATIVE\n"
        "P1_Down = KEY_Down, GC0_BUTTON_DPDOWN, GC0_AXIS_LEFTY_POSITIVE, JOY0_HAT0_DOWN, "
        "JOY0_AXIS_1_POSITIVE\n"
        "P1_Left = KEY_Left, GC0_BUTTON_DPLEFT, GC0_AXIS_LEFTX_NEGATIVE, JOY0_HAT0_LEFT, "
        "JOY0_AXIS_0_NEGATIVE\n"
        "P1_Right = KEY_Right, GC0_BUTTON_DPRIGHT, GC0_AXIS_LEFTX_POSITIVE, JOY0_HAT0_RIGHT, "
        "JOY0_AXIS_0_POSITIVE\n"
        "P1_Button1 = KEY_Q, GC0_BUTTON_A, JOY0_BUTTON_0\n"
        "P1_Button2 = KEY_W, GC0_BUTTON_B, JOY0_BUTTON_1\n"
        "P1_Button3 = KEY_E, GC0_BUTTON_X, JOY0_BUTTON_2\n"
        "P1_Card1Insert = KEY_F7, JOY0_BUTTON_11\n"
        "P2_Card2Insert = KEY_F8, JOY1_BUTTON_11\n"
        "\n[Driving]\n"
        "P1_Steer_Left = KEY_Left\n"
        "P1_Steer_Right = KEY_Right\n"
        "P1_Gas_Digital = KEY_Up\n"
        "P1_Brake_Digital = KEY_Down\n"
        "ViewChange = KEY_V, GC0_BUTTON_Y, JOY0_BUTTON_2\n"
        "Boost = KEY_Q, GC0_BUTTON_A, JOY0_BUTTON_0\n"
        "BoostRight = KEY_W, GC0_BUTTON_B, JOY0_BUTTON_1\n"
        "GearUp = KEY_A, GC0_BUTTON_RIGHTSHOULDER, JOY0_BUTTON_5\n"
        "GearDown = KEY_Z, GC0_BUTTON_LEFTSHOULDER, JOY0_BUTTON_4\n"
        "MusicChange = KEY_M, GC0_BUTTON_X, JOY0_BUTTON_3\n"
        "Up = KEY_I, GC0_BUTTON_DPUP, JOY0_HAT0_UP\n"
        "Down = KEY_K, GC0_BUTTON_DPDOWN, JOY0_HAT0_DOWN\n"
        "Left = KEY_J, GC0_BUTTON_DPLEFT, JOY0_HAT0_LEFT\n"
        "Right = KEY_L, GC0_BUTTON_DPRIGHT, JOY0_HAT0_RIGHT\n"
        "CardInsert = KEY_F7, JOY0_BUTTON_12\n"
        "P1_Steer = GC0_AXIS_LEFTX, JOY0_AXIS_0\n"
        "P1_Gas = GC0_AXIS_RIGHTTRIGGER, JOY0_AXIS_5\n"
        "P1_Brake = GC0_AXIS_LEFTTRIGGER, JOY0_AXIS_2\n"
        "\n[Flying]\n"
        "Flying_Left = KEY_Left\n"
        "Flying_Right = KEY_Right\n"
        "Flying_Up = KEY_Up\n"
        "Flying_Down = KEY_Down\n"
        "Throttle_Accelerate = KEY_A\n"
        "Throttle_Slowdown = KEY_Z\n"
        "GunTrigger = KEY_Q, GC0_BUTTON_A, JOY0_BUTTON_0\n"
        "MissileTrigger = KEY_W, GC0_BUTTON_B, JOY0_BUTTON_1\n"
        "ClimaxSwitch = KEY_E, GC0_BUTTON_Y, JOY0_BUTTON_4\n"
        "Flying_X = GC0_AXIS_LEFTX, JOY0_AXIS_0\n"
        "Flying_Y = GC0_AXIS_LEFTY, JOY0_AXIS_1\n"
        "Throttle = GC0_AXIS_RIGHTY_INVERTED, GC0_AXIS_RIGHTTRIGGER_POSITIVE_HALF, "
        "GC0_AXIS_LEFTTRIGGER_NEGATIVE_HALF, JOY0_AXIS_4_INVERTED, JOY0_AXIS_5_POSITIVE_HALF, "
        "JOY0_AXIS_4_NEGATIVE_HALF\n"
        // The light-gun games' and the mahjong panels' own, greyed until
        // #575 and not supported (arcadematch.h), as the loader writes them.
        "\n[Shooting]\n"
        "P1_GunX = MOUSE_AXIS_X\n"
        "P1_GunY = MOUSE_AXIS_Y\n"
        "P1_Trigger = MOUSE_LEFT_BUTTON, KEY_Q\n"
        "P1_Reload = MOUSE_RIGHT_BUTTON, KEY_W\n"
        "P1_GunButton = MOUSE_MIDDLE_BUTTON, KEY_E\n"
        "P1_ActionButton = KEY_R\n"
        "P1_PedalLeft = KEY_Left\n"
        "P1_PedalRight = KEY_Right\n"
        "\n[Mahjong]\n"
        "ButtonA = KEY_Y\n"
        "ButtonB = KEY_U\n"
        "ButtonC = KEY_I\n"
        "ButtonD = KEY_O\n"
        "ButtonE = KEY_G\n"
        "ButtonF = KEY_H\n"
        "ButtonG = KEY_J\n"
        "ButtonH = KEY_K\n"
        "ButtonI = KEY_L\n"
        "ButtonJ = KEY_V\n"
        "ButtonK = KEY_B\n"
        "ButtonL = KEY_N\n"
        "ButtonM = KEY_M\n"
        "ButtonN = KEY_,\n"
        "ButtonChi = KEY_F1\n"
        "ButtonPon = KEY_F2\n"
        "ButtonKan = KEY_F3\n"
        "ButtonReach = KEY_F4\n"
        "ButtonAgari = KEY_F5\n"
        "ButtonCancel = KEY_F6\n"
        "CardInsert = KEY_F7\n";
    return t;
}

}  // namespace

std::string lindberghSaves(const std::string& saveDir) { return saveDir + "/saves"; }

bool prepareLindbergh(const Emulator&, const std::string& saveDir, int players,
                      quality::Level level, std::string* err) {
    storage::makeDirs(lindberghSaves(saveDir));
    // EACH PLAYER'S CONTROLLER, BY NAME, for the loader's SDL, which knows
    // none of the virtual ones by itself.
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    std::string ini = "# Written by CabinetOS before every start (standalone_lindbergh.cpp).\n";
    const char* section = "";
    for (const Line& l : settings(level)) {
        if (std::string(section) != l.section) {
            ini += std::string("\n[") + l.section + "]\n";
            section = l.section;
        }
        ini += std::string(l.key) + " = " + l.value + "\n";
    }
    if (!writeFile(saveDir + "/gamecontrollerdb.txt", db) ||
        !writeFile(saveDir + "/linuxloader.ini", ini) ||
        !writeFile(saveDir + "/controls.ini", controls())) {
        *err = "could not write Linux Loader's settings in " + saveDir;
        return false;
    }
    const int lines = quality::lindberghLines(level);
    std::fprintf(stderr, "[lindbergh] settings for %d player(s) at %s\n", players,
                 lines > 0 ? (std::to_string(lines) + " lines").c_str() : "the game's own size");
    return true;
}

void lindberghArgs(std::vector<std::string>& full, const Emulator& e,
                   const std::string& entryPath, const std::string& saveDir,
                   const std::string& romPath) {
    // THE SANDBOX'S OWN /var/tmp, and in it the game's folder and this
    // person's at fixed paths with no space (above), before bwrap's "--".
    for (size_t i = 0; i < full.size(); ++i) {
        if (full[i] != "--") continue;
        full.insert(full.begin() + static_cast<long>(i),
                    {"--tmpfs", "/var/tmp", "--bind", entryPath, kGameAt, "--bind", saveDir,
                     kPersonAt});
        break;
    }
    // THE FOLDER HOLDING THE PROGRAM, as it sits in the game's folder (the
    // top, or `elf/` and the like in a TeknoParrot copy).
    std::string rel = romPath.substr(0, romPath.find_last_of('/'));
    rel = rel.size() > entryPath.size() ? rel.substr(entryPath.size()) : std::string();
    const std::string person = kPersonAt;
    // The unpacked AppImage's root, from the program's path in it
    // (`usr/bin/linuxloader`).
    std::string root = binaryOf(e);
    for (int up = 0; up < 3; ++up) root = root.substr(0, root.find_last_of('/'));
    // As its AppImage's own start sets it (AppRun): where its library and
    // the rest of its own are, and its libraries before the image's. ONLY THE
    // VIRTUAL CONTROLLERS, as for every program: its SDL sees no real pad.
    full.insert(full.end() - 1, "APP_IMG_ROOT=" + root);
    full.insert(full.end() - 1, "LD_LIBRARY_PATH=" + root + "/usr/lib32");
    full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=" + cab::vpad::sdlIds());
    full.insert(full.end(), {"-g", std::string(kGameAt) + rel, "-c", person + "/linuxloader.ini",
                             "-o", person + "/controls.ini", "-d",
                             person + "/gamecontrollerdb.txt"});
}

}  // namespace cab::standalone::detail
