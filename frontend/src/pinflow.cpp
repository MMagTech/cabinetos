// The PIN: asked before what it protects, chosen, changed and turned off,
// and the pad's answers (#398, restructure batch e). A Remote Play pairing
// rides on the same pad through pinCode (remoteplayflow.cpp).

#include "app.h"

#include <cstdio>
#include <utility>

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

void App::askPin(const std::string& title, const std::string& detail,
                 std::function<void()> then, bool always) {
    pinExpect.clear();
    if (!accounts::pinIsSet() || (pinUnlocked && !always)) { then(); return; }
    pinThen = std::move(then);
    pinScreen.open(screens::PinScreen::Mode::Check, title, detail);
    std::fprintf(stderr, "[pin] asked: %s\n", title.c_str());
}

void App::askCode(const std::string& code, const std::string& title,
                  const std::string& detail, std::function<void()> then) {
    pinExpect = code;
    pinThen = std::move(then);
    pinScreen.open(screens::PinScreen::Mode::Check, title, detail);
    std::fprintf(stderr, "[pin] code asked: %s\n", title.c_str());
}

void App::choosePin(const std::string& title, const std::string& detail,
                    std::function<void()> then) {
    pinExpect.clear();
    pinThen = std::move(then);
    pinScreen.open(screens::PinScreen::Mode::Choose, title, detail);
}

// DEVELOPER ACCESS NEEDS A PIN TO EXIST (MMagTech, 2026-10-07). It hands
// over the whole machine, so turning it on is the one place a console
// with no PIN asks for one: the PIN is what a Linux install's admin
// password would be. It only lets somebody at the television turn it on
// and see the password; the network still needs the password. NOT File
// access: the PIN is one lock for the whole console, and requiring it
// there would put every family that only copies saves behind PIN prompts
// for Wi-Fi, accounts and updates too. Only the owner can set a PIN, so
// another account on a console without one is told who can.
void App::askPinToOpen(const std::string& detail, std::function<void()> then) {
    if (accounts::pinIsSet()) {
        askPin("Enter the PIN", detail, std::move(then));
        return;
    }
    if (accounts::activeId() != accounts::ownerId()) {
        const std::vector<accounts::Account> list = accounts::all();
        const accounts::Account* owner = accounts::find(list, accounts::ownerId());
        menuNotice.say("Needs a PIN from " + (owner ? owner->name : std::string("the owner")),
                       Tone::Problem);
        sound::play(sound::Cue::Edge);
        return;
    }
    choosePin("Choose a PIN", detail, [&, then = std::move(then)]() {
        pinUnlocked = true;
        then();
    });
}

void App::pinOutcome(screens::PinScreen::Outcome o) {
    using O = screens::PinScreen::Outcome;
    if (o == O::None) return;
    if (pinCode) {
        auto took = std::move(pinCode);
        auto left = std::move(pinCodeCancel);
        pinCode = nullptr;
        pinCodeCancel = nullptr;
        const std::string digits = pinScreen.pin();
        pinScreen.close();
        if (o == O::Cancelled) {
            if (left) left();
        } else {
            sound::play(sound::Cue::Activate);
            took(digits);
        }
        return;
    }
    if (o == O::Cancelled) {
        pinScreen.close();
        pinThen = nullptr;
        pinExpect.clear();
        std::fprintf(stderr, "[pin] left without one\n");
        return;
    }
    if (!pinExpect.empty()) {
        if (pinScreen.pin() != pinExpect) {
            sound::play(sound::Cue::Edge);
            pinScreen.reject("Wrong code");
            return;
        }
        pinExpect.clear();
    } else if (pinScreen.mode() == screens::PinScreen::Mode::Check) {
        if (!accounts::checkPin(pinScreen.pin())) {
            ++pinFails;
            sound::play(sound::Cue::Edge);
            std::fprintf(stderr, "[pin] wrong, %d of %d\n", pinFails, kPinTries);
            pinScreen.reject("Wrong PIN");
            if (pinFails >= kPinTries) {
                pinFails = 0;
                pinScreen.lockFor(kPinLockSeconds);
            }
            return;
        }
        pinFails = 0;
        pinUnlocked = true;
        std::fprintf(stderr, "[pin] accepted\n");
    } else {
        std::string err;
        if (!accounts::setPin(pinScreen.pin(), &err)) {
            pinScreen.reject("The PIN could not be saved");
            std::fprintf(stderr, "[pin] could not save: %s\n", err.c_str());
            return;
        }
        std::fprintf(stderr, "[pin] set\n");
    }
    // CLOSED BEFORE THE ACTION RUNS, so an action that opens the pad
    // again (Change PIN: the old one, then the new one) opens it fresh.
    pinScreen.close();
    auto then = std::move(pinThen);
    pinThen = nullptr;
    sound::play(sound::Cue::Activate);
    if (then) then();
}

// Leaving Settings locks the PIN again.
void App::lockPinOffSettings() {
    if (here() != Screen::Settings && !pinScreen.isOpen()) pinUnlocked = false;
}

void App::pressPinOff() {
    askPin("Enter the PIN", "To turn it off", [&]() {
        std::string err;
        if (accounts::setPin("", &err))
            std::fprintf(stderr, "[pin] turned off\n");
        buildSettings();
    }, true);
    sound::play(sound::Cue::Activate);
}

void App::pressPinChange() {
    // ALWAYS ASKS (#237), even after the PIN was entered once
    // this visit: the once-per-visit rule is for Downloads, and
    // changing or removing the PIN itself must prove you know it.
    askPin("Enter your current PIN", "", [&]() {
        choosePin("Choose a new PIN", "",
                  [&]() { buildSettings(); });
    }, true);
    sound::play(sound::Cue::Activate);
}

void App::pressPinSet() {
    choosePin("Choose a PIN", "",
              [&]() { buildSettings(); });
    sound::play(sound::Cue::Activate);
}
