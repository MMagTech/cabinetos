#include "update.h"

#include "unit.h"

#include <json-c/json.h>
#include <climits>

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
    return unit::start(download ? "cabinetos-update-download.service"
                                : "cabinetos-update-check.service",
                       why);
}

std::string bootedVersion() {
    // --update-dir's stand-in first, so "Updated to" can be shown by the TV
    // loop on an image that is not the one being described.
    const std::string standIn = firstLine(slurp(g_dir + "/version"));
    if (!standIn.empty()) return standIn;
    return firstLine(slurp("/usr/share/cabinetos/version"));
}

std::string bootId() { return firstLine(slurp("/proc/sys/kernel/random/boot_id")); }

std::string channel() {
    // The kernel command line names the booted deployment (ostree=/ostree/
    // boot.N/...), a chain of symlinks ending in /ostree/deploy/<os>/deploy/
    // <hash>.0, and its .origin file holds the image reference bootc follows.
    // All of it is world-readable, so this needs no root.
    const std::string cmdline = slurp("/proc/cmdline");
    const size_t at = cmdline.find("ostree=");
    if (at == std::string::npos) return "";
    size_t end = cmdline.find_first_of(" \n", at);
    if (end == std::string::npos) end = cmdline.size();
    const std::string link = "/sysroot" + cmdline.substr(at + 7, end - at - 7);
    char real[PATH_MAX];
    if (!realpath(link.c_str(), real)) return "";
    const std::string origin = slurp(std::string(real) + ".origin");
    const std::string key = "container-image-reference=";
    const size_t k = origin.find(key);
    if (k == std::string::npos) return "";
    const std::string ref = firstLine(origin.substr(k + key.size()));
    const size_t colon = ref.rfind(':');
    const size_t slash = ref.rfind('/');
    if (colon == std::string::npos || (slash != std::string::npos && colon < slash)) return "";
    return ref.substr(colon + 1);
}

std::string baseVersion() {
    const std::string body = slurp("/usr/share/ublue-os/image-info.json");
    json_object* o = body.empty() ? nullptr : json_tokener_parse(body.c_str());
    std::string out;
    json_object* v = nullptr;
    if (o && json_object_object_get_ex(o, "version", &v) &&
        json_object_get_type(v) == json_type_string)
        out = json_object_get_string(v);
    if (o) json_object_put(o);
    return out;
}

}  // namespace update
