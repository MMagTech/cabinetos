#include "net.h"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>

namespace net {
namespace {

// --- Running a command, with no shell anywhere in it ------------------------
//
// See the header: a scan puts strings chosen by strangers into this process, so
// nothing may ever be concatenated into a command line. argv, execvp, done.
//
// stdout and stderr are captured separately because they answer different
// questions: nmcli puts its data on one and its reason for failing on the
// other, and the reason is the half a person can act on.
struct Run {
    int status = -1;      // the process's exit status, or -1 if it never ran
    bool timedOut = false;
    std::string out;
    std::string err;
    bool ok() const { return status == 0; }
};

int64_t nowMs() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<int64_t>(ts.tv_sec) * 1000 + ts.tv_nsec / 1000000;
}

Run run(const std::vector<std::string>& args, int timeoutSeconds) {
    Run r;
    if (args.empty()) return r;

    int outPipe[2], errPipe[2];
    if (pipe(outPipe) != 0) return r;
    if (pipe(errPipe) != 0) {
        close(outPipe[0]);
        close(outPipe[1]);
        return r;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        return r;
    }
    if (pid == 0) {
        // The child. Nothing here may allocate or throw in a way that matters;
        // it is a few dups and an exec.
        dup2(outPipe[1], STDOUT_FILENO);
        dup2(errPipe[1], STDERR_FILENO);
        close(outPipe[0]); close(outPipe[1]);
        close(errPipe[0]); close(errPipe[1]);
        // A child that inherits our stdin can block forever on a prompt.
        // nmcli asks for secrets interactively when it wants them and we never
        // want it to, so stdin is closed rather than passed through.
        const int devnull = open("/dev/null", O_RDONLY);
        if (devnull >= 0) { dup2(devnull, STDIN_FILENO); close(devnull); }

        std::vector<char*> argv;
        argv.reserve(args.size() + 1);
        for (const std::string& a : args) argv.push_back(const_cast<char*>(a.c_str()));
        argv.push_back(nullptr);
        execvp(argv[0], argv.data());
        _exit(127);   // execvp only returns on failure
    }

    close(outPipe[1]);
    close(errPipe[1]);

    // Read both pipes until they close or the deadline passes. A deadline
    // matters more here than it looks: `nmcli device wifi connect` against a
    // network that is simply not answering will sit there, and a console that
    // stops responding is worse than one that says it could not join.
    const int64_t deadline = nowMs() + static_cast<int64_t>(timeoutSeconds) * 1000;
    struct pollfd fds[2] = {
        {outPipe[0], POLLIN, 0},
        {errPipe[0], POLLIN, 0},
    };
    bool open0 = true, open1 = true;
    while (open0 || open1) {
        const int64_t left = deadline - nowMs();
        if (left <= 0) { r.timedOut = true; break; }
        fds[0].events = open0 ? POLLIN : 0;
        fds[1].events = open1 ? POLLIN : 0;
        const int n = poll(fds, 2, static_cast<int>(std::min<int64_t>(left, 1000)));
        if (n < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < 2; ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) continue;
            char buf[4096];
            const ssize_t got = read(fds[i].fd, buf, sizeof buf);
            if (got > 0) {
                (i == 0 ? r.out : r.err).append(buf, static_cast<size_t>(got));
            } else {
                (i == 0 ? open0 : open1) = false;
            }
        }
    }
    close(outPipe[0]);
    close(errPipe[0]);

    if (r.timedOut) {
        kill(pid, SIGTERM);
        // Give it a moment to go politely, then stop being polite. A wedged
        // nmcli left behind becomes a zombie this process never reaps.
        for (int i = 0; i < 20; ++i) {
            int st = 0;
            if (waitpid(pid, &st, WNOHANG) == pid) return r;
            usleep(100000);
        }
        kill(pid, SIGKILL);
        int st = 0;
        waitpid(pid, &st, 0);
        return r;
    }

    int st = 0;
    if (waitpid(pid, &st, 0) == pid && WIFEXITED(st)) r.status = WEXITSTATUS(st);
    return r;
}

// Trailing whitespace off a captured stream. nmcli newline-terminates
// everything and a trailing "\n" in an error message reads badly on a screen.
std::string trimmed(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\n' || s[b] == '\r' || s[b] == '\t')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\n' || s[e - 1] == '\r' || s[e - 1] == '\t')) --e;
    return s.substr(b, e - b);
}

std::vector<std::string> lines(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    while (start <= s.size()) {
        const size_t nl = s.find('\n', start);
        if (nl == std::string::npos) {
            if (start < s.size()) out.push_back(s.substr(start));
            break;
        }
        out.push_back(s.substr(start, nl - start));
        start = nl + 1;
    }
    return out;
}

// THE SPLIT THAT HAS TO BE RIGHT. nmcli --terse separates fields with ':' and
// escapes any ':' or '\' inside a value with a backslash. An SSID may contain
// both, and anybody within radio range picks their own SSID — so a naive
// `split(':')` is not a tidiness bug, it is a stranger deciding how many fields
// this console thinks it received.
std::vector<std::string> splitFields(const std::string& line) {
    std::vector<std::string> out;
    std::string cur;
    for (size_t i = 0; i < line.size(); ++i) {
        const char c = line[i];
        if (c == '\\' && i + 1 < line.size()) {
            cur.push_back(line[++i]);   // an escaped ':' or '\' — taken literally
        } else if (c == ':') {
            out.push_back(cur);
            cur.clear();
        } else {
            cur.push_back(c);
        }
    }
    out.push_back(cur);
    return out;
}

const std::string& fieldAt(const std::vector<std::string>& f, size_t i) {
    static const std::string kEmpty;
    return i < f.size() ? f[i] : kEmpty;
}

bool haveNmcli() {
    // `nmcli --version` rather than looking for the file: a binary that is
    // present and cannot run answers the same question the wrong way.
    return run({"nmcli", "--version"}, 5).ok();
}

}  // namespace

std::string unescapeField(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\\' && i + 1 < s.size()) out.push_back(s[++i]);
        else out.push_back(s[i]);
    }
    return out;
}

bool available() {
    if (!haveNmcli()) return false;
    const Run r = run({"nmcli", "-t", "-f", "RUNNING", "general"}, 5);
    return r.ok() && trimmed(r.out) == "running";
}

Status status() {
    Status s;
    if (!available()) {
        s.managerMissing = true;
        return s;
    }

    // DEVICE:TYPE:STATE:CONNECTION, one line per device.
    const Run dev = run({"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION",
                         "device", "status"}, 10);
    if (!dev.ok()) {
        s.managerMissing = true;
        return s;
    }

    // WHICH DEVICE IS "THE" CONNECTION, when more than one is up. Wired wins.
    // It is the faster link and the one a console under a television is
    // normally on, and — more to the point — the Wi-Fi step's whole question is
    // "is there already another way out of here", which is exactly this.
    for (const std::string& line : lines(dev.out)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = splitFields(line);
        const std::string& device = fieldAt(f, 0);
        const std::string& type = fieldAt(f, 1);
        const std::string& state = fieldAt(f, 2);
        const std::string& conn = fieldAt(f, 3);
        // `connected` exactly. NetworkManager also says "connected (externally)"
        // for loopback and for devices somebody else brought up, and
        // "connecting" for one that has not arrived yet; neither is online.
        const bool up = state == "connected";

        if (type == "ethernet") {
            s.ethernetPresent = true;
            if (up && !s.ethernetUp) {
                s.ethernetUp = true;
                s.online = true;
                s.link = Link::Ethernet;
                s.device = device;
                s.connection = conn;
            }
        } else if (type == "wifi") {
            s.wifiPresent = true;
            if (up && !s.wifiUp) {
                s.wifiUp = true;
                if (!s.online) {
                    s.online = true;
                    s.link = Link::WiFi;
                    s.device = device;
                    s.connection = conn;
                }
            }
        }
        // Everything else — wifi-p2p, loopback, bridges, tun — is deliberately
        // ignored. A console is online over a cable or a radio; a machine whose
        // only route is a tunnel is not a case this product has.
    }

    if (s.wifiPresent) {
        const Run radio = run({"nmcli", "-t", "-f", "WIFI", "radio"}, 5);
        s.wifiEnabled = radio.ok() && trimmed(radio.out) == "enabled";
    }

    if (!s.device.empty()) {
        const Run ip = run({"nmcli", "-t", "-f", "IP4.ADDRESS", "device", "show",
                            s.device}, 10);
        if (ip.ok()) {
            for (const std::string& line : lines(ip.out)) {
                const std::vector<std::string> f = splitFields(line);
                if (f.size() >= 2 && !f[1].empty()) { s.ipv4 = f[1]; break; }
            }
        }
    }
    return s;
}

namespace {

// Every SSID NetworkManager already holds a saved connection for.
//
// Read from the connection's own `802-11-wireless.ssid` rather than from its
// NAME. nmcli names a profile after the SSID by default, which makes the two
// look interchangeable right up until somebody renames one — and then the
// console offers to type a passphrase for a network it already knows.
std::vector<std::string> savedSsids() {
    std::vector<std::string> out;
    const Run r = run({"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"}, 10);
    if (!r.ok()) return out;
    for (const std::string& line : lines(r.out)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = splitFields(line);
        if (fieldAt(f, 1) != "802-11-wireless") continue;
        const Run ssid = run({"nmcli", "-t", "-f", "802-11-wireless.ssid",
                              "connection", "show", fieldAt(f, 0)}, 10);
        if (!ssid.ok()) continue;
        const std::vector<std::string> sf = splitFields(trimmed(ssid.out));
        if (sf.size() >= 2 && !sf[1].empty()) out.push_back(sf[1]);
    }
    return out;
}

bool listWifi(bool rescan, std::vector<Network>* out, std::string* err) {
    if (!out) return false;
    out->clear();
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }

    // A scan is the slow call in this file, which is why it is separated from
    // cachedScan: a screen can draw the cached list at once and replace it when
    // this returns.
    const Run r = run({"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                       "device", "wifi", "list",
                       "--rescan", rescan ? "yes" : "no"},
                      rescan ? 30 : 10);
    if (!r.ok()) {
        if (err) {
            *err = r.timedOut ? "the scan did not finish"
                              : trimmed(r.err).empty() ? "nmcli could not list networks"
                                                       : trimmed(r.err);
        }
        return false;
    }

    const std::vector<std::string> saved = savedSsids();

    for (const std::string& line : lines(r.out)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = splitFields(line);
        Network n;
        n.active = fieldAt(f, 0) == "*";
        n.ssid = fieldAt(f, 1);
        // A network that does not broadcast its name comes back with an empty
        // SSID. There is nothing to show and nothing to tap, and the way to
        // join one is the hidden-network path where the name is typed — so it
        // is dropped here rather than drawn as a blank row.
        if (n.ssid.empty()) continue;
        n.signal = std::atoi(fieldAt(f, 2).c_str());
        n.security = fieldAt(f, 3);
        n.secured = !n.security.empty();
        n.enterprise = n.security.find("802.1X") != std::string::npos ||
                       n.security.find("EAP") != std::string::npos;
        n.known = std::find(saved.begin(), saved.end(), n.ssid) != saved.end();

        // One row per network, not one per access point. A house with three
        // mesh nodes broadcasts the same SSID three times and nmcli lists all
        // of them; three identical rows is a list nobody can use. The strongest
        // wins, which is also the one NetworkManager would pick.
        auto same = std::find_if(out->begin(), out->end(),
                                 [&](const Network& x) { return x.ssid == n.ssid; });
        if (same == out->end()) {
            out->push_back(n);
        } else if (n.signal > same->signal) {
            const bool wasActive = same->active;
            *same = n;
            same->active = same->active || wasActive;
        } else if (n.active) {
            same->active = true;
        }
    }

    std::sort(out->begin(), out->end(), [](const Network& a, const Network& b) {
        if (a.active != b.active) return a.active;      // the one we are on, first
        if (a.known != b.known) return a.known;         // then ones needing no typing
        return a.signal > b.signal;
    });
    return true;
}

}  // namespace

bool scan(std::vector<Network>* out, std::string* err) { return listWifi(true, out, err); }
bool cachedScan(std::vector<Network>* out, std::string* err) { return listWifi(false, out, err); }

bool join(const std::string& ssid, const std::string& passphrase, bool hidden,
          std::string* err, int timeoutSeconds) {
    if (ssid.empty()) {
        if (err) *err = "no network name";
        return false;
    }
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }

    // `--wait` is nmcli's own deadline and `run`'s is the backstop for nmcli
    // itself wedging. The backstop is longer on purpose: a process killed at
    // exactly its own timeout never gets to print why it failed, and that
    // message is the only actionable thing a wrong passphrase produces.
    std::vector<std::string> args = {
        "nmcli", "--wait", std::to_string(timeoutSeconds),
        "device", "wifi", "connect", ssid,
    };
    if (!passphrase.empty()) {
        args.push_back("password");
        args.push_back(passphrase);
    }
    if (hidden) {
        args.push_back("hidden");
        args.push_back("yes");
    }

    const Run r = run(args, timeoutSeconds + 10);
    if (r.ok()) return true;

    if (err) {
        const std::string why = trimmed(r.err).empty() ? trimmed(r.out) : trimmed(r.err);
        *err = r.timedOut ? "the network did not answer in time"
               : why.empty() ? "could not join that network"
                             : why;
        // SAY WHEN THE PRIVILEGE IS THE PROBLEM, because it is the one failure
        // whose message points nowhere near its cause. Joining succeeds and
        // saving the profile does not, so the console comes up on a network it
        // has no memory of, every boot, forever.
        if (why.find("not authorized") != std::string::npos ||
            why.find("Not authorized") != std::string::npos ||
            why.find("insufficient privileges") != std::string::npos) {
            *err += " — this console is not permitted to save a network "
                    "(polkit says " + polkitVerdict() + "); see "
                    "60-cabinetos-network.rules";
        }
    }
    return false;
}

bool forget(const std::string& ssid, std::string* err) {
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }
    // EVERY profile for the SSID, not the first. NetworkManager will hold two
    // for one network without complaint, and deleting one of them leaves the
    // console rejoining with the other — which looks exactly like "forget did
    // nothing".
    const Run r = run({"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"}, 10);
    if (!r.ok()) {
        if (err) *err = "could not list saved networks";
        return false;
    }
    bool deletedAny = false;
    for (const std::string& line : lines(r.out)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = splitFields(line);
        if (fieldAt(f, 1) != "802-11-wireless") continue;
        const std::string name = fieldAt(f, 0);
        const Run got = run({"nmcli", "-t", "-f", "802-11-wireless.ssid",
                             "connection", "show", name}, 10);
        if (!got.ok()) continue;
        const std::vector<std::string> sf = splitFields(trimmed(got.out));
        if (sf.size() < 2 || sf[1] != ssid) continue;
        if (run({"nmcli", "connection", "delete", name}, 15).ok()) deletedAny = true;
    }
    if (!deletedAny && err) *err = "no saved network by that name";
    return deletedAny;
}

bool setRadio(bool on, std::string* err) {
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }
    const Run r = run({"nmcli", "radio", "wifi", on ? "on" : "off"}, 15);
    if (r.ok()) return true;
    if (err) *err = trimmed(r.err).empty() ? "could not change the radio" : trimmed(r.err);
    return false;
}

std::string polkitVerdict() {
    // Asked about THIS process, which is the subject that will actually make
    // the request. Asking about anything else answers a different question —
    // the grant depends on the session the caller is in, which is why the same
    // check from an SSH shell and from the console can legitimately disagree.
    const std::string pid = std::to_string(static_cast<long>(getpid()));
    const Run r = run({"pkcheck", "--action-id",
                       "org.freedesktop.NetworkManager.settings.modify.system",
                       "--process", pid}, 10);
    if (r.status == -1) return "unknown (pkcheck did not run)";

    // pkcheck prints SEVERAL `key=value` lines, not one, and the keys are
    // written with the dot escaped:
    //
    //     polkit\56result=auth_admin_keep
    //     polkit\56retains_authorization_after_challenge=1
    //
    // So the answer is the line whose key ends in `result`, and taking the last
    // '=' in the output reports `1` — which is not even one of the values this
    // action can have. Measured on both machines before it was written this way.
    for (const std::string& line : lines(r.out)) {
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = line.substr(0, eq);
        if (key.size() >= 6 && key.compare(key.size() - 6, 6, "result") == 0)
            return trimmed(line.substr(eq + 1));
    }
    if (r.ok()) return "yes";
    const std::string e = trimmed(r.err);
    return e.empty() ? "no" : e;
}

}  // namespace net
