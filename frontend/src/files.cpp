#include "files.h"

#include "unit.h"

#include <cstdio>
#include <cstdlib>

namespace files {

namespace {

std::string g_state = "/run/cabinetos-files.state";
std::string g_password = "/var/lib/cabinetos-files/password";

std::string slurp(const std::string& path) {
    std::string out;
    if (FILE* f = std::fopen(path.c_str(), "rb")) {
        char buf[512];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
        std::fclose(f);
    }
    return out;
}

}  // namespace

void setDir(const std::string& dir) {
    g_state = dir + "/state";
    g_password = dir + "/password";
}

State read() {
    State st;
    const std::string body = slurp(g_state);
    size_t pos = 0;
    while (pos < body.size()) {
        size_t nl = body.find('\n', pos);
        if (nl == std::string::npos) nl = body.size();
        const std::string line = body.substr(pos, nl - pos);
        pos = nl + 1;
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string k = line.substr(0, eq), v = line.substr(eq + 1);
        if (k == "state") {
            st.on = (v == "on");
            st.failed = (v == "failed");
        } else if (k == "reason") {
            st.reason = v;
        } else if (k == "at") {
            st.at = std::strtoll(v.c_str(), nullptr, 10);
        }
    }
    return st;
}

std::string password() {
    std::string p = slurp(g_password);
    while (!p.empty() && (p.back() == '\n' || p.back() == '\r')) p.pop_back();
    return p;
}

bool turnOn(std::string* why) { return unit::start("cabinetos-files.service", why); }
bool turnOff(std::string* why) { return unit::stop("cabinetos-files.service", why); }
bool newPassword(std::string* why) {
    return unit::start("cabinetos-files-password.service", why);
}

}  // namespace files
