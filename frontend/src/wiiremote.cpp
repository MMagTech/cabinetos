#include "wiiremote.h"

#include "prefs.h"
#include "proc.h"
#include "storage.h"
#include "unit.h"

#include <SDL3/SDL.h>
#include <systemd/sd-bus.h>

#include <dirent.h>
#include <signal.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <endian.h>
#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cctype>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <strings.h>
#include <ctime>
#include <fstream>
#include <map>
#include <mutex>
#include <sstream>
#include <thread>

namespace wiiremote {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kPrefix = "Nintendo RVL-CNT";
constexpr const char* kSearchUnit = "cabinetos-wii-search.service";
constexpr const char* kSearchState = "/run/cabinetos-wii-search.state";
// The console's one radio. Dolphin and the kernel's own Wii Remote code use
// the first adapter too.
constexpr const char* kAdapter = "/org/bluez/hci0";
constexpr const char* kAgentPath = "/cabinetos/wiiagent";
// The bridge's stand-ins say this as their HID phys (wiibridge/bridge.cpp).
constexpr const char* kBridgePhys = "cabinetos-wii-bridge";

bool isRemoteName(const std::string& name) { return name.rfind(kPrefix, 0) == 0; }

std::string devicePath(const std::string& address) {
    std::string p = std::string(kAdapter) + "/dev_" + address;
    for (char& c : p)
        if (c == ':') c = '_';
    return p;
}

// --- The search ---------------------------------------------------------------

std::map<std::string, std::string> readState(std::vector<std::string>* remotes) {
    std::map<std::string, std::string> kv;
    std::ifstream in(kSearchState);
    std::string line;
    while (std::getline(in, line)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "remote") {
            if (remotes) remotes->push_back(v);
        } else {
            kv[k] = v;
        }
    }
    return kv;
}

// --- Pairing over D-Bus ---------------------------------------------------------
//
// AN AGENT OF OUR OWN, ON OUR OWN CONNECTION, NOT THE DEFAULT ONE. bluetoothd
// asks the agent of whoever called Pair (src/device.c, pair_device:
// agent_get(sender)), so this one is asked about this pairing and nothing
// else. Every other pad keeps pairing exactly as it did.

struct PairState {
    std::string device;   // the object path being paired
    bool done = false;
    std::string error;    // empty on success
};

int replyOk(sd_bus_message* m, void*, sd_bus_error*) { return sd_bus_reply_method_return(m, ""); }

int replyRejected(sd_bus_message* m, void*, sd_bus_error* e) {
    (void)m;
    return sd_bus_error_set(e, "org.bluez.Error.Rejected", "Only a Wii Remote's PIN is answered here");
}

// THE TECHKEN'S PIN. BlueZ has already offered the Wii PIN by the time it asks
// here; a Remote that took that never reaches this. Answered only for the
// device being paired.
int onRequestPinCode(sd_bus_message* m, void* userdata, sd_bus_error* e) {
    const auto* st = static_cast<const PairState*>(userdata);
    const char* path = nullptr;
    if (sd_bus_message_read(m, "o", &path) < 0 || !path || st->device != path)
        return replyRejected(m, userdata, e);
    std::fprintf(stderr, "[wiiremote] the Wii PIN was refused; answering 0000\n");
    return sd_bus_reply_method_return(m, "s", "0000");
}

const sd_bus_vtable kAgentVtable[] = {
    SD_BUS_VTABLE_START(0),
    SD_BUS_METHOD("Release", "", "", replyOk, 0),
    SD_BUS_METHOD("RequestPinCode", "o", "s", onRequestPinCode, 0),
    SD_BUS_METHOD("DisplayPinCode", "os", "", replyOk, 0),
    SD_BUS_METHOD("RequestPasskey", "o", "u", replyRejected, 0),
    SD_BUS_METHOD("DisplayPasskey", "ouq", "", replyOk, 0),
    SD_BUS_METHOD("RequestConfirmation", "ou", "", replyRejected, 0),
    SD_BUS_METHOD("RequestAuthorization", "o", "", replyRejected, 0),
    SD_BUS_METHOD("AuthorizeService", "os", "", replyOk, 0),
    SD_BUS_METHOD("Cancel", "", "", replyOk, 0),
    SD_BUS_VTABLE_END,
};

bool deviceKnown(sd_bus* bus, const std::string& path) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    char* addr = nullptr;
    const int r = sd_bus_get_property_string(bus, "org.bluez", path.c_str(), "org.bluez.Device1",
                                             "Address", &err, &addr);
    sd_bus_error_free(&err);
    std::free(addr);
    return r >= 0;
}

// MAKING BLUETOOTHD KNOW IT. The limited search ran in another process, and
// bluetoothd forgets what it did not search for itself; a connection to the
// Remote's control channel (L2CAP PSM 0x11) makes it create the device, named.
// Defined here rather than taken from the BlueZ headers the builder does not
// carry; the layout is the kernel's sockaddr_l2.
struct SockaddrL2 {
    sa_family_t family;
    uint16_t psm;
    uint8_t bdaddr[6];
    uint16_t cid;
    uint8_t bdaddrType;
} __attribute__((packed));
constexpr int kAfBluetooth = 31;
constexpr int kBtProtoL2cap = 0;

bool parseAddress(const std::string& s, uint8_t out[6]) {
    unsigned b[6];
    if (std::sscanf(s.c_str(), "%x:%x:%x:%x:%x:%x", &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6)
        return false;
    for (int i = 0; i < 6; ++i) out[i] = static_cast<uint8_t>(b[5 - i]);   // little-endian
    return true;
}

void touchControlChannel(const std::string& address) {
    SockaddrL2 sa{};
    sa.family = kAfBluetooth;
    sa.psm = htole16(0x11);
    if (!parseAddress(address, sa.bdaddr)) return;
    const int fd = ::socket(kAfBluetooth, SOCK_SEQPACKET | SOCK_CLOEXEC, kBtProtoL2cap);
    if (fd < 0) {
        std::fprintf(stderr, "[wiiremote] no Bluetooth socket: %s\n", std::strerror(errno));
        return;
    }
    timeval tv{6, 0};
    ::setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
    if (::connect(fd, reinterpret_cast<sockaddr*>(&sa), sizeof sa) < 0)
        std::fprintf(stderr, "[wiiremote] control channel to %s: %s\n", address.c_str(),
                     std::strerror(errno));
    ::close(fd);
}

// One method call on `bus`, answered while the agent above stays able to
// answer bluetoothd (a blocking call would leave both waiting on each other).
bool callAndWait(sd_bus* bus, const std::string& path, const char* method, int seconds,
                 std::string* error) {
    struct Wait {
        bool done = false;
        std::string error;
    } w;
    sd_bus_message* msg = nullptr;
    if (sd_bus_message_new_method_call(bus, &msg, "org.bluez", path.c_str(), "org.bluez.Device1",
                                       method) < 0) {
        *error = "could not build the call";
        return false;
    }
    sd_bus_slot* slot = nullptr;
    const int r = sd_bus_call_async(
        bus, &slot, msg,
        [](sd_bus_message* m, void* ud, sd_bus_error*) -> int {
            auto* w = static_cast<Wait*>(ud);
            if (sd_bus_message_is_method_error(m, nullptr)) {
                const sd_bus_error* e = sd_bus_message_get_error(m);
                w->error = e && e->name ? e->name : "failed";
            }
            w->done = true;
            return 0;
        },
        &w, static_cast<uint64_t>(seconds) * 1000000u);
    sd_bus_message_unref(msg);
    if (r < 0) {
        *error = std::strerror(-r);
        return false;
    }
    const auto end = Clock::now() + std::chrono::seconds(seconds + 2);
    while (!w.done && Clock::now() < end) {
        if (sd_bus_process(bus, nullptr) > 0) continue;
        sd_bus_wait(bus, 200000);
    }
    sd_bus_slot_unref(slot);
    if (!w.done) {
        *error = "no answer";
        return false;
    }
    *error = w.error;
    return w.error.empty();
}

// --- What is paired -------------------------------------------------------------

std::atomic<bool> gAnyPaired{false};
std::mutex gPairedLock;
std::vector<Paired> gPaired;

std::vector<std::pair<std::string, std::string>> listed(const char* filter) {
    std::vector<std::pair<std::string, std::string>> out;
    const proc::Result r = proc::run({"bluetoothctl", "devices", filter}, 10);
    if (!r.ok()) return out;
    std::istringstream in(r.out);
    std::string line;
    while (std::getline(in, line)) {
        if (line.rfind("Device ", 0) != 0 || line.size() < 7 + 17) continue;
        const std::string address = line.substr(7, 17);
        const std::string name = line.size() > 25 ? line.substr(25) : "";
        if (isRemoteName(name)) out.emplace_back(address, name);
    }
    return out;
}

// --- The running Remotes ----------------------------------------------------------
//
// Each Remote is a hidraw node. Reports read from it start with their report
// id; the core buttons are the two bytes after it in every report that has
// them (WiiBrew, "Wiimote": status 0x20, read 0x21, ack 0x22, data 0x30-0x37,
// 0x3e and 0x3f). Written reports start with the id too, and bit 0 of the
// first byte after it is the rumble motor, kept off.

enum : uint16_t {
    kLeft = 0x0001, kRight = 0x0002, kDown = 0x0004, kUp = 0x0008, kPlus = 0x0010,
    kTwo = 0x0100, kOne = 0x0200, kB = 0x0400, kA = 0x0800, kMinus = 0x1000, kHome = 0x8000,
};

// Upright, as the Wii's own menus are used. A is A and B (the trigger) is
// Back, as on a Wii; 1 and 2 are the on-screen keyboard's delete and shift,
// where a pad's X and Y are; + is Start.
struct Mapping {
    uint16_t bit;
    SDL_GamepadButton button;
};
constexpr Mapping kMap[] = {
    {kUp, SDL_GAMEPAD_BUTTON_DPAD_UP},       {kDown, SDL_GAMEPAD_BUTTON_DPAD_DOWN},
    {kLeft, SDL_GAMEPAD_BUTTON_DPAD_LEFT},   {kRight, SDL_GAMEPAD_BUTTON_DPAD_RIGHT},
    {kA, SDL_GAMEPAD_BUTTON_SOUTH},          {kB, SDL_GAMEPAD_BUTTON_EAST},
    {kOne, SDL_GAMEPAD_BUTTON_WEST},         {kTwo, SDL_GAMEPAD_BUTTON_NORTH},
    {kPlus, SDL_GAMEPAD_BUTTON_START},
};

// Not any real pad's id: SDL's start at 1 and count up.
constexpr SDL_JoystickID kRemoteId = 0x57494900;

constexpr auto kHomeHold = std::chrono::milliseconds(1000);

struct Remote {
    std::string node;      // "hidraw3"
    std::string address;   // from the HID's uniq
    int fd = -1;
    int slot = 0;          // 0..3, its player light
    bool bridged = false;  // the bridge's stand-in, not the Remote itself
    uint16_t buttons = 0;
    Clock::time_point homeSince{};
    bool homeFired = false;
    // ON OR OFF: a stand-in stays when its Remote goes off (the bridge keeps
    // it, wiibridge/bridge.cpp, detach), so the sign is reports: a Remote that
    // is on streams them (continuous mode, setUp).
    Clock::time_point lastReport{};
    Clock::time_point lastActive{};   // the last button change
    bool switchedOff = false;
};

constexpr auto kQuiet = std::chrono::milliseconds(1500);
// A WII'S OWN IDLE TIME: a Remote untouched this long out of a game is switched
// off, as a Wii does (a Wii Remote has no timer of its own; the console turns it
// off). In a game the bridge does it when the game lets the Remote go.
constexpr auto kIdleOff = std::chrono::minutes(5);

bool isOn(const Remote& r) { return Clock::now() - r.lastReport < kQuiet; }

// Asks bluetoothd to disconnect it, which turns a Wii Remote off. Off the
// reading thread: bluetoothctl can take a second.
void switchOff(const std::string& address) {
    std::string a = address;
    for (char& c : a) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    std::thread([a] { proc::run({"bluetoothctl", "disconnect", a}, 10); }).detach();
}

std::mutex gLock;
std::vector<Remote> gRemotes;
std::atomic<bool> gGame{false};
std::atomic<bool> gHomeHold{false};
std::once_flag gStarted;

void send(int fd, std::initializer_list<uint8_t> report) {
    const std::vector<uint8_t> b(report);
    if (::write(fd, b.data(), b.size()) < 0 && errno != EAGAIN)
        std::fprintf(stderr, "[wiiremote] write: %s\n", std::strerror(errno));
}

// What a Wii sends a Remote that has just connected.
void setUp(const Remote& r) {
    send(r.fd, {0x15, 0x00});                                         // status, please
    send(r.fd, {0x11, static_cast<uint8_t>(0x10u << r.slot)});         // its player light
    // Buttons, continuous. Continuous because Dolphin's own reset is the same
    // mode without it (12 00 30), which the bridge reads as a game letting the
    // Remote go (wiibridge/bridge.cpp, checkDropped). A copy streams either way.
    send(r.fd, {0x12, 0x04, 0x30});
}

std::string readFile(const std::string& path) {
    std::ifstream in(path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string ueventField(const std::string& uevent, const char* key) {
    const std::string want = std::string(key) + "=";
    std::istringstream in(uevent);
    std::string line;
    while (std::getline(in, line))
        if (line.rfind(want, 0) == 0) return line.substr(want.size());
    return {};
}

void pushButton(SDL_GamepadButton b, bool down) {
    SDL_Event ev{};
    ev.type = down ? SDL_EVENT_GAMEPAD_BUTTON_DOWN : SDL_EVENT_GAMEPAD_BUTTON_UP;
    ev.gbutton.timestamp = SDL_GetTicksNS();
    ev.gbutton.which = kRemoteId;
    ev.gbutton.button = static_cast<Uint8>(b);
    ev.gbutton.down = down;
    SDL_PushEvent(&ev);
}

// New Remotes in, gone ones out. Called with gLock held.
void rescan() {
    // Gone: its node is no longer there.
    for (auto it = gRemotes.begin(); it != gRemotes.end();) {
        if (::access(("/dev/" + it->node).c_str(), F_OK) != 0) {
            std::fprintf(stderr, "[wiiremote] %s left (light %d)\n", it->address.c_str(), it->slot + 1);
            ::close(it->fd);
            it = gRemotes.erase(it);
        } else {
            ++it;
        }
    }
    // THE BRIDGE'S STAND-IN, WHEN THERE IS ONE, NOT THE REAL REMOTE
    // (wiibridge/bridge.cpp): every Remote that has a stand-in is driven
    // through it, the console's light and menus included, and the real node
    // is the bridge's alone (the udev rule hides it from everybody else).
    struct Node {
        std::string node, uevent;
    };
    std::vector<Node> nodes;
    std::vector<std::string> bridged;   // addresses that have a stand-in
    DIR* d = ::opendir("/sys/class/hidraw");
    if (!d) return;
    while (dirent* e = ::readdir(d)) {
        const std::string node = e->d_name;
        if (node.rfind("hidraw", 0) != 0) continue;
        Node n{node, readFile("/sys/class/hidraw/" + node + "/device/uevent")};
        if (!isRemoteName(ueventField(n.uevent, "HID_NAME"))) continue;
        if (ueventField(n.uevent, "HID_PHYS") == kBridgePhys)
            bridged.push_back(ueventField(n.uevent, "HID_UNIQ"));
        nodes.push_back(std::move(n));
    }
    ::closedir(d);
    auto hasStandIn = [&](const std::string& address) {
        for (const std::string& b : bridged)
            if (strcasecmp(b.c_str(), address.c_str()) == 0) return true;
        return false;
    };
    // A real Remote taken before its stand-in appeared gives way to it.
    for (auto it = gRemotes.begin(); it != gRemotes.end();) {
        if (!it->bridged && hasStandIn(it->address)) {
            std::fprintf(stderr, "[wiiremote] %s now through the bridge\n", it->address.c_str());
            ::close(it->fd);
            it = gRemotes.erase(it);
        } else {
            ++it;
        }
    }
    for (const Node& n : nodes) {
        const std::string& node = n.node;
        const std::string& uevent = n.uevent;
        bool have = false;
        for (const Remote& r : gRemotes) have = have || r.node == node;
        if (have) continue;
        const bool standIn = ueventField(uevent, "HID_PHYS") == kBridgePhys;
        if (!standIn && hasStandIn(ueventField(uevent, "HID_UNIQ"))) continue;
        Remote r;
        r.node = node;
        r.address = ueventField(uevent, "HID_UNIQ");
        r.bridged = standIn;
        r.fd = ::open(("/dev/" + node).c_str(), O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (r.fd < 0) {
            // A real Remote the bridge has hidden is no news: its stand-in
            // follows within half a second.
            if (errno != EACCES)
                std::fprintf(stderr, "[wiiremote] cannot open /dev/%s: %s\n", node.c_str(),
                             std::strerror(errno));
            continue;
        }
        // Its light: the lowest one no other Remote has, so lights follow the
        // order Remotes connected in.
        for (int s = 0; s < 4; ++s) {
            bool taken = false;
            for (const Remote& o : gRemotes) taken = taken || o.slot == s;
            if (!taken) {
                r.slot = s;
                break;
            }
        }
        std::fprintf(stderr, "[wiiremote] %s connected on %s%s, light %d\n", r.address.c_str(),
                     node.c_str(), standIn ? " (through the bridge)" : "", r.slot + 1);
        r.lastActive = Clock::now();
        r.lastReport = Clock::now();
        // While a game runs Dolphin sets it up, with its own light.
        if (!gGame) setUp(r);
        gRemotes.push_back(std::move(r));
    }
}

void buttonsChanged(Remote& r, uint16_t now) {
    const uint16_t was = r.buttons;
    r.buttons = now;
    r.lastActive = Clock::now();
    for (const Mapping& m : kMap) {
        if ((now & m.bit) && !(was & m.bit)) pushButton(m.button, true);
        if (!(now & m.bit) && (was & m.bit)) pushButton(m.button, false);
    }
    if ((now & kHome) && !(was & kHome)) {
        r.homeSince = Clock::now();
        r.homeFired = false;
    }
}

void readReports(Remote& r) {
    uint8_t buf[32];
    for (;;) {
        const ssize_t n = ::read(r.fd, buf, sizeof buf);
        if (n <= 0) return;
        if (!isOn(r)) {
            // BACK ON (a button, after it was switched off or went away): its
            // light again, and the idle time starts over.
            r.lastActive = Clock::now();
            if (r.lastReport != Clock::time_point{})
                std::fprintf(stderr, "[wiiremote] %s is back on\n", r.address.c_str());
            r.switchedOff = false;
            r.lastReport = Clock::now();
            if (!gGame) setUp(r);
        }
        r.lastReport = Clock::now();
        const uint8_t id = buf[0];
        // A status report stops a Remote's reports until the mode is set
        // again: an extension went in or out, or it answered our request.
        if (id == 0x20 && !gGame) send(r.fd, {0x12, 0x04, 0x30});
        const bool hasButtons = (id >= 0x20 && id <= 0x22) || (id >= 0x30 && id <= 0x37) ||
                                id == 0x3e || id == 0x3f;
        if (hasButtons && n >= 3) {
            const uint16_t b = static_cast<uint16_t>(((buf[1] & 0x1f) | ((buf[2] & 0x9f) << 8)));
            if (b != r.buttons) buttonsChanged(r, b);
        }
    }
}

void run() {
    auto lastScan = Clock::time_point{};
    for (;;) {
        std::vector<pollfd> fds;
        {
            std::lock_guard<std::mutex> lk(gLock);
            if (Clock::now() - lastScan > std::chrono::milliseconds(500)) {
                rescan();
                lastScan = Clock::now();
            }
            for (const Remote& r : gRemotes) fds.push_back({r.fd, POLLIN, 0});
        }
        if (fds.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
            continue;
        }
        ::poll(fds.data(), fds.size(), 50);
        std::lock_guard<std::mutex> lk(gLock);
        for (Remote& r : gRemotes) {
            readReports(r);
            // HOME HELD, while a game runs: the console's own menu. A press is
            // the game's HOME menu, which Dolphin passes on as a Wii would.
            if ((r.buttons & kHome) && !r.homeFired && gGame &&
                Clock::now() - r.homeSince >= kHomeHold) {
                r.homeFired = true;
                gHomeHold = true;
                std::fprintf(stderr, "[wiiremote] HOME held on %s\n", r.address.c_str());
            }
            if (!gGame && isOn(r) && !r.switchedOff && Clock::now() - r.lastActive >= kIdleOff) {
                r.switchedOff = true;
                std::fprintf(stderr, "[wiiremote] %s untouched for 5 minutes; switching it off\n",
                             r.address.c_str());
                switchOff(r.address);
            }
        }
    }
}

}  // namespace

// --- Public ---------------------------------------------------------------------------

bool search(std::vector<std::string>* found, std::string* err, const std::atomic<bool>* cancel) {
    found->clear();
    const long startedAt = static_cast<long>(std::time(nullptr));
    std::string why;
    if (!unit::start(kSearchUnit, &why)) {
        if (err) *err = why;
        return false;
    }
    // The unit is a oneshot; it answers in its state file when it is done. A
    // pass takes about 12 seconds (measured on the A9).
    const auto end = Clock::now() + std::chrono::seconds(40);
    while (Clock::now() < end && !(cancel && *cancel)) {
        std::vector<std::string> remotes;
        auto kv = readState(&remotes);
        const long at = kv.count("at") ? std::strtol(kv["at"].c_str(), nullptr, 10) : 0;
        if (at >= startedAt && (kv["state"] == "done" || kv["state"] == "failed")) {
            if (kv["state"] == "failed") {
                if (err) *err = kv["reason"].empty() ? "the search did not run" : kv["reason"];
                return false;
            }
            *found = remotes;
            std::fprintf(stderr, "[wiiremote] search: %zu Remote(s)\n", found->size());
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (err) *err = (cancel && *cancel) ? "cancelled" : "the search did not finish";
    return false;
}

bool pair(const std::string& address, std::string* err) {
    sd_bus* bus = nullptr;
    if (int r = sd_bus_open_system(&bus); r < 0) {
        if (err) *err = std::string("No system bus: ") + std::strerror(-r);
        return false;
    }
    PairState st;
    st.device = devicePath(address);

    for (int i = 0; i < 3 && !deviceKnown(bus, st.device); ++i) {
        touchControlChannel(address);
        for (int w = 0; w < 10 && !deviceKnown(bus, st.device); ++w)
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    if (!deviceKnown(bus, st.device)) {
        if (err) *err = "Couldn't reach it";
        sd_bus_unref(bus);
        return false;
    }

    sd_bus_slot* objSlot = nullptr;
    sd_bus_add_object_vtable(bus, &objSlot, kAgentPath, "org.bluez.Agent1", kAgentVtable, &st);
    sd_bus_error e = SD_BUS_ERROR_NULL;
    if (sd_bus_call_method(bus, "org.bluez", "/org/bluez", "org.bluez.AgentManager1",
                           "RegisterAgent", &e, nullptr, "os", kAgentPath, "KeyboardDisplay") < 0)
        std::fprintf(stderr, "[wiiremote] agent: %s\n", e.message ? e.message : "refused");
    sd_bus_error_free(&e);

    std::string why;
    bool ok = callAndWait(bus, st.device, "Pair", 40, &why);
    if (!ok && why == "org.bluez.Error.AlreadyExists") ok = true;
    std::fprintf(stderr, "[wiiremote] pair %s: %s\n", address.c_str(), ok ? "paired" : why.c_str());

    if (ok) {
        // TRUSTED, OR IT NEVER COMES BACK: bluez refuses the Remote's own
        // reconnection otherwise (docs/lessons/frontend.md, Bluetooth).
        sd_bus_error te = SD_BUS_ERROR_NULL;
        if (sd_bus_set_property(bus, "org.bluez", st.device.c_str(), "org.bluez.Device1",
                                "Trusted", &te, "b", 1) < 0)
            std::fprintf(stderr, "[wiiremote] trust: %s\n", te.message ? te.message : "refused");
        sd_bus_error_free(&te);
        std::string cwhy;
        if (!callAndWait(bus, st.device, "Connect", 20, &cwhy))
            std::fprintf(stderr, "[wiiremote] connect: %s\n", cwhy.c_str());
    }

    sd_bus_error ue = SD_BUS_ERROR_NULL;
    sd_bus_call_method(bus, "org.bluez", "/org/bluez", "org.bluez.AgentManager1", "UnregisterAgent",
                       &ue, nullptr, "o", kAgentPath);
    sd_bus_error_free(&ue);
    sd_bus_slot_unref(objSlot);
    sd_bus_unref(bus);

    if (!ok && err) *err = "Couldn't pair";
    refresh();
    return ok;
}

bool remove(const std::string& address, std::string* err) {
    const bool ok = proc::run({"bluetoothctl", "remove", address}, 15).ok();
    if (!ok && err) *err = "Couldn't remove it";
    refresh();
    return ok;
}

std::vector<Paired> known() {
    std::vector<Paired> out;
    {
        std::lock_guard<std::mutex> lk(gPairedLock);
        out = gPaired;
    }
    std::lock_guard<std::mutex> lk(gLock);
    for (Paired& p : out)
        for (const Remote& r : gRemotes)
            if (strcasecmp(r.address.c_str(), p.address.c_str()) == 0 && isOn(r)) {
                p.connected = true;
                p.light = gGame ? 0 : r.slot + 1;
            }
    return out;
}

bool anyPaired() { return gAnyPaired; }

void refresh() {
    std::vector<Paired> now;
    for (const auto& [address, name] : listed("Paired")) {
        Paired p;
        p.address = address;
        p.name = name;
        now.push_back(p);
    }
    const bool any = !now.empty();
    if (any != gAnyPaired)
        std::fprintf(stderr, "[wiiremote] %zu Remote(s) paired\n", now.size());
    {
        std::lock_guard<std::mutex> lk(gPairedLock);
        gPaired = std::move(now);
    }
    gAnyPaired = any;
}

// THE BRIDGE (wiibridge/bridge.cpp, its own program, GPL because it carries
// Dolphin's extension encryption): started with the console and stopped with
// it. Without it Remotes still work, Nunchuk games do not on copies, and Cemu
// does not see a copy at all.
//
// Its one argument is where it keeps the Miis sent to a Remote, one file per
// player light (#275): the console's, beside its other state, not synced.
void startBridge() {
    const char* env = std::getenv("CABINETOS_WII_BRIDGE");
    const std::string path = env && *env ? env : "/usr/libexec/cabinetos-wii-bridge";
    if (::access(path.c_str(), X_OK) != 0) {
        std::fprintf(stderr, "[wiiremote] no bridge at %s\n", path.c_str());
        return;
    }
    const std::string miis = storage::root() + "/wii-remote-miis";
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::prctl(PR_SET_PDEATHSIG, SIGTERM);
        ::execl(path.c_str(), path.c_str(), miis.c_str(), static_cast<char*>(nullptr));
        std::_Exit(127);
    }
    if (pid > 0) std::fprintf(stderr, "[wiiremote] bridge started (%s, pid %d)\n", path.c_str(), pid);
}

void start() {
    std::call_once(gStarted, [] {
        refresh();
        startBridge();
        std::thread(run).detach();
    });
}

int connected() {
    std::lock_guard<std::mutex> lk(gLock);
    int n = 0;
    for (const Remote& r : gRemotes) n += isOn(r) ? 1 : 0;
    return n;
}

void setGameRunning(bool running) {
    if (gGame == running) return;
    gGame = running;
    gHomeHold = false;
    if (running) return;
    // Back from a game: each Remote gets its own light and mode again, which
    // Dolphin changed.
    std::lock_guard<std::mutex> lk(gLock);
    for (Remote& r : gRemotes) {
        setUp(r);
        r.homeFired = true;   // a HOME still held from the game does nothing here
    }
}

bool takeHomeHold() { return gHomeHold.exchange(false); }

bool sensorBarAbove() { return prefs::get("wii_sensor_bar", "below") == "above"; }

void setSensorBarAbove(bool above) { prefs::set("wii_sensor_bar", above ? "above" : "below"); }

}  // namespace wiiremote
