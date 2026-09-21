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
#include "accounts.h"
#include "storage.h"

namespace firstrun {
namespace {

std::string markerPath() { return storage::configDir() + "/first-run.json"; }
std::string serverPath() { return storage::configDir() + "/server.json"; }

// The token, which is a credential and lives under the session user rather than
// in the console's storage tree. Same path main.cpp uses; kept in step with it
// deliberately rather than shared, because this file must not drag main's
// argument handling in behind it.
// WHAT "PAIRED" MEANS, AND IT CHANGED ON 2026-09-21. It used to be the single
// `~/.config/cabinetos/romm.json`. Pairing now writes an ACCOUNT, so the fact
// to test is whether this console has one — `accounts::activeId()` is true
// only when the list holds somebody and one of them is active.
//
// There is deliberately no fallback to the old file. Open question 26: nobody
// else is running this, the two machines here were written into accounts.json
// by hand, and a permanent branch serving users who do not exist is the
// migration tool the folder layout already refused to add.
bool havePairedAccount() { return accounts::activeId() > 0; }

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
    const bool haveToken = havePairedAccount();
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
            //
            // AND IT IS THE WHOLE OF THE WI-FI RULE TOO, now that the two steps
            // are one. "Offered even when Ethernet is up, skippable then and
            // required otherwise" is exactly `online ? Ready : Blocked` — the
            // list is drawn either way, and being online is what decides
            // whether anybody has to touch it. The old second step needed its
            // own Skip button to say the same thing, and then said it on a
            // screen headed "Set up Wi-Fi" to somebody who had just set up
            // Wi-Fi.
            return facts_.online ? Gate::Ready : Gate::Blocked;

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

// One line, and it assumes a competent adult.
//
// MMagTech, 2026-09-20, on the first draft of these: *"if you have a RomM
// server and can install an OS I shouldn't need to tell you in depth how to
// pair a controller."* That is the right test, and it is a stronger one than
// "is this clear" — the person in front of this screen has already stood up a
// self-hosted web application and written an operating system to a USB stick.
// Explaining what a pairing button is insults them and buries the one thing
// they actually need, which is what this step will and will not let them do.
//
// So each of these says the CONSTRAINT and stops. Required or optional, and
// why if the why is not obvious. No instructions for things that have one
// obvious way to do them.
std::string Machine::because() const {
    switch (step_) {
        case Step::Network:
            // SIX ANSWERS, AND THE WORD "REQUIRED" OR "OPTIONAL" IS IN ALL THE
            // ONES WHERE IT IS IN QUESTION.
            //
            // MMagTech, 2026-09-20: *"the wording needs to say something along
            // the lines of wifi is optional but not required and probably
            // something different if nothing is plugged in."* The old line
            // described what Wi-Fi would buy and never said you could walk past
            // it, and the offline line said a connection was required without
            // saying what to do about it. Neither is a sentence somebody can
            // act on.
            if (!facts_.online) {
                // Nothing is carrying the connection, so this is the hard gate
                // and the sentence has to end in an instruction.
                // NO PHONE TETHERING ON THIS SCREEN, and that reverses what
                // open question 17 decided.
                //
                // It used to end "or share a phone's connection over USB",
                // because tethering presents as an ordinary wired device and
                // nobody thinks of it. MMagTech, 2026-09-20: *"i dont know if i
                // like the idea of the phone option, leaves a lot of potential
                // on me when this doesn't work for people."*
                //
                // He is right, and checking the image settles it. Android
                // tethering is pure kernel — rndis_host and cdc_ncm are in the
                // image and it simply appears as a wired device. **iPhone
                // tethering needs usbmuxd**, which is installed but inactive
                // and `static`, and it needs the phone to TRUST the computer:
                // a prompt, an unlock and a pairing step, none of which anybody
                // here has ever run on this console.
                //
                // So it is a promise that holds for one phone ecosystem and is
                // untested for the other, offered on the one screen where
                // somebody is already stuck and out of options. A console
                // should not suggest a fix it has never seen work. It stays in
                // the documentation as a trick; it does not go on a television.
                if (!facts_.wifiPresent)
                    return "A network connection is required. Plug in a cable.";
                return "A network connection is required. Pick a network to "
                       "join, or plug in a cable.";
            }
            // Online. What is carrying it, and whether anything below is worth
            // touching.
            if (facts_.wiredOnline && facts_.wifiConfigured)
                return "Connected over Ethernet, with Wi-Fi set up as a "
                       "fallback.";
            if (facts_.wifiConfigured) return "Connected over Wi-Fi.";
            if (!facts_.wifiPresent) return "Connected over Ethernet.";
            return "Connected over Ethernet. Wi-Fi is optional — setting it up "
                   "now gives the console a way back if the cable is ever "
                   "unplugged.";

        case Step::Server:
            if (facts_.serverAnswered) return {};
            if (!facts_.haveServerAddress)
                return "Enter the address of your RomM server.";
            if (!facts_.serverChecked) return "Checking…";
            return "Nothing answered at that address.";

        case Step::Pair:
            if (facts_.havePairedToken) return {};
            return "Approve this console in a browser signed in to RomM.";

        case Step::Controller:
            if (facts_.gamepadCount > 0) return {};
            // SAY WHAT SKIPPING COSTS, in the same breath as saying it is
            // allowed. Without the second half this reads as "this does not
            // matter", and the person finds out it did on the last screen.
            return "Optional, but without one you will still need the keyboard.";

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

Facts observeLocal(const romm::Client& client, int gamepadCount) {
    Facts f;
    f.haveServerAddress = !serverAddress().empty();
    // Only something that has tried can set these two; see the header.
    f.serverChecked = false;
    f.havePairedToken = client.haveToken() || havePairedAccount();
    // Left to the caller: only something that has tried can say. See the
    // header.
    f.serverAnswered = false;

    f.gamepadCount = gamepadCount;
    return f;
}

Facts observe(const romm::Client& client, int gamepadCount) {
    Facts f = observeLocal(client, gamepadCount);
    const net::Status s = net::status();
    f.online = s.online;
    f.wiredOnline = s.ethernetUp;
    f.wifiPresent = s.wifiPresent;
    // A radio that is up is by definition configured; one that is merely
    // enabled is not. Asking NetworkManager for saved profiles would be a
    // second shell-out for a fact the status already implies, and a saved
    // profile that has never connected is not a working fallback anyway.
    f.wifiConfigured = s.wifiUp;
    return f;
}

}  // namespace firstrun
