#include "power.h"

#include <systemd/sd-bus.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <string>
#include <thread>
#include <unistd.h>

namespace power {

namespace {

constexpr const char* kLogind = "org.freedesktop.login1";
constexpr const char* kPath = "/org/freedesktop/login1";
constexpr const char* kManager = "org.freedesktop.login1.Manager";

// Held for the life of the process and never closed on purpose: closing it is
// what the kernel does for us when the process ends, however it ends.
int gButtonLock = -1;

// The delay lock, and the connection that hears logind's announcements. Kept
// open for the life of the process.
int gDelayLock = -1;
sd_bus* gSignals = nullptr;
Event gPending = Event::None;

int onPrepare(sd_bus_message* m, void* userdata, sd_bus_error*) {
    int starting = 0;
    if (sd_bus_message_read(m, "b", &starting) < 0) return 0;
    const bool sleep = userdata != nullptr;
    gPending = !starting ? Event::Woke : (sleep ? Event::GoingToSleep : Event::GoingDown);
    return 0;
}

int takeLock(sd_bus* bus, const char* what, const char* why, const char* mode) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    int lock = -1;
    if (sd_bus_call_method(bus, kLogind, kPath, kManager, "Inhibit", &err, &reply, "ssss",
                           what, "CabinetOS", why, mode) >= 0) {
        int fd = -1;
        if (sd_bus_message_read(reply, "h", &fd) >= 0 && fd >= 0)
            lock = fcntl(fd, F_DUPFD_CLOEXEC, 3);
    } else {
        std::fprintf(stderr, "[power] logind refused a %s lock: %s\n", mode,
                     err.message ? err.message : "?");
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return lock;
}

double clockSeconds(clockid_t id) {
    timespec ts{};
    clock_gettime(id, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

}  // namespace

bool takeButtons() {
    sd_bus* bus = nullptr;
    if (int r = sd_bus_open_system(&bus); r < 0) {
        std::fprintf(stderr, "[power] no system bus (%s); the button stays logind's\n",
                     std::strerror(-r));
        return false;
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_call_method(
        bus, kLogind, kPath, kManager, "Inhibit", &err, &reply, "ssss",
        "handle-power-key:handle-suspend-key", "CabinetOS",
        "The console opens its Power menu instead", "block");
    bool ok = false;
    if (r < 0) {
        std::fprintf(stderr, "[power] logind kept the button: %s\n",
                     err.message ? err.message : std::strerror(-r));
    } else {
        int fd = -1;
        if (sd_bus_message_read(reply, "h", &fd) >= 0 && fd >= 0) {
            // The descriptor in the message belongs to the message and closes
            // with it; the lock has to outlive it.
            gButtonLock = fcntl(fd, F_DUPFD_CLOEXEC, 3);
            ok = gButtonLock >= 0;
        }
        std::fprintf(stderr, "[power] %s\n",
                     ok ? "the power button opens the Power menu now"
                        : "logind answered without a lock; the button stays logind's");
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return ok;
}

bool takeShutdownDelay() {
    if (!gSignals) {
        if (sd_bus_open_system(&gSignals) < 0) {
            gSignals = nullptr;
            std::fprintf(stderr, "[power] no system bus; a shutdown will not wait for the game\n");
            return false;
        }
        // userdata marks which of the two it is: null for shutdown.
        sd_bus_match_signal(gSignals, nullptr, kLogind, kPath, kManager,
                            "PrepareForShutdown", onPrepare, nullptr);
        static int sleepMarker = 1;
        sd_bus_match_signal(gSignals, nullptr, kLogind, kPath, kManager,
                            "PrepareForSleep", onPrepare, &sleepMarker);
    }
    if (gDelayLock < 0)
        gDelayLock = takeLock(gSignals, "shutdown:sleep",
                              "Leaving the game and sending its saves first", "delay");
    std::fprintf(stderr, "[power] %s\n",
                 gDelayLock >= 0 ? "a shutdown or sleep will wait for the game to be left"
                                 : "no delay lock; a shutdown will not wait for the game");
    return gDelayLock >= 0;
}

Event poll() {
    if (!gSignals) return Event::None;
    // Drain whatever arrived; each message runs onPrepare if it matches.
    while (sd_bus_process(gSignals, nullptr) > 0) {
    }
    const Event e = gPending;
    gPending = Event::None;
    if (e == Event::Woke && gDelayLock < 0) takeShutdownDelay();
    return e;
}

void releaseDelay() {
    if (gDelayLock >= 0) {
        close(gDelayLock);
        gDelayLock = -1;
        std::fprintf(stderr, "[power] released: the machine may go down now\n");
    }
}

bool canRest() {
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0) return false;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    std::string answer = "na";
    if (sd_bus_call_method(bus, kLogind, kPath, kManager, "CanSuspend", &err, &reply,
                           "") >= 0) {
        const char* s = nullptr;
        if (sd_bus_message_read(reply, "s", &s) >= 0 && s) answer = s;
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    // "yes" only. "challenge" would put a password prompt on a television
    // nobody can type into, which is the same as not being able to.
    std::fprintf(stderr, "[power] can this machine rest? logind says %s\n",
                 answer.c_str());
    return answer == "yes";
}

const char* name(Action a) {
    switch (a) {
        case Action::Rest: return "rest";
        case Action::Restart: return "restart";
        case Action::PowerOff: return "power off";
    }
    return "?";
}

void act(Action a) {
    std::thread([a] {
        const char* method = a == Action::Rest      ? "Suspend"
                             : a == Action::Restart ? "Reboot"
                                                    : "PowerOff";
        sd_bus* bus = nullptr;
        if (int r = sd_bus_open_system(&bus); r < 0) {
            std::fprintf(stderr, "[power] %s: no system bus (%s)\n", name(a),
                         std::strerror(-r));
            return;
        }
        sd_bus_error err = SD_BUS_ERROR_NULL;
        // interactive=false: never a password prompt on a television.
        const int r = sd_bus_call_method(bus, kLogind, kPath, kManager, method, &err,
                                         nullptr, "b", 0);
        std::fprintf(stderr, "[power] %s: %s\n", name(a),
                     r >= 0 ? "logind took it"
                            : (err.message ? err.message : std::strerror(-r)));
        sd_bus_error_free(&err);
        sd_bus_unref(bus);
    }).detach();
}

bool justWoke() {
    // CLOCK_BOOTTIME counts the time spent asleep and CLOCK_MONOTONIC does
    // not, so the gap between them grows by exactly the length of a sleep.
    static double lastGap = -1.0;
    static double wokeAt = -1e9;
    const double mono = clockSeconds(CLOCK_MONOTONIC);
    const double gap = clockSeconds(CLOCK_BOOTTIME) - mono;
    if (lastGap >= 0.0 && gap - lastGap > 1.0) {
        wokeAt = mono;
        std::fprintf(stderr, "[power] woke from a %.0fs sleep\n", gap - lastGap);
    }
    lastGap = gap;
    return mono - wokeAt < 3.0;
}

}  // namespace power
