// First run: the order setup happens in, and the rules about what may be
// skipped. No screens, no drawing, no network calls.
//
// THE REQUIREMENT THIS EXISTS TO MEET, from docs/PROJECT.md open question 15b:
//
//     A keyboard is needed exactly once, ever. After first run the console must
//     never require one again, for anything.
//
// It is testable, which is why it is the requirement rather than a principle.
// A keyboard is the one input an installed machine is guaranteed to have had —
// the firmware boot menu needs one, so a machine cannot reach an installed
// state without one having been present. A controller is NOT guaranteed: owning
// a Bluetooth pad and having its cable to hand at setup time are different
// things. So setup runs on a keyboard and PAIRS a controller as its last step.
//
// --- The chain, and why it is a line and not a tree -------------------------
//
//     keyboard -> network -> RomM server -> pair pad 1 -> unplug the keyboard
//
// MMagTech, 2026-09-19: *you cannot have kept games until a server has been
// paired and you have kept one.* So first run ASSUMES A SERVER and is a linear
// path to pairing one. There is no "set this up later" and no "use it without
// a server", because on the other side of a skip there is nothing to show — no
// library, no covers, no saves, no user and no kept games. The only thing a
// skip could offer is the stand-in demo library, which PROJECT.md says must
// never appear on a console because it looks like a working machine showing
// somebody else's games.
//
// That is a simplification rather than a restriction: every step may assume the
// one before it succeeded, and the last one may assume a real library exists to
// drop the person into.
//
// AND IT IS WHY THE OFFLINE CONSOLE IS NOT A BRANCH OF THIS. Open question 22
// is a machine that HAS been paired and now cannot reach its server, so it may
// assume it knows the user, the library it last saw and which games are kept.
// "No server yet" and "no server right now" are different problems and only the
// second has anything to work with. Nothing in this file is the offline case.
//
// --- Why the facts are handed in rather than fetched ------------------------
//
// `Machine` is given a `Facts` and judges it. It never calls the network, the
// disk or a server itself. That is the same split `screens::` makes — a screen
// returns an Action and the app decides whether it can — and it buys the same
// thing: the whole flow can be walked in a test, at any point in it, on a
// machine with no network, no server and no pad attached. `observe()` below is
// the one place that goes and looks, and it is deliberately separate.

#pragma once

#include <string>

namespace romm { class Client; }

namespace firstrun {

// --- Is this the first run at all -------------------------------------------
//
// THE HARD PART OF THIS QUESTION IS THE MACHINE THAT WAS SET UP BY HAND. The
// reference console was: somebody SSHed in and wrote /etc/cabinetos/session.env
// and a token, which is exactly what this flow will one day produce. It has
// never seen a setup screen and must never be shown one — a console that boots
// into a wizard after a year of use is a far worse failure than one that skips
// a wizard it did not need.
//
// So the answer is in two parts, and the second matters more than the first:
//
//   1. A marker file says setup finished. Written by `markCompleted`.
//   2. With no marker, a machine that ALREADY HAS everything setup produces —
//      a server address, a token, and a user behind that token — is taken as
//      set up, and the marker is back-filled saying so.
//
// Rule 2 is not a convenience. It is the difference between "has this flow been
// run" and "is this machine configured", and only the second is the question
// anybody cares about.
enum class Why {
    NotRun,          // no marker, and the machine is not configured either
    Completed,       // the marker says somebody walked the flow
    AdoptedExisting, // no marker, but the machine was already set up by hand
};

struct Completion {
    bool done = false;
    Why why = Why::NotRun;
    std::string when;    // ISO-8601, or empty
};

// Reads the marker and, when there is none, judges the machine. Cheap: two
// file tests and a look at the environment. Safe to call at startup.
Completion completion();

// Writes the marker. `adopted` records WHICH of the two answers above put it
// there, because "somebody walked the flow" and "we found a configured machine"
// are different facts and the difference will matter one day.
bool markCompleted(bool adopted, std::string* err);

// --- The server address, which is first run's one lasting output ------------
//
// WHERE IT LIVES AND WHY IT IS NOT /etc. The session script reads
// /etc/cabinetos/session.env, which is root-owned; the session runs as an
// unprivileged user and cannot write it. Rather than invent a privileged helper
// for one string that is not a secret, first run writes its own file next to
// the console's other machine-local state, and `main` reads both.
//
// ROOT'S ANSWER WINS. /etc/cabinetos/session.env is what root put there and is
// the documented way to set a console up by hand; a file the session wrote must
// not silently override it. So the order is: --romm, then $CABINETOS_ROMM, then
// session.env itself, then this. First run only ever writes its own file when
// the two before it said nothing.
//
// WHY session.env IS READ DIRECTLY AND NOT ONLY THROUGH THE ENVIRONMENT. The
// session script sources it and exports it, so a frontend started by the
// session sees it either way — but one started over SSH does not, and that is
// how every check of this gets made. Without this, `--first-run` on the
// reference console reports "NEEDED" on a machine that has been set up and
// working for a day. An instrument that lies about the thing it exists to
// report is worse than no instrument, which this project has now paid for
// twice.
std::string serverAddress();

// Which of the four it came from, for a report that has to be trusted.
std::string serverAddressSource();

bool setServerAddress(const std::string& address, std::string* err);

// --- The steps --------------------------------------------------------------

enum class Step {
    // A link, and it is the ONE HARD GATE in the whole flow. MMagTech,
    // 2026-09-19: the entirety of this OS relies on a RomM server, so a console
    // that cannot reach a network cannot be set up and must not pretend
    // otherwise. With no cable, joining a Wi-Fi network is the only way past
    // this step.
    Network,

    // OFFERED EVEN WHEN ETHERNET IS ALREADY UP, and skippable only then. This
    // reverses what open question 17 used to say, and the reversal is the point:
    // Wi-Fi is not a duplicate of the cable, it is the FALLBACK FOR LOSING IT.
    // A console under a television is exactly where a cable gets tripped over
    // or borrowed, and a machine whose only route to its server is one cable is
    // one accident away from being a brick.
    //
    // And setup is the one moment the fallback is cheap to configure, because
    // it is the one moment a real keyboard is near-certain. Changing a Wi-Fi
    // password later from a sofa is not.
    WiFi,

    // The address of the RomM server. The ONLY thing anybody types in the whole
    // of first run, which is what the QR code at the next step buys.
    Server,

    // Device authorisation: the server hands back a URL and a short code, and
    // somebody approves it in a browser they are already signed in to. A
    // console has no browser, so the URL goes on the screen as a QR code and a
    // phone reads it off the television. NO PASSWORD EVER REACHES THIS PROGRAM.
    //
    // A phone is therefore a real dependency of setup and should be stated as
    // one rather than discovered.
    Pair,

    // Pad one. INSISTENT BUT NOT A HARD BLOCK: the input model says a keyboard
    // must keep working forever, so refusing to finish without a controller
    // would break the very guarantee this flow exists to make. But somebody who
    // skips it owns a games console they cannot play from a sofa, and Settings
    // has to offer it again.
    Controller,

    Done,
};

const char* name(Step s);

// What the flow may do at the step it is on.
enum class Gate {
    Blocked,    // the condition is not met and there is no way past it
    Ready,      // met; `advance()` will move on
    Skippable,  // not met, but this one may be passed over
};

// Everything the rules are judged against. Nothing here is fetched by
// `Machine`; see the note at the top of this file.
struct Facts {
    // Any device NetworkManager calls connected. Deliberately not "the internet
    // is reachable" — RomM is on the LAN, and a console behind a dead uplink
    // can still be set up.
    bool online = false;
    // Online by something that is NOT the radio. The only thing that makes the
    // Wi-Fi step skippable.
    bool wiredOnline = false;
    bool wifiPresent = false;      // there is a radio at all
    bool wifiConfigured = false;   // a Wi-Fi connection is saved

    bool haveServerAddress = false;
    bool serverAnswered = false;   // something spoke RomM at that address
    // WHETHER ANYBODY HAS ACTUALLY ASKED. Without this, "we have an address and
    // it has not answered" and "we have an address and have not tried it yet"
    // are the same state, and the console tells somebody their perfectly good
    // server did not answer before it has sent a single packet at it. Arriving
    // at this step with an address already in /etc is the COMMON case, not an
    // edge one.
    bool serverChecked = false;
    bool havePairedToken = false;

    int gamepadCount = 0;
};

class Machine {
public:
    void update(const Facts& f) { facts_ = f; }
    const Facts& facts() const { return facts_; }

    Step step() const { return step_; }
    Gate gate() const;

    // One line saying why the gate is what it is, written for a person and not
    // for a log. Empty when a step is simply ready.
    //
    // A SETUP SCREEN NOBODY CAN OPERATE, WITH NO EXPLANATION, IS WORSE THAN A
    // BLACK ONE — open question 15b says so about the no-input case and it is
    // the same rule here. Every Blocked has a sentence.
    std::string because() const;

    // Moves on when the gate allows it. Returns false when it does not, which
    // is the whole of the enforcement: a caller cannot skip a hard gate by
    // asking twice.
    bool advance();

    // Moves on past a Skippable step. Returns false on anything else —
    // including a Ready one, because "skip" and "continue" are different
    // intentions and collapsing them hides which one a screen meant.
    bool skip();

    // Back, for a person who mistyped the address two screens ago. It stops at
    // the first step rather than falling off the front, and it never re-opens
    // `Done`.
    bool back();

    bool finished() const { return step_ == Step::Done; }

    // Starts the flow at a named step, so any one of them can be reached
    // without walking the others — the same reasoning as `--screen`, which
    // opens a screen by walking the route a person walks so a capture cannot
    // show a state the product cannot reach. Returns false for a name that is
    // not a step.
    bool openAt(const std::string& stepName);

private:
    Step step_ = Step::Network;
    Facts facts_;
};

// Goes and looks, which is the one thing `Machine` will not do. `client` is
// asked nothing over the network — only whether it is holding a token — so this
// is cheap apart from the Wi-Fi status, which shells out to nmcli.
//
// `serverAnswered` is left false: whether an address answers is a question only
// something that has tried can answer, and trying belongs to the caller.
Facts observe(const romm::Client& client, int gamepadCount);

}  // namespace firstrun
