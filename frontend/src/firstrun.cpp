#include "firstrun.h"

#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

#include <json-c/json.h>

#include "net.h"
#include "romm.h"
#include "storage.h"

namespace firstrun {
namespace {

std::string markerPath() { return storage::configDir() + "/first-run.json"; }
std::string serverPath() { return storage::configDir() + "/server.json"; }

// The token, which is a credential and lives under the session user rather than
// in the console's storage tree. Same path main.cpp uses; kept in step with it
// deliberately rather than shared, because this file must not drag main's
// argument handling in behind it.
std::string tokenPath() {
    const char* home = getenv("HOME");
    return std::string(home ? home : ".") + "/.config/cabinetos/romm.json";
}

bool fileExists(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0;
}

std::string readAll(const std::string& path) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return {};
    std::string body;
    char buf[512];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    std::fclose(f);
    return body;
}

bool writeAll(const std::string& path, const std::string& body, std::string* err) {
    // Written through a temporary and renamed, because the alternative is a
    // console that loses its server address to a power cut halfway through a
    // 40-byte write and comes up on the stand-in library with nobody able to
    // say why. A rename within one directory is atomic; a truncating write is
    // not.
    const std::string tmp = path + ".tmp";
    FILE* f = std::fopen(tmp.c_str(), "wb");
    if (!f) {
        if (err) *err = std::string("cannot write ") + tmp + ": " + std::strerror(errno);
        return false;
    }
    const bool wrote = std::fwrite(body.data(), 1, body.size(), f) == body.size();
    const bool flushed = std::fflush(f) == 0;
    const bool synced = ::fsync(fileno(f)) == 0;
    std::fclose(f);
    if (!wrote || !flushed || !synced) {
        ::unlink(tmp.c_str());
        if (err) *err = std::string("cannot write ") + tmp + ": " + std::strerror(errno);
        return false;
    }
    if (::rename(tmp.c_str(), path.c_str()) != 0) {
        ::unlink(tmp.c_str());
        if (err) *err = std::string("cannot replace ") + path + ": " + std::strerror(errno);
        return false;
    }
    return true;
}

std::string isoNow() {
    const time_t t = ::time(nullptr);
    struct tm g;
    ::gmtime_r(&t, &g);
    char buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%dT%H:%M:%SZ", &g);
    return buf;
}

std::string jsonString(const std::string& body, const char* key) {
    json_object* o = json_tokener_parse(body.c_str());
    if (!o) return {};
    json_object* v = nullptr;
    std::string out;
    if (json_object_object_get_ex(o, key, &v) && v) {
        if (const char* s = json_object_get_string(v)) out = s;
    }
    json_object_put(o);
    return out;
}

// Everything first run would produce, already present. See the header: this is
// the rule that stops a machine set up by hand booting into a wizard.
bool alreadyConfigured() {
    const bool haveAddress = !serverAddress().empty();
    const bool haveToken = fileExists(tokenPath());
    // A cached user means a token that was actually USED — somebody reached
    // /api/users/me with it — which is a stronger fact than a file existing,
    // and it is what the save tree is namespaced on.
    const bool haveUser = fileExists(storage::configDir() + "/user.json");
    return haveAddress && haveToken && haveUser;
}

}  // namespace

const char* name(Step s) {
    switch (s) {
        case Step::Network:    return "network";
        case Step::WiFi:       return "wifi";
        case Step::Server:     return "server";
        case Step::Pair:       return "pair";
        case Step::Controller: return "controller";
        case Step::Done:       return "done";
    }
    return "?";
}

// --- Is this the first run --------------------------------------------------

Completion completion() {
    Completion c;
    const std::string body = readAll(markerPath());
    if (!body.empty()) {
        c.done = true;
        c.when = jsonString(body, "completed");
        c.why = jsonString(body, "how") == "adopted" ? Why::AdoptedExisting
                                                     : Why::Completed;
        return c;
    }
    if (alreadyConfigured()) {
        c.done = true;
        c.why = Why::AdoptedExisting;
        return c;
    }
    return c;
}

bool markCompleted(bool adopted, std::string* err) {
    if (!storage::makeDirs(storage::configDir())) {
        if (err) *err = "cannot create " + storage::configDir();
        return false;
    }
    json_object* o = json_object_new_object();
    json_object_object_add(o, "completed", json_object_new_string(isoNow().c_str()));
    json_object_object_add(o, "how",
                           json_object_new_string(adopted ? "adopted" : "walked"));
    // The version of the flow that wrote it. A console upgraded into a first
    // run that asks something new has to be able to tell that it never answered
    // the new question — and a marker with no version in it cannot say.
    json_object_object_add(o, "flow", json_object_new_int(1));
    const std::string body = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    json_object_put(o);
    return writeAll(markerPath(), body + "\n", err);
}

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

// --- The rules --------------------------------------------------------------

Gate Machine::gate() const {
    switch (step_) {
        case Step::Network:
            // THE ONE HARD GATE. No skip, and none may ever be added: on the
            // far side of it there is no server, so there is nothing to show.
            return facts_.online ? Gate::Ready : Gate::Blocked;

        case Step::WiFi:
            // A machine with no radio has nothing to offer here, so this is not
            // a step it can fail — it is a step it does not have.
            if (!facts_.wifiPresent) return Gate::Ready;
            if (facts_.wifiConfigured) return Gate::Ready;
            // Skippable when SOMETHING ELSE is already carrying the connection
            // — which is `online`, not `wiredOnline`.
            //
            // THIS WAS KEYED ON ETHERNET AND IT WAS A LATENT DEADLOCK, found by
            // walking every combination of facts rather than by reading it
            // again. The documents say "offered even when Ethernet is up"
            // because Ethernet is the case anybody has; the RULE is "offered
            // when you are already online", and the two only coincide while
            // this console knows about exactly two kinds of link. The day
            // net.cpp counts a third, a machine online over it would pass the
            // network gate and then sit at a Wi-Fi step it could neither
            // satisfy nor skip — setup stuck on a machine that is on the
            // network.
            return facts_.online ? Gate::Skippable : Gate::Blocked;

        case Step::Server:
            return facts_.serverAnswered ? Gate::Ready : Gate::Blocked;

        case Step::Pair:
            return facts_.havePairedToken ? Gate::Ready : Gate::Blocked;

        case Step::Controller:
            // Insistent, not blocking. See the header: refusing to finish
            // without a pad would break the keyboard guarantee this whole flow
            // exists to make.
            return facts_.gamepadCount > 0 ? Gate::Ready : Gate::Skippable;

        case Step::Done:
            return Gate::Ready;
    }
    return Gate::Blocked;
}

std::string Machine::because() const {
    switch (step_) {
        case Step::Network:
            if (facts_.online) return {};
            if (!facts_.wifiPresent)
                return "This console is not on a network, and it has no Wi-Fi. "
                       "Plug in an Ethernet cable — a phone sharing its "
                       "connection over USB works too.";
            return "This console is not on a network yet. Plug in an Ethernet "
                   "cable, or join a Wi-Fi network below.";

        case Step::WiFi:
            if (!facts_.wifiPresent) return "This console has no Wi-Fi.";
            if (facts_.wifiConfigured) return {};
            if (facts_.wiredOnline)
                return "You are online over the cable. Setting up Wi-Fi now "
                       "means the console still works if the cable is ever "
                       "unplugged — and this is the easiest moment to do it, "
                       "while a keyboard is to hand.";
            if (facts_.online)
                return "You are already online. Setting up Wi-Fi now gives the "
                       "console a second way to reach your server.";
            return "Join a network to carry on.";

        case Step::Server:
            if (facts_.serverAnswered) return {};
            if (!facts_.haveServerAddress)
                return "CabinetOS keeps your games on a RomM server. Enter its "
                       "address to carry on.";
            return "Nothing answered at that address.";

        case Step::Pair:
            if (facts_.havePairedToken) return {};
            return "Open the link on a phone or computer that is signed in to "
                   "RomM, and approve this console.";

        case Step::Controller:
            if (facts_.gamepadCount > 0) return {};
            return "No controller is paired. You can finish without one and "
                   "add it later in Settings, but you will not be able to play "
                   "anything from the sofa until you do.";

        case Step::Done:
            return {};
    }
    return {};
}

bool Machine::advance() {
    if (step_ == Step::Done) return false;
    if (gate() != Gate::Ready) return false;
    step_ = static_cast<Step>(static_cast<int>(step_) + 1);
    return true;
}

bool Machine::skip() {
    if (gate() != Gate::Skippable) return false;
    step_ = static_cast<Step>(static_cast<int>(step_) + 1);
    return true;
}

bool Machine::back() {
    if (step_ == Step::Network) return false;
    if (step_ == Step::Done) return false;
    step_ = static_cast<Step>(static_cast<int>(step_) - 1);
    return true;
}

bool Machine::openAt(const std::string& stepName) {
    for (int i = 0; i <= static_cast<int>(Step::Done); ++i) {
        const Step s = static_cast<Step>(i);
        if (stepName == name(s)) { step_ = s; return true; }
    }
    return false;
}

// --- Going and looking ------------------------------------------------------

Facts observe(const romm::Client& client, int gamepadCount) {
    Facts f;

    const net::Status s = net::status();
    f.online = s.online;
    f.wiredOnline = s.ethernetUp;
    f.wifiPresent = s.wifiPresent;
    // A radio that is up is by definition configured; one that is merely
    // enabled is not. Asking NetworkManager for saved profiles would be a
    // second shell-out for a fact the status already implies, and a saved
    // profile that has never connected is not a working fallback anyway.
    f.wifiConfigured = s.wifiUp;

    f.haveServerAddress = !serverAddress().empty();
    f.havePairedToken = client.haveToken() || fileExists(tokenPath());
    // Left to the caller: only something that has tried can say. See the
    // header.
    f.serverAnswered = false;

    f.gamepadCount = gamepadCount;
    return f;
}

}  // namespace firstrun
