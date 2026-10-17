// The server address (serveraddress.h): read, in rank order, from
// CABINETOS_ROMM, /etc/cabinetos/session.env and config/server.json, and
// written to config/server.json through first run's fsyncing writeAll.
// Moved out of firstrun.cpp in the restructure (#398).

#include "serveraddress.h"

#include <unistd.h>

#include <cstdio>
#include <cstdlib>

#include <json-c/json.h>

#include "firstrun_internal.h"
#include "storage.h"

namespace firstrun {
namespace {

std::string serverPath() { return storage::configDir() + "/server.json"; }

}  // namespace

// --- The server address -----------------------------------------------------

namespace {

// One `CABINETOS_ROMM=...` out of /etc/cabinetos/session.env.
//
// This reads ONE NAME out of a shell file and nothing else. It is not a shell
// parser and must never become one: no expansion, no continuation lines, no
// command substitution. `export ` is allowed because people write it, and
// matching quotes are stripped because people use them. Anything cleverer than
// that belongs in the session script, which is where the file is actually
// sourced.
std::string addressFromSessionEnv() {
    const std::string body = readAll("/etc/cabinetos/session.env");
    if (body.empty()) return {};
    size_t at = 0;
    while (at < body.size()) {
        size_t end = body.find('\n', at);
        if (end == std::string::npos) end = body.size();
        std::string line = body.substr(at, end - at);
        at = end + 1;

        size_t i = line.find_first_not_of(" \t");
        if (i == std::string::npos || line[i] == '#') continue;
        line = line.substr(i);
        if (line.compare(0, 7, "export ") == 0)
            line = line.substr(line.find_first_not_of(" \t", 7));
        const std::string key = "CABINETOS_ROMM=";
        if (line.compare(0, key.size(), key) != 0) continue;

        std::string value = line.substr(key.size());
        // Trailing whitespace and a carriage return from a file edited on a
        // machine that does not share this one's line endings.
        while (!value.empty() && (value.back() == ' ' || value.back() == '\t' ||
                                  value.back() == '\r'))
            value.pop_back();
        if (value.size() >= 2 && (value.front() == '"' || value.front() == '\'') &&
            value.back() == value.front())
            value = value.substr(1, value.size() - 2);
        return value;
    }
    return {};
}

}  // namespace

std::string serverAddress() {
    // ROOT'S ANSWER WINS, and the header says why.
    if (const char* env = getenv("CABINETOS_ROMM"); env && *env) return env;
    if (const std::string fromEtc = addressFromSessionEnv(); !fromEtc.empty())
        return fromEtc;
    const std::string body = readAll(serverPath());
    if (body.empty()) return {};
    return jsonString(body, "address");
}

std::string serverAddressSource() {
    if (const char* env = getenv("CABINETOS_ROMM"); env && *env)
        return "the environment";
    if (!addressFromSessionEnv().empty()) return "/etc/cabinetos/session.env";
    if (!readAll(serverPath()).empty()) return "config/server.json";
    return "nowhere";
}

bool setServerAddress(const std::string& address, std::string* err) {
    if (address.empty()) {
        if (err) *err = "no address";
        return false;
    }
    if (!storage::makeDirs(storage::configDir())) {
        if (err) *err = "cannot create " + storage::configDir();
        return false;
    }
    json_object* o = json_object_new_object();
    json_object_object_add(o, "address", json_object_new_string(address.c_str()));
    json_object_object_add(o, "written", json_object_new_string(isoNow().c_str()));
    const std::string body = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    json_object_put(o);
    if (!writeAll(serverPath(), body + "\n", err)) return false;

    // SAID OUT LOUD WHEN IT WILL NOT TAKE EFFECT. Writing this file while
    // something that outranks it names a different server is a write that
    // succeeds and changes nothing anybody can see, which is the worst shape a
    // setting can have.
    const std::string winner = serverAddress();
    if (winner != address) {
        std::fprintf(stderr,
                     "[first-run] saved %s, but %s says %s and that wins — edit "
                     "/etc/cabinetos/session.env to change it\n",
                     address.c_str(), serverAddressSource().c_str(),
                     winner.c_str());
    }
    return true;
}

void forgetServerAddress() { ::unlink(serverPath().c_str()); }

}  // namespace firstrun
