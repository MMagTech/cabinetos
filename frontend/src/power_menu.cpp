// The Power menu and the power button: Sleep, Restart and Power off, the
// game left first, the button borrowed at start, and logind's warnings
// answered every frame (#398, restructure batch f).

#include "app.h"

#include <cstdio>

#include "core.h"
#include "power.h"

using namespace design;   // NOLINT, as main.cpp names them

void App::powerActivate(PowerItem item) {
    if (item == PwResume || item == PwCancel) {
        closeOverlay();
        return;
    }
    if (item == PwSteam) {
        powerCue(sound::Cue::Activate);
        closeOverlay(false);
        steamPressed();
        return;
    }
    const power::Action act = item == PwRest      ? power::Action::Rest
                              : item == PwRestart ? power::Action::Restart
                                                  : power::Action::PowerOff;
    std::fprintf(stderr, "[power] %s chosen%s\n", power::name(act),
                 playing ? " from a game; leaving it first" : "");
    // Restart and Power off are silent here: how the console sounds and
    // looks on its way down is #393's. Sleep clicks like any choice.
    if (act == power::Action::Rest) powerCue(sound::Cue::Activate);
    if (playing) finishExit();
    closeOverlay(false);
    if (act == power::Action::Rest) {
        restPending = true;
        restWaitStart = SDL_GetTicksNS();
    } else {
        // A restart or a power-off stops this process on its way down, and
        // the uploads just queued are drained then — PR #50.
        power::act(act);
    }
}

// Pressed while it is already open, it closes, like Resume.
void App::openPowerMenu() {
    if (overlayOpen && powerMenu) {
        closeOverlay();
        return;
    }
    powerItems.clear();
    // THE FIRST ITEM DOES NOTHING, and it is the one focused. In a game
    // that is Resume; on Home it is Cancel. MMagTech, 2026-09-27: Start
    // pressed by accident opened the menu on Sleep, one A from asleep.
    powerItems.push_back(playing ? PwResume : PwCancel);
    // STEAM'S DOOR (#223), never over a game. MMagTech, 2026-10-04: in
    // the Library it read as part of the ROM library, and it is not; here
    // it is a mode the machine switches into, the mirror of Steam's own
    // "Switch to Desktop". Straight under Cancel, his call: it is what
    // Start will most often be pressed for, and a slip still meets "Start
    // Steam?" or the PIN. Gone when somebody chose Hide Steam.
    if (!playing && steam::available() && !steam::hidden()) powerItems.push_back(PwSteam);
    if (restAvailable) powerItems.push_back(PwRest);
    powerItems.push_back(PwRestart);
    powerItems.push_back(PwPowerOff);
    powerMenu = true;
    overlayOpen = true;
    overlaySlot = 0;
    overlayFade.retarget(1.0f, overlayFadeSeconds);
    overlayFocus.retarget(1.0f, kOverlayFocusDuration);
    overlayFocus.elapsed = kOverlayFocusDuration;
    std::fprintf(stderr, "[power] menu open%s\n", playing ? " over a game" : "");
    powerCue(sound::Cue::Activate);
}

// The button is borrowed for the life of this process — power.h. Not for a
// capture, which is not the console and must not take its button.
void App::borrowPowerButton() {
    if (!shotMode) {
        power::takeButtons();
        power::takeShutdownDelay();
        restAvailable = power::canRest();
    }
    if (powerMenuDemo) {
        if (shotMode) restAvailable = power::canRest();
        openPowerMenu();
    }
}

// Every frame: a wake noticed, a Restart, Power off or Sleep that logind
// refused said, and logind's warning that the machine is going down.
void App::pollPower() {
    // Kept current every frame, so it can tell a wake the moment one
    // happens rather than only when a key asks.
    power::justWoke();
    // A restart, power off or rest that logind refused (#241), said in the
    // pill rather than left looking frozen. Pressing again tries again.
    if (const std::string no = power::refusal(); !no.empty())
        menuNotice.say(no, Tone::Problem);

    // THE MACHINE IS ABOUT TO GO DOWN OR SLEEP, and logind is holding it
    // for us. Leave the game the one way games are left — finishExit, every
    // save class for every emulator — then let it go once the uploads are
    // through, or at 25 s, inside the image's 30 s allowance.
    switch (power::poll()) {
        case power::Event::GoingDown:
        case power::Event::GoingToSleep:
            std::fprintf(stderr, "[shutdown] the machine is going down%s\n",
                         playing ? "; leaving the game first" : "");
            if (playing && cab::Core::shared().running()) finishExit();
            // An emulator of its own is asked to close as its own window
            // would, which is when it writes what it has not.
            standaloneRun.stop();
            releasePending = true;
            releaseWaitStart = SDL_GetTicksNS();
            break;
        case power::Event::Woke:
        case power::Event::None:
            break;
    }
}

// Going down, or to Sleep, once the uploads are through or the wait is up.
void App::waitForUploadsToPowerDown() {
    if (releasePending) {
        const double waited = (SDL_GetTicksNS() - releaseWaitStart) / 1e9;
        if (uploader.pending() == 0 || waited > 25.0) {
            if (uploader.pending() > 0)
                std::fprintf(stderr, "[shutdown] going down with %d upload(s) still owed\n",
                             uploader.pending());
            releasePending = false;
            power::releaseDelay();
        }
    }
    if (restPending) {
        const double waited = (SDL_GetTicksNS() - restWaitStart) / 1e9;
        if (uploader.pending() == 0 || waited > 20.0) {
            if (uploader.pending() > 0)
                std::fprintf(stderr, "[power] resting with %d upload(s) still owed\n",
                             uploader.pending());
            restPending = false;
            power::act(power::Action::Rest);
        }
    }
}
