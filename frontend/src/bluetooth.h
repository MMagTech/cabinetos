// Pairing a controller, which is the last step of first run and the one that
// makes the whole thing worth doing.
//
// THE REQUIREMENT, from docs/PROJECT.md open question 15b: a keyboard is needed
// exactly once, ever. Setup runs on a keyboard and PAIRS A PAD as its last act,
// and from then on the console is driven from a sofa. Without this step the
// guarantee is not kept — somebody finishes setup owning a games console they
// cannot play.
//
// THE CHICKEN AND EGG THIS AVOIDS. The instinct is to build setup around a
// wired controller, because a console owner owns controllers. But owning a
// Bluetooth pad and having its cable to hand at setup time are different
// things, and most pads sold now are Bluetooth. With a keyboard underneath,
// pairing is simply another step INSIDE setup, driven by our own UI rather than
// by a Linux utility.
//
// --- Why bluetoothctl, and not sd-bus onto bluez ---------------------------
//
// The same reasoning as net.cpp, and it is worth being consistent about: bluez
// is a D-Bus API whose pairing flow involves registering an agent object and
// answering method calls on it, which is a lot of machinery to get subtly wrong
// for one screen. `bluetoothctl` ships with bluez, is already in the image, and
// its non-interactive form does exactly what is needed. Any fault reproduces at
// a shell in one line.
//
// Everything goes through `proc::run`, so a device NAME — which is chosen by
// whoever owns the device, not by us — is an argument and never part of a
// command line. A scan puts strangers' strings into this process.
//
// --- What it deliberately does NOT do --------------------------------------
//
// No passkey entry, no numeric comparison, no PIN. Game controllers pair with
// "just works" association and nothing in this product should be teaching
// somebody to type a passkey with a d-pad. A device that demands one is
// reported as having failed, with its own reason, rather than half-handled.

#pragma once

#include <string>
#include <vector>

namespace bt {

// --- The adapter ------------------------------------------------------------

struct Adapter {
    bool present = false;    // there is a radio at all
    bool powered = false;    // and it is switched on
    std::string name;        // "cabinetos", from the hostname
    std::string address;
};

// THREE STATES AND NOT TWO, which Phase 6 already demands and open question 15b
// repeats one level up: searching, found, and *no adapter — plug something in*.
// A machine with no Bluetooth that is shown a spinner has been lied to, and the
// person is left waiting for something that will never arrive.
Adapter adapter();

// Switches the radio on. A console has no reason to have it off, but it may
// well find it off, and a scan that silently returns nothing on a powered-down
// adapter is the failure this prevents.
bool powerOn(std::string* err);

// --- What is in range -------------------------------------------------------

struct Device {
    std::string address;      // "E4:17:D8:71:F1:ED" — the identity
    std::string name;         // "Pro Controller" — decoration, and untrusted
    bool paired = false;
    bool connected = false;
    // Bluetooth says what KIND of thing it is, and for this screen that is the
    // whole difference between a useful list and a list of everybody's
    // headphones, televisions and phones. Judged from the device's icon and its
    // class of device, both of which bluez reports.
    bool gamepad = false;
};

// Asks the adapter to look for `seconds`, then reports everything it knows
// about. THIS BLOCKS FOR THE WHOLE SCAN — ten seconds is the sensible floor for
// a pad somebody has just put into pairing mode, and it belongs on a worker.
//
// It returns devices that are already paired as well as ones just discovered,
// because "this pad is already set up" is an answer the screen has to be able
// to give rather than a reason to hide the row.
bool scan(int seconds, std::vector<Device>* out, std::string* err);

// What the adapter already knows, without looking again. For drawing something
// immediately while `scan` runs.
bool known(std::vector<Device>* out, std::string* err);

// --- Pairing ----------------------------------------------------------------

// Pair, trust, connect — in that order, and all three matter.
//
// PAIR exchanges the keys. TRUST is what lets the pad reconnect on its own
// afterwards, and it is the step whose absence looks exactly like a pad that
// "keeps disconnecting": without it bluez will refuse the incoming connection
// every time the controller wakes up, forever. CONNECT brings it up now, so the
// setup screen can say the thing that actually matters — press a button and
// watch it respond.
//
// Blocks. Pairing a controller is normally two or three seconds and can be
// thirty when a pad has dropped out of pairing mode.
bool pair(const std::string& address, std::string* err, int timeoutSeconds = 40);

// Drops a device entirely, so a half-finished pairing can be retried cleanly.
// A device bluez has seen but failed to pair with will otherwise sit in its
// cache refusing to pair again.
bool forget(const std::string& address, std::string* err);

// Is there a bluetoothctl to talk to. False means nothing else here will work
// and the console should say so rather than reporting an empty list.
bool available();

}  // namespace bt
