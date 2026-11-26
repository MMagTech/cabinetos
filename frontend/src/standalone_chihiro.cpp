// Sega Chihiro, through xemu-chihiro in the image: the board's files taken out
// of RomM's BIOS zips, its settings written whole in this person's folder for
// the game before every start (prepareChihiro), and its command line
// (chihiroArgs).

#include "standalone_internal.h"

#include "arcadematch.h"
#include "dirsave.h"
#include "fileio.h"
#include "gpu.h"
#include "storage.h"
#include "vpad.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <iterator>
#include <string>
#include <vector>

namespace cab::standalone::detail {

// --- Sega Chihiro (xemu-chihiro) -----------------------------------------------
//
// Decided with MMagTech 2026-11-25 (#619, "Sega Chihiro: Virtua Cop 3 and The
// House of the Dead III in the xemu fork"; docs/decisions/
// 2026-11-26-chihiro-in-the-xemu-fork.md): Tovarichtch's xemu fork at a pin,
// built here as Batocera 44 builds it and never patched
// (cores/build-xemu-chihiro.sh), drawing with Vulkan, and run with no network
// like every emulator (Run::start's sandbox), its own network and linked
// cabinets off besides. No save states, as for every program of its own: its
// state keys are off below, and the pause menu is Resume, Screenshot and Exit
// to Home. No free play, Test and Service off, region Export, as every arcade
// game here. The light-gun games are greyed until #575 (arcadematch.h), so
// nothing here aims.
//
// WHERE THINGS ARE. The board's files are in its home (`bios/`), taken out of
// RomM's zips once for everyone. Its settings are `xemu.toml` in this
// person's folder for the game (`-config_path`), and everything it writes
// goes under its data folder, SDL's preferred folder for "xemu" under
// XDG_DATA_HOME, which is this person's folder for the game too
// (chihiroData): `saves/<game>.sav`, the board's backup (scores, the test
// menu's settings), `saves/<game>.dimm` where the media board keeps rankings,
// and `cards/`, a player's card in Maximum Tune, Gundam and Ghost Squad, with
// its EEPROM, snapshot store and session logs, which are no part of a save.
// `cards` is a link to `saves`, so the save is one folder of the emulator's
// own files under their own names, which is how it travels to RomM
// (saveRoot; catalog::SaveForm::aloneAsItIs). Its shader caches,
// `vk_shader_cache/` and `shader_seeds/`, are links to one folder in its home
// that every game and person shares, as the driver's own is; its pipeline
// cache is a file it replaces whole, so it stays beside the save, a few MB.
// It writes the backup 8 s after a start, every 5 s while the board changes
// it, and as it quits, which SIGTERM is (Run::stop; measured on the A9,
// 2026-11-25), each time to a temporary file renamed over the last.
//
// ITS SETTINGS ARE CHECKED AGAINST ITS SOURCE when it is built:
// cores/build-xemu-chihiro.sh reads every row of the table below, `{"<table>",
// "<key>",`, and finds each in config_spec.yml at the pin, so every setting
// written here is one of those rows.

std::string chihiroData(const std::string& saveDir) { return saveDir + "/xemu/xemu"; }
std::string chihiroSaves(const std::string& saveDir) { return chihiroData(saveDir) + "/saves"; }

// THE BOARD'S BACKUP, `<game>.sav`, the name the fork gives it: the game's
// parent set by the ID its image carries (chihiro_game_names), which the
// catalogue gives each set (arcadematch::gameName).
std::string chihiroAloneSave(const Emulator& e, const std::string& romPath) {
    const std::string game = arcadematch::gameName(e.core, romPath);
    return game.empty() ? std::string() : game + ".sav";
}

namespace {

// THE BOARD'S FILES, as MAME's chihiro.zip names them and the fork finds them
// beside chihiro_xbox_bios.bin (chihiro_load_flash_rom, chihiro_load_eeproms),
// with their sizes; and the network board's firmware from seganetw.zip, which
// MAME's list gives every Chihiro set (its `seganetw` device). Each is taken
// out of whichever zip in `bios/` holds it, by what the zip holds, not its
// name.
struct BoardFile {
    const char* name;
    size_t size;
};
constexpr BoardFile kBoard[] = {
    {"chihiro_xbox_bios.bin", 524288},
    {"fpr21042_m29w160et.bin", 2097152},
    {"ic10_g24lc64.bin", 8192},
    {"ic11_24lc024.bin", 128},
    {"pc20_g24lc64.bin", 8192},
};
constexpr BoardFile kNetFirmware = {"ver1305.bin", 2097152};

bool sameName(const std::string& path, const char* name) {
    const std::string base = path.substr(path.find_last_of('/') + 1);
    return base.size() == std::strlen(name) && endsWithNoCase(base, name);
}

// The zip in `bios/` that holds `name`, or empty.
std::string zipHolding(const char* name) {
    const std::string bios = storage::biosDir();
    for (const std::string& f : listDir(bios)) {
        if (!endsWithNoCase(f, ".zip")) continue;
        std::vector<arcadematch::Member> members;
        if (!arcadematch::readMembers(bios + "/" + f, &members, nullptr)) continue;
        for (const arcadematch::Member& m : members)
            if (sameName(m.name, name)) return bios + "/" + f;
    }
    return {};
}

// `wanted` taken out of `zip` into `dir`, each only where the file there
// differs, and checked for its size. False when the zip will not read, or one
// is missing or the wrong size.
bool takeOut(const std::string& zip, const std::vector<BoardFile>& wanted, const std::string& dir,
             std::string* why) {
    std::vector<cab::ZipFile> files;
    std::string err;
    if (!cab::readZip(cab::readBytes(zip), &files, &err)) {
        *why = zip + " does not read: " + err;
        return false;
    }
    for (const BoardFile& w : wanted) {
        const cab::ZipFile* got = nullptr;
        for (const cab::ZipFile& z : files)
            if (sameName(z.relPath, w.name)) got = &z;
        if (!got || got->bytes.size() != w.size) {
            *why = std::string(w.name) + (got ? " is the wrong size" : " is not") + " in " + zip;
            return false;
        }
        const std::string to = dir + "/" + w.name;
        if (cab::readBytes(to) != got->bytes && !cab::writeBytes(to, got->bytes)) {
            *why = "could not write " + to;
            return false;
        }
    }
    return true;
}

// A pad's button as the fork numbers it, 2001 plus SDL's, and an axis pushed
// one way, 3001 plus the axis times two plus 1 for positive (ui/xemu-input.h,
// asserted in cores/build-xemu-chihiro.sh). It reads them from the pad on
// port 1, player 1's. 0 is nothing.
std::string padButton(SDL_GamepadButton b) { return std::to_string(2001 + static_cast<int>(b)); }
std::string padAxis(SDL_GamepadAxis a, bool positive) {
    return std::to_string(3001 + static_cast<int>(a) * 2 + (positive ? 1 : 0));
}

// THE CARD IN EACH READER'S SLOT, from this person's save: the newest card
// there for that slot, or none. The fork makes a player's card the first time
// they press Card In, named after the game (`wangmid2_2026-11-25.bin`;
// `ghostsqu_p1_...` and `ghostsqu_p2_...` for Ghost Squad's two slots), and
// names it in the slot's setting, which this file writes whole at every start;
// so the card is found again by its name, on this console or after RomM
// brought the save to another. Slots and names asserted in
// cores/build-xemu-chihiro.sh.
struct CardSlot {
    const char* table;
    const char* key;
    const char* prefix;
};
constexpr CardSlot kCardSlots[] = {
    {"chihiro.card_reader.hw210", "slot1", "ghostsqu_p1_"},
    {"chihiro.card_reader.hw210", "slot2", "ghostsqu_p2_"},
    {"chihiro.card_reader.hw210", "gundam", "gundamos_"},
    {"chihiro.card_reader.crp1231", "mt1", "wangmid_"},
    {"chihiro.card_reader.crp1231", "mt2", "wangmid2_"},
};
std::string cardFor(const std::string& saves, const std::string& cards, const char* prefix) {
    std::string pick;
    int64_t pickNs = -1;
    for (const std::string& f : listDir(saves)) {
        if (f.compare(0, std::strlen(prefix), prefix) != 0 || !endsWithNoCase(f, ".bin")) continue;
        const int64_t ns = cab::modifiedNs(saves + "/" + f);
        if (ns > pickNs) {
            pick = f;
            pickNs = ns;
        }
    }
    return pick.empty() ? std::string() : cards + "/" + pick;
}

struct Setting {
    const char* table;
    const char* key;
    std::string value;
};

}  // namespace

bool prepareChihiro(const Emulator& e, const std::string& saveDir, int players,
                    quality::Level level, bool* missing, std::string* err) {
    const std::string dir = home(e);
    const std::string bios = dir + "/bios";
    storage::makeDirs(bios);
    // THE BOARD'S FILES AND THE NETWORK BOARD'S FIRMWARE, for every game, or
    // it is refused with "No <folder> BIOS on the server" (the row's `needs`):
    // without the first the board does not start, and without the second
    // OutRun 2 Special Tours stops on the board's "Error 14: Network firmware
    // version does not fulfill the game spec" (seen on the A9, 2026-11-25),
    // as Gundam would. Only the firmware's version is read, at start-up;
    // nothing goes online (net and chihiro.link below).
    const std::string board = zipHolding(kBoard[0].name);
    const std::string netZip = zipHolding(kNetFirmware.name);
    std::string why;
    if (board.empty() || netZip.empty() ||
        !takeOut(board, std::vector<BoardFile>(std::begin(kBoard), std::end(kBoard)), bios,
                 &why) ||
        !takeOut(netZip, {kNetFirmware}, bios, &why)) {
        if (board.empty()) why = "no zip in bios/ holds chihiro_xbox_bios.bin";
        else if (netZip.empty()) why = "no zip in bios/ holds ver1305.bin";
        std::fprintf(stderr, "[chihiro] the board's files are not here: %s\n", why.c_str());
        *missing = true;
        return false;
    }
    const std::string net = bios + "/" + kNetFirmware.name;

    // ITS DATA FOLDER, this person's for the game: the save folder, cards in
    // it, and the shared shader caches linked in.
    const std::string data = chihiroData(saveDir);
    const std::string saves = chihiroSaves(saveDir);
    const std::string shaders = dir + "/shaders";
    storage::makeDirs(saves);
    storage::makeDirs(shaders + "/vk_shader_cache");
    storage::makeDirs(shaders + "/shader_seeds");
    if (!placeLink("saves", data + "/cards") ||
        !placeLink(shaders + "/vk_shader_cache", data + "/vk_shader_cache") ||
        !placeLink(shaders + "/shader_seeds", data + "/shader_seeds")) {
        *err = "could not link xemu-chihiro's cards and shader caches in " + data;
        return false;
    }
    // EACH PLAYER'S CONTROLLER, BY NAME, and nothing else, as for xemu.
    std::string db;
    for (int p = 0; p < cab::vpad::kMaxPlayers; ++p) db += cab::vpad::sdlMapping(p) + "\n";
    if (!writeFile(dir + "/gamecontrollerdb.txt", db)) {
        *err = "could not write xemu-chihiro's controller list";
        return false;
    }

    const std::string off = "0";
    const std::string gpu = cab::gpu::vulkan().deviceName;
    // THE CONSOLE'S SETTINGS, written whole before every start, so nothing
    // the fork kept can differ; a key left out is its own default. Names from
    // config_spec.yml at the pin, checked there when it is built.
    std::vector<Setting> s = {
        {"general", "show_welcome", "false"},
        {"general", "skip_boot_anim", "true"},
        {"general.updates", "check", "false"},
        // ONLY PLAYER 1'S CONTROLLER: the board reads its controls from the
        // pad on port 1, and a second player's comes with light guns (#575).
        {"input", "auto_bind", "false"},
        {"input", "gamecontrollerdb_path", tomlQuote(dir + "/gamecontrollerdb.txt")},
        {"input.bindings", "port1", tomlQuote(cab::vpad::sdlGuid(0))},
        {"input.bindings", "port2", "\"\""},
        {"input.bindings", "port3", "\"\""},
        {"input.bindings", "port4", "\"\""},
        // EVERY KEYBOARD SHORTCUT OFF (0, none): its menu, pause, reset,
        // quit, states, fullscreen, screenshots, light-gun mode and the disc,
        // which a keyboard plugged into the console would otherwise reach.
        {"input.hotkeys", "settings", off},
        {"input.hotkeys", "quick_menu", off},
        {"input.hotkeys", "pause", off},
        {"input.hotkeys", "reset", off},
        {"input.hotkeys", "quit", off},
        {"input.hotkeys", "quick_save1", off},
        {"input.hotkeys", "quick_save2", off},
        {"input.hotkeys", "quick_save3", off},
        {"input.hotkeys", "quick_save4", off},
        {"input.hotkeys", "quick_load1", off},
        {"input.hotkeys", "quick_load2", off},
        {"input.hotkeys", "quick_load3", off},
        {"input.hotkeys", "quick_load4", off},
        {"input.hotkeys", "fullscreen", off},
        {"input.hotkeys", "screenshot", off},
        {"input.hotkeys", "lightgun_mode", off},
        {"input.hotkeys", "eject_disc", off},
        {"input.hotkeys", "load_disc", off},
        {"input.hotkeys", "monitor", off},
        // VULKAN, not its OpenGL default, on the GPU the console chose, never
        // llvmpipe (gpu.cpp), at the level's scale (quality.h).
        {"display", "renderer", "\"VULKAN\""},
        {"display.vulkan", "preferred_physical_device", tomlQuote(gpu)},
        {"display.quality", "surface_scale", std::to_string(quality::chihiro(level))},
        {"display.window", "fullscreen_on_startup", "true"},
        // ITS MENU BAR, ITS MESSAGES ("Connected ... to port 1") AND THE
        // POINTER, none over the game.
        {"display.ui", "show_menubar", "false"},
        {"display.ui", "show_notifications", "false"},
        {"display.ui", "hide_cursor", "true"},
        // NO NETWORK OF ITS OWN and no linked cabinets, inside a sandbox
        // with none anyway.
        {"net", "enable", "false"},
        {"chihiro.link", "enable", "false"},
        // THE CABINET as every arcade game here: coins, Export, no light gun.
        // Its drive board's forces reach the player's pad as rumble, under the
        // console's Rumble switch (rumble.h), as Model 2's and Model 3's do.
        {"chihiro.settings", "freeplay", "false"},
        {"chihiro.settings", "region", "\"ex\""},
        {"chihiro.settings", "lightgun_mode", "false"},
        {"chihiro.settings", "pointer_devices", "false"},
        {"chihiro.settings", "sinden_border", "false"},
        {"chihiro.settings", "force_feedback", "true"},
        {"chihiro.roms", "bios_path", tomlQuote(bios + "/" + kBoard[0].name)},
        {"chihiro.roms", "net_firmware_path", tomlQuote(net)},
        // THE ARCADE LAYOUT ON A PAD, as Batocera 44 sets it and every arcade
        // emulator here has it: Coin on Select and Start on Start; steering
        // on the left stick, the d-pad too (the row's `dpadOnStick`); the
        // accelerator on the right trigger and the brake on the left; gear up
        // and down on the right and left shoulders (OutRun 2's shifter,
        // Maximum Tune's lever a gear at a time, Crazy Taxi's Drive and
        // Reverse); Card In on Y. Test and Service off. No pointer.
        {"chihiro.jvs", "pointer_device", "\"\""},
        {"chihiro.jvs", "start", padButton(SDL_GAMEPAD_BUTTON_START)},
        {"chihiro.jvs", "coin", padButton(SDL_GAMEPAD_BUTTON_BACK)},
        {"chihiro.jvs", "service", off},
        {"chihiro.jvs", "test", off},
        {"chihiro.jvs", "steer_left", padAxis(SDL_GAMEPAD_AXIS_LEFTX, false)},
        {"chihiro.jvs", "steer_right", padAxis(SDL_GAMEPAD_AXIS_LEFTX, true)},
        {"chihiro.jvs", "gas", padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, true)},
        {"chihiro.jvs", "brake", padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true)},
        {"chihiro.jvs", "gear_up", padButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},
        {"chihiro.jvs", "gear_down", padButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)},
        {"chihiro.jvs", "card_in", padButton(SDL_GAMEPAD_BUTTON_NORTH)},
        // The light-gun games, greyed until #575: nothing.
        {"chihiro.jvs.hotd3", "trigger", off},
        {"chihiro.jvs.hotd3", "body_button", off},
        {"chihiro.jvs.vc3", "trigger", off},
        {"chihiro.jvs.vc3", "body_button", off},
        {"chihiro.jvs.vc3", "pedal", off},
        {"chihiro.jvs.vc3", "reload", off},
        {"chihiro.jvs.gs", "trigger", off},
        {"chihiro.jvs.gs", "body_button", off},
        {"chihiro.jvs.gs", "change", off},
        {"chihiro.jvs.gs", "reload", off},
        // Crazy Taxi High Roller: Drive and Reverse on the shoulders, Jump on A.
        {"chihiro.jvs.ctx", "drive_gear", padButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},
        {"chihiro.jvs.ctx", "reverse", padButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)},
        {"chihiro.jvs.ctx", "jump", padButton(SDL_GAMEPAD_BUTTON_SOUTH)},
        // OutRun 2: the view on X.
        {"chihiro.jvs.or2", "view_change", padButton(SDL_GAMEPAD_BUTTON_WEST)},
        // Maximum Tune: the lever a gear at a time on the shoulders (no
        // gear of its own), the view on X, the intrude change on B.
        {"chihiro.jvs.wmmt2", "gear1", off},
        {"chihiro.jvs.wmmt2", "gear2", off},
        {"chihiro.jvs.wmmt2", "gear3", off},
        {"chihiro.jvs.wmmt2", "gear4", off},
        {"chihiro.jvs.wmmt2", "gear5", off},
        {"chihiro.jvs.wmmt2", "gear6", off},
        {"chihiro.jvs.wmmt2", "view_change", padButton(SDL_GAMEPAD_BUTTON_WEST)},
        {"chihiro.jvs.wmmt2", "intrude_change", padButton(SDL_GAMEPAD_BUTTON_EAST)},
        // Ollie King's board: swing on the left stick, lean forward and back
        // on it, the grabs on the shoulders.
        {"chihiro.jvs.ok", "swing_left", padAxis(SDL_GAMEPAD_AXIS_LEFTX, false)},
        {"chihiro.jvs.ok", "swing_right", padAxis(SDL_GAMEPAD_AXIS_LEFTX, true)},
        {"chihiro.jvs.ok", "board_front", padAxis(SDL_GAMEPAD_AXIS_LEFTY, false)},
        {"chihiro.jvs.ok", "board_rear", padAxis(SDL_GAMEPAD_AXIS_LEFTY, true)},
        {"chihiro.jvs.ok", "left_grab", padButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)},
        {"chihiro.jvs.ok", "right_grab", padButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},
        // Gundam's cockpit: its two sticks on the pad's two, their triggers
        // on the triggers, their buttons on the shoulders, the pedal on A.
        {"chihiro.jvs.gundam", "l_up", padAxis(SDL_GAMEPAD_AXIS_LEFTY, false)},
        {"chihiro.jvs.gundam", "l_down", padAxis(SDL_GAMEPAD_AXIS_LEFTY, true)},
        {"chihiro.jvs.gundam", "l_left", padAxis(SDL_GAMEPAD_AXIS_LEFTX, false)},
        {"chihiro.jvs.gundam", "l_right", padAxis(SDL_GAMEPAD_AXIS_LEFTX, true)},
        {"chihiro.jvs.gundam", "l_trigger", padAxis(SDL_GAMEPAD_AXIS_LEFT_TRIGGER, true)},
        {"chihiro.jvs.gundam", "l_button", padButton(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER)},
        {"chihiro.jvs.gundam", "r_up", padAxis(SDL_GAMEPAD_AXIS_RIGHTY, false)},
        {"chihiro.jvs.gundam", "r_down", padAxis(SDL_GAMEPAD_AXIS_RIGHTY, true)},
        {"chihiro.jvs.gundam", "r_left", padAxis(SDL_GAMEPAD_AXIS_RIGHTX, false)},
        {"chihiro.jvs.gundam", "r_right", padAxis(SDL_GAMEPAD_AXIS_RIGHTX, true)},
        {"chihiro.jvs.gundam", "r_trigger", padAxis(SDL_GAMEPAD_AXIS_RIGHT_TRIGGER, true)},
        {"chihiro.jvs.gundam", "r_button", padButton(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)},
        {"chihiro.jvs.gundam", "pedal", padButton(SDL_GAMEPAD_BUTTON_SOUTH)},
        // Player 2's controls, all for the light-gun games: nothing.
        {"chihiro.jvs_p2", "pointer_device", "\"\""},
        {"chihiro.jvs_p2", "start", off},
        {"chihiro.jvs_p2", "coin", off},
        {"chihiro.jvs_p2", "card_in", off},
        {"chihiro.jvs_p2", "hotd3_trigger", off},
        {"chihiro.jvs_p2", "hotd3_body_button", off},
        {"chihiro.jvs_p2", "vc3_trigger", off},
        {"chihiro.jvs_p2", "vc3_body_button", off},
        {"chihiro.jvs_p2", "vc3_pedal", off},
        {"chihiro.jvs_p2", "vc3_reload", off},
        {"chihiro.jvs_p2", "gs_trigger", off},
        {"chihiro.jvs_p2", "gs_body_button", off},
        {"chihiro.jvs_p2", "gs_change", off},
        {"chihiro.jvs_p2", "gs_reload", off},
        {"chihiro.card_reader", "enable", "true"},
    };
    for (const CardSlot& c : kCardSlots)
        s.push_back({c.table, c.key, tomlQuote(cardFor(saves, data + "/cards", c.prefix))});

    // ONE TABLE AT A TIME, as TOML wants each once: its rows gathered in the
    // order the table first appears.
    std::vector<std::string> tables;
    for (const Setting& x : s)
        if (std::find(tables.begin(), tables.end(), x.table) == tables.end())
            tables.push_back(x.table);
    std::string t = "# Written by CabinetOS before every start (standalone_chihiro.cpp).\n";
    for (const std::string& table : tables) {
        t += "[" + table + "]\n";
        for (const Setting& x : s)
            if (table == x.table) t += std::string(x.key) + " = " + x.value + "\n";
    }
    if (!writeFile(saveDir + "/xemu.toml", t)) {
        *err = "could not write xemu-chihiro's settings in " + saveDir;
        return false;
    }
    std::fprintf(stderr, "[chihiro] settings for %d player(s) at scale %d\n", players,
                 quality::chihiro(level));
    return true;
}

// Run::start's arguments for xemu-chihiro, as for xeniaArgs: the environment
// entries before the program, then its settings and the game.
void chihiroArgs(std::vector<std::string>& full, const std::string& saveDir,
                 const std::string& romPath) {
    // ITS DATA FOLDER IS THIS PERSON'S FOR THE GAME (above). ONLY THE
    // VIRTUAL CONTROLLERS, as for xemu: its SDL sees no real pad, whose Guide
    // would open its menu. SDL on gamescope's Xwayland. Numbers read in C's
    // form, as qemu expects.
    full.insert(full.end() - 1, "XDG_DATA_HOME=" + saveDir);
    full.insert(full.end() - 1, "SDL_GAMECONTROLLER_IGNORE_DEVICES_EXCEPT=" + cab::vpad::sdlIds());
    full.insert(full.end() - 1, "SDL_VIDEO_DRIVER=x11");
    full.insert(full.end() - 1, "LC_NUMERIC=C");
    full.insert(full.end(), {"-config_path", saveDir + "/xemu.toml", "-dvd_path", romPath});
}

}  // namespace cab::standalone::detail
