// Pairing a Wii Remote (wiipair.h): the Wii's own search, through the root
// helper; the L2CAP touch that tells bluetoothd of a Remote; bluetoothd's
// pairing with this session's agent answering the PIN; and forgetting one.
// wiiremote.h says how pairing works. Moved out of wiiremote.cpp in the
// restructure (#398).

#include "wiipair.h"

#include <endian.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <fstream>
#include <map>
#include <thread>

#include <systemd/sd-bus.h>

#include "proc.h"
#include "unit.h"
#include "wiiremote.h"

namespace wiiremote {
namespace {

using Clock = std::chrono::steady_clock;

constexpr const char* kSearchUnit = "cabinetos-wii-search.service";
constexpr const char* kSearchState = "/run/cabinetos-wii-search.state";
// The console's one radio. Dolphin and the kernel's own Wii Remote code use
// the first adapter too.
constexpr const char* kAdapter = "/org/bluez/hci0";
constexpr const char* kAgentPath = "/cabinetos/wiiagent";

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

}  // namespace

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

}  // namespace wiiremote
