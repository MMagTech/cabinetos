// udisks over D-Bus, as the drive watcher (drives.cpp) asks it, and systemd's
// reload of a unit, which Eject and Format wait on (udisks.h). Every one runs
// on the drives worker, except the first look in drives::start(), on the main
// thread. Moved out of drives.cpp in the restructure (#398).

#include "udisks.h"

#include <sys/sysmacros.h>
#include <unistd.h>

#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <systemd/sd-bus.h>

namespace drives {

bool boolProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
              bool* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int v = 0;
    const int r = sd_bus_get_property_trivial(bus, kUDisks, path.c_str(), iface, name, &err,
                                              'b', &v);
    sd_bus_error_free(&err);
    if (r < 0) return false;
    *out = v != 0;
    return true;
}

bool u64Prop(sd_bus* bus, const std::string& path, const char* iface, const char* name,
             uint64_t* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    const int r = sd_bus_get_property_trivial(bus, kUDisks, path.c_str(), iface, name, &err,
                                              't', out);
    sd_bus_error_free(&err);
    return r >= 0;
}

// `s` or `o`. False when the object has no such interface, which is how
// "is this a partition" and "does it have a partition table" are asked.
bool stringProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
                const char* type, std::string* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_get_property(bus, kUDisks, path.c_str(), iface, name, &err, &reply,
                                      type);
    if (r >= 0) {
        const char* s = nullptr;
        if (sd_bus_message_read(reply, type, &s) >= 0 && s) *out = s;
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return r >= 0;
}

// An `ay` property that is a path with its NUL: Block.PreferredDevice.
bool pathBytesProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
                   std::string* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_get_property(bus, kUDisks, path.c_str(), iface, name, &err, &reply,
                                      "ay");
    if (r >= 0) {
        const void* p = nullptr;
        size_t n = 0;
        if (sd_bus_message_read_array(reply, 'y', &p, &n) >= 0 && p) {
            out->assign(static_cast<const char*>(p), n);
            while (!out->empty() && out->back() == '\0') out->pop_back();
        }
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return r >= 0;
}

std::string driveOf(sd_bus* bus, const std::string& block) {
    std::string d;
    stringProp(bus, block, kBlock, "Drive", "o", &d);
    return d == "/" ? std::string() : d;
}

// False when the object has no filesystem on it at all.
bool mountPoints(sd_bus* bus, const std::string& path, std::vector<std::string>* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_get_property(bus, kUDisks, path.c_str(), kFs, "MountPoints", &err,
                                      &reply, "aay");
    sd_bus_error_free(&err);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return false;
    }
    if (sd_bus_message_enter_container(reply, 'a', "ay") > 0) {
        const void* p = nullptr;
        size_t n = 0;
        while (sd_bus_message_read_array(reply, 'y', &p, &n) > 0) {
            // udisks sends the path with its terminating NUL.
            std::string s(static_cast<const char*>(p), n);
            while (!s.empty() && s.back() == '\0') s.pop_back();
            if (!s.empty()) out->push_back(s);
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    return true;
}

std::vector<std::string> blockDevices(sd_bus* bus) {
    std::vector<std::string> out;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(bus, kUDisks, kManagerPath, kManager, "GetBlockDevices", &err, &reply,
                           "a{sv}", 0) >= 0) {
        if (sd_bus_message_enter_container(reply, 'a', "o") > 0) {
            const char* p = nullptr;
            while (sd_bus_message_read(reply, "o", &p) > 0)
                if (p) out.push_back(p);
            sd_bus_message_exit_container(reply);
        }
    } else {
        std::fprintf(stderr, "[drives] udisks did not answer: %s\n",
                     err.message ? err.message : "?");
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return out;
}

// udisks' object path for a kernel block device: every byte that is not a
// letter or a digit becomes _xx.
std::string blockPathFor(dev_t d) {
    char link[64];
    std::snprintf(link, sizeof link, "/sys/dev/block/%u:%u", major(d), minor(d));
    char resolved[PATH_MAX];
    if (!::realpath(link, resolved)) return "";
    const char* name = std::strrchr(resolved, '/');
    std::string out = kBlockPrefix;
    for (const char* c = name ? name + 1 : resolved; *c; ++c) {
        if (std::isalnum(static_cast<unsigned char>(*c))) {
            out += *c;
        } else {
            char esc[4];
            std::snprintf(esc, sizeof esc, "_%02x", static_cast<unsigned char>(*c));
            out += esc;
        }
    }
    return out;
}

// One call that takes only auth.no_user_interaction: a password prompt on a
// television is the same as a refusal, so it is asked for as one.
bool callNoPrompt(sd_bus* bus, const std::string& path, const char* iface, const char* method,
                  std::string* mountedAt, std::string* why) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_call_method(bus, kUDisks, path.c_str(), iface, method, &err, &reply,
                                     "a{sv}", 1, "auth.no_user_interaction", "b", 1);
    const bool ok = r >= 0;
    if (ok && mountedAt) {
        const char* s = nullptr;
        if (sd_bus_message_read(reply, "s", &s) >= 0 && s) *mountedAt = s;
    }
    if (!ok && why) *why = err.message ? err.message : std::strerror(-r);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return ok;
}

// WAITS FOR A RELOAD OF A UNIT TO FINISH, up to `seconds`. File access
// takes its view of a drive down in its reload, and the drive's last
// unmount does not return until everything written to it is out: on the
// SanDisk stick, 1.5 s after the mount had already gone from the mount table
// (A9, 2026-10-04). Until then the drive is held, so "Safe to unplug" would
// be early and Format's wipe is refused ("Device or resource busy").
void waitReloaded(sd_bus* bus, const char* unitName, int seconds) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const char* path = nullptr;
    std::string unitPath;
    if (sd_bus_call_method(bus, "org.freedesktop.systemd1", "/org/freedesktop/systemd1",
                           "org.freedesktop.systemd1.Manager", "GetUnit", &err, &reply, "s",
                           unitName) >= 0 &&
        sd_bus_message_read(reply, "o", &path) >= 0 && path)
        unitPath = path;
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    if (unitPath.empty()) return;
    for (int i = 0; i < seconds * 10; ++i) {
        char* state = nullptr;
        sd_bus_error e = SD_BUS_ERROR_NULL;
        const int r = sd_bus_get_property_string(bus, "org.freedesktop.systemd1",
                                                 unitPath.c_str(),
                                                 "org.freedesktop.systemd1.Unit", "ActiveState",
                                                 &e, &state);
        sd_bus_error_free(&e);
        const bool reloading = r >= 0 && state && std::strcmp(state, "reloading") == 0;
        std::free(state);
        if (!reloading) return;
        usleep(100 * 1000);
    }
    std::fprintf(stderr, "[drives] %s still reloading after %d s\n", unitName, seconds);
}

}  // namespace drives
