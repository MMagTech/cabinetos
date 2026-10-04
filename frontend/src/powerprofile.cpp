#include "powerprofile.h"

#include <systemd/sd-bus.h>

#include <condition_variable>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>

namespace powerprofile {
namespace {

constexpr const char* kHome = "balanced";
constexpr const char* kGame = "throughput-performance-bazzite";

std::mutex gLock;
std::condition_variable gWake;
int gWanted = -1;     // 1 a game, 0 Home, -1 nothing asked yet
bool gStarted = false;
bool gComplained = false;

// One D-Bus call to tuned. Blocks for as long as tuned takes to apply it.
void apply(bool game) {
    const char* profile = game ? kGame : kHome;
    sd_bus* bus = nullptr;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int ok = 0;
    const char* msg = nullptr;
    int r = sd_bus_open_system(&bus);
    if (r >= 0)
        r = sd_bus_call_method(bus, "com.redhat.tuned", "/Tuned", "com.redhat.tuned.control",
                               "switch_profile", &err, &reply, "s", profile);
    if (r >= 0) r = sd_bus_message_read(reply, "(bs)", &ok, &msg);
    if (r >= 0 && ok) {
        std::fprintf(stderr, "[profile] %s, %s\n", profile, game ? "for the game" : "for Home");
    } else if (!gComplained) {
        // Once: a machine without tuned, or without permission, would say it
        // at every game otherwise.
        gComplained = true;
        std::fprintf(stderr, "[profile] tuned did not switch to %s: %s\n", profile,
                     r < 0 ? (err.message ? err.message : "no answer") : (msg ? msg : "refused"));
    }
    sd_bus_message_unref(reply);
    sd_bus_error_free(&err);
    sd_bus_flush_close_unref(bus);
}

// The latest wish wins: a game started and left inside one switch costs one
// more switch, never a queue of them.
void worker() {
    int applied = -1;
    for (;;) {
        int want;
        {
            std::unique_lock<std::mutex> lk(gLock);
            gWake.wait(lk, [&] { return gWanted != applied; });
            want = gWanted;
        }
        apply(want == 1);
        applied = want;
    }
}

}  // namespace

void applyNow(bool game) { apply(game); }

void update(bool gameRunning) {
    const int want = gameRunning ? 1 : 0;
    std::lock_guard<std::mutex> lk(gLock);
    if (want == gWanted) return;
    gWanted = want;
    if (!gStarted) {
        gStarted = true;
        std::thread(worker).detach();
    }
    gWake.notify_one();
}

}  // namespace powerprofile
