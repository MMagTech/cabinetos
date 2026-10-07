#include "unit.h"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace unit {

namespace {

bool call(const char* method, const char* name, std::string* why) {
    sd_bus* bus = nullptr;
    if (int r = sd_bus_open_system(&bus); r < 0) {
        if (why) *why = std::string("No system bus: ") + std::strerror(-r);
        return false;
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    // "replace", and never interactive: a password prompt on a television is
    // the same as a refusal.
    const int r = sd_bus_call_method(bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                                     "org.freedesktop.systemd1.Manager", method, &err, &reply,
                                     "ss", name, "replace");
    const bool ok = r >= 0;
    const char* msg = err.message ? err.message : std::strerror(-r);
    if (!ok && why) *why = msg;
    std::fprintf(stderr, "[unit] %s %s: %s\n", method, name, ok ? "taken" : msg);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return ok;
}

}  // namespace

bool start(const char* name, std::string* why) { return call("StartUnit", name, why); }
bool stop(const char* name, std::string* why) { return call("StopUnit", name, why); }
bool reload(const char* name, std::string* why) { return call("ReloadUnit", name, why); }

std::string state(const char* name) {
    sd_bus* bus = nullptr;
    if (sd_bus_open_system(&bus) < 0) return "";
    // The unit's object path from its name; systemd loads the unit on the
    // first read, so a unit nobody has started since boot answers too.
    char* path = nullptr;
    std::string out;
    if (sd_bus_path_encode("/org/freedesktop/systemd1/unit", name, &path) >= 0) {
        sd_bus_error err = SD_BUS_ERROR_NULL;
        char* v = nullptr;
        if (sd_bus_get_property_string(bus, "org.freedesktop.systemd1", path,
                                       "org.freedesktop.systemd1.Unit", "ActiveState", &err,
                                       &v) >= 0 &&
            v) {
            out = v;
        }
        std::free(v);
        sd_bus_error_free(&err);
        std::free(path);
    }
    sd_bus_unref(bus);
    return out;
}

}  // namespace unit
