// The original Xbox, through xemu from Flathub: its BIOS, EEPROM and drive and
// the settings the console writes before every start (prepareXemu), a full
// disc dump cut to its game (installXbox), and this person's saves carried onto
// the game's drive and off it around a game (xemuBeforeStart, xemuAfterEnd).
// Moved out of standalone.cpp in the restructure (#398).

#include "standalone_internal.h"

#include "gpu.h"
#include "storage.h"
#include "vpad.h"
#include "xboxhdd.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <vector>

namespace cab::standalone::detail {
namespace {

// --- Xbox (xemu) --------------------------------------------------------------
//
// Decided with MMagTech 2026-09-29, issue #172: xemu from Flathub as it ships;
// the BIOS and MCPX boot ROM from RomM; xemu's own blank drive and one EEPROM
// for every console, both in the image; the drive in the game's folder; saves
// carried on and off it by the console (xboxhdd.h).

constexpr const char* kXboxDrive = "xbox_hdd.qcow2";

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

}  // namespace

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
    // VSYNC ON, xemu's default, as for every emulator since 2026-10-04
    // (#209). Also measured on the A9 2026-10-02 (FlatOut, frames.py): with
    // it off xemu redrew the screen 631 times a second, all wasted on the
    // graphics chip; its own limiter paces the game, not the picture.
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

namespace {

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

}  // namespace

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

// What beforeStart does for xemu: the note in the game's folder, then this
// person's saves onto the game's drive.
bool xemuBeforeStart(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                     const std::string& note, std::string* err) {
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

// What afterEnd does for xemu: the saves off the drive, and only then the note.
bool xemuAfterEnd(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                  std::string* err) {
    const std::string drive = entryPath + "/" + kXboxDrive;
    if (!xboxhdd::readSaves(drive, saveRoot(e, saveDir), err)) {
        // The note stays: the saves are still only on the drive.
        return false;
    }
    ::unlink((entryPath + "/" + kPlayingNote).c_str());
    return true;
}

}  // namespace cab::standalone::detail
