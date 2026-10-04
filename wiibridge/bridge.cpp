// SPDX-License-Identifier: GPL-2.0-or-later
//
// cabinetos-wii-bridge: a stand-in for each real Wii Remote. Issue #200.
//
// GPL, NOT MIT, BECAUSE IT CARRIES DOLPHIN'S EXTENSION ENCRYPTION
// (encryption.cpp), and it is its own program for that reason: the console
// app stays MIT and only starts it.
//
// WHY IT EXISTS (measured on the A9, 2026-10-02/03, MMagTech's TechKen copies):
//   * A copy's Nunchuk drops off whenever a 16-byte encryption key is written
//     to it (0xA40040..4F), which every Wii game does when it sets a Nunchuk
//     up. Plain (unencrypted) it is stable. The game then sets it up again,
//     about twice a second, forever. Dolphin passes the game's commands to a
//     real Remote unchanged, so the game never gets a Nunchuk.
//   * The copies report Samsung's id (04E8:7021), so Cemu, which looks only for
//     Nintendo's 057E:0306/0330, cannot see them at all.
//
// WHAT IT DOES: for each real Remote it creates a stand-in through /dev/uhid,
// named as the Remote is, with Nintendo's id, and passes every report both
// ways, except the Nunchuk's encryption: 0xAA to 0xA400F0 and the key are
// answered here and never reach the Remote, which stays plain, and while the
// game has encryption on, extension bytes going to the game (data reports
// and register reads) are encrypted with the game's own key, as Dolphin's
// emulated Remote does. Nothing in Dolphin, Cemu, BlueZ or the kernel changes.
// A stand-in lasts as long as the bridge: a Remote that goes off and comes back
// is joined to the same one (see detach).
//
// THE REAL REMOTE IS KEPT FROM EVERYONE ELSE by a udev rule
// (99-cabinetos-wii-remote.rules: cabinet owns its hidraw node, mode 0000);
// this program makes it readable for the moment it opens it, then closes it
// again, so Dolphin and Cemu see only the stand-in.

#include "encryption.h"

#include <linux/hidraw.h>
#include <linux/input.h>
#include <linux/uhid.h>

#include <dirent.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

namespace {

constexpr const char* kPhys = "cabinetos-wii-bridge";
constexpr const char* kPrefix = "Nintendo RVL-CNT";
constexpr unsigned kNintendo = 0x057e;
constexpr unsigned kWiiRemote = 0x0306;

volatile sig_atomic_t gStop = 0;

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string field(const std::string& uevent, const char* key) {
    const std::string want = std::string(key) + "=";
    std::istringstream in(uevent);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(want, 0) == 0) return line.substr(want.size());
    return {};
}

struct PendingRead {
    bool reg;        // register space (0x04), not EEPROM
    unsigned addr;   // 24-bit start
    unsigned size;
    unsigned done = 0;
};

struct Remote {
    std::string node, name, uniq;
    int real = -1;
    int uhid = -1;
    uint8_t btn1 = 0, btn2 = 0;
    bool ext = false;
    bool plain = false;      // the real extension has had 0x55/0x00 since it connected
    bool enc = false;        // the game believes encryption is on
    std::array<uint8_t, 16> keyData{};
    bool keyDirty = true;
    WiimoteEmu::EncryptionKey key;
    std::deque<PendingRead> reads;
    // ANSWERS TO THE BRIDGE'S OWN COMMANDS, kept from the game, but only for a
    // second after the last of them. A count that outlived its commands hid the
    // game's own answers instead, and a Wii game that misses one says
    // "Communication with the Wii Remote has been interrupted": measured on the
    // A9 2026-10-03 20:29, Geometry Wars right after a replay in Wild West Guns.
    // The count can be high because these copies drop commands sent too fast,
    // and a dropped command is never answered.
    int swallowAcks = 0;
    int swallowed = 0;
    std::chrono::steady_clock::time_point swallowUntil{};
    // WHEN THE GAME LETS THE REMOTE GO (a Wii game's idle disconnect, about
    // five minutes untouched). See checkDropped.
    std::chrono::steady_clock::time_point lastOut{};
    bool lastOutWasReset = false;   // the last thing sent was Dolphin's 12 00 30
    bool driven = false;            // a game has asked for more than buttons
    // The last light and report mode asked for, put back when the Remote
    // comes back (see reattach).
    std::vector<uint8_t> lastLed, lastMode;
    // EVERY OTHER SETUP COMMAND THE GAME SENT, in order: the camera (0x13, 0x1a,
    // registers 0xB0..), the speaker (0x14, 0x19, 0xA2..), the extension and
    // MotionPlus (0xA4.., 0xA6..). A Remote switched off forgets all of it;
    // reattach sends it again. The encryption the bridge answers itself never
    // reached the Remote and is not here.
    std::vector<std::vector<uint8_t>> setup;
};

constexpr size_t kSetupMax = 96;

// Whether the Remote answers a command with an acknowledgement (0x22): every
// memory write, and anything with the acknowledge bit set (WiiBrew, "Wiimote",
// Output Reports). A report mode without that bit is not answered.
bool answered(const uint8_t* d, size_t n) {
    return n >= 2 && (d[0] == 0x16 || (d[1] & 0x02));
}

// Sent by the bridge, not the game: its answer is the bridge's.
void sentOurs(Remote& r, const uint8_t* d, size_t n) {
    if (!answered(d, n)) return;
    ++r.swallowAcks;
    r.swallowUntil = std::chrono::steady_clock::now() + std::chrono::seconds(1);
}

bool isSetup(const uint8_t* d, size_t n) {
    if (n < 2) return false;
    switch (d[0]) {
        case 0x13: case 0x14: case 0x19: case 0x1a: return true;
        case 0x16: {
            if (n < 7 || !(d[1] & 0x04)) return false;
            const uint8_t space = d[2];
            return space == 0xa2 || space == 0xa4 || space == 0xa6 || space == 0xb0;
        }
        default: return false;
    }
}

void remember(Remote& r, const uint8_t* d, size_t n) {
    std::vector<uint8_t> cmd(d, d + n);
    for (auto it = r.setup.begin(); it != r.setup.end(); ++it)
        if (*it == cmd) {
            r.setup.erase(it);   // the same command again: keep only its latest place
            break;
        }
    r.setup.push_back(std::move(cmd));
    if (r.setup.size() > kSetupMax) r.setup.erase(r.setup.begin());
}

std::vector<std::unique_ptr<Remote>> gRemotes;

void toReal(Remote& r, const uint8_t* d, size_t n) {
    if (::write(r.real, d, n) < 0 && errno != EAGAIN)
        std::fprintf(stderr, "[wiibridge] write to %s: %s\n", r.node.c_str(), std::strerror(errno));
}

void toGame(Remote& r, const uint8_t* d, size_t n) {
    uhid_event ev{};
    ev.type = UHID_INPUT2;
    ev.u.input2.size = static_cast<__u16>(n);
    std::memcpy(ev.u.input2.data, d, n);
    if (::write(r.uhid, &ev, sizeof ev) < 0)
        std::fprintf(stderr, "[wiibridge] input to stand-in: %s\n", std::strerror(errno));
}

void ackToGame(Remote& r, uint8_t reportId) {
    const uint8_t ack[5] = {0x22, r.btn1, r.btn2, reportId, 0x00};
    toGame(r, ack, sizeof ack);
}

// The real extension in plain mode, if the game never put it there (an old
// game's setup writes the key without 0x55/0x00 first).
void ensurePlain(Remote& r) {
    if (r.plain) return;
    uint8_t w1[22] = {0x16, 0x04, 0xa4, 0x00, 0xf0, 0x01, 0x55};
    uint8_t w2[22] = {0x16, 0x04, 0xa4, 0x00, 0xfb, 0x01, 0x00};
    toReal(r, w1, sizeof w1);
    sentOurs(r, w1, sizeof w1);
    toReal(r, w2, sizeof w2);
    sentOurs(r, w2, sizeof w2);
    r.plain = true;
    std::fprintf(stderr, "[wiibridge] %s: extension put in plain mode\n", r.uniq.c_str());
}

void updateKey(Remote& r) {
    if (!r.keyDirty) return;
    r.key = WiimoteEmu::KeyGen1stParty().GenerateFromExtensionKeyData(r.keyData);
    r.keyDirty = false;
}

// Where a data report carries extension bytes (WiiBrew, "Wiimote", Data
// Reporting): offset after the report id, and how many. Encrypted from
// extension register 0x00, as Dolphin reads them (ExtensionPort::REPORT_I2C_ADDR).
bool extensionBytes(uint8_t id, size_t* off, size_t* len) {
    switch (id) {
        case 0x32: *off = 3; *len = 8; return true;
        case 0x34: *off = 3; *len = 19; return true;
        case 0x35: *off = 6; *len = 16; return true;
        case 0x36: *off = 13; *len = 9; return true;
        case 0x37: *off = 16; *len = 6; return true;
        case 0x3d: *off = 1; *len = 21; return true;
        default: return false;
    }
}

// From the game (Dolphin, Cemu) to the Remote.
void fromGame(Remote& r, const uint8_t* d, size_t n) {
    if (n < 1) return;
    const uint8_t id = d[0];
    if (id == 0x11 && n >= 2) r.lastLed.assign(d, d + n);
    if (id == 0x12 && n >= 3) r.lastMode.assign(d, d + n);
    // Dolphin's once-a-second rumble-off (10 00) while it holds a Remote it is
    // not using says nothing about the game, so it is not counted.
    const bool keepAlive = id == 0x10 && n >= 2 && (d[1] & 0x01) == 0;
    if (!keepAlive) {
        r.lastOut = std::chrono::steady_clock::now();
        // Dolphin's ResetDataReporting: core buttons, not continuous, no rumble.
        // The console's own setup asks for continuous (0x04), and retail games
        // always do, so this is only ever Dolphin.
        r.lastOutWasReset = n >= 3 && id == 0x12 && d[1] == 0x00 && d[2] == 0x30;
    }
    if (id == 0x12 && n >= 3 && d[2] > 0x30) r.driven = true;
    const bool encryptionWrite = id == 0x16 && n >= 7 && (d[1] & 0x04) && d[2] == 0xa4 &&
                                 ((d[4] == 0xf0 && d[5] >= 1 && d[6] == 0xaa) ||
                                  (d[4] < 0x50 && d[4] + d[5] > 0x40));
    if (isSetup(d, n) && !encryptionWrite) remember(r, d, n);
    if (r.real < 0) {
        // The Remote is off. Writes are answered so nothing waits on them;
        // the rest goes nowhere until it is back.
        if (id == 0x16) ackToGame(r, 0x16);
        return;
    }
    if (id == 0x16 && n >= 7 && (d[1] & 0x04) && d[2] == 0xa4) {
        const unsigned reg = d[4];
        const unsigned count = d[5];
        const uint8_t* p = d + 6;
        if (reg == 0xf0 && count >= 1 && p[0] == 0xaa) {
            // ENCRYPTION ON: kept from the Remote, answered here.
            ensurePlain(r);
            r.enc = true;
            r.keyDirty = true;
            ackToGame(r, 0x16);
            std::fprintf(stderr, "[wiibridge] %s: game turned encryption on (kept from the Remote)\n",
                         r.uniq.c_str());
            return;
        }
        if (reg < 0x50 && reg + count > 0x40) {
            // THE KEY: kept, and answered here.
            ensurePlain(r);
            for (unsigned i = 0; i < count && i < 16; ++i) {
                const unsigned at = reg + i;
                if (at >= 0x40 && at < 0x50) r.keyData[at - 0x40] = p[i];
            }
            r.keyDirty = true;
            ackToGame(r, 0x16);
            return;
        }
        if (reg == 0xf0 && count >= 1 && p[0] == 0x55) r.enc = false;
        if (reg == 0xfb && count >= 1 && p[0] == 0x00) r.plain = true;
    }
    if (id == 0x17 && n >= 7) {
        PendingRead pr;
        pr.reg = (d[1] & 0x04) != 0;
        pr.addr = (unsigned(d[2]) << 16) | (unsigned(d[3]) << 8) | d[4];
        pr.size = (unsigned(d[5]) << 8) | d[6];
        r.reads.push_back(pr);
    }
    toReal(r, d, n);
}

// From the Remote to the game.
void fromReal(Remote& r, uint8_t* d, size_t n) {
    if (n < 1) return;
    const uint8_t id = d[0];
    const bool hasButtons = (id >= 0x20 && id <= 0x22) || (id >= 0x30 && id <= 0x37) ||
                            id == 0x3e || id == 0x3f;
    if (hasButtons && n >= 3) {
        r.btn1 = d[1];
        r.btn2 = d[2];
    }
    if (id == 0x22 && n >= 5 && r.swallowAcks > 0 &&
        std::chrono::steady_clock::now() < r.swallowUntil) {
        --r.swallowAcks;   // an answer to something the bridge sent, not the game
        ++r.swallowed;
        return;
    }
    if (id == 0x20 && n >= 4) {
        const bool ext = (d[3] & 0x02) != 0;
        if (ext != r.ext) {
            // A new extension (or none): its state starts again, as the game's will.
            r.enc = false;
            r.plain = false;
            r.keyDirty = true;
            r.ext = ext;
            // The game sets a new extension up from scratch; the old one's setup
            // (and MotionPlus') is not replayed onto it.
            std::erase_if(r.setup, [](const std::vector<uint8_t>& c) {
                return c[0] == 0x16 && (c[2] == 0xa4 || c[2] == 0xa6);
            });
        }
    }
    if (id == 0x21 && n >= 6 && !r.reads.empty()) {
        PendingRead& pr = r.reads.front();
        const unsigned size = (d[3] >> 4) + 1;
        const unsigned err = d[3] & 0x0f;
        if (!err && r.enc && pr.reg && (pr.addr >> 16) == 0xa4 && n >= 6 + size) {
            updateKey(r);
            r.key.Encrypt(d + 6, d[5], size);
        }
        pr.done += size;
        if (err || pr.done >= pr.size) r.reads.pop_front();
    }
    size_t off = 0, len = 0;
    if (r.enc && extensionBytes(id, &off, &len) && n >= off + len) {
        updateKey(r);
        r.key.Encrypt(d + off, 0, static_cast<u32>(len));
    }
    toGame(r, d, n);
}

// THE GAME LET THE REMOTE GO, so switch it off, as a Wii does.
//
// A Wii game disconnects a Remote left alone for about five minutes ("Communication
// with the Wii Remote was interrupted"); on a Wii that turns the Remote off and any
// button connects it again. Through hidraw Dolphin cannot turn it off: it stops
// listening (WiimoteReal.cpp, EmuStop: reports are dropped while unlinked), so the
// button press that should bring it back is never seen, and the game waits forever.
// Measured on the A9 2026-10-03, Geometry Wars.
//
// The sign: Dolphin's reset (12 00 30) after a game has driven the Remote, then
// nothing for two seconds but Dolphin's once-a-second rumble-off. Dolphin sends the
// same reset when it takes a Remote, but the game's own commands follow at once;
// and when a game ends the console sets the Remote up again within a frame. Then
// bluetoothd is asked to disconnect, and the Remote turns off; its stand-in stays
// (see detach), and a button brings it back to the same stand-in.
//
// A GAME DOES NOT PICK IT UP AGAIN YET: Dolphin ignores the Remote's buttons once
// it has let go (WiimoteReal.cpp, Read drops reports while unlinked, since 2019),
// so after an idle drop the game is left with hold HOME, Exit. This still saves
// the batteries, and is the whole of a Wii's behaviour the day Dolphin listens.
void checkDropped(Remote& r) {
    if (r.real < 0 || !r.driven || !r.lastOutWasReset) return;
    if (std::chrono::steady_clock::now() - r.lastOut < std::chrono::seconds(2)) return;
    r.driven = false;
    std::string address = r.uniq;
    for (char& c : address) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::fprintf(stderr, "[wiibridge] %s: the game let it go; switching it off, as a Wii would\n",
                 r.uniq.c_str());
    const pid_t pid = ::fork();
    if (pid == 0) {
        const int null = ::open("/dev/null", O_RDWR);
        if (null >= 0) {
            ::dup2(null, 0);
            ::dup2(null, 1);
        }
        ::execlp("bluetoothctl", "bluetoothctl", "disconnect", address.c_str(),
                 static_cast<char*>(nullptr));
        std::_Exit(127);
    }
}

// A SECOND AFTER THE BRIDGE'S LAST COMMAND every answer is the game's again,
// whatever the count says.
void settleAnswers(Remote& r) {
    if (r.swallowAcks == 0 && r.swallowed == 0) return;
    if (std::chrono::steady_clock::now() < r.swallowUntil) return;
    std::fprintf(stderr, "[wiibridge] %s: %d answer(s) to the bridge's commands kept from the game, %d never came\n",
                 r.uniq.c_str(), r.swallowed, r.swallowAcks);
    r.swallowAcks = 0;
    r.swallowed = 0;
}

// THE REMOTE WENT AWAY (switched off, batteries, out of reach): its stand-in
// STAYS. libretro's Dolphin crashes when a Remote it holds disappears mid-game:
// its SetSource returns early on a null source (#ifdef __LIBRETRO__, commit
// ae98a5fa9e), so the slot keeps the freed Remote and the next frame calls into
// it. Measured on the A9 2026-10-03 09:10:04, SIGSEGV, Geometry Wars, the Remote
// switched off by hand. Upstream Dolphin does not have that line. So Dolphin never
// sees a Remote vanish: the stand-in goes quiet, and the Remote, when it comes
// back, is joined to the same stand-in.
void detach(Remote& r) {
    if (r.real >= 0) ::close(r.real);
    r.real = -1;
    r.node.clear();
    r.reads.clear();
    r.swallowAcks = 0;
    r.swallowed = 0;
    // The real extension forgets its setup when the Remote goes off; what the
    // GAME believes (extension in, encryption on, its key) is kept, because the
    // game never saw the Remote go.
    r.plain = false;
    std::fprintf(stderr, "[wiibridge] %s went away; its stand-in stays\n", r.uniq.c_str());
}

// BACK, AND TO THE GAME NOTHING HAPPENED: the real extension is set up again
// (plain), and the light and report mode it had are put back, all answered here.
// Measured on the A9 2026-10-03: a Remote switched off and on mid-game came back
// with its Nunchuk unset while the game still expected it encrypted, and the
// stick read wrong; the game does not set the extension up again by itself.
void reattach(Remote& r, const std::string& node, int fd) {
    r.real = fd;
    r.node = node;
    // The game's setup again, in its order, gently: these copies drop commands
    // sent too fast. Every write is acknowledged by the Remote; those answers
    // are the bridge's, not the game's.
    for (const auto& c : r.setup) {
        toReal(r, c.data(), c.size());
        sentOurs(r, c.data(), c.size());
        if (c[0] == 0x16 && c[2] == 0xa4 && c[4] == 0xfb) r.plain = true;
        ::usleep(15000);
    }
    if (r.ext) ensurePlain(r);   // a game that skipped the plain setup
    if (!r.lastMode.empty()) {
        toReal(r, r.lastMode.data(), r.lastMode.size());
        sentOurs(r, r.lastMode.data(), r.lastMode.size());
    }
    if (!r.lastLed.empty()) {
        toReal(r, r.lastLed.data(), r.lastLed.size());
        sentOurs(r, r.lastLed.data(), r.lastLed.size());
    }
    std::fprintf(stderr, "[wiibridge] %s back on %s, to the same stand-in; %zu setup command(s) sent again\n",
                 r.uniq.c_str(), node.c_str(), r.setup.size());
}

void drop(Remote& r) {
    if (r.uhid >= 0) {
        uhid_event ev{};
        ev.type = UHID_DESTROY;
        (void)::write(r.uhid, &ev, sizeof ev);
        ::close(r.uhid);
    }
    if (r.real >= 0) ::close(r.real);
    std::fprintf(stderr, "[wiibridge] %s gone\n", r.uniq.c_str());
}

// Open a real Remote's node. If the udev rule has hidden it (ours, mode 0000),
// open it for the moment it takes, then hide it again.
int openReal(const std::string& path) {
    int fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (fd >= 0) return fd;
    struct stat st {};
    if (errno == EACCES && ::stat(path.c_str(), &st) == 0 && st.st_uid == ::getuid()) {
        ::chmod(path.c_str(), 0600);
        fd = ::open(path.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        ::chmod(path.c_str(), 0000);
    }
    return fd;
}

void adopt(const std::string& node, const std::string& uevent) {
    const std::string uniq = field(uevent, "HID_UNIQ");
    for (auto& old : gRemotes) {
        if (old->real < 0 && strcasecmp(old->uniq.c_str(), uniq.c_str()) == 0) {
            const int fd = openReal("/dev/" + node);
            if (fd < 0) {
                std::fprintf(stderr, "[wiibridge] cannot open /dev/%s: %s\n", node.c_str(),
                             std::strerror(errno));
                return;
            }
            reattach(*old, node, fd);
            return;
        }
    }
    auto r = std::make_unique<Remote>();
    r->node = node;
    r->name = field(uevent, "HID_NAME");
    r->uniq = uniq;
    const std::string path = "/dev/" + node;
    r->real = openReal(path);
    if (r->real < 0) {
        std::fprintf(stderr, "[wiibridge] cannot open %s: %s\n", path.c_str(), std::strerror(errno));
        return;
    }
    int size = 0;
    hidraw_report_descriptor rd{};
    if (::ioctl(r->real, HIDIOCGRDESCSIZE, &size) < 0 || size <= 0 ||
        size > HID_MAX_DESCRIPTOR_SIZE) {
        std::fprintf(stderr, "[wiibridge] no report descriptor on %s\n", path.c_str());
        ::close(r->real);
        return;
    }
    rd.size = static_cast<__u32>(size);
    if (::ioctl(r->real, HIDIOCGRDESC, &rd) < 0) {
        std::fprintf(stderr, "[wiibridge] report descriptor read failed on %s\n", path.c_str());
        ::close(r->real);
        return;
    }
    r->uhid = ::open("/dev/uhid", O_RDWR | O_NONBLOCK | O_CLOEXEC);
    if (r->uhid < 0) {
        std::fprintf(stderr, "[wiibridge] cannot open /dev/uhid: %s\n", std::strerror(errno));
        ::close(r->real);
        return;
    }
    uhid_event ev{};
    ev.type = UHID_CREATE2;
    std::snprintf(reinterpret_cast<char*>(ev.u.create2.name), sizeof ev.u.create2.name, "%s",
                  r->name.c_str());
    std::snprintf(reinterpret_cast<char*>(ev.u.create2.phys), sizeof ev.u.create2.phys, "%s", kPhys);
    std::snprintf(reinterpret_cast<char*>(ev.u.create2.uniq), sizeof ev.u.create2.uniq, "%s",
                  r->uniq.c_str());
    // USB, not Bluetooth: the kernel's Wii Remote driver binds Bluetooth
    // devices with this id and would start driving the stand-in itself.
    ev.u.create2.bus = BUS_USB;
    ev.u.create2.vendor = kNintendo;
    ev.u.create2.product = kWiiRemote;
    ev.u.create2.rd_size = static_cast<__u16>(rd.size);
    std::memcpy(ev.u.create2.rd_data, rd.value, rd.size);
    if (::write(r->uhid, &ev, sizeof ev) < 0) {
        std::fprintf(stderr, "[wiibridge] stand-in not created: %s\n", std::strerror(errno));
        ::close(r->uhid);
        ::close(r->real);
        return;
    }
    std::fprintf(stderr, "[wiibridge] %s on %s: stand-in created as %04x:%04x \"%s\"\n",
                 r->uniq.c_str(), node.c_str(), kNintendo, kWiiRemote, r->name.c_str());
    gRemotes.push_back(std::move(r));
}

void rescan() {
    DIR* dir = ::opendir("/sys/class/hidraw");
    if (!dir) return;
    while (dirent* e = ::readdir(dir)) {
        const std::string node = e->d_name;
        if (node.rfind("hidraw", 0) != 0) continue;
        bool have = false;
        for (const auto& r : gRemotes) have = have || r->node == node;
        if (have) continue;
        const std::string uevent = readFile("/sys/class/hidraw/" + node + "/device/uevent");
        if (field(uevent, "HID_NAME").rfind(kPrefix, 0) != 0) continue;
        if (field(uevent, "HID_PHYS") == kPhys) continue;                // one of ours
        if (field(uevent, "HID_ID").rfind("0005:", 0) != 0) continue;    // Bluetooth only
        adopt(node, uevent);
    }
    ::closedir(dir);
}

}  // namespace

int main() {
    // ONE BRIDGE: a second would give every Remote two stand-ins.
    const char* rt = std::getenv("XDG_RUNTIME_DIR");
    const std::string lock = std::string(rt && *rt ? rt : "/tmp") + "/cabinetos-wii-bridge.lock";
    const int lockFd = ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (lockFd < 0 || ::flock(lockFd, LOCK_EX | LOCK_NB) != 0) {
        std::fprintf(stderr, "[wiibridge] already running; leaving it to that one\n");
        return 0;
    }
    ::signal(SIGTERM, [](int) { gStop = 1; });
    ::signal(SIGINT, [](int) { gStop = 1; });
    ::signal(SIGPIPE, SIG_IGN);
    ::signal(SIGCHLD, SIG_IGN);   // bluetoothctl, reaped by the kernel
    std::fprintf(stderr, "[wiibridge] started\n");
    auto lastScan = std::chrono::steady_clock::time_point{};
    while (!gStop) {
        if (std::chrono::steady_clock::now() - lastScan > std::chrono::milliseconds(500)) {
            rescan();
            lastScan = std::chrono::steady_clock::now();
        }
        std::vector<pollfd> fds;
        for (const auto& r : gRemotes) {
            fds.push_back({r->real, POLLIN, 0});   // -1 while it is off: poll skips it
            fds.push_back({r->uhid, POLLIN, 0});
        }
        if (fds.empty()) {
            ::usleep(250000);
            continue;
        }
        ::poll(fds.data(), fds.size(), 100);
        for (size_t i = 0; i < gRemotes.size();) {
            Remote& r = *gRemotes[i];
            bool gone = false;
            uint8_t buf[64];
            for (; r.real >= 0;) {
                const ssize_t n = ::read(r.real, buf, sizeof buf);
                if (n > 0) {
                    fromReal(r, buf, static_cast<size_t>(n));
                    continue;
                }
                if (n < 0 && errno != EAGAIN && errno != EINTR) gone = true;
                break;
            }
            for (;;) {
                uhid_event ev{};
                const ssize_t n = ::read(r.uhid, &ev, sizeof ev);
                if (n <= 0) break;
                switch (ev.type) {
                    case UHID_OUTPUT:
                        fromGame(r, ev.u.output.data, ev.u.output.size);
                        break;
                    case UHID_GET_REPORT: {
                        uhid_event rep{};
                        rep.type = UHID_GET_REPORT_REPLY;
                        rep.u.get_report_reply.id = ev.u.get_report.id;
                        rep.u.get_report_reply.err = EIO;
                        (void)::write(r.uhid, &rep, sizeof rep);
                        break;
                    }
                    case UHID_SET_REPORT: {
                        fromGame(r, ev.u.set_report.data, ev.u.set_report.size);
                        uhid_event rep{};
                        rep.type = UHID_SET_REPORT_REPLY;
                        rep.u.set_report_reply.id = ev.u.set_report.id;
                        rep.u.set_report_reply.err = 0;
                        (void)::write(r.uhid, &rep, sizeof rep);
                        break;
                    }
                    default:
                        break;   // START, STOP, OPEN, CLOSE: nothing to do
                }
            }
            checkDropped(r);
            settleAnswers(r);
            if (r.real >= 0 && (gone || ::access(("/dev/" + r.node).c_str(), F_OK) != 0)) detach(r);
            ++i;
        }
    }
    for (auto& r : gRemotes) drop(*r);
    return 0;
}
