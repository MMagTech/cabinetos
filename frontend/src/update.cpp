#include "update.h"

#include <systemd/sd-bus.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace update {

namespace {

std::string g_dir = "/run/cabinetos-update";

std::string slurp(const std::string& path) {
    std::string out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[1024];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

std::string firstLine(const std::string& s) {
    const size_t nl = s.find('\n');
    return nl == std::string::npos ? s : s.substr(0, nl);
}

State stateOf(const std::string& s) {
    if (s == "checking") return State::Checking;
    if (s == "uptodate") return State::UpToDate;
    if (s == "available") return State::Available;
    if (s == "downloading") return State::Downloading;
    if (s == "installing") return State::Installing;
    if (s == "ready") return State::Ready;
    if (s == "failed") return State::Failed;
    return State::None;
}

}  // namespace

void setDir(const std::string& dir) { g_dir = dir; }

Status read() {
    Status st;
    const std::string body = slurp(g_dir + "/status");
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        if (nl == std::string::npos) nl = body.size();
        const std::string line = body.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        const int64_t n = std::strtoll(v.c_str(), nullptr, 10);
        if (k == "state") st.state = stateOf(v);
        else if (k == "op") st.download = (v == "download");
        else if (k == "version") st.version = v;
        else if (k == "size") st.size = n;
        else if (k == "done") st.done = n;
        else if (k == "total") st.total = n;
        else if (k == "since") st.since = n;
        else if (k == "written") st.written = n;
        else if (k == "reason") st.reason = v;
        else if (k == "at") st.at = n;
    }
    return st;
}

bool start(bool download, std::string* why) {
    const char* unit = download ? "cabinetos-update-download.service"
                                : "cabinetos-update-check.service";
    sd_bus* bus = nullptr;
    if (int r = sd_bus_open_system(&bus); r < 0) {
        if (why) *why = std::string("No system bus: ") + std::strerror(-r);
        return false;
    }
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    // "replace", and never interactive: a password prompt on a television is
    // the same as a refusal. 61-cabinetos-update.rules grants it outright.
    const int r = sd_bus_call_method(bus, "org.freedesktop.systemd1",
                                     "/org/freedesktop/systemd1",
                                     "org.freedesktop.systemd1.Manager", "StartUnit", &err,
                                     &reply, "ss", unit, "replace");
    const bool ok = r >= 0;
    if (!ok && why) *why = err.message ? err.message : std::strerror(-r);
    std::fprintf(stderr, "[update] start %s: %s\n", unit,
                 ok ? "started" : (err.message ? err.message : std::strerror(-r)));
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    sd_bus_unref(bus);
    return ok;
}

std::string bootedVersion() {
    // --update-dir's stand-in first, so "Updated to" can be shown by the TV
    // loop on an image that is not the one being described.
    const std::string standIn = firstLine(slurp(g_dir + "/version"));
    if (!standIn.empty()) return standIn;
    return firstLine(slurp("/usr/share/cabinetos/version"));
}

std::string bootId() { return firstLine(slurp("/proc/sys/kernel/random/boot_id")); }

}  // namespace update
