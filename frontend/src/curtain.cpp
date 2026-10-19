// The curtain: down on a sign out, a new server address or Steam, up off
// the startup screen onto Home, and its draw over everything (#109)
// (#398, restructure batch e). An account switch's pump is accountsflow's.

#include "app.h"

#include <cstdio>

#include "powerprofile.h"

using namespace design;   // NOLINT, as main.cpp names them

void App::startLeaving(Leave what, const std::string& label) {
    leaving = what;
    leaveLabel = label;
    leaveDownAt = 0;
    accountsOpen = false;
    barFocused = false;
    curtain.retarget(1.0f, kCurtainDown);
    sound::play(sound::Cue::Activate);
    std::fprintf(stderr, "[server] %s\n", label.c_str());
}

// ---- Leaving, behind the startup screen ------------------------------
//
// Sign out and a new server address both end with this program starting
// again: the address is read once at startup, and first run is a loop of
// its own that runs before this one (setup.h). The curtain is the startup
// screen, so the picture the app leaves on is the one it comes back on.
// MMagTech, 2026-09-25: not a black screen.
void App::pumpLeave() {
    if (leaving == Leave::None) return;
    curtain.retarget(1.0f, kCurtainDown);
    if (curtain.value() < 0.995f) return;
    const Uint64 now = SDL_GetTicks();
    if (leaveDownAt == 0) leaveDownAt = now;
    // UPLOADS FIRST, at most twenty seconds, as for sleep. A save that
    // is still going up would otherwise be cleared by Sign out, or cut
    // off by the restart. By design this is nothing: leaving a game
    // uploads, and Settings is a walk away from any game.
    if (uploader.pending() > 0 && now - leaveDownAt < 20000) return;
    // The line stays up long enough to read: the switch's reason.
    constexpr Uint64 kLeaveHoldMs = 700;
    if (now - leaveDownAt < kLeaveHoldMs) return;
    if (uploader.pending() > 0)
        std::fprintf(stderr, "[server] leaving with %d upload(s) still owed\n",
                     uploader.pending());
    if (leaving == Leave::Steam) {
        // Full speed for Steam's games, as for a game here (the frontend
        // is not running to ask while Steam is); the console's own start
        // puts it back to balanced. Then the request, and out: the session
        // script does the rest.
        powerprofile::applyNow(true);
        if (!steam::requestHandover()) {
            std::fprintf(stderr, "[steam] could not ask the session to hand over\n");
            leaving = Leave::None;
            curtain.retarget(0.0f, kCurtainUp);
            menuNotice.say("Couldn't start Steam", Tone::Problem);
            return;
        }
        running = false;
        return;
    }
    if (leaving == Leave::SignOut) {
        std::string err;
        if (!server::signOut(&err)) {
            std::fprintf(stderr, "[sign out] could not: %s\n", err.c_str());
            leaving = Leave::None;
            curtain.retarget(0.0f, kCurtainUp);
            menuNotice.say("Couldn't sign out", Tone::Problem);
            return;
        }
    }
    restartSelf = true;
    running = false;
}

// ---- Arriving, from behind the startup screen (see `arriving`) -------
//
// Held down for a few frames first, so the first frames of Home, which
// upload textures and can take several frame times each, are spent behind
// it rather than eating the start of the lift. Then held while Home's
// covers are still coming off the disk or fading in, for at most a
// second, so the shelves do not fill in during the lift. Then up, as onto
// a game.
//
// ANYTHING ELSE THAT WANTS THE CURTAIN TAKES IT. A game launched as the
// console starts, or a press on Play during the lift, brings it down as
// its own; from then on it is theirs, and this stops drawing the startup
// screen on it.
void App::pumpArrive() {
    if (!arriving) return;
    if (launchJob.stage.load() != LaunchJob::Stage::Idle || switchPendingId ||
        leaving != Leave::None || (arriveLifting && curtain.to > 0.0f)) {
        arriving = false;
        return;
    }
    if (arriveLifting) {
        if (curtain.value() <= 0.001f) {
            arriving = false;
            std::fprintf(stderr, "[startup] Home is up\n");
        }
        return;
    }
    const Uint64 now = SDL_GetTicks();
    if (arriveHeldAt == 0) arriveHeldAt = now;
    constexpr int kArriveHoldFrames = 3;
    constexpr Uint64 kArriveCoversMs = 1000;   // a starting value
    if (++arriveFrames < kArriveHoldFrames) return;
    const int waiting = images.settlingCount();
    if (waiting > 0 && now - arriveHeldAt < kArriveCoversMs) return;
    std::fprintf(stderr,
                 "[startup] lifting onto Home after %llu ms, %d image(s) still settling\n",
                 static_cast<unsigned long long>(now - arriveHeldAt), waiting);
    curtain.retarget(0.0f, kCurtainUp);
    arriveLifting = true;
}

void App::startCurtain() {
    curtain.smooth = true;
    curtain.from = curtain.to = 0.0f;
    if (startupShown && !shotMode) {
        curtain.from = curtain.to = 1.0f;
        curtain.elapsed = curtain.duration;
        arriving = true;
    }
}

// ---- The curtain, over everything --------------------------------
//
// Over the screens and the in-game overlay; the keyboard and the dim are
// drawn after it (frame.cpp). It is not part of any screen, it is the
// screen going away. See design.h.
void App::drawCurtain(float sc) {
    {
        const float c = curtain.value();
        if (c > 0.001f && (leaving != Leave::None || arriving)) {
            // Leaving: the startup screen, which is what comes back.
            // Arriving: the startup screen, which is what was there.
            const float keep = renderer.contentAlpha();
            renderer.setContentAlpha(1.0f);
            setup::drawStartup(renderer, text,
                               leaving != Leave::None ? leaveLabel.c_str() : kLoadingLine, c);
            renderer.setContentAlpha(keep);
        } else if (c > 0.001f && switchCurtain) {
            // The console's own backdrop, in one piece, faded. Two bands
            // were drawn here first and left a line under the name where
            // they met; MMagTech saw it on the TV.
            renderer.drawBackdrop(look::backdrop(), c);
        } else if (c > 0.001f) {
            renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                   ui::Color::black(c)});
        }
        if (c <= 0.001f && switchPendingId == 0) switchCurtain = false;
        // WHO IT IS BECOMING, on the curtain, only while the curtain is
        // fully down (see switchText).
        const float ta = switchText.value();
        if (ta > 0.001f && !switchLabel.empty()) {
            const float lw = text.measure(switchLabel, ui::TextStyle::Title2, sc);
            text.draw(renderer, switchLabel, (ui::kCanvasWidth - lw) * 0.5f,
                      ui::kCanvasHeight * 0.5f + text.ascent(ui::TextStyle::Title2, sc) * 0.5f,
                      ui::TextStyle::Title2, ui::Color::white(0.9f * ta), sc);
        } else if (ta <= 0.001f && switchPendingId == 0) {
            switchLabel.clear();
        }
    }
}
