#include "setup.h"

#include <cmath>

#include <SDL3/SDL.h>

#include <atomic>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "bluetooth.h"
#include "design.h"
#include "firstrun.h"
#include "keyboard.h"
#include "net.h"
#include "qr.h"
#include "romm.h"
#include "accounts.h"
#include "storage.h"
#include "text.h"
#include "ui.h"

namespace setup {
namespace {

using design::Animated;
using ui::Color;
using ui::Rect;

// --- Blocking work, moved off the frame ------------------------------------
//
// One worker, one result, polled once a frame. The same shape as LaunchJob and
// StateLoad in main.cpp, generalised because first run needs five of them and
// writing the thread handling five times is how one of them ends up subtly
// different.
template <class T>
class Job {
public:
    ~Job() { join(); }
    Job() = default;
    Job(const Job&) = delete;
    Job& operator=(const Job&) = delete;

    bool busy() const { return running_.load(); }
    bool ready() const { return done_.load(); }

    // `fn` runs on the worker and must touch nothing else in this program.
    void start(std::function<bool(T&, std::string&)> fn) {
        join();
        result_ = T{};
        error_.clear();
        ok_ = false;
        done_.store(false);
        running_.store(true);
        worker_ = std::thread([this, fn = std::move(fn)] {
            T r{};
            std::string e;
            const bool good = fn(r, e);
            result_ = std::move(r);
            error_ = std::move(e);
            ok_ = good;
            running_.store(false);
            done_.store(true);
        });
    }

    // Consumes a finished result. Returns false while the worker is still
    // going, so a caller can poll this every frame and act on the one frame it
    // becomes true.
    bool take(T* out, std::string* err, bool* ok) {
        if (!done_.load()) return false;
        join();                      // the worker has finished; reap it
        done_.store(false);
        if (out) *out = std::move(result_);
        if (err) *err = error_;
        if (ok) *ok = ok_;
        return true;
    }

    void cancel() { join(); done_.store(false); }

private:
    void join() {
        if (worker_.joinable()) worker_.join();
    }
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> done_{false};
    T result_{};
    std::string error_;
    bool ok_ = false;
};

// --- Words that fit --------------------------------------------------------
//
// TextRenderer measures and truncates; it does not wrap, because nothing until
// now needed more than one line. A setup screen is mostly prose, so this is
// where wrapping lives.
//
// It breaks on spaces. A word longer than the whole column is handed to
// `hardWrap` below rather than being left to overflow.
std::vector<std::string> hardWrap(ui::TextRenderer& text, const std::string& s,
                                  ui::TextStyle style, float scale, float maxWidth);

std::vector<std::string> wrap(ui::TextRenderer& text, const std::string& s,
                              ui::TextStyle style, float scale, float maxWidth) {
    std::vector<std::string> out;
    if (s.empty()) return out;
    std::string line;
    size_t at = 0;
    while (at <= s.size()) {
        const size_t sp = s.find(' ', at);
        const std::string word = s.substr(at, sp == std::string::npos ? std::string::npos
                                                                      : sp - at);
        const std::string candidate = line.empty() ? word : line + " " + word;
        if (!line.empty() && text.measure(candidate, style, scale) > maxWidth) {
            out.push_back(line);
            line = word;
        } else {
            line = candidate;
        }
        // A SINGLE WORD WIDER THAN THE COLUMN, which is not a hypothetical
        // here: the pairing URL is one unbroken token and it is the longest
        // string this flow ever draws. Left alone it runs off the column and
        // under the panel beside it.
        if (text.measure(line, style, scale) > maxWidth) {
            for (const std::string& piece : hardWrap(text, line, style, scale, maxWidth))
                out.push_back(piece);
            line.clear();
        }
        if (sp == std::string::npos) break;
        at = sp + 1;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

// Breaks anywhere, on codepoint boundaries.
//
// UTF-8 AWARE, and that is not pedantry: a break in the middle of a multi-byte
// character produces a glyph the font cannot resolve, and a console whose
// library is full of Japanese arcade titles will meet one.
std::vector<std::string> hardWrap(ui::TextRenderer& text, const std::string& s,
                                  ui::TextStyle style, float scale, float maxWidth) {
    std::vector<std::string> out;
    std::string line;
    size_t i = 0;
    while (i < s.size()) {
        const size_t start = i;
        ui::nextCodepoint(s, i);                 // advances i past one character
        const std::string ch = s.substr(start, i - start);
        if (!line.empty() && text.measure(line + ch, style, scale) > maxWidth) {
            out.push_back(line);
            line.clear();
        }
        line += ch;
    }
    if (!line.empty()) out.push_back(line);
    return out;
}

// The QR code texture now lives in ui.h as `ui::QrTexture`, because the
// account switcher's Add-user screen needs the same one. Moved 2026-09-21; the
// reasoning that was here went with it.

// --- The layout -------------------------------------------------------------
//
// One shape for every step, so the flow does not appear to jump between five
// unrelated screens: prose on the left, the thing you act on on the right,
// actions along the bottom. The step changes what goes in the panel and nothing
// else.
constexpr float kInset = 80.0f;
constexpr float kDotsY = 96.0f;
constexpr float kTitleTop = 150.0f;

// THE GUTTER BETWEEN THE TWO COLUMNS IS SET BY THE TITLE, NOT BY THE PROSE.
//
// The prose wraps and can be any width you like; the TITLE does not. At 76pt,
// "Connect to Network" runs to about x=850 on the 1920 canvas, and the panel
// used to start at 880 — thirty points of air between the largest text on the
// screen and a filled rectangle. Every screen read as cramped down the middle
// and it was always the same thirty points.
//
// Reported by MMagTech, 2026-09-20, looking at it on the television.
//
// So the panel starts at 1020, which leaves about 170 points of gutter past the
// longest title this flow has. That takes width off the panel, which it can
// afford: a row holds a network name and one short word, and the QR is 470.
constexpr float kProseWidth = 760.0f;
constexpr float kPanelX = 1020.0f;
constexpr float kPanelY = 168.0f;
constexpr float kPanelW = ui::kCanvasWidth - kPanelX - kInset;
constexpr float kPanelH = 660.0f;
constexpr float kRowH = 74.0f;
constexpr float kRowGap = 10.0f;
constexpr float kFooterY = 900.0f;

// A focusable line inside the panel: a network, a controller, a text field.
struct Row {
    std::string title;
    std::string detail;     // right-hand side — a signal, a state
    bool enabled = true;
    int value = 0;          // index into whatever list the step is showing
    Animated focus;
    // Where `draw` last put it, in canvas points, so a pointer can be asked
    // what it is over. Written by the draw pass and read by the event pass on
    // the frame after, which is the same order `DetailScreen` already relies on.
    float x = 0, y = 0, w = 0, h = 0;
};

// What pressing a footer button means. Named rather than positional, because
// which buttons exist changes per step and a screen that reasons about "the
// third button" breaks the first time one is hidden.
enum class Act { None, Continue, Skip, Back, Rescan, Edit, Restart, Finish, ShowAll };

struct Button {
    Act act = Act::None;
    std::string label;
    bool enabled = true;
    Animated focus;
    float x = 0, y = 0, w = 0, h = 0;
};

// What the on-screen keyboard is currently filling in. One field at a time, and
// the same field a physical keyboard types into — see keyboard.h.
enum class Typing { None, Passphrase, Address };

struct ServerProbe {
    std::string baseUrl;
    std::string version;
};

class Flow {
public:
    Flow(const Deps& d, const Options& o) : d_(d), o_(o) {}

    Outcome run();

private:
    void observe();
    void enterStep();
    void rebuild();
    void pumpJobs();
    void tick(float dt);
    void draw();

    int nextEnabledRow(int from, int dy) const;
    bool anyEnabledRow() const;
    void moveFocus(int dy);
    void moveAction(int dx);
    // A MOUSE MOVES FOCUS AND CLICKS THE FOCUSED THING, AND NOTHING ELSE.
    //
    // docs/PROJECT.md open question 16, answered 2026-09-19. Once the KEYBOARD
    // became the guaranteed input, the mouse had nothing it uniquely enabled —
    // so it gets the cheap treatment: a second way to drive the one model that
    // exists, rather than a second model. No cursor of our own, no hover
    // treatment, no click targets that are not already focus targets.
    //
    // Returns true when the pointer is over something focusable.
    bool pointAt(float windowX, float windowY);
    bool canvasPoint(float windowX, float windowY, float* cx, float* cy) const;

    // ONE PLACE WHERE A FILLED-IN FIELD IS ACTED ON, whichever of the three
    // inputs finished it: Return on a physical keyboard, Start or A-on-"done"
    // with a controller, or a click on "done". Three call sites meant the
    // controller's own "done" key was the one nobody had wired up, and what had
    // been typed was thrown away in silence.
    void typingFinished(ui::KeyboardResult result, const std::string& value);
    void activate();
    void goBack();

    void startWifiScan();
    void joinSelected(const std::string& passphrase);
    void startServerProbe(const std::string& address);
    void startPairing();
    void pollPairing();
    void startBtScan();
    void pairSelected();
    void finish();

    const char* title() const;
    std::string prose() const;

    Deps d_;
    Options o_;

    firstrun::Machine machine_;
    firstrun::Facts facts_;
    // The link's own details — device, name, address. `Facts` deliberately
    // carries only what the RULES need; this is what the screen says out loud.
    net::Status netStatus_;
    romm::Client client_;
    ui::Keyboard keyboard_;
    Typing typing_ = Typing::None;

    std::vector<Row> rows_;
    std::vector<Button> buttons_;
    int row_ = 0;             // -1 means focus is on the footer
    int button_ = 0;
    bool onFooter_ = false;
    // Has the person moved focus on this step yet. Until they have, focus is
    // still the console's to place.
    bool moved_ = false;

    // A line under the panel, for whatever the console last did or refused.
    std::string notice_;
    bool noticeIsError_ = false;

    // THE LINK, WATCHED RATHER THAN SAMPLED ONCE.
    //
    // Without this the network step is a photograph: somebody sitting on
    // "A network connection is required" who then plugs a cable in watches
    // nothing happen, forever, because the facts are only re-read when the step
    // changes. That is the exact moment this screen exists for.
    //
    // It is a job because it is not free — net::status() is three or four
    // nmcli round trips, several hundred milliseconds, which is ten frames.
    Job<net::Status> statusJob_;
    uint64_t lastStatusNS_ = 0;
    uint64_t lastScanNS_ = 0;
    // Told apart on purpose: "we looked and there is nothing" and "we could not
    // look" are different sentences, and only one of them means try again.
    bool scanFailed_ = false;

    // Wi-Fi
    std::vector<net::Network> networks_;
    int chosenNetwork_ = -1;
    Job<std::vector<net::Network>> scanJob_;
    Job<bool> joinJob_;

    // Server
    std::string address_;
    Job<ServerProbe> serverJob_;

    // Pairing
    romm::Pairing pairing_;
    bool pairingLive_ = false;
    qr::Code code_;
    ui::QrTexture qrTex_;
    Job<romm::Pairing> pairBeginJob_;
    Job<int> pairPollJob_;

    // Controller
    std::vector<bt::Device> devices_;
    int chosenDevice_ = -1;
    // The cheap refresh, separate from the ten-second discovery: asks the
    // adapter what it already knows. Still a job, because "cheap" here is one
    // subprocess plus one more per device.
    Job<std::vector<bt::Device>> btKnownJob_;
    uint64_t lastBtKnownNS_ = 0;
    // CACHED, because asking costs two subprocesses and `rebuild()` runs on
    // every status tick. This was called straight from the panel code, so a
    // console sitting on the controller step forked `bluetoothctl` twice every
    // two seconds, for ever, to decide the wording of one row.
    bt::Adapter btAdapter_;
    bool btAdapterKnown_ = false;
    bool showAllDevices_ = false;
    int hiddenDevices_ = 0;
    Job<std::vector<bt::Device>> btScanJob_;
    Job<bool> btPairJob_;

    bool running_ = true;
    Outcome outcome_ = Outcome::Quit;
    int frame_ = 0;
    Animated stepFade_;
};

// --- What each step says ----------------------------------------------------

const char* Flow::title() const {
    switch (machine_.step()) {
        case firstrun::Step::Network:    return "Connect to Network";
        case firstrun::Step::Server:     return "RomM Server";
        case firstrun::Step::Pair:       return "Pair with RomM";
        case firstrun::Step::Controller: return "Pair a Controller";
        case firstrun::Step::Done:       return "Ready";   // see prose(): what
                                                          // "ready" means
                                                          // depends on whether
                                                          // a pad was paired
    }
    return "";
}

// The step's own sentence comes from the state machine, so the words a person
// reads and the rule the console is enforcing cannot drift apart. Anything
// added here is either a state the machine cannot see — a request in flight —
// or a fact about THIS machine that changes what the sentence should say.
std::string Flow::prose() const {
    const std::string why = machine_.because();
    switch (machine_.step()) {
        case firstrun::Step::Network:
            if (joinJob_.busy()) return "Joining…";
            return why;

        case firstrun::Step::Server:
            if (serverJob_.busy()) return "Checking " + address_ + "…";
            return why.empty() ? "Connected." : why;

        case firstrun::Step::Pair:
            if (pairBeginJob_.busy()) return "Starting…";
            return why.empty() ? "Paired." : why;

        case firstrun::Step::Controller:
            if (btScanJob_.busy()) return "Scanning. Put a controller into "
                                          "pairing mode.";
            if (btPairJob_.busy()) return "Pairing…";
            return why.empty() ? "Ready." : why;

        case firstrun::Step::Done:
            // THE PROMISE IS ONLY MADE WHEN IT IS TRUE.
            //
            // "You can unplug the keyboard" is the one line worth spending,
            // because it is the guarantee the whole design exists to keep. But
            // it is a guarantee about A CONSOLE WITH A CONTROLLER ON IT — and
            // the controller step is deliberately skippable, so somebody can
            // and will arrive here without one. Telling them to unplug the only
            // input they have would not be a clumsy sentence, it would be the
            // console lying about the one thing it set out to promise.
            //
            // Found by MMagTech, 2026-09-20, walking the flow: *"on the
            // bluetooth pairing screen it said it wasn't required and could be
            // done later, but then on the last screen said the keyboard could
            // be unplugged and wasn't needed anymore."*
            if (facts_.gamepadCount > 0)
                return "You can unplug the keyboard. You will not need it "
                       "again.";
            return "No controller is paired, so keep the keyboard plugged in. "
                   "Add one in Settings and you can put it away.";
    }
    return {};
}

// --- Looking at the machine -------------------------------------------------

// THE LINK IS NOT ASKED ABOUT HERE, and that is the point.
//
// This used to call net::status() — three or four nmcli round trips — and it is
// called on every step transition, so pressing Continue cost a visible hitch on
// the frame thread. The link is the status watcher's job (see `statusJob_`), it
// is never more than two seconds old, and everything left in here is a file
// test.
void Flow::observe() {
    const firstrun::Facts before = facts_;
    facts_ = firstrun::observeLocal(client_, SDL_HasGamepad() ? 1 : 0);
    // Carried over rather than re-derived. The link belongs to the watcher;
    // only something that has TRIED can say whether a server answered; and the
    // token may have arrived in this very session.
    facts_.online = before.online;
    facts_.wiredOnline = before.wiredOnline;
    facts_.wifiPresent = before.wifiPresent;
    facts_.wifiConfigured = before.wifiConfigured;
    facts_.serverAnswered = before.serverAnswered;
    facts_.serverChecked = before.serverChecked;
    facts_.havePairedToken = before.havePairedToken || facts_.havePairedToken;
    machine_.update(facts_);
}

void Flow::enterStep() {
    rows_.clear();
    row_ = 0;
    onFooter_ = false;
    button_ = 0;
    notice_.clear();
    noticeIsError_ = false;
    moved_ = false;
    showAllDevices_ = false;
    hiddenDevices_ = 0;
    lastBtKnownNS_ = 0;
    stepFade_.from = 0.0f;
    stepFade_.to = 1.0f;
    stepFade_.elapsed = 0.0f;
    stepFade_.duration = design::kOverlayFade;
    stepFade_.smooth = true;

    switch (machine_.step()) {
        case firstrun::Step::Network:
            // Draw whatever NetworkManager already knows immediately, then ask
            // the radio to look again. A screen that shows nothing for five
            // seconds reads as broken even when it is working.
            // ASKED FOR, NOT FETCHED HERE. This used to call cachedScan on
            // the frame thread for an instant list — one subprocess, plus one
            // more for every saved profile. Zeroing the clock makes the watcher
            // scan on its very next pump instead, which costs a few frames of
            // "Looking for networks…" and no stutter at all.
            lastScanNS_ = 0;
            break;
        case firstrun::Step::Server:
            address_ = firstrun::serverAddress();
            // ARRIVING WITH AN ADDRESS IS THE COMMON CASE, not the edge one:
            // /etc/cabinetos/session.env may already name a server, and coming
            // Back to this step certainly does. Try it rather than sitting
            // there implying it is wrong.
            if (!address_.empty() && !facts_.serverAnswered) startServerProbe(address_);
            break;
        case firstrun::Step::Pair:
            // A MACHINE THAT IS ALREADY PAIRED WOULD NEVER DRAW THIS SCREEN,
            // and both machines here are paired — so without this the one
            // screen with a picture on it could not be looked at or
            // photographed at all. Under dryRun the pairing is real (the server
            // issues a genuine code, which expires on its own) but the token is
            // never written; see the poll job.
            if (!facts_.havePairedToken || o_.dryRun) startPairing();
            break;
        case firstrun::Step::Controller:
            startBtScan();
            break;
        case firstrun::Step::Done:
            finish();
            break;
    }
    rebuild();
}

// --- What is on the panel right now -----------------------------------------

void Flow::rebuild() {
    const int keepRow = row_;
    rows_.clear();
    buttons_.clear();

    const firstrun::Gate gate = machine_.gate();

    switch (machine_.step()) {
        case firstrun::Step::Network: {
            // NO RADIO: there is no list to draw, so the panel shows the link
            // instead — or says there is not one.
            if (!facts_.wifiPresent) {
                Row r;
                if (facts_.online) {
                    r.title = "Ethernet";
                    r.detail = netStatus_.ipv4.empty() ? "connected" : netStatus_.ipv4;
                } else {
                    r.title = "No network";
                    r.detail = "plug in a cable";
                }
                r.enabled = false;
                rows_.push_back(std::move(r));
                break;
            }
            // WITH A RADIO THE LIST IS ALWAYS DRAWN, on a cable or not. That is
            // the whole of the Wi-Fi rule: offered either way, and the gate
            // decides whether anybody has to use it. The row for the network
            // already carrying the connection says so, so a person on Wi-Fi can
            // see at a glance that this screen is already satisfied.

            if (scanJob_.busy() && networks_.empty()) {
                Row r;
                r.title = "Looking for networks…";
                r.enabled = false;
                rows_.push_back(std::move(r));
                break;
            }
            if (networks_.empty()) {
                Row r;
                r.title = scanFailed_ ? "Could not scan" : "Nothing on the air";
                r.detail = scanFailed_ ? "" : "looking again…";
                r.enabled = false;
                rows_.push_back(std::move(r));
                break;
            }
            for (size_t i = 0; i < networks_.size(); ++i) {
                const net::Network& n = networks_[i];
                Row r;
                r.title = n.ssid;
                r.value = static_cast<int>(i);
                // WHICH ONE IS CONNECTED COMES FROM THE LIVE STATUS, not from
                // the scan. The scan's own IN-USE column is a snapshot and the
                // status is re-read every two seconds, so this stays right even
                // in the seconds before a refreshed list arrives.
                const bool active =
                    netStatus_.wifiUp && !netStatus_.connection.empty() &&
                    netStatus_.connection == n.ssid;
                // The right-hand side answers "what will happen if I press
                // this" rather than reporting a signal nobody can act on.
                r.detail = active        ? "connected"
                           : n.enterprise ? "not supported"
                           : n.known      ? "saved"
                           : n.secured    ? "password"
                                          : "open";
                // 802.1X is refused plainly rather than half-supported — it
                // needs a certificate, an identity and an inner method, which
                // is a different form and not one anybody fills in from a sofa.
                r.enabled = !n.enterprise;
                rows_.push_back(std::move(r));
            }
            break;
        }

        case firstrun::Step::Server: {
            Row r;
            r.title = address_.empty() ? "Enter the address" : address_;
            r.detail = serverJob_.busy() ? "checking…"
                       : facts_.serverAnswered ? "found"
                                               : "edit";
            rows_.push_back(std::move(r));
            break;
        }

        case firstrun::Step::Pair: {
            Row r;
            r.enabled = false;
            if (facts_.havePairedToken) {
                r.title = "Approved";
                r.detail = "done";
            } else if (pairBeginJob_.busy()) {
                r.title = "Asking the server…";
            } else if (pairingLive_) {
                r.title = pairing_.userCode;
                r.detail = "waiting";
            } else {
                r.title = "Could not start pairing";
            }
            rows_.push_back(std::move(r));
            break;
        }

        case firstrun::Step::Controller: {
            if (btScanJob_.busy() && devices_.empty()) {
                Row r;
                r.title = "Looking for controllers…";
                r.enabled = false;
                rows_.push_back(std::move(r));
                break;
            }
            if (devices_.empty()) {
                Row r;
                // The three-state rule: no adapter is a different sentence from
                // nothing found, and a machine with no Bluetooth must never be
                // shown a spinner it will never finish.
                r.title = (!btAdapterKnown_ || btAdapter_.present)
                              ? "Nothing found yet"
                              : "This console has no Bluetooth";
                r.enabled = false;
                rows_.push_back(std::move(r));
                break;
            }
            // MOST OF WHAT A BLUETOOTH SCAN FINDS IS NOT A CONTROLLER, and on
            // this screen that is not a detail. A real scan in a living room
            // returned a Pro Controller, a television, and five bare MAC
            // addresses belonging to the neighbours — so the list somebody is
            // meant to pick their pad out of was six parts noise.
            //
            // A device with no name at all cannot be identified by anybody
            // looking at the screen, so it is hidden by default and COUNTED,
            // with a button to show them. Hidden, not dropped: a pad that bluez
            // has not resolved yet is exactly the thing somebody has just woken
            // up, and it has to stay reachable.
            hiddenDevices_ = 0;
            for (size_t i = 0; i < devices_.size(); ++i) {
                const bt::Device& dev = devices_[i];
                const bool worthShowing = dev.gamepad || dev.paired || !dev.name.empty();
                if (!worthShowing && !showAllDevices_) {
                    ++hiddenDevices_;
                    continue;
                }
                Row r;
                r.title = dev.name.empty() ? dev.address : dev.name;
                r.value = static_cast<int>(i);
                r.detail = dev.connected ? "connected"
                           : dev.paired  ? "paired"
                           : dev.gamepad ? "controller"
                                         : "";
                rows_.push_back(std::move(r));
            }
            if (rows_.empty() && hiddenDevices_ > 0) {
                // Everything found was unidentifiable. Showing an empty list
                // next to "we found seven things" would be the worst of both.
                showAllDevices_ = true;
                hiddenDevices_ = 0;
                for (size_t i = 0; i < devices_.size(); ++i) {
                    Row r;
                    r.title = devices_[i].name.empty() ? devices_[i].address
                                                       : devices_[i].name;
                    r.value = static_cast<int>(i);
                    rows_.push_back(std::move(r));
                }
            }
            break;
        }

        case firstrun::Step::Done:
            break;
    }

    // --- The footer ---------------------------------------------------------
    //
    // CONTINUE AND SKIP ARE DIFFERENT BUTTONS AND ONLY ONE IS EVER SHOWN. The
    // state machine refuses `skip()` on a step that is merely ready and
    // `advance()` on one that is not, so a single button that tried to be both
    // would be asking the machine a question it deliberately will not answer.
    switch (machine_.step()) {
        case firstrun::Step::Network:
            if (facts_.wifiPresent)
                buttons_.push_back({Act::Rescan, "Scan again", !scanJob_.busy(), {}});
            break;
        case firstrun::Step::Server:
            buttons_.push_back({Act::Edit, "Change the address", !serverJob_.busy(), {}});
            break;
        case firstrun::Step::Pair:
            buttons_.push_back({Act::Restart, "Start again", !pairBeginJob_.busy(), {}});
            break;
        case firstrun::Step::Controller:
            buttons_.push_back({Act::Rescan, "Scan again", !btScanJob_.busy(), {}});
            if (hiddenDevices_ > 0)
                buttons_.push_back({Act::ShowAll,
                                    "Show " + std::to_string(hiddenDevices_) +
                                        " unnamed device" +
                                        (hiddenDevices_ == 1 ? "" : "s"),
                                    true, {}});
            break;
        case firstrun::Step::Done:
            break;
    }

    // CONTINUE IS THE SKIP, AND THE PROSE SAYS SO RATHER THAN THE BUTTON.
    //
    // Folding Wi-Fi into the network step means there is no second button:
    // being online is the whole rule, so pressing on IS declining the optional
    // half. That was briefly spelled out on the button itself — "Continue
    // without Wi-Fi" — and MMagTech's call was to take it back off:
    // *"continue was fine if you let the wording carry it."*
    //
    // He is right. The sentence beside it already says the console is connected
    // over Ethernet and what Wi-Fi would buy; a button that restates the
    // sentence is a second voice saying the same thing, and it grows every time
    // a step gains an optional half.
    if (machine_.step() == firstrun::Step::Done) {
        buttons_.push_back({Act::Finish, "Start playing", true, {}});
    } else if (gate == firstrun::Gate::Skippable) {
        buttons_.push_back({Act::Skip, "Skip", true, {}});
    } else {
        buttons_.push_back({Act::Continue, "Continue", gate == firstrun::Gate::Ready, {}});
    }

    if (machine_.step() != firstrun::Step::Network &&
        machine_.step() != firstrun::Step::Done)
        buttons_.push_back({Act::Back, "Back", true, {}});

    // Where focus lands after the panel's contents change under it. A list that
    // has just been replaced by a placeholder must move focus to the buttons,
    // or the screen becomes unusable without anything looking wrong.
    row_ = std::max(0, std::min(keepRow, static_cast<int>(rows_.size()) - 1));
    // WHERE FOCUS SITS BEFORE ANYBODY HAS TOUCHED ANYTHING SAYS WHAT THE STEP
    // WANTS — and the network step wants two different things depending on
    // whether it is already satisfied.
    //
    // MMagTech, 2026-09-20: *"if ethernet is connected will this screen allow me
    // to skip and not need wifi."* It does — Continue is live and the list is
    // optional — but focus was landing on the first network in the list, which
    // is an invitation to pick one. The answer to "do I have to do this?" ought
    // to be visible without pressing anything.
    //
    // So: a step that is ALREADY SATISFIED puts focus on the way out, and a
    // step that is BLOCKED puts it on the thing that unblocks it. Once somebody
    // moves, it is theirs and this stops interfering.
    if (!moved_) {
        const bool satisfied = gate == firstrun::Gate::Ready;
        int continueAt = -1;
        for (size_t i = 0; i < buttons_.size(); ++i)
            if (buttons_[i].act == Act::Continue || buttons_[i].act == Act::Finish)
                continueAt = static_cast<int>(i);
        if (satisfied && continueAt >= 0) {
            onFooter_ = true;
            button_ = continueAt;
        } else if (anyEnabledRow()) {
            onFooter_ = false;
            row_ = nextEnabledRow(0, +1);
        } else if (!buttons_.empty()) {
            onFooter_ = true;
        }
    }
    if (!anyEnabledRow()) {
        onFooter_ = true;
    } else if (!onFooter_ && !rows_[static_cast<size_t>(row_)].enabled) {
        const int down = nextEnabledRow(row_, +1);
        row_ = down >= 0 ? down : nextEnabledRow(row_, -1);
    }
    button_ = std::max(0, std::min(button_, static_cast<int>(buttons_.size()) - 1));

    // THE FLASHING.
    //
    // Rebuilding throws the old rows and buttons away, and each new one arrives
    // with a fresh `Animated` sitting at zero — so the focused thing fades up
    // from nothing every single time. That was invisible while rebuilds only
    // happened when somebody pressed something, and became a steady blink the
    // moment the link began being re-read every two seconds: Continue pulsing
    // on a screen nobody was touching.
    //
    // Reported by MMagTech, 2026-09-20, watching it on the television.
    //
    // So focus is SETTLED rather than animated after a rebuild — the focused
    // item starts already lit. Moving focus still animates, because `tick`
    // retargets and a retarget to a value the animation is not already at is
    // what starts one.
    for (size_t i = 0; i < rows_.size(); ++i) {
        const bool focused = !onFooter_ && static_cast<int>(i) == row_;
        rows_[i].focus.from = rows_[i].focus.to = focused ? 1.0f : 0.0f;
        rows_[i].focus.elapsed = rows_[i].focus.duration;
    }
    for (size_t i = 0; i < buttons_.size(); ++i) {
        const bool focused = onFooter_ && static_cast<int>(i) == button_;
        buttons_[i].focus.from = buttons_[i].focus.to = focused ? 1.0f : 0.0f;
        buttons_[i].focus.elapsed = buttons_[i].focus.duration;
    }
}

// --- Starting the blocking things -------------------------------------------

void Flow::startWifiScan() {
    if (scanJob_.busy()) return;
    scanJob_.start([](std::vector<net::Network>& out, std::string& err) {
        return net::scan(&out, &err);
    });
}

void Flow::joinSelected(const std::string& passphrase) {
    if (chosenNetwork_ < 0 || chosenNetwork_ >= static_cast<int>(networks_.size())) return;
    const std::string ssid = networks_[static_cast<size_t>(chosenNetwork_)].ssid;
    notice_ = "Joining " + ssid + "…";
    noticeIsError_ = false;
    joinJob_.start([ssid, passphrase](bool& out, std::string& err) {
        out = net::join(ssid, passphrase, /*hidden=*/false, &err);
        return out;
    });
}

void Flow::startServerProbe(const std::string& address) {
    if (address.empty() || serverJob_.busy()) return;
    address_ = address;
    notice_ = "Looking for RomM at " + address + "…";
    noticeIsError_ = false;
    serverJob_.start([address](ServerProbe& out, std::string& err) {
        // A CLIENT OF ITS OWN, ON THE WORKER. romm::Client is safe to call
        // concurrently but this one is being reconfigured, and handing a worker
        // a reference to the object the frame loop also reads is how a probe
        // that fails leaves a half-set address behind.
        romm::Client probe;
        if (!probe.setAddress(address, &err)) return false;
        out.baseUrl = probe.baseUrl();
        out.version = probe.serverVersion();
        return true;
    });
}

void Flow::startPairing() {
    if (pairBeginJob_.busy()) return;
    pairingLive_ = false;
    code_ = qr::Code{};
    const std::string address = address_.empty() ? firstrun::serverAddress() : address_;
    pairBeginJob_.start([address](romm::Pairing& out, std::string& err) {
        romm::Client c;
        if (!c.setAddress(address, &err)) return false;
        return c.beginPairing(&out, &err);
    });
}

void Flow::pollPairing() {
    if (pairPollJob_.busy() || !pairingLive_) return;
    const romm::Pairing p = pairing_;
    const std::string address = address_.empty() ? firstrun::serverAddress() : address_;
    const bool dryRun = o_.dryRun;
    pairPollJob_.start([p, address, dryRun](int& out, std::string& err) {
        // THE INTERVAL IS SLEPT ON THE WORKER, not counted on the frame loop.
        // RomM asks to be polled on its own interval and a console that ignored
        // it would be hammering somebody's server from a screen that is
        // otherwise doing nothing.
        SDL_Delay(static_cast<Uint32>(p.intervalSeconds > 0 ? p.intervalSeconds : 5) * 1000);
        romm::Client c;
        if (!c.setAddress(address, &err)) { out = -1; return false; }
        out = c.pollPairing(p, &err);
        if (out == 1 && dryRun) {
            // Approved during a capture. The pairing was real and the server
            // has issued a token; deliberately dropping it on the floor is the
            // whole meaning of dryRun, and it costs nothing — an unsaved token
            // simply goes unused and the pairing ages out.
            return true;
        }
        if (out == 1) {
            // The token is only ever written here, on the one path where the
            // server said yes. Saving it anywhere else is how a console ends up
            // holding a credential it never earned.
            //
            // IT BECOMES AN ACCOUNT RATHER THAN A LONE TOKEN, since 2026-09-21.
            // `recordPairing` asks the server who the token belongs to and
            // files it under that id — the same id the save directory is built
            // from. A console whose first run wrote a bare token would have a
            // credential and no account, which is the state open question 26
            // removed the migration path for.
            std::string aerr;
            accounts::Paired paired;
            if (!accounts::recordPairing(c, &paired, &aerr)) {
                // PAIRED, BUT NOT SAVED, IS NOT "PAIRED". Calling it success
                // here is exactly how the first console ever installed came up
                // on the stand-in library with nobody able to say why.
                err = aerr;
                out = -1;
                return false;
            }
        }
        return out >= 0;
    });
}

void Flow::startBtScan() {
    if (btScanJob_.busy()) return;
    // The cached answer, and only once it is known. Before the first refresh
    // lands the scan is allowed through: bt::scan() checks the adapter itself
    // and says so properly, which is a better answer than silently doing
    // nothing.
    if (btAdapterKnown_ && !btAdapter_.present) return;
    btScanJob_.start([](std::vector<bt::Device>& out, std::string& err) {
        // Ten seconds: the sensible floor for a pad somebody has only just put
        // into pairing mode, and short enough that "Scan again" is a reasonable
        // thing to press.
        return bt::scan(10, &out, &err);
    });
}

void Flow::pairSelected() {
    if (chosenDevice_ < 0 || chosenDevice_ >= static_cast<int>(devices_.size())) return;
    const bt::Device dev = devices_[static_cast<size_t>(chosenDevice_)];
    notice_ = "Pairing with " + (dev.name.empty() ? dev.address : dev.name) + "…";
    noticeIsError_ = false;
    btPairJob_.start([dev](bool& out, std::string& err) {
        out = bt::pair(dev.address, &err);
        return out;
    });
}

void Flow::finish() {
    if (o_.dryRun) return;
    std::string err;
    if (!firstrun::markCompleted(/*adopted=*/false, &err))
        std::fprintf(stderr, "[first-run] could not write the marker: %s\n", err.c_str());
    else
        std::fprintf(stderr, "[first-run] setup complete\n");
}

// --- Polling them once a frame ----------------------------------------------

void Flow::pumpJobs() {
    bool changed = false;
    std::string err;
    bool ok = false;

    // Re-read the link about every two seconds. Slow enough not to hammer
    // NetworkManager from a screen that is otherwise idle, quick enough that
    // plugging a cable in feels like it did something.
    constexpr uint64_t kStatusEveryNS = 2000000000ull;
    if (!statusJob_.busy() && SDL_GetTicksNS() - lastStatusNS_ > kStatusEveryNS) {
        lastStatusNS_ = SDL_GetTicksNS();
        statusJob_.start([](net::Status& out, std::string& e) {
            out = net::status();
            (void)e;
            return true;
        });
    }
    if (net::Status st; statusJob_.take(&st, &err, &ok)) {
        const bool wasOnline = facts_.online;
        const bool wasWifiUp = facts_.wifiConfigured;
        netStatus_ = st;
        facts_.online = st.online;
        facts_.wiredOnline = st.ethernetUp;
        facts_.wifiPresent = st.wifiPresent;
        facts_.wifiConfigured = st.wifiUp;
        machine_.update(facts_);
        // A LINK THAT ARRIVES WHILE THE NETWORK STEP IS BLOCKED IS THE ANSWER
        // TO THAT STEP, so say so rather than silently ungreying a button.
        if (!wasOnline && st.online &&
            machine_.step() == firstrun::Step::Network) {
            notice_ = st.ethernetUp ? "Connected over Ethernet." : "Connected.";
            noticeIsError_ = false;
        }
        // A LINK THAT CHANGES MAKES THE LIST WRONG, not just the sentence. Both
        // "this is the one you are on" and "this one needs no password" are
        // properties of the moment the scan was taken, and losing or gaining a
        // connection changes both.
        if (st.online != wasOnline || st.wifiUp != wasWifiUp) lastScanNS_ = 0;

        // LOSING THE LINK UNDOES "the server answered", because it did — a
        // moment ago, over a connection that is gone. The same snapshot problem
        // one step along: without this the server step goes on saying
        // "Connected." with no network at all, Continue stays live, and the
        // failure surfaces two screens later as a pairing that will not start.
        if (wasOnline && !st.online) {
            facts_.serverAnswered = false;
            facts_.serverChecked = false;
            machine_.update(facts_);
            notice_ = "The network connection was lost.";
            noticeIsError_ = true;
        }
        changed = true;
    }

    // A SCAN IS STARTED BY WHAT THE SCREEN NEEDS, NOT BY ARRIVING AT A STEP.
    //
    // This was driven from `enterStep` alone, and the cable being unplugged is
    // exactly the case that breaks: the step was entered while the machine was
    // online, so no list was needed and none was asked for — then the cable came
    // out, the panel correctly switched to showing a Wi-Fi list, and the list it
    // showed was the empty one nobody had ever filled. It said "Nothing on the
    // air" in a house with four networks in it, and the only thing on screen
    // that could fix it was a button somebody had to know to press.
    //
    // Found by MMagTech, 2026-09-20, unplugging the cable.
    {
        const bool needList =
            facts_.wifiPresent && machine_.step() == firstrun::Step::Network;
        // A LIST IS REFRESHED, NOT JUST FILLED.
        //
        // This was gated on `networks_.empty()`, so the first scan populated it
        // and nothing ever looked again. Two of the three things a row says are
        // properties of the moment the scan was taken — whether it is the
        // network you are ON, and whether it is one the console already KNOWS —
        // and both go stale the instant anything about the link changes.
        //
        // MMagTech, 2026-09-20, having had the saved network deleted out from
        // under a screen that was already showing it: *"the wifi still says its
        // connected and gave an error... i dont think it was actually connected
        // just the ui said it still was."* The row still claimed to be
        // connected AND still claimed to be saved, so pressing it tried to join
        // with no passphrase and failed. One stale list, both symptoms.
        //
        // Empty gets an eager retry; a list that already has something in it is
        // refreshed on a slower clock, and immediately whenever the link moves.
        const uint64_t since = SDL_GetTicksNS() - lastScanNS_;
        const uint64_t due = networks_.empty() ? 8000000000ull : 20000000000ull;
        if (needList && !scanJob_.busy() && since > due) {
            lastScanNS_ = SDL_GetTicksNS();
            startWifiScan();
            changed = true;
        }
    }

    // Cheap enough to ask every frame, and it is how a pad waking up mid-setup
    // answers the controller step by itself.
    if (const int pads = SDL_HasGamepad() ? 1 : 0; pads != facts_.gamepadCount) {
        facts_.gamepadCount = pads;
        machine_.update(facts_);
        changed = true;
    }

    if (std::vector<net::Network> found; scanJob_.take(&found, &err, &ok)) {
        scanFailed_ = !ok;
        if (ok) {
            networks_ = std::move(found);
        } else {
            notice_ = err;
            noticeIsError_ = true;
        }
        changed = true;
    }

    if (bool joined = false; joinJob_.take(&joined, &err, &ok)) {
        if (ok) {
            notice_ = "Joined.";
            noticeIsError_ = false;
            observe();
            lastScanNS_ = 0;      // the list's "connected" and "saved" just moved
        } else if (chosenNetwork_ >= 0 &&
                   chosenNetwork_ < static_cast<int>(networks_.size()) &&
                   networks_[static_cast<size_t>(chosenNetwork_)].secured &&
                   (err.find("Secrets") != std::string::npos ||
                    err.find("secrets") != std::string::npos ||
                    err.find("password") != std::string::npos)) {
            // A NETWORK THE CONSOLE THOUGHT IT KNEW AND DOES NOT. NetworkManager
            // answers "Secrets were required, but not provided", which is true
            // and is not something a person can act on — and the remedy is
            // obvious, so do it rather than printing the sentence and stopping.
            //
            // This is the safety net under the stale-list fix above rather than
            // a substitute for it: the list should already be right, and if it
            // ever is not, the screen asks for the password instead of dying.
            const net::Network& n = networks_[static_cast<size_t>(chosenNetwork_)];
            ui::Keyboard::Config cfg;
            cfg.title = n.ssid;
            cfg.hint = "The password for this network";
            cfg.conceal = true;   // masked, last character shown; keyboard.h
            keyboard_.open(cfg);
            typing_ = Typing::Passphrase;
            notice_.clear();
            lastScanNS_ = 0;
        } else {
            notice_ = err;
            noticeIsError_ = true;
        }
        changed = true;
    }

    if (ServerProbe probe; serverJob_.take(&probe, &err, &ok)) {
        facts_.serverChecked = true;
        if (ok) {
            facts_.serverAnswered = true;
            machine_.update(facts_);
            notice_ = "Found RomM " + probe.version + " at " + probe.baseUrl;
            noticeIsError_ = false;
            if (!o_.dryRun) {
                std::string werr;
                if (!firstrun::setServerAddress(address_, &werr)) {
                    notice_ = werr;
                    noticeIsError_ = true;
                }
            }
        } else {
            facts_.serverAnswered = false;
            machine_.update(facts_);
            notice_ = err;
            noticeIsError_ = true;
        }
        changed = true;
    }

    if (romm::Pairing p; pairBeginJob_.take(&p, &err, &ok)) {
        if (ok) {
            pairing_ = std::move(p);
            pairingLive_ = true;
            std::string qerr;
            code_ = qr::encode(pairing_.verificationUrl, &qerr);
            if (code_.valid()) {
                qrTex_.set(code_);
            } else {
                // A URL this encoder cannot hold would be four times longer
                // than any RomM produces. Say so rather than drawing nothing:
                // the address and the code below it are still usable.
                notice_ = qerr;
                noticeIsError_ = true;
            }
            pollPairing();
        } else {
            notice_ = err;
            noticeIsError_ = true;
        }
        changed = true;
    }

    if (int state = 0; pairPollJob_.take(&state, &err, &ok)) {
        if (state == 1) {
            facts_.havePairedToken = true;
            machine_.update(facts_);
            pairingLive_ = false;
            notice_ = "Approved.";
            noticeIsError_ = false;
            // WHO THIS CONSOLE IS, settled before anything writes a save. The
            // adoption rule in firstrun.cpp looks for this file, so a console
            // that paired and never asked would be judged unconfigured on its
            // next boot.
            std::string serr;
            if (client_.setAddress(address_.empty() ? firstrun::serverAddress() : address_,
                                   &serr)) {
                const char* home = getenv("HOME");
                client_.loadToken(std::string(home ? home : ".") +
                                  "/.config/cabinetos/romm.json");
                storage::resolveCurrentUser(client_, &serr);
            }
        } else if (state == 0) {
            pollPairing();      // still waiting; ask again on its interval
        } else {
            pairingLive_ = false;
            notice_ = err.empty() ? "The pairing expired." : err;
            noticeIsError_ = true;
        }
        changed = true;
    }

    if (std::vector<bt::Device> found; btScanJob_.take(&found, &err, &ok)) {
        if (ok) {
            devices_ = std::move(found);
        } else {
            notice_ = err;
            noticeIsError_ = true;
        }
        changed = true;
    }

    if (bool paired = false; btPairJob_.take(&paired, &err, &ok)) {
        // `bt::pair` returns true with an error set when the pad is paired and
        // trusted but asleep. That is a real success with a caveat, and the
        // caveat is the sentence that tells somebody what to do next.
        notice_ = err.empty() ? "Controller ready." : err;
        noticeIsError_ = !paired;
        if (paired) {
            observe();
            // THIS USED TO CALL bt::known() RIGHT HERE, ON THE FRAME THREAD.
            // That is one subprocess to list the devices and ANOTHER PER DEVICE
            // to describe it — a real scan on the A9 found twenty-odd — so the
            // console froze solid for several seconds at the exact moment it
            // had just told somebody their controller was ready. Ask for a
            // refresh instead and let the worker do it.
            lastBtKnownNS_ = 0;
        }
        changed = true;
    }

    if (std::vector<bt::Device> refreshed; btKnownJob_.take(&refreshed, &err, &ok)) {
        // Only replaces the list when the adapter actually answered. A failed
        // refresh must not blank a list somebody is looking at.
        if (ok && !refreshed.empty()) devices_ = std::move(refreshed);
        changed = true;
    }

    // THE SAME STALENESS THE WI-FI LIST HAD, and MMagTech spotted it in the
    // same breath. "paired" and "connected" are properties of the moment the
    // list was taken: wake a pad, or pair one, and every row goes on saying
    // what was true a minute ago. Discovery is expensive and stays on its
    // button; asking the adapter what it already knows is not, so that runs on
    // a clock.
    //
    // Never while a discovery or a pairing is in flight — bluetoothctl does not
    // want two of these at once, and the answers would race.
    if (machine_.step() == firstrun::Step::Controller && !btScanJob_.busy() &&
        !btPairJob_.busy() && !btKnownJob_.busy() &&
        SDL_GetTicksNS() - lastBtKnownNS_ > 5000000000ull) {
        lastBtKnownNS_ = SDL_GetTicksNS();
        btKnownJob_.start([this](std::vector<bt::Device>& out, std::string& e) {
            // The adapter comes back on the same trip. Writing it from the
            // worker is safe because nothing else ever writes it, and the frame
            // thread only reads it to pick a word.
            btAdapter_ = bt::adapter();
            btAdapterKnown_ = true;
            return bt::known(&out, &e);
        });
    }

    if (changed) rebuild();
}

// --- Moving about -----------------------------------------------------------
//
// Two regions: the list in the panel, and the row of buttons under it. Down off
// the end of the list lands on the buttons and Up off the top of the buttons
// goes back to the list, which is the whole of the model. NO WRAPPING at either
// end — the same rule the on-screen keyboard follows, and for the same reason:
// wrapping reads as a glitch when you are holding a direction.

// A DISABLED ROW IS NEVER FOCUSED. A row that cannot be activated still draws
// a focus rim if focus is allowed to rest on it, and pressing it then does
// nothing at all with no explanation — which is the single worst thing a setup
// screen can do, because the person has no way to tell it from a crash. Every
// placeholder this flow draws ("Looking for networks…", "This console has no
// Bluetooth") is disabled, so this is the common case rather than the edge.
int Flow::nextEnabledRow(int from, int dy) const {
    for (int i = from; i >= 0 && i < static_cast<int>(rows_.size()); i += dy)
        if (rows_[static_cast<size_t>(i)].enabled) return i;
    return -1;
}

bool Flow::anyEnabledRow() const {
    return nextEnabledRow(0, +1) >= 0;
}

void Flow::moveFocus(int dy) {
    moved_ = true;
    if (!onFooter_) {
        const int next = nextEnabledRow(row_ + dy, dy);
        if (next < 0) {
            // Off the bottom lands on the buttons; off the top stays put,
            // because there is nothing above the list.
            if (dy > 0 && !buttons_.empty()) { onFooter_ = true; button_ = 0; }
            return;
        }
        row_ = next;
        return;
    }
    if (dy < 0) {
        const int last = nextEnabledRow(static_cast<int>(rows_.size()) - 1, -1);
        if (last >= 0) {
            onFooter_ = false;
            row_ = last;
        }
    }
}

// Window pixels to canvas points, through the letterbox.
//
// The renderer scales the 1920x1080 canvas to fit and centres it, so a pointer
// in window space is not in canvas space on any display that is not exactly
// 16:9 — and the bars are real estate the pointer can sit in and nothing can be
// under it.
bool Flow::canvasPoint(float windowX, float windowY, float* cx, float* cy) const {
    int dw = 0, dh = 0, lw = 0, lh = 0;
    SDL_GetWindowSizeInPixels(d_.window, &dw, &dh);
    SDL_GetWindowSize(d_.window, &lw, &lh);
    if (dw <= 0 || dh <= 0 || lw <= 0 || lh <= 0) return false;
    // SDL reports the pointer in the window's logical space, which is not the
    // pixel space on a scaled display.
    const float px = windowX * (static_cast<float>(dw) / static_cast<float>(lw));
    const float py = windowY * (static_cast<float>(dh) / static_cast<float>(lh));

    const float sc = std::min(static_cast<float>(dw) / ui::kCanvasWidth,
                              static_cast<float>(dh) / ui::kCanvasHeight);
    if (sc <= 0.0f) return false;
    *cx = (px - (dw - ui::kCanvasWidth * sc) * 0.5f) / sc;
    *cy = (py - (dh - ui::kCanvasHeight * sc) * 0.5f) / sc;
    return true;
}

void Flow::typingFinished(ui::KeyboardResult result, const std::string& value) {
    if (result == ui::KeyboardResult::Committed) {
        if (typing_ == Typing::Address) startServerProbe(value);
        else if (typing_ == Typing::Passphrase) joinSelected(value);
    }
    if (result != ui::KeyboardResult::Typing) typing_ = Typing::None;
    rebuild();
}

bool Flow::pointAt(float windowX, float windowY) {
    float cx = 0, cy = 0;
    if (!canvasPoint(windowX, windowY, &cx, &cy)) return false;

    auto inside = [&](float x, float y, float w, float h) {
        return cx >= x && cx <= x + w && cy >= y && cy <= y + h;
    };
    for (size_t i = 0; i < rows_.size(); ++i) {
        if (!rows_[i].enabled) continue;
        if (!inside(rows_[i].x, rows_[i].y, rows_[i].w, rows_[i].h)) continue;
        moved_ = true;
        onFooter_ = false;
        row_ = static_cast<int>(i);
        return true;
    }
    for (size_t i = 0; i < buttons_.size(); ++i) {
        if (!buttons_[i].enabled) continue;
        if (!inside(buttons_[i].x, buttons_[i].y, buttons_[i].w, buttons_[i].h)) continue;
        moved_ = true;
        onFooter_ = true;
        button_ = static_cast<int>(i);
        return true;
    }
    return false;
}

void Flow::moveAction(int dx) {
    moved_ = true;
    if (!onFooter_) return;
    const int next = button_ + dx;
    if (next < 0 || next >= static_cast<int>(buttons_.size())) return;
    button_ = next;
}

void Flow::goBack() {
    if (machine_.back()) {
        observe();
        enterStep();
    }
}

void Flow::activate() {
    if (onFooter_) {
        if (buttons_.empty()) return;
        const Button& b = buttons_[static_cast<size_t>(button_)];
        if (!b.enabled) return;
        switch (b.act) {
            case Act::Continue:
                if (machine_.advance()) { observe(); enterStep(); }
                break;
            case Act::Skip:
                if (machine_.skip()) { observe(); enterStep(); }
                break;
            case Act::Back: goBack(); break;
            case Act::Rescan:
                lastScanNS_ = SDL_GetTicksNS();
                if (machine_.step() == firstrun::Step::Controller) startBtScan();
                else startWifiScan();
                rebuild();
                break;
            case Act::Edit: {
                ui::Keyboard::Config cfg;
                cfg.title = "Your RomM server";
                cfg.hint = "The address you open RomM at, without http://";
                cfg.initial = address_;
                cfg.placeholder = "192.168.1.10:6005";
                // The two keys that save the most typing on this particular
                // field. A passphrase wants neither.
                cfg.shortcuts = {".local", ":8080"};
                keyboard_.open(cfg);
                typing_ = Typing::Address;
                break;
            }
            case Act::Restart: startPairing(); rebuild(); break;
            case Act::ShowAll: showAllDevices_ = true; rebuild(); break;
            case Act::Finish: running_ = false; outcome_ = Outcome::Completed; break;
            case Act::None: break;
        }
        return;
    }

    if (rows_.empty()) return;
    const Row& r = rows_[static_cast<size_t>(row_)];
    if (!r.enabled) return;

    switch (machine_.step()) {
        case firstrun::Step::Network: {
            chosenNetwork_ = r.value;
            const net::Network& n = networks_[static_cast<size_t>(r.value)];
            // A saved or open network needs nothing typed. That is not a
            // shortcut, it is the commonest case: NetworkManager remembers, and
            // asking again for something it already holds is the kind of
            // friction that makes people distrust a setup flow.
            if (n.known || !n.secured) { joinSelected(""); break; }
            ui::Keyboard::Config cfg;
            cfg.title = n.ssid;
            cfg.hint = "The password for this network";
            // MASKED, WITH THE LAST CHARACTER SHOWN as it is typed and a
            // "show" key for the lot. It was shown in full until 2026-09-24
            // (docs/PROJECT.md open question 17); MMagTech reversed it once
            // the brief reveal answered the typo objection. keyboard.h.
            cfg.conceal = true;
            keyboard_.open(cfg);
            typing_ = Typing::Passphrase;
            break;
        }
        case firstrun::Step::Server:
            // The field is the row, so pressing it is the same as pressing
            // "Change the address".
            button_ = 0;
            onFooter_ = true;
            activate();
            onFooter_ = false;
            break;
        case firstrun::Step::Controller:
            chosenDevice_ = r.value;
            pairSelected();
            rebuild();
            break;
        default: break;
    }
}

// --- Drawing ----------------------------------------------------------------

void Flow::tick(float dt) {
    stepFade_.tick(dt);
    for (size_t i = 0; i < rows_.size(); ++i) {
        Row& r = rows_[i];
        const bool focused = !onFooter_ && static_cast<int>(i) == row_;
        r.focus.retarget(focused ? 1.0f : 0.0f, design::kFocusDuration);
        r.focus.tick(dt);
    }
    for (size_t i = 0; i < buttons_.size(); ++i) {
        Button& b = buttons_[i];
        const bool focused = onFooter_ && static_cast<int>(i) == button_;
        b.focus.retarget(focused ? 1.0f : 0.0f, design::kFocusDuration);
        b.focus.tick(dt);
    }
}

void Flow::draw() {
    ui::Renderer& r = *d_.renderer;
    ui::TextRenderer& t = *d_.text;
    const float sc = r.scale();

    r.drawBackdrop({ui::palette::kBackdropTop, ui::palette::kBackdropMid,
                    ui::palette::kBackdropBottom, 0.55f});

    // The five steps, as dots. Not a percentage and not "step 3 of 5" — a
    // shape somebody can see the end of at a glance, which is the one thing a
    // setup flow owes a person who does not know how long it is.
    {
        // One per step, Done excluded — it is the end, not a stop along the
        // way.
        constexpr int kSteps = 4;
        const int at = static_cast<int>(machine_.step());
        for (int i = 0; i < kSteps; ++i) {
            Rect dot;
            dot.w = i == at ? 44.0f : 22.0f;
            dot.h = 6.0f;
            dot.radius = 3.0f;
            dot.x = kInset + static_cast<float>(i) * 34.0f;
            dot.y = kDotsY;
            dot.fill = i < at    ? ui::palette::kScreenCyan
                       : i == at ? Color::white(0.95f)
                                 : Color::white(0.22f);
            r.draw(dot);
        }
    }

    // Title and prose.
    float y = kTitleTop;
    t.draw(r, title(), kInset, y + t.ascent(ui::TextStyle::LargeTitle, sc),
           ui::TextStyle::LargeTitle, Color::white(1.0f), sc);
    y += t.lineHeight(ui::TextStyle::LargeTitle, sc) + 24.0f;

    for (const std::string& line :
         wrap(t, prose(), ui::TextStyle::Body, sc, kProseWidth)) {
        t.draw(r, line, kInset, y + t.ascent(ui::TextStyle::Body, sc),
               ui::TextStyle::Body, Color::white(0.74f), sc);
        y += t.lineHeight(ui::TextStyle::Body, sc);
    }

    // The pairing step puts the address and the code in the prose column,
    // because they are the things somebody reads out or types — the QR is only
    // a shortcut past typing them.
    if (machine_.step() == firstrun::Step::Pair && pairingLive_) {
        y += 34.0f;
        for (const std::string& line :
             wrap(t, pairing_.verificationUrl, ui::TextStyle::Body, sc, kProseWidth)) {
            t.draw(r, line, kInset, y + t.ascent(ui::TextStyle::Body, sc),
                   ui::TextStyle::Body, ui::palette::kScreenCyan, sc);
            y += t.lineHeight(ui::TextStyle::Body, sc);
        }
        y += 22.0f;
        // THE CODE IS THE BIGGEST THING ON THE SCREEN AFTER THE TITLE. It is
        // what somebody reads out loud, checks against their phone, or types
        // when the QR will not scan — and RomM shows the same characters on the
        // page they are approving, so the two have to be comparable at a
        // glance from a sofa.
        t.draw(r, "Code " + pairing_.userCode, kInset,
               y + t.ascent(ui::TextStyle::Title1, sc), ui::TextStyle::Title1,
               Color::white(1.0f), sc);
    }

    // THE PANEL IS THE SIZE OF WHAT IS IN IT. A fixed-height box with one line
    // in it and six rows of empty space below reads as a list that failed to
    // load — which is exactly the wrong thing to say on the screen where the
    // console is looking for networks.
    // A STEP WITH NOTHING TO ACT ON GETS NO PANEL. The last screen has no rows
    // and was drawing an empty grey box beside "Start playing" — which reads as
    // a list that failed to load on the one screen whose whole job is to say
    // that everything worked.
    const bool showingQr = machine_.step() == firstrun::Step::Pair && qrTex_.valid();
    const bool showPanel = showingQr || !rows_.empty();
    const float contentH =
        showingQr ? kPanelH
                  : std::min(kPanelH, 40.0f + static_cast<float>(rows_.size()) *
                                                  (kRowH + kRowGap) - kRowGap);
    const float panelH = std::max(contentH, kRowH + 40.0f);

    if (showPanel) {
        Rect panel;
        panel.x = kPanelX;
        panel.y = kPanelY;
        panel.w = kPanelW;
        panel.h = panelH;
        panel.radius = design::kTileRadius;
        panel.fill = ui::palette::kSurface;
        panel.fill.a = 0.55f;
        r.draw(panel);
    }

    if (showingQr) {
        const float side = 470.0f;
        qrTex_.draw(r, kPanelX + (kPanelW - side) * 0.5f,
                    kPanelY + (panelH - side) * 0.5f, side);
    } else {
        // The list. It scrolls by keeping the focused row in view rather than
        // by paging, because a list whose length nobody controls — every Wi-Fi
        // network in a block of flats — has no sensible page.
        const int visible = static_cast<int>((kPanelH - 40.0f) / (kRowH + kRowGap));
        (void)panelH;
        int first = 0;
        if (!onFooter_ && row_ >= visible) first = row_ - visible + 1;
        float ry = kPanelY + 20.0f;
        for (size_t i = static_cast<size_t>(first);
             i < rows_.size() && static_cast<int>(i) - first < visible; ++i) {
            Row& row = rows_[i];
            const float f = row.focus.value();
            Rect box;
            box.x = kPanelX + 18.0f;
            box.y = ry;
            box.w = kPanelW - 36.0f;
            box.h = kRowH;
            // Remembered so a pointer can be asked what it is over.
            rows_[i].x = box.x;
            rows_[i].y = box.y;
            rows_[i].w = box.w;
            rows_[i].h = box.h;
            box.radius = design::kRowRadius;
            box.fill = Color::white(0.05f + 0.10f * f);
            box.border = design::kFocusRimWidth * f;
            box.borderColor = ui::palette::kFocusRim;
            r.draw(box);

            const float baseline = ry + (kRowH - t.lineHeight(ui::TextStyle::Title3, sc)) * 0.5f +
                                   t.ascent(ui::TextStyle::Title3, sc);
            const float detailW =
                row.detail.empty() ? 0.0f
                                   : t.measure(row.detail, ui::TextStyle::Footnote, sc);
            const float titleMax = box.w - 56.0f - detailW - (detailW > 0 ? 24.0f : 0.0f);
            t.draw(r, t.truncate(row.title, ui::TextStyle::Title3, sc, titleMax),
                   box.x + 28.0f, baseline, ui::TextStyle::Title3,
                   Color::white(row.enabled ? 1.0f : 0.45f), sc);
            if (!row.detail.empty())
                t.draw(r, row.detail, box.x + box.w - 28.0f - detailW,
                       ry + (kRowH - t.lineHeight(ui::TextStyle::Footnote, sc)) * 0.5f +
                           t.ascent(ui::TextStyle::Footnote, sc),
                       ui::TextStyle::Footnote, Color::white(0.60f), sc);
            ry += kRowH + kRowGap;
        }
    }

    // Whatever the console last did, or last refused to do.
    if (!notice_.empty()) {
        float ny = kPanelY + panelH + 18.0f;
        for (const std::string& line :
             wrap(t, notice_, ui::TextStyle::Footnote, sc, kPanelW)) {
            t.draw(r, line, kPanelX, ny + t.ascent(ui::TextStyle::Footnote, sc),
                   ui::TextStyle::Footnote,
                   noticeIsError_ ? ui::palette::kMarqueeAmber : Color::white(0.72f), sc);
            ny += t.lineHeight(ui::TextStyle::Footnote, sc);
        }
    }

    // The buttons.
    float bx = kInset;
    for (Button& b : buttons_) {
        const float f = b.focus.value();
        const float tw = t.measure(b.label, ui::TextStyle::Title3, sc);
        const float bw = tw + design::kPillPadX * 4.0f;
        const float bh = t.lineHeight(ui::TextStyle::Title3, sc) + design::kPillPadY * 3.0f;
        Rect pill;
        pill.x = bx;
        pill.y = kFooterY;
        pill.w = bw;
        pill.h = bh;
        b.x = bx;
        b.y = kFooterY;
        b.w = bw;
        b.h = bh;
        pill.radius = bh * 0.5f;
        pill.fill = Color::white(b.enabled ? 0.08f + 0.14f * f : 0.04f);
        pill.border = design::kFocusRimWidth * f;
        pill.borderColor = ui::palette::kFocusRim;
        r.draw(pill);
        t.draw(r, b.label, bx + (bw - tw) * 0.5f,
               kFooterY + design::kPillPadY * 1.5f + t.ascent(ui::TextStyle::Title3, sc),
               ui::TextStyle::Title3,
               Color::white(b.enabled ? (f > 0.5f ? 1.0f : 0.78f) : 0.35f), sc);
        bx += bw + design::kPillGap;
    }

    // EVERYTHING ABOVE WENT INTO A TEXTURE, NOT ONTO THE SCREEN.
    //
    // `beginFrame` binds an offscreen scene target so that panels can blur what
    // is behind them, and `presentScene` is what puts that texture on the actual
    // framebuffer. Miss it and the frame loop runs perfectly, at sixty frames a
    // second, presenting nothing — which is exactly what the television showed:
    // a blank screen from a process using 5% of a core and reporting no error
    // anywhere. gamescope's own screenshot came back entirely black while the
    // panel showed white, and neither of those is a message anybody can act on.
    //
    // It did not show up in any capture, because `--render-size` takes the
    // offscreen path and `saveFrame` reads that target directly. So every
    // screenshot of these screens was correct and the console still drew
    // nothing. **An offscreen capture does not prove a window ever gets a
    // frame.**
    r.presentScene();

    // THE KEYBOARD IS DRAWN AFTER, and owns every key while it is open — the
    // same way the core owns the pad while a game is running. After, because
    // presentScene has just bound the real framebuffer: anything drawn from
    // here lands on top of the scene rather than inside it.
    if (keyboard_.isOpen()) keyboard_.draw(r, t, sc);
}

// --- The loop ---------------------------------------------------------------

Outcome Flow::run() {
    // TEXT INPUT IS OFF BY DEFAULT IN SDL3 and has to be asked for. Without
    // this the physical keyboard cannot type a single character into any field
    // here — SDL_EVENT_TEXT_INPUT simply never arrives — which would break the
    // one requirement the whole flow exists to meet.
    if (d_.window) SDL_StartTextInput(d_.window);

    observe();
    if (o_.startStep && !machine_.openAt(o_.startStep))
        std::fprintf(stderr, "[first-run] no step called '%s'\n", o_.startStep);
    enterStep();

    std::fprintf(stderr, "[first-run] starting at %s\n", firstrun::name(machine_.step()));

    uint64_t previous = SDL_GetTicksNS();
    while (running_) {
        const uint64_t now = SDL_GetTicksNS();
        const float dt = static_cast<float>(now - previous) / 1e9f;
        previous = now;

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    running_ = false;
                    outcome_ = Outcome::Quit;
                    break;

                case SDL_EVENT_GAMEPAD_ADDED:
                    SDL_OpenGamepad(e.gdevice.which);
                    // A pad that turns up mid-setup is the controller step
                    // answering itself, which is exactly what should happen
                    // when somebody wakes a pad that was already paired.
                    observe();
                    rebuild();
                    break;

                case SDL_EVENT_TEXT_INPUT:
                    if (keyboard_.isOpen()) keyboard_.typeText(e.text.text);
                    break;

                // A MOUSE MOVES FOCUS AND CLICKS THE FOCUSED THING. See
                // `pointAt` and open question 16 — this is deliberately not a
                // pointer: there is no cursor of ours, nothing lights up under
                // it that would not light up under the d-pad, and a click on
                // empty space does nothing at all.
                //
                // The on-screen keyboard owns every input while it is open, so
                // the pointer is ignored there too rather than half-working. A
                // physical keyboard types into that field anyway, which is the
                // one input this flow guarantees.
                case SDL_EVENT_MOUSE_MOTION: {
                    float cx = 0, cy = 0;
                    if (keyboard_.isOpen()) {
                        if (canvasPoint(e.motion.x, e.motion.y, &cx, &cy))
                            keyboard_.focusAt(cx, cy);
                    } else {
                        pointAt(e.motion.x, e.motion.y);
                    }
                    break;
                }

                case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                    if (e.button.button != SDL_BUTTON_LEFT) break;
                    if (keyboard_.isOpen()) {
                        float cx = 0, cy = 0;
                        if (!canvasPoint(e.button.x, e.button.y, &cx, &cy)) break;
                        const std::string value = keyboard_.value();
                        bool hit = false;
                        const ui::KeyboardResult res = keyboard_.pressAt(cx, cy, &hit);
                        // A click on the scrim is not a press. Without this a
                        // miss would act on whatever key happened to be focused
                        // last, which is the one thing a pointer must never do.
                        if (hit) typingFinished(res, value);
                        break;
                    }
                    // Focus first, then activate what is now focused — so a
                    // click is exactly "point at it and press A", and never a
                    // second path into the same action.
                    if (pointAt(e.button.x, e.button.y)) activate();
                    break;
                }

                case SDL_EVENT_KEY_DOWN:
                    if (keyboard_.isOpen()) {
                        switch (e.key.key) {
                            case SDLK_LEFT: keyboard_.moveFocus(-1, 0); break;
                            case SDLK_RIGHT: keyboard_.moveFocus(+1, 0); break;
                            case SDLK_UP: keyboard_.moveFocus(0, -1); break;
                            case SDLK_DOWN: keyboard_.moveFocus(0, +1); break;
                            case SDLK_BACKSPACE: keyboard_.backspace(); break;
                            case SDLK_RETURN:
                            case SDLK_KP_ENTER: {
                                const std::string value = keyboard_.value();
                                typingFinished(keyboard_.commit(), value);
                                break;
                            }
                            case SDLK_ESCAPE:
                                typingFinished(keyboard_.cancel(), {});
                                break;
                            default: break;
                        }
                        break;
                    }
                    switch (e.key.key) {
                        case SDLK_UP: moveFocus(-1); break;
                        case SDLK_DOWN: moveFocus(+1); break;
                        case SDLK_LEFT: moveAction(-1); break;
                        case SDLK_RIGHT: moveAction(+1); break;
                        case SDLK_RETURN:
                        case SDLK_KP_ENTER:
                        case SDLK_SPACE: activate(); break;
                        case SDLK_ESCAPE:
                        case SDLK_BACKSPACE: goBack(); break;
                        default: break;
                    }
                    break;

                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    if (keyboard_.isOpen()) {
                        switch (e.gbutton.button) {
                            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: keyboard_.moveFocus(-1, 0); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: keyboard_.moveFocus(+1, 0); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_UP: keyboard_.moveFocus(0, -1); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: keyboard_.moveFocus(0, +1); break;
                            case SDL_GAMEPAD_BUTTON_SOUTH: {
                                // A on the "done" KEY ends the session, and
                                // that used to be dropped on the floor. The
                                // value has to be read before the press,
                                // because the press is what closes it.
                                const std::string value = keyboard_.value();
                                typingFinished(keyboard_.pressKey(), value);
                                break;
                            }
                            case SDL_GAMEPAD_BUTTON_WEST: keyboard_.backspace(); break;
                            case SDL_GAMEPAD_BUTTON_NORTH: keyboard_.toggleShift(); break;
                            case SDL_GAMEPAD_BUTTON_START: {
                                const std::string value = keyboard_.value();
                                typingFinished(keyboard_.commit(), value);
                                break;
                            }
                            case SDL_GAMEPAD_BUTTON_EAST:
                                typingFinished(keyboard_.cancel(), {});
                                break;
                            default: break;
                        }
                        break;
                    }
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_UP: moveFocus(-1); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: moveFocus(+1); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: moveAction(-1); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: moveAction(+1); break;
                        case SDL_GAMEPAD_BUTTON_SOUTH: activate(); break;
                        case SDL_GAMEPAD_BUTTON_EAST: goBack(); break;
                        default: break;
                    }
                    break;

                default: break;
            }
        }

        pumpJobs();
        tick(dt);

        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(d_.window, &dw, &dh);
        bool offscreen = false;
        if (o_.renderWidth > 0 && o_.renderHeight > 0) {
            offscreen = d_.renderer->beginOffscreen(o_.renderWidth, o_.renderHeight);
            if (offscreen) {
                dw = o_.renderWidth;
                dh = o_.renderHeight;
            }
        }
        d_.renderer->beginFrame(dw, dh);
        draw();

        ++frame_;
        const bool capturing = o_.screenshotPath && frame_ >= o_.frames;
        if (capturing) {
            d_.renderer->saveFrame(o_.screenshotPath, dw, dh);
            std::fprintf(stderr, "[first-run] wrote %s\n", o_.screenshotPath);
            running_ = false;
            outcome_ = Outcome::Quit;
        }
        // The offscreen target has to be released before the swap, or the
        // window is presented with a framebuffer that is still bound.
        if (offscreen) d_.renderer->endOffscreen();
        SDL_GL_SwapWindow(d_.window);

        // PACED AT SIXTY, AND THIS IS NOT ONLY TIDINESS.
        //
        // A setup screen is static: a list, some prose and a focus rim. Left
        // unpaced it runs as fast as the GPU will go — on the A9's Radeon that
        // is thousands of frames a second, spinning a discrete graphics chip at
        // full tilt to draw a page of text, on a machine that may be sitting in
        // a cabinet under a television.
        //
        // AND IT IS WHAT MAKES `--frames` MEAN ANYTHING. Every one of these
        // screens is waiting on something that takes seconds — a scan, a server
        // probe, a pairing round trip — so a capture has to be able to wait in
        // units a person can reason about. Unpaced, two hundred frames on the
        // A9 went by before the server had answered, and the capture of the
        // pairing screen came out with no code on it. That is the same trap
        // `--launch-after` fell into, one screen along.
        const uint64_t spent = SDL_GetTicksNS() - now;
        constexpr uint64_t kFrameNS = 1000000000ull / 60ull;
        if (spent < kFrameNS)
            SDL_DelayNS(kFrameNS - spent);
    }

    if (d_.window) SDL_StopTextInput(d_.window);
    return outcome_;
}

}  // namespace

// THE AFTERBURNER CABINET, DRAWN RATHER THAN SHIPPED.
//
// docs/PROJECT.md, "The icon has two variants and the second one is earned":
// the warm-screened cabinet with the lit power switch marks a platform that
// runs more emulators at full power. macOS has it; CabinetOS earned it with
// PlayStation 2 and GameCube. So this console draws THAT one, not the cyan
// version `tools/make_icon.swift` generates.
//
// Five rounded rectangles and four circles, which is all the icon is. Drawn
// means sharp at any panel size, no image asset in the OS image, and the
// marquee and screen can be animated later — which is what the boot splash
// wants when somebody builds it.
//
// The numbers are the icon's own, in its 1024 space, sampled from
// AppIconMac.appiconset because that PNG is currently the only definition of
// the Afterburner variant. `s` scales that space; `ox`/`oy` place it.
static void drawCabinet(ui::Renderer& r, float ox, float oy, float s, float a) {
    auto R = [&](float x, float y, float w, float h, float rad, Color c) {
        c.a *= a;
        r.draw(ui::Rect{ox + x * s, oy + y * s, w * s, h * s, rad * s, c});
    };
    auto dot = [&](float cx, float cy, float rad, Color c) {
        R(cx - rad, cy - rad, rad * 2.0f, rad * 2.0f, rad, c);
    };

    const Color body  = Color::rgb(0xF2EEE8);
    const Color panel = Color::rgb(0xD7D1C8);

    // A GRADIENT IS A TEXTURE, NOT A STACK OF RECTANGLES.
    //
    // The first two attempts drew the ramps as 3 and then 30 strips, because
    // this renderer has no gradient rect. Both read as corduroy — MMagTech:
    // *"that looks really bad"* — and the second one looked worse than the
    // first, because thirty seams are more obviously wrong than two. Widening
    // the overlap to two device pixels did not help either, which was the clue
    // that the seam was not an overlap problem at all.
    //
    // The renderer already draws textured quads with linear filtering and a
    // rounded clip — `drawImage` and the QR code both use it. So the ramp is a
    // 64-texel texture and the GPU interpolates it: no seams, at any size,
    // because there are no interior edges to seam.
    //
    // It also makes the marquee animatable for free. A sweep of light across
    // it is `u0`/`u1` moving over time, which is what the boot splash in
    // docs/PROJECT.md describes when it says the marquee lights.
    // `vertical` makes the texture 1xN instead of Nx1. Rotating the quad's UVs
    // was the first attempt and it came out sideways AND reversed, which is a
    // lot of guessing about someone else's rotation convention to save four
    // characters. A texture shaped the way it is used needs no convention.
    auto rampTexture = [](Color from, Color to, bool vertical) {
        GLuint tex = 0;
        constexpr int kN = 64;
        unsigned char px[kN * 4];
        for (int i = 0; i < kN; ++i) {
            const float f = static_cast<float>(i) / (kN - 1);
            px[i * 4 + 0] = static_cast<unsigned char>((from.r + (to.r - from.r) * f) * 255);
            px[i * 4 + 1] = static_cast<unsigned char>((from.g + (to.g - from.g) * f) * 255);
            px[i * 4 + 2] = static_cast<unsigned char>((from.b + (to.b - from.b) * f) * 255);
            px[i * 4 + 3] = 255;
        }
        glGenTextures(1, &tex);
        glBindTexture(GL_TEXTURE_2D, tex);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, vertical ? 1 : kN, vertical ? kN : 1,
                     0, GL_RGBA, GL_UNSIGNED_BYTE, px);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
        return tex;
    };
    // Built once. This screen is drawn a handful of times per boot and the
    // textures outlive it deliberately.
    static GLuint marqueeTex =
        rampTexture(Color::rgb(0xFF96CA), Color::rgb(0xFFCB6E), /*vertical=*/false);
    static GLuint screenTex =
        rampTexture(Color::rgb(0xFEE08F), Color::rgb(0xEB5736), /*vertical=*/true);

    auto ramp = [&](float x, float y, float w, float h, float rad, GLuint tex) {
        const float px = ox + x * s, py = oy + y * s;
        const float pw = w * s, ph = h * s;
        r.drawTextured(px, py, pw, ph, tex, 0, 0, 1, 1, Color::white(a), false, 0.0f,
                       px, py, pw, ph, rad * s);
    };

    R(268, 130, 488, 670, 42, body);

    // The marquee, pink to amber, left to right.
    ramp(318, 176, 390, 84, 16, marqueeTex);

    // **NO SWEEP ACROSS THE MARQUEE, AND IT WAS BUILT AND REMOVED.**
    // MMagTech asked for one, then looked at it: *"don't like that, the static
    // was better."* He is right — a light crossing the sign pulls the eye to
    // the one part of the screen with nothing to say, and the icon is better
    // still than moving.
    //
    // **The need it was meant to serve is real and is met elsewhere**: this
    // screen can be up for ninety seconds and has to prove it has not stalled.
    // That is done with a NUMBER THAT CHANGES rather than with motion — the
    // seconds spent waiting for the server, and the games counted as they
    // arrive. Information rather than decoration, and it cannot look tacky
    // because it is not a flourish.

    ramp(316, 292, 394, 284, 18, screenTex);

    R(248, 600, 527, 112, 20, panel);
    dot(348, 656, 30, Color::rgb(0x3A3444));
    dot(500, 656, 25, Color::rgb(0xEC405C));
    dot(583, 656, 24, Color::rgb(0xFFC457));

    // THE LIT POWER SWITCH, which is the whole point of this variant: the
    // machine is ON. Its glow is three fading discs, largest first.
    dot(692, 656, 54, Color::rgb(0xFFB27A, 0.10f));
    dot(692, 656, 40, Color::rgb(0xFFB27A, 0.16f));
    dot(692, 656, 28, Color::rgb(0xFFB27A, 0.22f));
    R(668, 646, 48, 20, 10, Color::rgb(0xFFF0E2));

    R(303, 795, 419, 55, 14, panel);
}

void showWaiting(const Deps& d, const char* title, const char* detail) {
    if (!d.window || !d.renderer || !d.text) return;
    ui::Renderer& r = *d.renderer;
    ui::TextRenderer& t = *d.text;

    // **PUMP FIRST, OR THIS DRAWS AT THE WRONG SIZE ON EVERY BOOT.**
    //
    // MMagTech, 2026-09-22: *"I see the starting up screen appear and it's a
    // tinier image in the bottom left with the rest of the screen black."*
    // Measured: the drawable reported 1920x1080 while the panel is 3840x2160,
    // so this drew a quarter-size frame into a 4K framebuffer — in the bottom
    // left, because that is where GL's origin is.
    //
    // SDL creates the window at its requested size and gamescope resizes it
    // immediately afterwards; the size arrives as an EVENT. Nothing had pumped
    // the queue by the time this ran, so `SDL_GetWindowSizeInPixels` answered
    // with the size before the compositor had its say. The main loop pumps and
    // gets it right, which is why only this screen was wrong.
    //
    // THIS IS NOT A TEST-RIG ARTEFACT. It is every boot on a 4K panel, and
    // this screen exists precisely because the console used to show nothing
    // for the seconds — up to ninety — that reaching the server and pulling
    // sixteen hundred games takes.
    SDL_PumpEvents();
    int dw = 0, dh = 0;
    SDL_GetWindowSizeInPixels(d.window, &dw, &dh);
    if (dw <= 0 || dh <= 0) return;
    std::fprintf(stderr, "[waiting] drawable %dx%d\n", dw, dh);
    r.beginFrame(dw, dh);
    const float sc = r.scale();

    r.drawBackdrop({ui::palette::kBackdropTop, ui::palette::kBackdropMid,
                    ui::palette::kBackdropBottom, 0.55f});

    // CENTRED, AND NOT A SETUP STEP. This used to be "Starting up" at the
    // setup inset with the prose under it, which dressed the last beat of a
    // boot as a page in a wizard. MMagTech, 2026-09-22: *"the one we have now
    // is a bit dull and plain."*
    //
    // docs/PROJECT.md is explicit about what a boot screen says: *"the
    // wordmark 'CabinetOS' and nothing else. A boot screen names the machine;
    // it does not explain it."* So "Starting up" is gone — it labelled a state
    // that is self-evident — and what is left is the icon, the name, and one
    // quiet line saying which stage is taking the time.
    //
    // THE STAGE LINE IS NOT DECORATION. This screen can be up for ninety
    // seconds when the server is slow, and one sentence that never changes for
    // ninety seconds is indistinguishable from a hang.
    // **THE CABINET IS SIZED FROM THE WORDMARK, and that order matters.**
    // MMagTech, 2026-09-22: *"it looks weird that the CabinetOS is smaller in
    // width than the cabinet."* It did — the cabinet was a fixed 460 tall and
    // the name came out narrower, so the mark and the name floated as two
    // objects instead of locking into one.
    //
    // Measuring the text first and deriving the cabinet's width from it means
    // they are the same width on any panel and at any font metric, rather than
    // agreeing only at the size somebody happened to tune. Nothing here is a
    // constant that has to be re-tuned when the type changes.
    const char* kWordmark = "CabinetOS";
    const ui::TextStyle markStyle = ui::TextStyle::LargeTitle;
    const float ww = t.measure(kWordmark, markStyle, sc);

    // The icon's cabinet spans x 248..775 and y 130..850 in its own 1024 space.
    // The cabinet occupies x 248..775 and y 130..850 of the icon's own space,
    // so `oy` is where y=0 would be and the artwork starts 130 below it. The
    // first version subtracted the cabinet's HEIGHT instead of its bottom edge
    // and the wordmark landed on top of the base.
    constexpr float kIconW = 527.0f, kIconTop = 130.0f, kIconBottom = 850.0f;
    const float s = ww / kIconW;
    const float cabTop = 150.0f;
    const float oy = cabTop - kIconTop * s;
    drawCabinet(r, (ui::kCanvasWidth - ww) * 0.5f - 248.0f * s, oy, s, 1.0f);

    float y = oy + kIconBottom * s + 56.0f;
    t.draw(r, kWordmark, (ui::kCanvasWidth - ww) * 0.5f,
           y + t.ascent(markStyle, sc), markStyle, Color::white(0.94f), sc);
    y += t.lineHeight(markStyle, sc) + 16.0f;

    if (detail) {
        const float dw = t.measure(detail, ui::TextStyle::Body, sc);
        t.draw(r, detail, (ui::kCanvasWidth - dw) * 0.5f,
               y + t.ascent(ui::TextStyle::Body, sc), ui::TextStyle::Body,
               Color::white(0.42f), sc);
    }
    (void)title;

    // Without this the frame goes into the scene texture and never reaches the
    // window — see the note at the end of Flow::draw.
    r.presentScene();
    SDL_GL_SwapWindow(d.window);
}

Outcome run(const Deps& d, const Options& o) {
    if (!d.window || !d.renderer || !d.text) return Outcome::Quit;
    Flow flow(d, o);
    return flow.run();
}

}  // namespace setup
