#include "bluetooth.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>

#include "proc.h"

namespace bt {
namespace {

using proc::lines;
using proc::trimmed;

// A `Key: value` line out of `bluetoothctl info`, which indents everything
// after the first line with a tab.
std::string field(const std::string& body, const char* key) {
    const std::string want = std::string(key) + ":";
    for (const std::string& raw : lines(body)) {
        const std::string line = trimmed(raw);
        if (line.compare(0, want.size(), want) != 0) continue;
        return trimmed(line.substr(want.size()));
    }
    return {};
}

// Is this thing a game controller?
//
// TWO ANSWERS, BECAUSE NEITHER IS ALWAYS THERE. bluez fills `Icon` once it has
// resolved the device, and `input-gaming` is unambiguous when present. A pad
// that has only just been discovered may have no icon yet but will still carry
// its class of device, so the class is the fallback.
//
// The class is 24 bits: bits 8..12 are the MAJOR class and 5 is "peripheral",
// which covers keyboards, mice and gamepads alike. That is deliberately the
// wider test — a peripheral in a list somebody is choosing a controller from is
// worth showing, and a television is not.
bool looksLikeGamepad(const std::string& icon, const std::string& classText) {
    if (icon == "input-gaming" || icon == "input-joystick") return true;
    if (classText.empty()) return false;
    // "0x00002508 (9480)" — take the hex, which is what bluez prints first.
    const size_t x = classText.find("0x");
    if (x == std::string::npos) return false;
    const unsigned long cod = std::strtoul(classText.c_str() + x, nullptr, 16);
    return ((cod >> 8) & 0x1F) == 0x05;
}

// BLUEZ USES THE ADDRESS AS THE NAME WHEN A DEVICE HAS NOT GIVEN ONE, with
// dashes where the address has colons:
//
//     Device 52:BE:86:6B:D9:90 52-BE-86-6B-D9-90
//
// So a device with no name does not have an EMPTY name, it has a name that
// looks like a name and is not one. Anything deciding whether a row is worth
// showing has to know that, or a scan in a block of flats fills the list a
// person is picking their controller out of with six of the neighbours'
// beacons — which is exactly what it did.
bool nameIsJustTheAddress(const std::string& name, const std::string& address) {
    if (name.size() != address.size()) return false;
    for (size_t i = 0; i < name.size(); ++i) {
        const char a = name[i];
        const char b = address[i];
        if (a == b) continue;
        // The one substitution bluez makes.
        if (b == ':' && a == '-') continue;
        // Addresses are hex; bluez is not consistent about its case.
        if (std::tolower(static_cast<unsigned char>(a)) ==
            std::tolower(static_cast<unsigned char>(b)))
            continue;
        return false;
    }
    return true;
}

bool fill(Device* d) {
    const proc::Result info = proc::run({"bluetoothctl", "info", d->address}, 10);
    if (!info.ok()) return false;
    const std::string name = field(info.out, "Name");
    if (!name.empty()) d->name = name;
    if (nameIsJustTheAddress(d->name, d->address)) d->name.clear();
    d->paired = field(info.out, "Paired") == "yes";
    d->connected = field(info.out, "Connected") == "yes";
    d->gamepad = looksLikeGamepad(field(info.out, "Icon"), field(info.out, "Class"));
    return true;
}

// `Device <MAC> <Name>` lines, from `bluetoothctl devices`.
//
// The name is everything after the address and MAY CONTAIN ANYTHING, including
// spaces and colons, because its owner chose it. It is only ever drawn, never
// parsed, and never passed anywhere but proc::run's argv.
std::vector<Device> parseDeviceList(const std::string& body) {
    std::vector<Device> out;
    for (const std::string& raw : lines(body)) {
        const std::string line = trimmed(raw);
        if (line.compare(0, 7, "Device ") != 0) continue;
        const std::string rest = line.substr(7);
        const size_t sp = rest.find(' ');
        Device d;
        d.address = sp == std::string::npos ? rest : rest.substr(0, sp);
        // A bare MAC with no name is a device bluez has not resolved yet. Keep
        // it: it may be exactly the pad somebody has just woken up, and a row
        // showing an address beats a pad that does not appear at all.
        if (sp != std::string::npos) d.name = trimmed(rest.substr(sp + 1));
        if (d.address.size() != 17) continue;   // not an address; skip the line
        if (nameIsJustTheAddress(d.name, d.address)) d.name.clear();
        out.push_back(std::move(d));
    }
    return out;
}

// Pads sort above headphones, and a pad that is already set up sorts above one
// that is not.
void order(std::vector<Device>* v) {
    std::stable_sort(v->begin(), v->end(), [](const Device& a, const Device& b) {
        if (a.gamepad != b.gamepad) return a.gamepad;
        if (a.connected != b.connected) return a.connected;
        if (a.paired != b.paired) return a.paired;
        return false;
    });
}

// A scan in a block of flats can find dozens of devices and each one costs an
// `info` call. Thirty-two is well past any living room and bounds the worst
// case at a few seconds.
constexpr size_t kMaxDevices = 32;

std::vector<Device> describe(std::vector<Device> found) {
    if (found.size() > kMaxDevices) found.resize(kMaxDevices);
    for (Device& d : found) fill(&d);
    order(&found);
    return found;
}

}  // namespace

bool available() { return proc::run({"bluetoothctl", "--version"}, 5).ok(); }

Adapter adapter() {
    Adapter a;
    if (!available()) return a;
    const proc::Result r = proc::run({"bluetoothctl", "show"}, 10);
    if (!r.ok()) return a;
    // With no adapter at all, `show` says so rather than printing a controller.
    if (r.out.find("Controller") == std::string::npos) return a;
    a.present = true;
    a.powered = field(r.out, "Powered") == "yes";
    a.name = field(r.out, "Name");
    // "Controller 50:BB:B5:A2:33:9B (public)"
    const size_t at = r.out.find("Controller ");
    if (at != std::string::npos && r.out.size() >= at + 11 + 17)
        a.address = r.out.substr(at + 11, 17);
    return a;
}

bool powerOn(std::string* err) {
    if (!available()) {
        if (err) *err = "bluetoothctl is not installed";
        return false;
    }
    const proc::Result r = proc::run({"bluetoothctl", "power", "on"}, 15);
    if (r.ok()) return true;
    if (err) *err = trimmed(r.err).empty() ? "could not switch the radio on" : trimmed(r.err);
    return false;
}

bool known(std::vector<Device>* out, std::string* err) {
    if (!out) return false;
    out->clear();
    if (!available()) {
        if (err) *err = "bluetoothctl is not installed";
        return false;
    }
    const proc::Result r = proc::run({"bluetoothctl", "devices"}, 10);
    if (!r.ok()) {
        if (err) *err = "could not list devices";
        return false;
    }
    *out = describe(parseDeviceList(r.out));
    return true;
}

bool scan(int seconds, std::vector<Device>* out, std::string* err) {
    if (!out) return false;
    out->clear();
    if (!available()) {
        if (err) *err = "bluetoothctl is not installed";
        return false;
    }
    const Adapter a = adapter();
    if (!a.present) {
        // The third state, said plainly. See bluetooth.h.
        if (err) *err = "this console has no Bluetooth";
        return false;
    }
    if (!a.powered && !powerOn(err)) return false;

    if (seconds < 1) seconds = 1;
    // `--timeout` is bluetoothctl's own: it runs the command, waits, and exits.
    // The proc deadline is longer, so a tool that overruns its own timeout is
    // still given the chance to print why.
    proc::run({"bluetoothctl", "--timeout", std::to_string(seconds), "scan", "on"},
              seconds + 15);
    // The scan's exit status is deliberately ignored. It reports a failure when
    // discovery was already running — which is not a failure, it is two scans
    // overlapping — and the question that matters is what the adapter knows
    // afterwards, which the next call asks directly.
    return known(out, err);
}

bool pair(const std::string& address, std::string* err, int timeoutSeconds) {
    if (address.empty()) {
        if (err) *err = "no device";
        return false;
    }
    if (!available()) {
        if (err) *err = "bluetoothctl is not installed";
        return false;
    }

    const proc::Result p = proc::run({"bluetoothctl", "pair", address}, timeoutSeconds);
    // ALREADY PAIRED IS A SUCCESS, NOT A FAILURE. A pad that was set up before
    // and has been picked out of the list again must not be reported as broken,
    // and that is the common case on a machine being set up a second time.
    const bool alreadyPaired = p.out.find("already") != std::string::npos ||
                               p.err.find("already") != std::string::npos;
    if (!p.ok() && !alreadyPaired) {
        if (err) {
            const std::string why = trimmed(p.err).empty() ? trimmed(p.out) : trimmed(p.err);
            *err = p.timedOut ? "the controller did not answer. Put it back "
                                "into pairing mode and try again"
                   : why.empty() ? "could not pair with that controller"
                                 : why;
        }
        return false;
    }

    // TRUST IS THE STEP WHOSE ABSENCE LOOKS LIKE A BROKEN PAD. Without it bluez
    // refuses the incoming connection every time the controller wakes up, so
    // the pad pairs perfectly once and then never reconnects — which reads as
    // "it keeps disconnecting" and has nothing to do with pairing.
    if (!proc::run({"bluetoothctl", "trust", address}, 15).ok()) {
        if (err)
            *err = "paired, but this console could not mark the controller "
                   "trusted, so it may not reconnect on its own";
        return false;
    }

    // Bringing it up now is what lets the screen say the only thing that
    // actually proves it worked: press a button and watch it respond.
    const proc::Result c = proc::run({"bluetoothctl", "connect", address}, timeoutSeconds);
    if (!c.ok()) {
        if (err)
            *err = "paired and trusted, but it is not connected yet. Press a "
                   "button on the controller to wake it";
        // Deliberately NOT a failure. The lasting state — paired and trusted —
        // is correct, and a pad that is merely asleep is the commonest reason
        // this last step does not take.
        return true;
    }
    return true;
}

bool forget(const std::string& address, std::string* err) {
    if (!available()) {
        if (err) *err = "bluetoothctl is not installed";
        return false;
    }
    if (proc::run({"bluetoothctl", "remove", address}, 15).ok()) return true;
    if (err) *err = "this console does not know that controller";
    return false;
}

}  // namespace bt
