#include "setup.h"

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

// --- The QR code, as one texture -------------------------------------------
//
// A version-4 code is 33x33 modules, and drawing each as its own rounded
// rectangle is eleven hundred draw calls a frame for a picture that never
// changes. One texture, one quad, uploaded when the code changes and not again.
//
// SINGLE CHANNEL, AND NEAREST FILTERING. The renderer's single-channel path
// multiplies the texel by the tint's alpha, which is exactly what is wanted: a
// texel of 1 where a module is DARK, tinted near-black, over a white card. And
// nearest, because a QR code is the one thing on this console that must not be
// smoothed — a blurred module boundary is a module a camera cannot call.
class QrTexture {
public:
    ~QrTexture() { release(); }

    void set(const qr::Code& c) {
        release();
        if (!c.valid()) return;
        size_ = c.size;
        std::vector<uint8_t> px(static_cast<size_t>(size_) * size_);
        for (int y = 0; y < size_; ++y)
            for (int x = 0; x < size_; ++x)
                px[static_cast<size_t>(y) * size_ + x] = c.at(x, y) ? 255 : 0;
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size_, size_, 0, GL_RED,
                     GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool valid() const { return tex_ != 0; }

    // Draws it centred in the given square, WITH THE QUIET ZONE, because the
    // quiet zone is the renderer's job and a code drawn flush to the edge of
    // its card does not scan at all. Measured; see qr.h.
    void draw(ui::Renderer& r, float x, float y, float side) const {
        if (!tex_) return;
        constexpr int kQuiet = 4;
        const float modules = static_cast<float>(size_ + kQuiet * 2);
        const float m = side / modules;                 // one module, in points
        // The card the code sits on: white, and large enough to carry the quiet
        // zone as real light modules rather than as a promise.
        Rect card;
        card.x = x; card.y = y; card.w = side; card.h = side;
        card.radius = 12.0f;
        card.fill = Color::white(1.0f);
        r.draw(card);
        r.drawTextured(x + kQuiet * m, y + kQuiet * m, m * size_, m * size_, tex_,
                       0, 0, 1, 1, Color::rgb(0x0B0616, 1.0f), /*singleChannel=*/true);
    }

private:
    void release() {
        if (tex_) glDeleteTextures(1, &tex_);
        tex_ = 0;
        size_ = 0;
    }
    GLuint tex_ = 0;
    int size_ = 0;
};


// --- The layout -------------------------------------------------------------
//
// One shape for every step, so the flow does not appear to jump between five
// unrelated screens: prose on the left, the thing you act on on the right,
// actions along the bottom. The step changes what goes in the panel and nothing
// else.
constexpr float kInset = 80.0f;
constexpr float kDotsY = 96.0f;
constexpr float kTitleTop = 150.0f;
constexpr float kProseWidth = 700.0f;
constexpr float kPanelX = 880.0f;
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
    QrTexture qrTex_;
    Job<romm::Pairing> pairBeginJob_;
    Job<int> pairPollJob_;

    // Controller
    std::vector<bt::Device> devices_;
    int chosenDevice_ = -1;
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
        case firstrun::Step::WiFi:       return "Set up Wi-Fi";
        case firstrun::Step::Server:     return "RomM Server";
        case firstrun::Step::Pair:       return "Pair with RomM";
        case firstrun::Step::Controller: return "Pair a Controller";
        case firstrun::Step::Done:       return "Ready";
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
            if (!facts_.online) return why;
            return facts_.wiredOnline ? "Connected over Ethernet."
                                      : "Connected over Wi-Fi.";

        case firstrun::Step::WiFi:
            if (joinJob_.busy()) return "Joining…";
            if (facts_.wifiConfigured) return "Connected.";
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
            // The one line worth spending, because it is the promise the whole
            // flow was built to keep and nothing else on screen says it.
            return "You can unplug the keyboard. You will not need it again.";
    }
    return {};
}

// --- Looking at the machine -------------------------------------------------

void Flow::observe() {
    const firstrun::Facts before = facts_;
    facts_ = firstrun::observe(client_, SDL_HasGamepad() ? 1 : 0);
    // These two are not things `observe` can know: only something that has
    // tried can say whether a server answered, and the token may have arrived
    // in this very session.
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
    stepFade_.from = 0.0f;
    stepFade_.to = 1.0f;
    stepFade_.elapsed = 0.0f;
    stepFade_.duration = design::kOverlayFade;
    stepFade_.smooth = true;

    switch (machine_.step()) {
        case firstrun::Step::Network:
        case firstrun::Step::WiFi:
            // Draw whatever NetworkManager already knows immediately, then ask
            // the radio to look again. A screen that shows nothing for five
            // seconds reads as broken even when it is working.
            if (facts_.wifiPresent) {
                std::string err;
                net::cachedScan(&networks_, &err);
                startWifiScan();
            }
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
        case firstrun::Step::Network:
        case firstrun::Step::WiFi: {
            if (!facts_.wifiPresent) {
                Row r;
                r.title = "No Wi-Fi on this console";
                r.detail = "wired only";
                r.enabled = false;
                rows_.push_back(std::move(r));
                break;
            }
            if (scanJob_.busy() && networks_.empty()) {
                Row r;
                r.title = "Looking for networks…";
                r.enabled = false;
                rows_.push_back(std::move(r));
                break;
            }
            if (networks_.empty()) {
                Row r;
                r.title = "Nothing on the air";
                r.detail = "try again";
                r.enabled = false;
                rows_.push_back(std::move(r));
                break;
            }
            for (size_t i = 0; i < networks_.size(); ++i) {
                const net::Network& n = networks_[i];
                Row r;
                r.title = n.ssid;
                r.value = static_cast<int>(i);
                // The right-hand side answers "what will happen if I press
                // this" rather than reporting a signal nobody can act on.
                r.detail = n.active      ? "connected"
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
                r.title = bt::adapter().present ? "Nothing found yet"
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
        case firstrun::Step::WiFi:
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
    // A LIST THAT ARRIVES WHILE NOBODY HAS TOUCHED ANYTHING TAKES FOCUS. Every
    // one of these screens starts with a placeholder and fills in seconds
    // later, so without this focus is left on the footer and the thing the
    // person came here to pick is never under the cursor.
    if (!moved_ && onFooter_ && anyEnabledRow()) {
        onFooter_ = false;
        row_ = nextEnabledRow(0, +1);
    }
    if (!anyEnabledRow()) {
        onFooter_ = true;
    } else if (!onFooter_ && !rows_[static_cast<size_t>(row_)].enabled) {
        const int down = nextEnabledRow(row_, +1);
        row_ = down >= 0 ? down : nextEnabledRow(row_, -1);
    }
    button_ = std::max(0, std::min(button_, static_cast<int>(buttons_.size()) - 1));
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
            const char* home = getenv("HOME");
            const std::string dir = std::string(home ? home : ".") + "/.config/cabinetos";
            storage::makeDirs(dir);
            if (!c.saveToken(dir + "/romm.json")) {
                // PAIRED, BUT NOT SAVED, IS NOT "PAIRED". Calling it success
                // here is exactly how the first console ever installed came up
                // on the stand-in library with nobody able to say why.
                err = "paired, but the token could not be written to " + dir;
                out = -1;
                return false;
            }
        }
        return out >= 0;
    });
}

void Flow::startBtScan() {
    if (btScanJob_.busy()) return;
    if (!bt::adapter().present) return;
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

    if (std::vector<net::Network> found; scanJob_.take(&found, &err, &ok)) {
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
            startWifiScan();
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
            std::string berr;
            bt::known(&devices_, &berr);
        }
        changed = true;
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
        case firstrun::Step::Network:
        case firstrun::Step::WiFi: {
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
            // SHOWN, NOT HIDDEN, and this is a decision rather than an
            // oversight — docs/PROJECT.md open question 17. Nobody is
            // shoulder-surfing a living room, the passphrase is usually being
            // read off the underside of a router, and not being able to see
            // what you typed IS the difficulty.
            cfg.conceal = false;
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
        constexpr int kSteps = 5;
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
    const bool showingQr = machine_.step() == firstrun::Step::Pair && qrTex_.valid();
    const float contentH =
        showingQr ? kPanelH
                  : std::min(kPanelH, 40.0f + static_cast<float>(rows_.size()) *
                                                  (kRowH + kRowGap) - kRowGap);
    const float panelH = std::max(contentH, kRowH + 40.0f);

    Rect panel;
    panel.x = kPanelX;
    panel.y = kPanelY;
    panel.w = kPanelW;
    panel.h = panelH;
    panel.radius = design::kTileRadius;
    panel.fill = ui::palette::kSurface;
    panel.fill.a = 0.55f;
    r.draw(panel);

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
            const Row& row = rows_[i];
            const float f = row.focus.value();
            Rect box;
            box.x = kPanelX + 18.0f;
            box.y = ry;
            box.w = kPanelW - 36.0f;
            box.h = kRowH;
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
    for (const Button& b : buttons_) {
        const float f = b.focus.value();
        const float tw = t.measure(b.label, ui::TextStyle::Title3, sc);
        const float bw = tw + design::kPillPadX * 4.0f;
        const float bh = t.lineHeight(ui::TextStyle::Title3, sc) + design::kPillPadY * 3.0f;
        Rect pill;
        pill.x = bx;
        pill.y = kFooterY;
        pill.w = bw;
        pill.h = bh;
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
                                keyboard_.commit();
                                if (typing_ == Typing::Address) startServerProbe(value);
                                else if (typing_ == Typing::Passphrase) joinSelected(value);
                                typing_ = Typing::None;
                                rebuild();
                                break;
                            }
                            case SDLK_ESCAPE:
                                keyboard_.cancel();
                                typing_ = Typing::None;
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
                            case SDL_GAMEPAD_BUTTON_SOUTH: keyboard_.pressKey(); break;
                            case SDL_GAMEPAD_BUTTON_WEST: keyboard_.backspace(); break;
                            case SDL_GAMEPAD_BUTTON_NORTH: keyboard_.toggleShift(); break;
                            case SDL_GAMEPAD_BUTTON_START: {
                                const std::string value = keyboard_.value();
                                keyboard_.commit();
                                if (typing_ == Typing::Address) startServerProbe(value);
                                else if (typing_ == Typing::Passphrase) joinSelected(value);
                                typing_ = Typing::None;
                                rebuild();
                                break;
                            }
                            case SDL_GAMEPAD_BUTTON_EAST:
                                keyboard_.cancel();
                                typing_ = Typing::None;
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

Outcome run(const Deps& d, const Options& o) {
    if (!d.window || !d.renderer || !d.text) return Outcome::Quit;
    Flow flow(d, o);
    return flow.run();
}

}  // namespace setup
