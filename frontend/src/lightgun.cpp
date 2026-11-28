#include "lightgun.h"

#include "fileio.h"
#include "libretro.h"
#include "prefs.h"
#include "storage.h"

#include <SDL3/SDL.h>

#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

namespace lightgun {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kProduct = "SindenLightgun";
// Not any real pad's id, nor a Wii Remote's (wiiremote.cpp): SDL's start at 1
// and count up. One per gun, so two guns in the menus are two controllers.
constexpr SDL_JoystickID kGunId = 0x47554E00;
constexpr int64_t kPauseHoldMs = 1000;
// How long a tap of the rear right button is held for the game, from the
// moment it is let go: six frames, which every board's coin input takes.
constexpr int64_t kCoinPulseMs = 100;

// HOW OFTEN THE GUNS ARE LOOKED FOR: every half second, and every 50 ms for
// three seconds after one of a gun's nodes could not be opened. A node udev
// has just made is refused for a moment, until udev's rule grants the console
// access (72-cabinetos-light-gun.rules): on the reference console on
// 2026-11-28 a gun's mouse node was refused at the first look after it came
// back, six times in one morning, and the one left in long enough opened at a
// later look. A node refused for longer is still tried at every look, for as
// long as it is there.
constexpr int64_t kScanMs = 500;
constexpr int64_t kRetryMs = 50;
constexpr int64_t kRetryForMs = 3000;

int64_t nowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
        .count();
}

// --- What the rules decide, with no device -----------------------------------

// THE REAR RIGHT BUTTON (#575 rule 10): coin or Select in a game, and held a
// second the pause menu, in every game. A tap reaches the game as it is let
// go, held for kCoinPulseMs; a hold opens the menu and gives the game nothing,
// so a pause never spends a credit. Out of a game it does nothing at all.
struct RearRight {
    bool down = false;
    int64_t since = 0;
    bool fired = false;          // this hold has opened the menu
    int64_t pulseUntil = 0;      // the coin the game sees, after a tap
};

// `toGame`: a game is playing, not under the pause menu, so a tap is its coin.
void rearRightPress(RearRight& r, bool down, int64_t t, bool toGame) {
    if (down == r.down) return;
    r.down = down;
    if (down) {
        r.since = t;
        r.fired = false;
        return;
    }
    if (toGame && !r.fired) r.pulseUntil = t + kCoinPulseMs;
}

// True once, as a hold reaches a second while a game runs (`inGame`), under
// the pause menu too, where the hold closes it as a Wii Remote's HOME does.
bool rearRightHeld(RearRight& r, int64_t t, bool inGame) {
    if (!r.down || r.fired || !inGame || t - r.since < kPauseHoldMs) return false;
    r.fired = true;
    return true;
}

bool coinOn(const RearRight& r, int64_t t) { return t < r.pulseUntil; }

// THE NEXT LOOK FOR GUNS, in ms from the last: soon while a node refused in
// the last kRetryForMs is still waiting, else at the usual pace. `refused`
// holds each waiting node and when it was first refused.
int64_t scanEvery(const std::map<std::string, int64_t>& refused, int64_t t) {
    for (const auto& [node, since] : refused)
        if (t - since < kRetryForMs) return kRetryMs;
    return kScanMs;
}

// A GUN'S PORT, the key that tells guns apart and remembers their order: its
// controller's PCI address and the chain of hub ports it hangs from, from its
// USB device's place in sysfs. "/sys/devices/pci0000:00/0000:00:08.3/
// 0000:c8:00.0/usb3/3-1/3-1.2/3-1.2.2" is "0000:c8:00.0-1.2.2". Not the bus
// number, which the order controllers are found in can change; a path with no
// `usbN` in it is itself.
std::string portKey(const std::string& usbPath) {
    std::vector<std::string> parts;
    std::stringstream ss(usbPath);
    for (std::string p; std::getline(ss, p, '/');)
        if (!p.empty()) parts.push_back(p);
    for (size_t i = 1; i + 1 < parts.size(); ++i) {
        if (parts[i].rfind("usb", 0) != 0) continue;
        const std::string& last = parts.back();
        const size_t dash = last.find('-');
        return parts[i - 1] + "-" + (dash == std::string::npos ? last : last.substr(dash + 1));
    }
    return usbPath;
}

// WHICH NUMBER A GUN GETS. `slots` holds, for gun 1 and gun 2, the port of
// the gun that has had that number while the console runs, here or away;
// `here` says which of them is plugged in now. A gun that had a number gets
// it back; another takes the first number no gun here holds. -1 when both are
// held by guns that are here.
int place(std::string (&slots)[kMaxGuns], const bool (&here)[kMaxGuns], const std::string& port) {
    for (int s = 0; s < kMaxGuns; ++s)
        if (slots[s] == port) return s;
    for (int s = 0; s < kMaxGuns; ++s)
        if (slots[s].empty() || !here[s]) {
            slots[s] = port;
            return s;
        }
    return -1;
}

// GUNS FOUND TOGETHER (both in as the console starts) take their numbers in
// the order last remembered (`remembered`, ports by commas); a gun never seen
// goes after, by its port.
void orderArrivals(std::vector<std::string>& ports, const std::string& remembered) {
    std::vector<std::string> known;
    std::stringstream ss(remembered);
    for (std::string p; std::getline(ss, p, ',');)
        if (!p.empty()) known.push_back(p);
    auto rank = [&](const std::string& p) {
        const auto it = std::find(known.begin(), known.end(), p);
        return it == known.end() ? known.size() : static_cast<size_t>(it - known.begin());
    };
    std::stable_sort(ports.begin(), ports.end(), [&](const std::string& a, const std::string& b) {
        const size_t ra = rank(a), rb = rank(b);
        return ra != rb ? ra < rb : a < b;
    });
}

// IN THE MENUS a gun is a small controller (#575 rule 4 and rule 10): the
// trigger selects, the pump goes back, the rear left button is Start and the
// D-pad moves. The front right and rear right buttons do nothing there.
struct MenuPress {
    uint16_t bit;
    SDL_GamepadButton button;
};
constexpr MenuPress kMenu[] = {
    {kTrigger, SDL_GAMEPAD_BUTTON_SOUTH},    {kSecond, SDL_GAMEPAD_BUTTON_EAST},
    {kStart, SDL_GAMEPAD_BUTTON_START},      {kUp, SDL_GAMEPAD_BUTTON_DPAD_UP},
    {kDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},   {kLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT},
    {kRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
};

// A key code of the gun's to its button; 0 for one that is none of them.
// kCoin stands for the rear right button as pressed, before the rule above.
// THE REAR BUTTONS IN EITHER OF SINDEN'S TWO SETS: its software gives the gun
// it numbers 1 the keys 1 and 5 (MAME's player 1 Start and coin) and the gun
// it numbers 2 the keys 2 and 6 (its config file's ButtonRearLeftP2 and
// ButtonRearRightP2), and the gun keeps what it was given. Which number a gun
// had is Sinden's order, not ours, so both sets are read from every gun: each
// gun's own nodes already say which gun pressed.
uint16_t buttonOf(unsigned code) {
    switch (code) {
        case BTN_LEFT: return kTrigger;
        case BTN_RIGHT: return kSecond;
        case BTN_MIDDLE: return kThird;
        case KEY_1:
        case KEY_2: return kStart;
        case KEY_5:
        case KEY_6: return kCoin;
        case KEY_UP: return kUp;
        case KEY_DOWN: return kDown;
        case KEY_LEFT: return kLeft;
        case KEY_RIGHT: return kRight;
        default: return 0;
    }
}

// --- The guns plugged in --------------------------------------------------------

struct Device {
    std::string usb;       // its USB device in sysfs
    std::string port;      // portKey
    std::string id;        // its USB id, "16c0:0f01"
    int slot = -1;
    // The node with the aim (ABS_X, ABS_Y) and the one with the keys; the
    // trigger and the pump are on the first.
    std::string aimNode, keysNode;   // "event15"
    int aimFd = -1, keysFd = -1;
    int minX = 0, maxX = 32767, minY = 0, maxY = 32767;
    int rawX = 16384, rawY = 16384;
    uint16_t held = 0;               // as pressed now
    uint16_t pushed = 0;             // what the menus were told is down
    // HELD ACROSS THE CHANGE between the menus and a game, kept from the
    // side it was not pressed on until it is let go (as a pad's are, #103):
    // the trigger held as the pause menu opens is no Select, and the one
    // that pressed Resume is no shot.
    uint16_t keptFromMenus = 0, keptFromGame = 0;
    RearRight rear;
    // THE SIGHT (lightgun.h, Sight): each change between a reading on the
    // screen and one at its edge, oldest first, as (when, on the screen); the
    // latest is how it is now. And each trigger pull, with how it was then.
    // Kept for kSightKeepMs, with the change before them.
    std::deque<std::pair<int64_t, bool>> seen, shots;
    // The D-pad held left since then, -1 while it is not; and whether this
    // hold has been told (takeCalibrateHold).
    int64_t leftSince = -1;
    bool leftTold = false;
};

// ON THE SCREEN: in between on both axes. Sinden's readings off the border's
// frame, or with the border not seen, are exactly the ends of an axis.
bool onScreen(int x, int y, int minX, int maxX, int minY, int maxY) {
    return x > minX && x < maxX && y > minY && y < maxY;
}
bool onScreen(const Device& d) { return onScreen(d.rawX, d.rawY, d.minX, d.maxX, d.minY, d.maxY); }

// AT THE MIDDLE to within one step on both axes: 16383 or 16384 of 0..32767.
bool atMiddle(int x, int y, int minX, int maxX, int minY, int maxY) {
    return std::abs(2 * x - (minX + maxX)) <= 2 && std::abs(2 * y - (minY + maxY)) <= 2;
}

// The latest reading's class into the time line, and what is older than
// kSightKeepMs out of it (but the change the window starts in).
void noteSight(Device& d, int64_t t) {
    const bool on = onScreen(d);
    if (d.seen.empty() || d.seen.back().second != on) d.seen.emplace_back(t, on);
    while (d.seen.size() > 1 && d.seen[1].first <= t - kSightKeepMs) d.seen.pop_front();
    while (!d.shots.empty() && d.shots.front().first <= t - kSightKeepMs) d.shots.pop_front();
}

// A gun's sight from `from` to `now`, out of its time line: before its first
// reading nothing is counted, either way.
Sight sightOf(const std::deque<std::pair<int64_t, bool>>& seen,
              const std::deque<std::pair<int64_t, bool>>& shots, int64_t from, int64_t now) {
    Sight s;
    for (size_t i = 0; i < seen.size(); ++i) {
        const int64_t a = std::max(seen[i].first, from);
        const int64_t b = i + 1 < seen.size() ? std::min(seen[i + 1].first, now) : now;
        if (b > a) (seen[i].second ? s.onMs : s.offMs) += b - a;
        if (i > 0 && !seen[i].second && seen[i - 1].second && seen[i].first >= from &&
            seen[i].first <= now)
            ++s.flips;
    }
    for (const auto& [t, on] : shots) {
        if (t < from || t > now) continue;
        ++s.shots;
        if (!on) ++s.shotsOff;
    }
    return s;
}

std::mutex gLock;
std::vector<Device> gDevices;
std::string gSlots[kMaxGuns];
std::atomic<int> gCount{0};
std::atomic<bool> gInMenus{true};
std::atomic<bool> gInGame{false};
std::atomic<bool> gPauseHold{false};
std::atomic<bool> gPressInGame{false};
// The gun whose D-pad was held left long enough, not yet taken; -1 for none.
std::atomic<int> gCalibrateHold{-1};
// The guns a capture says are in (pretendForCapture); 0 for the real ones.
std::atomic<unsigned> gPretend{0};
std::once_flag gStarted;
// Each gun's node that could not be opened yet, and when it was first
// refused, while it is there (scanEvery). Under gLock.
std::map<std::string, int64_t> gRefused;
// Where the guns are looked for; the self-test points them at a tree of its own.
std::string gSysInput = "/sys/class/input";
std::string gDevInput = "/dev/input";

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    std::string s = ss.str();
    while (!s.empty() && (s.back() == '\n' || s.back() == ' ')) s.pop_back();
    return s;
}

// The USB device an input node hangs from: the nearest folder above it in
// sysfs with an idVendor of its own. Empty when there is none.
std::string usbDeviceOf(const std::string& eventNode) {
    char real[PATH_MAX];
    if (!::realpath((gSysInput + "/" + eventNode).c_str(), real)) return {};
    std::string p = real;
    while (p.size() > 1) {
        const size_t slash = p.find_last_of('/');
        if (slash == std::string::npos || slash == 0) break;
        p.erase(slash);
        if (::access((p + "/idVendor").c_str(), F_OK) == 0) return p;
    }
    return {};
}

void pushButton(int slot, SDL_GamepadButton b, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    ev.gbutton.timestamp = SDL_GetTicksNS();
    ev.gbutton.which = kGunId + static_cast<SDL_JoystickID>(slot);
    ev.gbutton.button = static_cast<Uint8>(b);
    ev.gbutton.down = down;
    SDL_PushEvent(&ev);
}

// The menus are told what is held, and only in the menus: a button that went
// down in a game is never a menu press, and one the menus were told is down
// is let go as the game takes the gun back.
void tellMenus(Device& d) {
    const uint16_t want = gInMenus ? static_cast<uint16_t>(d.held & ~d.keptFromMenus) : 0;
    for (const MenuPress& m : kMenu) {
        const bool now = (want & m.bit) != 0, was = (d.pushed & m.bit) != 0;
        if (now && !was && gInMenus) {
            pushButton(d.slot, m.button, true);
            d.pushed |= m.bit;
        } else if (!now && was) {
            pushButton(d.slot, m.button, false);
            d.pushed &= static_cast<uint16_t>(~m.bit);
        }
    }
}

void closeNode(int& fd, std::string& node) {
    if (fd >= 0) {
        ::ioctl(fd, EVIOCGRAB, 0);
        ::close(fd);
    }
    fd = -1;
    node.clear();
}

void readRanges(Device& d) {
    input_absinfo a{};
    if (::ioctl(d.aimFd, EVIOCGABS(ABS_X), &a) == 0 && a.maximum > a.minimum) {
        d.minX = a.minimum;
        d.maxX = a.maximum;
        d.rawX = a.value;
    }
    if (::ioctl(d.aimFd, EVIOCGABS(ABS_Y), &a) == 0 && a.maximum > a.minimum) {
        d.minY = a.minimum;
        d.maxY = a.maximum;
        d.rawY = a.value;
    }
}

void saveOrder() {
    std::string order;
    for (const std::string& s : gSlots) {
        if (s.empty()) continue;
        if (!order.empty()) order += ",";
        order += s;
    }
    if (prefs::get("lightgun_order", "") != order) prefs::set("lightgun_order", order);
}

// Guns in, guns out. Called with gLock held.
void rescan() {
    bool changed = false;
    // GONE: a node no longer there, or no longer the same device.
    for (Device& d : gDevices) {
        for (auto [fd, node] : {std::pair<int*, std::string*>{&d.aimFd, &d.aimNode},
                                std::pair<int*, std::string*>{&d.keysFd, &d.keysNode}}) {
            if (*fd < 0) continue;
            if (usbDeviceOf(*node) != d.usb) {
                closeNode(*fd, *node);
                changed = true;
            }
        }
    }
    for (auto it = gDevices.begin(); it != gDevices.end();) {
        if (it->aimFd < 0 && it->keysFd < 0) {
            std::fprintf(stderr, "[lightgun] gun %d left (%s)\n", it->slot + 1, it->port.c_str());
            for (const MenuPress& m : kMenu)
                if (it->pushed & m.bit) pushButton(it->slot, m.button, false);
            it = gDevices.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    // NEW: every event node whose USB device says it is a Sinden gun. One
    // that cannot be opened yet (udev has not granted it, or anything else)
    // is said once and tried again at every look until it opens or goes, so
    // a gun that is in without its aim keeps looking for it; once it opens
    // it is grabbed, read and handed on as if it had opened at once.
    std::vector<std::string> arrived;
    std::set<std::string> waiting;
    DIR* dir = ::opendir(gSysInput.c_str());
    if (!dir) return;
    while (dirent* e = ::readdir(dir)) {
        const std::string node = e->d_name;
        if (node.rfind("event", 0) != 0) continue;
        bool open = false;
        for (const Device& d : gDevices) open = open || d.aimNode == node || d.keysNode == node;
        if (open) continue;
        const std::string usb = usbDeviceOf(node);
        if (usb.empty() || readFile(usb + "/product") != kProduct) continue;
        // Its mouse has the aim and the trigger, its keyboard the rest. Any
        // other device it makes (Sinden's joystick mode) is none of ours.
        const std::string name = readFile(gSysInput + "/" + node + "/device/name");
        const bool aim = name.size() > 6 && name.compare(name.size() - 6, 6, " Mouse") == 0;
        const bool keys = name.size() > 9 && name.compare(name.size() - 9, 9, " Keyboard") == 0;
        if (!aim && !keys) continue;
        auto d = std::find_if(gDevices.begin(), gDevices.end(),
                              [&](const Device& x) { return x.usb == usb; });
        // A second of the same kind under one gun: the first stays.
        if (d != gDevices.end() && (aim ? d->aimFd : d->keysFd) >= 0) continue;
        const std::string path = gDevInput + "/" + node;
        const int fd = ::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) {
            const int err = errno;
            waiting.insert(node);
            if (gRefused.emplace(node, nowMs()).second)
                std::fprintf(stderr, "[lightgun] cannot open %s yet: %s; trying again\n",
                             path.c_str(), std::strerror(err));
            continue;
        }
        if (::ioctl(fd, EVIOCGRAB, 1) != 0)
            std::fprintf(stderr, "[lightgun] could not keep %s to itself: %s\n", path.c_str(),
                         std::strerror(errno));
        if (const auto r = gRefused.find(node); r != gRefused.end()) {
            if (d != gDevices.end() && d->slot >= 0)
                std::fprintf(stderr,
                             "[lightgun] gun %d's %s is in (%s), %lld ms after it was refused\n",
                             d->slot + 1, aim ? "aim" : "keys", node.c_str(),
                             static_cast<long long>(nowMs() - r->second));
            gRefused.erase(r);
        }
        if (d == gDevices.end()) {
            Device n;
            n.usb = usb;
            n.port = portKey(usb);
            n.id = readFile(usb + "/idVendor") + ":" + readFile(usb + "/idProduct");
            gDevices.push_back(std::move(n));
            d = gDevices.end() - 1;
            arrived.push_back(d->port);
        }
        (aim ? d->aimFd : d->keysFd) = fd;
        (aim ? d->aimNode : d->keysNode) = node;
        if (aim) {
            readRanges(*d);
            noteSight(*d, nowMs());
        }
        changed = true;
    }
    ::closedir(dir);
    // A refused node that has gone is forgotten: if its name comes again, it
    // is a new node, said again.
    for (auto it = gRefused.begin(); it != gRefused.end();)
        it = waiting.count(it->first) ? std::next(it) : gRefused.erase(it);
    // Numbers for the guns that came, in the remembered order when more than
    // one came at once.
    if (!arrived.empty()) orderArrivals(arrived, prefs::get("lightgun_order", ""));
    for (const std::string& port : arrived) {
        bool here[kMaxGuns] = {};
        for (const Device& d : gDevices)
            if (d.slot >= 0) here[d.slot] = true;
        for (Device& d : gDevices) {
            if (d.port != port) continue;
            d.slot = place(gSlots, here, port);
            if (d.slot < 0)
                std::fprintf(stderr, "[lightgun] a third gun at %s: not used\n", port.c_str());
            else
                std::fprintf(stderr, "[lightgun] gun %d is in at %s (%s, %s)\n", d.slot + 1,
                             port.c_str(), d.aimNode.empty() ? "no aim yet" : d.aimNode.c_str(),
                             d.keysNode.empty() ? "no keys yet" : d.keysNode.c_str());
        }
    }
    if (!arrived.empty()) saveOrder();
    int n = 0;
    for (const Device& d : gDevices) n += d.slot >= 0 && d.aimFd >= 0;
    if (changed || n != gCount) gCount = n;
}

void handle(Device& d, const input_event& ev, int64_t t) {
    if (ev.type == EV_SYN) {
        if (ev.code == SYN_REPORT) noteSight(d, t);
        return;
    }
    if (ev.type == EV_ABS) {
        if (ev.code == ABS_X) d.rawX = ev.value;
        if (ev.code == ABS_Y) d.rawY = ev.value;
        return;
    }
    if (ev.type != EV_KEY || ev.value == 2) return;
    const uint16_t b = buttonOf(ev.code);
    if (!b) return;
    const bool down = ev.value != 0;
    if (down && !gInMenus) gPressInGame = true;
    if (b == kCoin) rearRightPress(d.rear, down, t, gInGame && !gInMenus);
    if (b == kTrigger && down) d.shots.emplace_back(t, onScreen(d));
    if (b == kLeft) {
        d.leftSince = down ? t : -1;
        d.leftTold = false;
    }
    if (down) {
        d.held |= b;
    } else {
        d.held &= static_cast<uint16_t>(~b);
        d.keptFromMenus &= static_cast<uint16_t>(~b);
        d.keptFromGame &= static_cast<uint16_t>(~b);
    }
}

// True once per hold, as the D-pad has been held left kCalibrateHoldMs.
bool heldLeft(Device& d, int64_t t) {
    if (d.leftSince < 0 || d.leftTold || t - d.leftSince < kCalibrateHoldMs) return false;
    d.leftTold = true;
    return true;
}

// A node that dropped events (SYN_DROPPED) is read whole again.
void resync(Device& d, int fd) {
    unsigned char keys[KEY_MAX / 8 + 1] = {};
    if (::ioctl(fd, EVIOCGKEY(sizeof keys), keys) >= 0) {
        // Both of a pair (KEY_1 and KEY_2 are one button) down if either is.
        uint16_t now = 0;
        for (unsigned code : {BTN_LEFT, BTN_RIGHT, BTN_MIDDLE, KEY_1, KEY_2, KEY_5, KEY_6, KEY_UP,
                              KEY_DOWN, KEY_LEFT, KEY_RIGHT}) {
            const bool onThisNode = fd == d.aimFd ? code >= BTN_LEFT : code < BTN_LEFT;
            if (onThisNode && ((keys[code / 8] >> (code % 8)) & 1)) now |= buttonOf(code);
        }
        const uint16_t mine = fd == d.aimFd ? static_cast<uint16_t>(kTrigger | kSecond | kThird)
                                            : static_cast<uint16_t>(~(kTrigger | kSecond | kThird));
        d.held = static_cast<uint16_t>((d.held & ~mine) | (now & mine));
        rearRightPress(d.rear, (d.held & kCoin) != 0, nowMs(), gInGame && !gInMenus);
        // A button let go while events were dropped is no longer kept.
        d.keptFromMenus &= d.held;
        d.keptFromGame &= d.held;
    }
    if (fd == d.aimFd) {
        readRanges(d);
        noteSight(d, nowMs());
    }
    // The D-pad let go of while events were dropped is no longer held.
    if (!(d.held & kLeft)) d.leftSince = -1;
}

// Reads what is waiting; false when the node has gone (unplugged).
bool drain(Device& d, int fd, int64_t t) {
    input_event evs[64];
    bool dropped = false;
    for (;;) {
        const ssize_t n = ::read(fd, evs, sizeof evs);
        if (n < 0) {
            if (errno == EAGAIN || errno == EINTR) break;
            return false;   // ENODEV: it went
        }
        if (n == 0) return false;
        const size_t count = static_cast<size_t>(n) / sizeof(input_event);
        for (size_t i = 0; i < count; ++i) {
            if (evs[i].type == EV_SYN && evs[i].code == SYN_DROPPED) dropped = true;
            else if (!dropped) handle(d, evs[i], t);
            else if (evs[i].type == EV_SYN && evs[i].code == SYN_REPORT) {
                dropped = false;
                resync(d, fd);
            }
        }
    }
    return true;
}

void run() {
    int64_t lastScan = 0;
    for (;;) {
        std::vector<pollfd> fds;
        int64_t every = kScanMs;
        {
            std::lock_guard<std::mutex> lk(gLock);
            if (nowMs() - lastScan >= scanEvery(gRefused, nowMs())) {
                rescan();
                lastScan = nowMs();
            }
            every = scanEvery(gRefused, nowMs());
            for (const Device& d : gDevices) {
                if (d.aimFd >= 0) fds.push_back({d.aimFd, POLLIN, 0});
                if (d.keysFd >= 0) fds.push_back({d.keysFd, POLLIN, 0});
            }
        }
        if (fds.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(std::min<int64_t>(every, 250)));
            continue;
        }
        ::poll(fds.data(), fds.size(), 50);
        std::lock_guard<std::mutex> lk(gLock);
        const int64_t t = nowMs();
        for (Device& d : gDevices) {
            if (d.slot < 0) continue;
            if (d.aimFd >= 0 && !drain(d, d.aimFd, t)) closeNode(d.aimFd, d.aimNode);
            if (d.keysFd >= 0 && !drain(d, d.keysFd, t)) closeNode(d.keysFd, d.keysNode);
            if (rearRightHeld(d.rear, t, gInGame)) {
                gPauseHold = true;
                std::fprintf(stderr, "[lightgun] rear right held on gun %d\n", d.slot + 1);
            }
            if (heldLeft(d, t)) {
                gCalibrateHold = d.slot;
                std::fprintf(stderr, "[lightgun] D-pad held left %lld ms on gun %d; aim %s\n",
                             static_cast<long long>(kCalibrateHoldMs), d.slot + 1,
                             atMiddle(d.rawX, d.rawY, d.minX, d.maxX, d.minY, d.maxY)
                                 ? "at the middle"
                                 : "not at the middle");
            }
            tellMenus(d);
        }
    }
}

}  // namespace

// --- Public ------------------------------------------------------------------------

void start() {
    std::call_once(gStarted, [] { std::thread(run).detach(); });
}

int count() {
    if (const unsigned p = gPretend) return __builtin_popcount(p);
    return gCount;
}

unsigned plugged() {
    if (const unsigned p = gPretend) return p;
    unsigned bits = 0;
    std::lock_guard<std::mutex> lk(gLock);
    for (const Device& d : gDevices)
        if (d.slot >= 0 && d.aimFd >= 0) bits |= 1u << d.slot;
    return bits;
}

Gun gun(int n) {
    Gun g;
    std::lock_guard<std::mutex> lk(gLock);
    const int64_t t = nowMs();
    for (const Device& d : gDevices) {
        if (d.slot != n || d.aimFd < 0) continue;
        g.present = true;
        g.x = std::clamp(static_cast<float>(d.rawX - d.minX) / static_cast<float>(d.maxX - d.minX),
                         0.0f, 1.0f);
        g.y = std::clamp(static_cast<float>(d.rawY - d.minY) / static_cast<float>(d.maxY - d.minY),
                         0.0f, 1.0f);
        g.buttons = static_cast<uint16_t>(d.held & ~d.keptFromGame & ~kCoin);
        if (coinOn(d.rear, t)) g.buttons |= kCoin;
    }
    return g;
}

void setInMenus(bool inMenus) {
    if (gInMenus.exchange(inMenus) == inMenus) return;
    std::lock_guard<std::mutex> lk(gLock);
    for (Device& d : gDevices) {
        if (d.slot < 0) continue;
        if (inMenus) d.keptFromMenus = d.held;
        else d.keptFromGame = d.held;
        tellMenus(d);
        // A hold that began in the menus or in the game is over for the
        // other: nothing it started carries across.
        d.rear.fired = true;
        d.rear.pulseUntil = 0;
    }
}

void setInGame(bool inGame) {
    if (gInGame.exchange(inGame) == inGame) return;
    gPauseHold = false;
}

bool takePauseHold() { return gPauseHold.exchange(false); }

bool takePressInGame() { return gPressInGame.exchange(false); }

bool isGun(uint32_t padId) { return gunOf(padId) >= 0; }

int gunOf(uint32_t padId) {
    return padId >= kGunId && padId < kGunId + kMaxGuns ? static_cast<int>(padId - kGunId) : -1;
}

void pretendForCapture(unsigned plugged) { gPretend = plugged & ((1u << kMaxGuns) - 1); }

int64_t clockMs() { return nowMs(); }

Sight sight(int n, int64_t fromMs) {
    std::lock_guard<std::mutex> lk(gLock);
    for (const Device& d : gDevices) {
        if (d.slot != n || d.aimFd < 0) continue;
        Sight s = sightOf(d.seen, d.shots, fromMs, nowMs());
        s.present = true;
        return s;
    }
    return {};
}

bool takeCalibrateHold(int* n) {
    const int g = gCalibrateHold.exchange(-1);
    if (g < 0) return false;
    *n = g;
    return true;
}

bool parked(int n) {
    std::lock_guard<std::mutex> lk(gLock);
    for (const Device& d : gDevices)
        if (d.slot == n && d.aimFd >= 0)
            return atMiddle(d.rawX, d.rawY, d.minX, d.maxX, d.minY, d.maxY);
    return false;
}

Where where(int n) {
    std::lock_guard<std::mutex> lk(gLock);
    for (const Device& d : gDevices)
        if (d.slot == n && d.aimFd >= 0) return {d.usb, d.port, d.id};
    return {};
}

bool recoil() { return prefs::get("lightgun_recoil", "off") == "on"; }

void setRecoil(bool on) { prefs::set("lightgun_recoil", on ? "on" : "off"); }

bool onPicture(float x, float y, float picX, float picY, float picW, float picH, int16_t* outX,
               int16_t* outY) {
    if (picW <= 0.0f || picH <= 0.0f) return false;
    // A hair of slack at the edges, for the float sums that put them there.
    constexpr float kSlack = 1e-5f;
    const float u = (x - picX) / picW, v = (y - picY) / picH;
    if (u < -kSlack || u > 1.0f + kSlack || v < -kSlack || v > 1.0f + kSlack) return false;
    *outX = static_cast<int16_t>(std::lround(std::clamp(u, 0.0f, 1.0f) * 65534.0f) - 32767);
    *outY = static_cast<int16_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 65534.0f) - 32767);
    return true;
}

// FinalBurn Neo offers a "Lightgun" on every player port (its SetControllerInfo
// at our pin), and with it reads the aim as libretro's screen position and
// fire 1, fire 2, fire 3, fire 4, Start, coin and the D-pad from the light gun
// (retro_input.cpp, GameInpSpecialOne).
unsigned deviceFor(const std::string& core) {
    if (core == "fbneo_libretro") return RETRO_DEVICE_LIGHTGUN;
    return 0;
}

int test() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    // ---- The rear right button
    {
        RearRight r;
        rearRightPress(r, true, 1000, true);
        const bool earlyHold = rearRightHeld(r, 1500, true);
        const bool whileHeld = coinOn(r, 1500);
        rearRightPress(r, false, 1600, true);
        check(!earlyHold && !whileHeld && coinOn(r, 1600) && coinOn(r, 1699) && !coinOn(r, 1700),
              "a tap of the rear right button gives the game a coin as it is let go, for 100 ms, "
              "and nothing while it is held");
        RearRight h;
        rearRightPress(h, true, 1000, true);
        const bool at999 = rearRightHeld(h, 1999, true);
        const bool at1000 = rearRightHeld(h, 2000, true);
        const bool again = rearRightHeld(h, 2500, true);
        rearRightPress(h, false, 2600, true);
        check(!at999 && at1000 && !again && !coinOn(h, 2600),
              "held a second it opens the pause menu once, and spends no coin");
        RearRight m;
        rearRightPress(m, true, 1000, false);
        const bool menuHold = rearRightHeld(m, 3000, false);
        rearRightPress(m, false, 3100, false);
        check(!menuHold && !coinOn(m, 3100), "out of a game it does nothing, held or tapped");
    }

    // ---- Which gun is which
    {
        check(portKey("/sys/devices/pci0000:00/0000:00:08.3/0000:c8:00.0/usb3/3-1/3-1.2/3-1.2.2") ==
                  "0000:c8:00.0-1.2.2",
              "a gun's port is its controller's PCI address and its hub ports, not the bus number");
        check(portKey("/sys/devices/pci0000:00/0000:00:14.0/usb1/1-4") == "0000:00:14.0-4",
              "and a gun in a port of the PC itself is that port");
        std::string slots[kMaxGuns];
        bool here[kMaxGuns] = {};
        const int a = place(slots, here, "A");
        here[0] = true;
        const int b = place(slots, here, "B");
        here[1] = true;
        check(a == 0 && b == 1, "the first gun on is gun 1 and the next gun 2");
        here[0] = false;   // A drops off USB
        const int back = place(slots, here, "A");
        check(back == 0, "a gun that drops off USB and comes back gets its own number back");
        here[0] = false;
        const int other = place(slots, here, "C");
        check(other == 0 && slots[1] == "B",
              "another gun in its place while it is away takes the number no gun here holds");
        here[0] = true;
        check(place(slots, here, "D") == -1, "a third gun gets no number");
        std::vector<std::string> both = {"B", "A"};
        orderArrivals(both, "A,B");
        std::vector<std::string> fresh = {"Z", "Y"};
        orderArrivals(fresh, "");
        std::vector<std::string> one = {"N", "A"};
        orderArrivals(one, "A,B");
        check(both == std::vector<std::string>({"A", "B"}) &&
                  fresh == std::vector<std::string>({"Y", "Z"}) &&
                  one == std::vector<std::string>({"A", "N"}),
              "guns found at once are numbered as last time, a gun never seen after them by its "
              "port");
    }

    // ---- The aim on the picture
    {
        // A 4:3 picture inside the border on a 16:9 screen: 1382.4 by 1036.8
        // canvas points of 1920x1080, in the middle.
        const float h = 1.0f - 2.0f * kBorderShare;
        const float w = h * (4.0f / 3.0f) * (1080.0f / 1920.0f);
        const float px = (1.0f - w) / 2.0f, py = kBorderShare;
        int16_t x = 0, y = 0;
        const bool mid = onPicture(0.5f, 0.5f, px, py, w, h, &x, &y);
        check(mid && x == 0 && y == 0, "the middle of the screen is the middle of the picture");
        const bool tl = onPicture(px, py, px, py, w, h, &x, &y);
        const bool tlOk = tl && x == -32767 && y == -32767;
        const bool br = onPicture(px + w, py + h, px, py, w, h, &x, &y);
        check(tlOk && br && x == 32767 && y == 32767,
              "the picture's corners are -32767 and 32767, whatever its shape and place");
        check(!onPicture(0.1f, 0.5f, px, py, w, h, &x, &y),
              "aiming at the bar beside a 4:3 picture is off the picture: a reload where a game "
              "takes one");
        check(!onPicture(0.5f, 0.005f, px, py, w, h, &x, &y),
              "and so is aiming at the border");
        check(!onPicture(0.5f, 0.5f, 0, 0, 0, 0, &x, &y), "no picture drawn is off the picture");
    }

    // ---- The sight: a reading on the screen or at its edge (#575 rule 12)
    {
        check(onScreen(1, 1, 0, 32767, 0, 32767) && onScreen(32766, 16000, 0, 32767, 0, 32767) &&
                  !onScreen(0, 16000, 0, 32767, 0, 32767) &&
                  !onScreen(16000, 32767, 0, 32767, 0, 32767) &&
                  !onScreen(32767, 0, 0, 32767, 0, 32767),
              "a reading at either end of an axis is at the screen's edge, one in between on it");
        check(atMiddle(16383, 16384, 0, 32767, 0, 32767) &&
                  atMiddle(16384, 16383, 0, 32767, 0, 32767) &&
                  !atMiddle(16385, 16384, 0, 32767, 0, 32767) &&
                  !atMiddle(16383, 16381, 0, 32767, 0, 32767),
              "the middle Sinden holds the aim at while it calibrates is 16383 or 16384 on both "
              "axes, and nothing further");
        using Line = std::deque<std::pair<int64_t, bool>>;
        // On from 1000, lost at 1500, back at 1600, lost at 1700, back at 1900;
        // shots at 1550 (lost), 1650 and 2100.
        const Line seen = {{1000, true}, {1500, false}, {1600, true}, {1700, false}, {1900, true}};
        const Line shots = {{1550, false}, {1650, true}, {2100, true}};
        const Sight s = sightOf(seen, shots, 1200, 2200);
        check(s.onMs == 700 && s.offMs == 300 && s.flips == 2 && s.shots == 3 && s.shotsOff == 1,
              "a window's time on the screen and at its edge, its flips from one to the other and "
              "its shots, from the changes the gun reported");
        const Sight early = sightOf(seen, shots, 0, 900);
        check(early.onMs == 0 && early.offMs == 0 && early.flips == 0 && early.shots == 0,
              "before the gun's first reading nothing is counted either way");
        const Sight still = sightOf(Line{{1000, true}}, Line{}, 2000, 9000);
        check(still.onMs == 7000 && still.offMs == 0 && still.flips == 0,
              "a gun held still on the screen reports nothing and is on it all the while");
        Device d;
        d.rawX = 16000;
        d.rawY = 9000;
        noteSight(d, 0);
        d.rawX = 0;
        noteSight(d, 4000);
        noteSight(d, 4100);   // the same class: no new change
        d.rawX = 20000;
        noteSight(d, 12000);
        const size_t at12 = d.seen.size();   // the window from 2000 starts in the first
        noteSight(d, 15000);                 // from 5000: in the second
        check(at12 == 3 && d.seen.size() == 2 && d.seen[0].first == 4000 && !d.seen[0].second &&
                  d.seen[1].first == 12000 && d.seen[1].second,
              "only changes are kept, and what is older than ten seconds goes but for the change "
              "the window starts in");
    }

    // ---- The D-pad held left: Sinden's calibration (#575 rule 13)
    {
        Device d;
        d.slot = 0;
        input_event left{};
        left.type = EV_KEY;
        left.code = KEY_LEFT;
        left.value = 1;
        handle(d, left, 1000);
        const bool early = heldLeft(d, 3999);
        const bool on = heldLeft(d, 4000);
        const bool again = heldLeft(d, 6000);
        left.value = 0;
        handle(d, left, 6100);
        left.value = 1;
        handle(d, left, 7000);
        const bool tap = heldLeft(d, 7500);
        left.value = 0;
        handle(d, left, 7600);
        const bool after = heldLeft(d, 11000);
        check(!early && on && !again && !tap && !after,
              "the D-pad held left three seconds is told once per hold, and a shorter press "
              "never");
        input_event pull{};
        pull.type = EV_KEY;
        pull.code = BTN_LEFT;
        pull.value = 1;
        d.rawX = 0;
        handle(d, pull, 8000);
        check(d.shots.size() == 1 && d.shots[0].first == 8000 && !d.shots[0].second,
              "a trigger pull is kept with where the aim was: at the edge here");
    }

    // ---- The keys Sinden gives each of its two guns
    check(buttonOf(KEY_1) == kStart && buttonOf(KEY_2) == kStart && buttonOf(KEY_5) == kCoin &&
              buttonOf(KEY_6) == kCoin,
          "the rear buttons are read in Sinden's player 1 and player 2 keys alike, so a gun "
          "Sinden numbered 2 still has its Start and coin");
    check(buttonOf(BTN_LEFT) == kTrigger && buttonOf(BTN_RIGHT) == kSecond &&
              buttonOf(BTN_MIDDLE) == kThird && buttonOf(KEY_UP) == kUp &&
              buttonOf(KEY_LEFT) == kLeft && buttonOf(KEY_3) == 0,
          "the trigger, the pump, the front right button and the D-pad as Sinden sets them, and "
          "no other key");

    // ---- A node that cannot be opened yet (2026-11-28), on a gun laid out
    // in a scratch folder as sysfs has one: its USB device, its keyboard's
    // and its mouse's input devices, and a stand-in node for each, a pipe the
    // test writes events into (a pipe cannot be grabbed: the console says so
    // and reads it all the same). The mouse's node is missing at first, which
    // refuses it for root too, as udev's grant not yet given refuses the
    // console.
    {
        char tmpl[] = "/tmp/cabinetos-gun-XXXXXX";
        const char* made = ::mkdtemp(tmpl);
        check(made != nullptr, "a scratch folder");
        if (made) {
            const std::string root = made;
            storage::setRoot(root);   // where the gun's order is written
            const std::string usb =
                root + "/devices/pci0000:00/0000:c8:00.0/usb3/3-1/3-1.2/3-1.2.2";
            const std::string input = usb + "/3-1.2.2:1.2/0003:16C0:0F01.0012/input";
            cab::writeBytes(usb + "/idVendor", std::string("16c0\n"));
            cab::writeBytes(usb + "/product", std::string(kProduct) + "\n");
            cab::writeBytes(input + "/input47/name",
                            std::string("Unknown SindenLightgun Keyboard\n"));
            cab::writeBytes(input + "/input48/name",
                            std::string("Unknown SindenLightgun Mouse\n"));
            std::error_code ec;
            std::filesystem::create_directories(root + "/class/input", ec);
            std::filesystem::create_directories(root + "/dev", ec);
            auto sysNode = [&](const std::string& node, const std::string& dev) {
                const std::string at = input + "/" + dev + "/" + node;
                std::filesystem::create_directories(at, ec);
                std::filesystem::create_directory_symlink("..", at + "/device", ec);
                std::filesystem::create_directory_symlink(at, root + "/class/input/" + node, ec);
            };
            // The write end, held so that the console's read finds nothing
            // waiting rather than the end.
            auto devNode = [&](const std::string& node) {
                const std::string p = root + "/dev/" + node;
                if (::mkfifo(p.c_str(), 0600) != 0) return -1;
                return ::open(p.c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
            };
            auto look = [] {
                std::lock_guard<std::mutex> lk(gLock);
                rescan();
            };
            gSysInput = root + "/class/input";
            gDevInput = root + "/dev";
            sysNode("event13", "input47");
            sysNode("event14", "input48");
            const int keys = devNode("event13");

            look();
            const auto first = gRefused.find("event14");
            const int64_t since = first == gRefused.end() ? -1 : first->second;
            check(keys >= 0 && first != gRefused.end() && count() == 0 && plugged() == 0 &&
                      gDevices.size() == 1 && gDevices[0].slot == 0 && gDevices[0].keysFd >= 0,
                  "a gun whose mouse cannot be opened yet is gun 1 with its keys, and not yet "
                  "a gun a game is given");
            check(scanEvery(gRefused, since) == kRetryMs &&
                      scanEvery(gRefused, since + kRetryForMs - 1) == kRetryMs,
                  "and the guns are looked for every 50 ms for three seconds after");
            look();
            check(gRefused.size() == 1 && gRefused.begin()->second == since && count() == 0,
                  "still refused at the next look: tried again, and said only the first time");
            check(scanEvery(gRefused, since + kRetryForMs) == kScanMs &&
                      scanEvery({}, since) == kScanMs,
                  "after three seconds, or with nothing refused, every half second, the "
                  "refused node still tried at each look");

            const int aim = devNode("event14");
            look();
            input_event press[2] = {};
            press[0].type = EV_KEY;
            press[0].code = BTN_LEFT;
            press[0].value = 1;
            press[1].type = EV_SYN;
            press[1].code = SYN_REPORT;
            const bool pressed = aim >= 0 && ::write(aim, press, sizeof press) == sizeof press;
            {
                std::lock_guard<std::mutex> lk(gLock);
                for (Device& d : gDevices)
                    if (d.aimFd >= 0) drain(d, d.aimFd, nowMs());
            }
            const Gun g = gun(0);
            check(pressed && count() == 1 && plugged() == 1u && g.present &&
                      (g.buttons & kTrigger) && gRefused.empty() &&
                      scanEvery(gRefused, nowMs()) == kScanMs,
                  "once it opens, gun 1 has its aim and its trigger, as if it had opened at once");

            // Unplugged: both go, and a mouse node left refused when it goes
            // is forgotten, not looked for every 50 ms.
            std::filesystem::remove(root + "/class/input/event13", ec);
            std::filesystem::remove(root + "/class/input/event14", ec);
            look();
            const bool left = gDevices.empty() && count() == 0;
            std::filesystem::remove(root + "/dev/event14", ec);
            sysNode("event14", "input48");
            look();
            const bool refusedAlone = gRefused.count("event14") == 1 && gDevices.empty();
            std::filesystem::remove(root + "/class/input/event14", ec);
            look();
            check(left && refusedAlone && gRefused.empty() &&
                      scanEvery(gRefused, nowMs()) == kScanMs,
                  "a gun unplugged is gone, and a node refused that goes is no longer tried");

            if (keys >= 0) ::close(keys);
            if (aim >= 0) ::close(aim);
            gSysInput = "/sys/class/input";
            gDevInput = "/dev/input";
            std::filesystem::remove_all(root, ec);
        }
    }

    // ---- Which emulators get a gun
    check(deviceFor("fbneo_libretro") == RETRO_DEVICE_LIGHTGUN,
          "FinalBurn Neo is given libretro's light gun");
    check(deviceFor("mame2003_plus") == 0 && deviceFor("fceumm") == 0 && deviceFor("") == 0,
          "no other emulator is given a gun yet");
    check(isGun(kGunId) && isGun(kGunId + 1) && !isGun(kGunId + 2) && !isGun(1) &&
              !isGun(0x57494900) && gunOf(kGunId) == 0 && gunOf(kGunId + 1) == 1 &&
              gunOf(kGunId + 2) == -1 && gunOf(1) == -1,
          "a gun's presses in the menus are told from a pad's and a Wii Remote's, and gun 1's "
          "from gun 2's");

    std::printf("%s\n", failures ? "light-gun test FAILED" : "light-gun test passed");
    return failures ? 1 : 0;
}

}  // namespace lightgun
