#include "net.h"

#include <unistd.h>

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "proc.h"

namespace net {
namespace {

// Every command here goes through proc::run, which takes an argv array and
// never builds a command line. See proc.h: a scan puts strings chosen by
// strangers into this process, every time it runs.
using proc::lines;
using proc::trimmed;

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
    return proc::run({"nmcli", "--version"}, 5).ok();
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
    const proc::Result r = proc::run({"nmcli", "-t", "-f", "RUNNING", "general"}, 5);
    return r.ok() && trimmed(r.out) == "running";
}

Status status() {
    Status s;
    if (!available()) {
        s.managerMissing = true;
        return s;
    }

    // DEVICE:TYPE:STATE:CONNECTION, one line per device.
    const proc::Result dev = proc::run({"nmcli", "-t", "-f", "DEVICE,TYPE,STATE,CONNECTION",
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
        const proc::Result radio = proc::run({"nmcli", "-t", "-f", "WIFI", "radio"}, 5);
        s.wifiEnabled = radio.ok() && trimmed(radio.out) == "enabled";
    }

    if (!s.device.empty()) {
        const proc::Result ip = proc::run({"nmcli", "-t", "-f", "IP4.ADDRESS", "device", "show",
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
    const proc::Result r = proc::run({"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"}, 10);
    if (!r.ok()) return out;
    for (const std::string& line : lines(r.out)) {
        if (line.empty()) continue;
        const std::vector<std::string> f = splitFields(line);
        if (fieldAt(f, 1) != "802-11-wireless") continue;
        const proc::Result ssid = proc::run({"nmcli", "-t", "-f", "802-11-wireless.ssid",
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
    proc::Result r = proc::run({"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                                "device", "wifi", "list",
                                "--rescan", rescan ? "yes" : "no"},
                               rescan ? 30 : 10);
    // A REFUSED RESCAN IS NOT AN EMPTY SKY. NetworkManager declines
    // `--rescan yes` while a scan it started itself is already running, and it
    // scans on its own schedule — so the commonest reason this fails is that
    // results are on their way. Falling back to the cached list turns a
    // transient conflict into the list NetworkManager already has, instead of a
    // screen saying there is nothing on the air in a house full of routers.
    if (!r.ok() && rescan) {
        r = proc::run({"nmcli", "-t", "-f", "IN-USE,SSID,SIGNAL,SECURITY",
                       "device", "wifi", "list", "--rescan", "no"}, 10);
    }
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
        // A ROW NOBODY CAN PICK SORTS LAST, whatever its signal. 802.1X is
        // refused by decision rather than by omission — it needs a certificate,
        // an identity and an inner method, which is a different form and not
        // one anybody fills in from a sofa — so its row is drawn greyed and
        // cannot be activated. Leaving it ranked by signal put a dead row above
        // live ones in the middle of the list somebody is choosing from.
        if (a.enterprise != b.enterprise) return b.enterprise;
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

    const proc::Result r = proc::run(args, timeoutSeconds + 10);
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
    const proc::Result r = proc::run({"nmcli", "-t", "-f", "NAME,TYPE", "connection", "show"}, 10);
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
        const proc::Result got = proc::run({"nmcli", "-t", "-f", "802-11-wireless.ssid",
                             "connection", "show", name}, 10);
        if (!got.ok()) continue;
        const std::vector<std::string> sf = splitFields(trimmed(got.out));
        if (sf.size() < 2 || sf[1] != ssid) continue;
        if (proc::run({"nmcli", "connection", "delete", name}, 15).ok()) deletedAny = true;
    }
    if (!deletedAny && err) *err = "no saved network by that name";
    return deletedAny;
}

bool setRadio(bool on, std::string* err) {
    if (!available()) {
        if (err) *err = "NetworkManager is not running";
        return false;
    }
    const proc::Result r = proc::run({"nmcli", "radio", "wifi", on ? "on" : "off"}, 15);
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
    const proc::Result r = proc::run({"pkcheck", "--action-id",
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
