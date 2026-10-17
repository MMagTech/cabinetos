// The pairing agent a Bluetooth LE pad needs (PairAgent). While one exists it
// is bluez's default agent, at the one fixed path /cabinetos/padagent, so only
// one may exist at a time: bt::pair (bluetooth.cpp) makes it after "pairable
// on", keeps it through the retries, and drops it before "pairable off".
// Moved out of bluetooth.cpp in the restructure (#398), the class whole, as
// it was there.

#pragma once

#include <strings.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

#include <systemd/sd-bus.h>

namespace bt {

// THE YES A BLUETOOTH LE PAD WAITS FOR. A pad that pairs over Bluetooth LE (a
// newer Xbox pad: Series X|S, Elite 2) has bluetoothd ask the pairing's agent
// to confirm, even for "just works", and `bluetoothctl pair` run as a command
// registers no agent: "No agent available for request type 2", then
// AuthenticationFailed, and "Couldn't pair" on the screen, every time
// (MMagTech's Series pad on the A9, 2026-10-10). Classic pads never ask, which
// is why every pad before it paired.
//
// So for the length of one pairing an agent of ours is bluetoothd's DEFAULT
// agent, the one it uses when the caller has none (src/agent.c, agent_get).
// ITS BEING THERE IS THE FIX: for a "just works" pairing we started,
// bluetoothd accepts by itself, but only once an agent exists
// (src/device.c, device_confirm_passkey: new_auth, then the bonding check),
// so the Series pad paired with no question asked of it (A9, 2026-10-10).
// Asked anything else, it says yes only for the pad picked on screen and
// refuses the rest, as it was with no agent at all. Its capability is
// NoInputNoOutput, which is what bluetoothd assumes with no agent, so every
// pad that paired before pairs exactly as it did.
class PairAgent {
public:
    explicit PairAgent(const std::string& address) : suffix_("/dev_" + address) {
        for (char& c : suffix_)
            if (c == ':') c = '_';
        if (sd_bus_open_system(&bus_) < 0) {
            std::fprintf(stderr, "[bluetooth] agent: no system bus\n");
            bus_ = nullptr;
            return;
        }
        sd_bus_add_object_vtable(bus_, &slot_, kPath, "org.bluez.Agent1", kVtable, this);
        sd_bus_error e = SD_BUS_ERROR_NULL;
        if (sd_bus_call_method(bus_, "org.bluez", "/org/bluez", "org.bluez.AgentManager1",
                               "RegisterAgent", &e, nullptr, "os", kPath, "NoInputNoOutput") < 0 ||
            sd_bus_call_method(bus_, "org.bluez", "/org/bluez", "org.bluez.AgentManager1",
                               "RequestDefaultAgent", &e, nullptr, "o", kPath) < 0)
            std::fprintf(stderr, "[bluetooth] agent: %s\n", e.message ? e.message : "refused");
        sd_bus_error_free(&e);
        // bluetoothd calls in while `bluetoothctl pair` runs in another
        // process, so the calls are answered on a thread of their own.
        pump_ = std::thread([this] {
            while (!stop_) {
                const int r = sd_bus_process(bus_, nullptr);
                if (r < 0) break;
                if (r == 0) sd_bus_wait(bus_, 200000);
            }
        });
    }

    ~PairAgent() {
        if (!bus_) return;
        stop_ = true;
        pump_.join();
        sd_bus_error e = SD_BUS_ERROR_NULL;
        sd_bus_call_method(bus_, "org.bluez", "/org/bluez", "org.bluez.AgentManager1",
                           "UnregisterAgent", &e, nullptr, "o", kPath);
        sd_bus_error_free(&e);
        sd_bus_slot_unref(slot_);
        sd_bus_unref(bus_);
    }

    PairAgent(const PairAgent&) = delete;
    PairAgent& operator=(const PairAgent&) = delete;

private:
    static constexpr const char* kPath = "/cabinetos/padagent";

    bool isOurs(const char* device) const {
        const size_t n = device ? std::strlen(device) : 0;
        return n >= suffix_.size() &&
               strcasecmp(device + n - suffix_.size(), suffix_.c_str()) == 0;
    }

    static int ok(sd_bus_message* m, void*, sd_bus_error*) { return sd_bus_reply_method_return(m, ""); }

    static int refuse(sd_bus_message*, void*, sd_bus_error* e) {
        return sd_bus_error_set(e, "org.bluez.Error.Rejected", "Not the controller being paired");
    }

    // RequestConfirmation (ou), RequestAuthorization (o) and AuthorizeService
    // (os) all start with the device; yes for ours, no for anything else.
    static int yesIfOurs(sd_bus_message* m, void* ud, sd_bus_error* e) {
        const char* device = nullptr;
        if (sd_bus_message_read_basic(m, 'o', &device) < 0 ||
            !static_cast<PairAgent*>(ud)->isOurs(device))
            return refuse(m, ud, e);
        std::fprintf(stderr, "[bluetooth] %s: %s, yes\n", device, sd_bus_message_get_member(m));
        return sd_bus_reply_method_return(m, "");
    }

    static constexpr sd_bus_vtable kVtable[] = {
        SD_BUS_VTABLE_START(0),
        SD_BUS_METHOD("Release", "", "", ok, 0),
        SD_BUS_METHOD("RequestPinCode", "o", "s", refuse, 0),
        SD_BUS_METHOD("DisplayPinCode", "os", "", ok, 0),
        SD_BUS_METHOD("RequestPasskey", "o", "u", refuse, 0),
        SD_BUS_METHOD("DisplayPasskey", "ouq", "", ok, 0),
        SD_BUS_METHOD("RequestConfirmation", "ou", "", yesIfOurs, 0),
        SD_BUS_METHOD("RequestAuthorization", "o", "", yesIfOurs, 0),
        SD_BUS_METHOD("AuthorizeService", "os", "", yesIfOurs, 0),
        SD_BUS_METHOD("Cancel", "", "", ok, 0),
        SD_BUS_VTABLE_END,
    };

    std::string suffix_;
    sd_bus* bus_ = nullptr;
    sd_bus_slot* slot_ = nullptr;
    std::atomic<bool> stop_{false};
    std::thread pump_;
};

}  // namespace bt
