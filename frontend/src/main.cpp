// CabinetOS frontend — the program, and the screens it moves between.
//
// This file owns everything a screen is not allowed to: the RomM client, the
// core, the download worker, the upload worker, the cache, and the navigation
// between them. The screens themselves are in screens.cpp and return an Action
// rather than doing anything; the decisions about what an Action costs are all
// here, which is why a screen can never start a download by accident.
//
//   Home       resume-first: the Recent and Favorites shelves. The first Recent
//              card is what you were playing, focused on arrival, and the one
//              cover that goes straight into the game ("Resume" in the header);
//              every other cover opens the launch screen. No hero since
//              2026-09-21.
//   Library    every system and every collection on the server, as a tile grid.
//   Grid       one system's or one collection's games.
//   Launch     a full-screen cover over whatever was behind it: play, or
//              download and keep.
//   The player a full-screen cover over THAT, so quitting a game returns to the
//              launch screen and backing out again returns to the browsing.
//
// See docs/PROJECT.md, "The design system" and "Navigation model".

#include <SDL3/SDL.h>

#include <dirent.h>
#include <fcntl.h>
#include <sys/resource.h>
#include <strings.h>
#include <sys/stat.h>

#include <cctype>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <ctime>

#include <atomic>
#include <chrono>
#include <csignal>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include <unistd.h>

#include <json-c/json.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <optional>
#include <unordered_map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "core.h"
#include "design.h"
#include "idle.h"
#include "image.h"
#include "keyboard.h"
#include "cache.h"
#include "catalog.h"
#include "wii.h"
#include "wiiremote.h"
#include "covercache.h"
#include "dirsave.h"
#include "disc.h"
#include "filesave.h"
#include "firstrun.h"
#include "gpu.h"
#include "vkhost.h"
#include "net.h"
#include "qr.h"
#include "romfile.h"
#include "romm.h"
#include "savemirror.h"
#include "screens.h"
#include "settings.h"
#include "sound.h"
#include "proc.h"
#include "standalone.h"
#include "vpad.h"
#include "xboxhdd.h"
#include "setup.h"
#include "accounts.h"
#include "samenames.h"
#include "achievements.h"
#include "achlist.h"
#include "manual.h"
#include "manualview.h"
#include "storage.h"
#include "text.h"
#include "overlaywin.h"
#include "pin.h"
#include "choice.h"
#include "downloads.h"
#include "power.h"
#include "look.h"
#include "prefs.h"
#include "remoteplay.h"
#include "report.h"
#include "wikiqr.h"
#include "tailscale.h"
#include "optcheck.h"
#include "quality.h"
#include "screenfx.h"
#include "sysopts.h"
#include "server.h"
#include "update.h"
#include "files.h"
#include "unit.h"
#include "drives.h"
#include "players.h"
#include "battery.h"
#include "volume.h"
#include "powerprofile.h"
#include "steam.h"
#include "playtime.h"
#include "shortcuts.h"
#include "rewind.h"
#include "ps2.h"
#include "rumble.h"
#include "bluetooth.h"
#include "ui.h"

// Moved out of this file in the restructure (#398).
#include "accountremove.h"
#include "app.h"
#include "capture.h"
#include "coretests.h"
#include "credits.h"
#include "discs.h"
#include "dolphinprep.h"
#include "favorites.h"
#include "gamesession.h"
#include "launch.h"
#include "library.h"
#include "notice.h"
#include "noticegallery.h"
#include "offline_sync.h"
#include "probes.h"
#include "samplelibrary.h"
#include "saves.h"
#include "screenparts.h"
#include "states.h"
#include "timewords.h"
#include "uploader.h"
#include "wiimii.h"

// The SIGUSR1 capture (capture.h).
volatile std::sig_atomic_t gCaptureRequested = 0;
void requestCapture(int) { gCaptureRequested = 1; }

namespace {

// The design system's numbers, the animation primitive and the card live in
// design.h now, because the screens are separate translation units and two
// copies of a focus scale is how a shelf and a grid end up disagreeing about
// what focus looks like.
using namespace design;   // NOLINT — every name in it is a design-system value

}  // namespace

// --ps2-copy: PCSX2 without a window of its own, every frame copied back for
// this window to draw, as before #226. For measuring the two side by side.
bool gPs2Copy = false;

int main(int argc, char** argv) {
    // The version-bump check's frontend half (optcheck.h): before anything is
    // opened, so CI runs it with no display, no GPU and no core.
    if (argc == 3 && std::string(argv[1]) == "--check-option-tables")
        return optcheck::run(argv[2]);
    App app;
    return app.run(argc, argv);
}

// main()'s body: the start-up, the frame loop, the teardown and the restart,
// the start-up's and the teardown's steps called from startup.cpp in the
// old order. Its locals and lambdas are the App's members (app.h).
int App::run(int argc, char** argv) {
    if (const std::optional<int> rc = parseCommandLine(argc, argv)) return *rc;

    resolveCoresAndServer();

    // The probes that need no storage tree, no window, GL or controller,
    // before anything is created on disk (probes.cpp).
    if (const std::optional<int> rc = earlyProbe({.sameNamesMode = sameNamesMode,
                                                  .coreDir = coreDir,
                                                  .rommAddress = rommAddress,
                                                  .accountsTestMode = accountsTestMode,
                                                  .playersTestMode = playersTestMode,
                                                  .batteryTestMode = batteryTestMode,
                                                  .volumeTestMode = volumeTestMode,
                                                  .playtimeTestMode = playtimeTestMode,
                                                  .discTestMode = discTestMode,
                                                  .removeAccountId = removeAccountId,
                                                  .removeAnyway = removeAnyway,
                                                  .padsSeconds = padsSeconds,
                                                  .firstRunRulesMode = firstRunRulesMode,
                                                  .firstRunWriteMode = firstRunWriteMode,
                                                  .qrText = qrText,
                                                  .qrPbm = qrPbm,
                                                  .networkProbeMode = networkProbeMode,
                                                  .networkScan = networkScan,
                                                  .gpuProbeMode = gpuProbeMode}))
        return *rc;

    if (const std::optional<int> rc = startStorage()) return *rc;

    // The probes that need the storage tree and the core directory, before
    // SDL (probes.cpp).
    if (const std::optional<int> rc =
            storageProbe({.firstRunProbeMode = firstRunProbeMode,
                          .firstRunStep = firstRunStep,
                          .firstRunServerCheck = firstRunServerCheck,
                          .rommAddress = rommAddress,
                          .romProbeId = romProbeId,
                          .romProbeExts = romProbeExts,
                          .rommProbeMode = rommProbeMode,
                          .rommPair = rommPair,
                          .coreOptionsAudit = coreOptionsAudit,
                          .coreOptionsDetail = coreOptionsDetail,
                          .corePath = corePath,
                          .romPath = romPath,
                          .autoLaunchId = autoLaunchId,
                          .coreDir = coreDir,
                          .accountsProbeMode = accountsProbeMode,
                          .serverCheckAddress = serverCheckAddress,
                          .keepersRomId = keepersRomId,
                          .storageReport = storageReport}))
        return *rc;

    if (const std::optional<int> rc = startDisplay()) return *rc;

    if (const std::optional<int> rc = runFirstRun()) return *rc;

    if (const std::optional<int> rc = startStartupScreen()) return *rc;

    if (const std::optional<int> rc = connectToServer(argv)) return *rc;
    openKeyboardDemo();
    if (const std::optional<int> rc = startDeveloperLaunch()) return *rc;
    settleScreensAtStart();
    tsSignInScreen.setText("Sign in to Tailscale",
                           {"Use the Tailscale account", "your phone is signed in to."},
                           "Getting a link\xE2\x80\xA6");
    startBrowsing();
    previous = SDL_GetTicksNS();
    startSync();
    recoverPowerCutSaves();
    overlayFade.smooth = true;    // 350 ms ease-in-out, per the design system

    applyStartColour();
    startCurtain();

    readScreenOffAtStart();
    readUpdateCheckAtStart();
    startAccessAtBoot();
    startRemotePlayAtBoot();
    startTailscaleAtBoot();
    // A Remote Play session already open when the console starts (back from
    // Steam, a crash) needs gamescope composing: the session script starts it
    // so (cabinetos-session, #304), before the console draws a frame.

    noticeAppliedUpdate();
    settleTabsAtStart();

    // The first answer, before anything is drawn. Everything after this is a
    // refresh triggered by the thing that changed it.
    refreshKeeps();

    openScreenForCapture();

    // BACK FROM STEAM (#223): Home, where the console starts anyway; the
    // file is only taken so it is not read again.
    if (!initialScreen) steam::takeReturned();
    parseDiscScript();

    borrowPowerButton();

    walkNavScript();


    // The function statics in this loop stay function statics (#398, batch c):
    // run() runs once, so each is one object for the life of the process, as
    // in main(). The [pace] counters among them are never reset at a launch,
    // so a new game's first [pace] line carries the last game's tail; that is
    // recorded in the audit (Part 2, 3.14 item 12), not changed here.
    while (running) {
        // Full speed while any game runs, balanced on Home (powerprofile.h).
        powerprofile::update(playing || standaloneRun.active());
        // RetroAchievements' network answers, on this thread (achievements.h).
        ra::pump();
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (takeStandaloneEvent(e)) continue;
            const InputOwner owner = inputOwner();
            // ANY TOUCH WAKES THE SCREEN, AND ON A DARK SCREEN THAT IS ALL IT
            // DOES. The press that lights a dimmed or blank Home is swallowed,
            // so it cannot also launch whatever had focus, which nobody could
            // see. A running game is the exception: its pad is read by state
            // rather than by these events, so nothing is lost from play.
            bool touched = false;
            switch (e.type) {
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    touched = true;
                    break;
                case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                    touched = std::abs(static_cast<int>(e.gaxis.value)) > idle::kAxisDeadzone;
                    break;
                default:
                    break;
            }
            // The power and Sleep keys are never swallowed: on a dark screen
            // they light it AND open the Power menu, which is what the person
            // reaching for that button wants.
            const bool powerKey = e.type == SDL_EVENT_KEY_DOWN &&
                                  (e.key.key == SDLK_POWER || e.key.key == SDLK_SLEEP);
            if (touched) playClock.touch();
            if (touched && idleWatch.input(clockSeconds()) && owner != InputOwner::Game &&
                !powerKey)
                continue;
            // THE PRESS THAT ENDS THE WAIT after a state loads: A, as the
            // prompt says (Return on a keyboard). It was any button, and
            // MMagTech: "it says press A to continue and any button works".
            // Every other press does nothing while it waits. None goes further;
            // the frame read keeps them from the game until let go, as it does
            // the pause menu's. L3 and R3 still reach the pause menu below.
            if (stateHold && playing && !overlayOpen &&
                (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                 (e.type == SDL_EVENT_KEY_DOWN && !e.key.repeat && !powerKey))) {
                const bool stick = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                                   (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK ||
                                    e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK);
                const bool a = (e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN &&
                                e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) ||
                               (e.type == SDL_EVENT_KEY_DOWN && e.key.key == SDLK_RETURN);
                if (a) {
                    stateHold = false;
                    stateHoldJustEnded = true;
                    std::fprintf(stderr, "[state] continuing\n");
                }
                if (!stick) continue;
            }
            if (powerKey) {
                // The press that woke the machine from Rest arrives here too,
                // and must not wake it straight into this menu.
                if (power::justWoke())
                    std::fprintf(stderr, "[power] ignoring the press that woke the machine\n");
                else
                    openPowerMenu();
                continue;
            }
            if (interceptControllers(e)) continue;
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    askedToStop = true;
                    std::fprintf(stderr, "[shutdown] asked to stop%s\n",
                                 playing ? " with a game running" : "");
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    players::added(e.gdevice.which);
                    break;
                case SDL_EVENT_GAMEPAD_REMOVED: {
                    // A PLAYING PAD DROPPING OUT PAUSES THE GAME, as on a
                    // Switch or an Apple TV (MMagTech, 2026-09-26): its
                    // player would otherwise be run over while they find the
                    // charger. Any pad can resume.
                    const int was = players::playerOf(e.gdevice.which);
                    players::removed(e.gdevice.which);
                    if (playing && !overlayOpen && was >= 0) {
                        std::fprintf(stderr, "[players] player %d's pad went off: pausing\n",
                                     was + 1);
                        toggleOverlay();
                    }
                    break;
                }
                case SDL_EVENT_TEXT_INPUT:
                    // A physical keyboard types into the same field. Not a
                    // separate path — the same string and the same commit.
                    if (keyboard.isOpen()) keyboard.typeText(e.text.text);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    // A physical keyboard's digits go into the PIN pad, the
                    // same field the controller fills.
                    if (pinScreen.isOpen() && owner == InputOwner::UI) {
                        const SDL_Keycode k = e.key.key;
                        char d = 0;
                        if (k >= SDLK_0 && k <= SDLK_9) d = static_cast<char>('0' + (k - SDLK_0));
                        else if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
                            d = static_cast<char>('1' + (k - SDLK_KP_1));
                        else if (k == SDLK_KP_0) d = '0';
                        if (d) { pinOutcome(pinScreen.typeDigit(d)); break; }
                        if (k == SDLK_BACKSPACE) { pinScreen.deleteDigit(); break; }
                        if (k == SDLK_ESCAPE) { navigate(screens::Nav::Back); break; }
                    }
                    if (owner == InputOwner::Keyboard) {
                        // While it owns input it takes every key, the same way
                        // the core owns the pad while a game runs. A control
                        // that means two things at once is the bug.
                        switch (e.key.key) {
                            case SDLK_LEFT: keyboard.moveFocus(-1, 0); break;
                            case SDLK_RIGHT: keyboard.moveFocus(+1, 0); break;
                            case SDLK_UP: keyboardUp(); break;
                            case SDLK_DOWN: keyboard.moveFocus(0, +1); break;
                            case SDLK_BACKSPACE: keyboard.backspace(); break;
                            case SDLK_RETURN:
                                std::fprintf(stderr, "[keyboard] committed: %s\n",
                                             keyboard.value().c_str());
                                keyboardResult(keyboard.commit());
                                break;
                            case SDLK_ESCAPE: keyboardResult(keyboard.cancel()); break;
                            default: break;
                        }
                        break;
                    }
                    if (e.key.key == SDLK_ESCAPE) {
                        // In a game, Escape is the overlay — not a quit. A
                        // download is the one thing it should interrupt.
                        if (launchJob.busy()) launchJob.cancel = true;
                        // A question over the pause menu (Change disc) goes
                        // back first, as B does.
                        else if (playing && overlayOpen && choiceScreen.isOpen())
                            navigate(screens::Nav::Back);
                        else if (playing) toggleOverlay();
                        // Back, while there is anywhere to go back to. Quitting
                        // from the middle of the Library would throw away the
                        // whole stack the person had walked down.
                        else if (overlayOpen) closeOverlay();
                        // A question or Downloads open over any screen, Home
                        // included, takes Escape as B does: Back. On Home it
                        // opened the Power menu over the panel instead, so a
                        // keyboard could not cancel Steam's install (#223).
                        else if (choiceScreen.isOpen() || downloadsPanel.isOpen())
                            navigate(screens::Nav::Back);
                        else if (stack.size() > 1) navigate(screens::Nav::Back);
                        // At the root, Escape is Start: the Power menu.
                        else openPowerMenu();
                    }
                    // The shoulders, for a machine with no pad attached. `[`
                    // and `]` sit where L1 and R1 do on a controller and the
                    // capture tooling can drive them.
                    if (owner == InputOwner::UI) {
                        if (e.key.key == SDLK_LEFTBRACKET && !shoulderOnPage(-1))
                            switchDestination(-1);
                        if (e.key.key == SDLK_RIGHTBRACKET && !shoulderOnPage(+1))
                            switchDestination(+1);
                        // The triggers' equivalent, where a pad is not attached.
                        if (here() == Screen::Grid) {
                            if (e.key.key == SDLK_COMMA) gridScreen.jumpLetter(-1);
                            if (e.key.key == SDLK_PERIOD) gridScreen.jumpLetter(+1);
                        }
                        if (here() == Screen::Library) {
                            if (e.key.key == SDLK_COMMA) libraryScreen.jumpGroup(-1);
                            if (e.key.key == SDLK_PERIOD) libraryScreen.jumpGroup(+1);
                        }
                    }
                    if (owner == InputOwner::Overlay && choiceScreen.isOpen()) {
                        switch (e.key.key) {
                            case SDLK_UP: navigate(screens::Nav::Up); break;
                            case SDLK_DOWN: navigate(screens::Nav::Down); break;
                            case SDLK_LEFT: navigate(screens::Nav::Left); break;
                            case SDLK_RIGHT: navigate(screens::Nav::Right); break;
                            case SDLK_RETURN:
                            case SDLK_SPACE: navigate(screens::Nav::Activate); break;
                            default: break;
                        }
                        break;
                    }
                    if (owner == InputOwner::Overlay) {
                        if (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN) {
                            ovStep(e.key.key == SDLK_DOWN ? 1 : -1);
                            overlayFocus.retarget(0.0f, 0.0f);
                            overlayFocus.elapsed = 0.0f;
                            overlayFocus.retarget(1.0f, kOverlayFocusDuration);
                        }
                        if ((e.key.key == SDLK_LEFT || e.key.key == SDLK_RIGHT) && !powerMenu &&
                            choiceAt(overlaySlot))
                            stepChoice(overlaySlot, e.key.key == SDLK_RIGHT ? 1 : -1, false);
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)
                            overlayActivate();
                        break;
                    }
                    // F5 writes a state, F8 restores the newest one THIS build can
                    // load, F6 pushes the game's own save. Quitting syncs the
                    // save by itself; F6 is for testing without quitting.
                    if (playing && session.snapshots && e.key.key == SDLK_F5) saveStateNow(session, uploader, menuNotice);
                    if (playing && session.snapshots && e.key.key == SDLK_F8) beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
                    if (playing && e.key.key == SDLK_F6) syncSave(session, uploader);
                    if (owner != InputOwner::UI) break;
                    // Anything pushed on top of Home owns its own focus model,
                    // so the press goes there and this function does not get an
                    // opinion about it. Home is handled below because it is the
                    // one screen whose focus lives here.
                    if (here() != Screen::Home) {
                        switch (e.key.key) {
                            case SDLK_LEFT:  navigate(screens::Nav::Left); break;
                            case SDLK_RIGHT: navigate(screens::Nav::Right); break;
                            case SDLK_UP:    navigate(screens::Nav::Up); break;
                            case SDLK_DOWN:  navigate(screens::Nav::Down); break;
                            case SDLK_RETURN:
                            case SDLK_SPACE: navigate(screens::Nav::Activate); break;
                            case SDLK_BACKSPACE: navigate(screens::Nav::Back); break;
                            default: break;
                        }
                        break;
                    }
                    // THROUGH `navigate`, NOT STRAIGHT AT HOME. Every key on
                    // every screen arrives at one place now, so the bar can be
                    // offered it first wherever it holds focus.
                    // `e.key.repeat` is the system's own key repeat, at the
                    // system's own rate. Ignore it and run the same accelerating
                    // repeat the pad gets, so the two input devices behave
                    // identically — which is the rule the whole input model is
                    // built on.
                    if (!e.key.repeat) {
                        if (e.key.key == SDLK_LEFT) {
                            navigate(screens::Nav::Left); holdNav(screens::Nav::Left);
                        }
                        if (e.key.key == SDLK_RIGHT) {
                            navigate(screens::Nav::Right); holdNav(screens::Nav::Right);
                        }
                        if (e.key.key == SDLK_UP) {
                            navigate(screens::Nav::Up); holdNav(screens::Nav::Up);
                        }
                        if (e.key.key == SDLK_DOWN) {
                            navigate(screens::Nav::Down); holdNav(screens::Nav::Down);
                        }
                    }
                    // Until the navigation bar exists, this is the door to the
                    // Library. See docs/NEXT-SESSION.md: the bar the design
                    // system specifies costs exactly the vertical slack Home has
                    // left, which is a measurement only a television can settle.
                    if (e.key.key == SDLK_L) {
                        libraryScreen.enter();
                        stack.push_back(Screen::Library);
                    }
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
                        pressing = true;
                        if (!playing) navigate(screens::Nav::Activate);
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    if (e.key.key == SDLK_LEFT) releaseNav(screens::Nav::Left);
                    if (e.key.key == SDLK_RIGHT) releaseNav(screens::Nav::Right);
                    if (e.key.key == SDLK_UP) releaseNav(screens::Nav::Up);
                    if (e.key.key == SDLK_DOWN) releaseNav(screens::Nav::Down);
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) pressing = false;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    // THE SHOULDERS LEAVE SEARCH, keyboard or not. MMagTech,
                    // 2026-09-24: R1 landed on Search and then neither shoulder
                    // got you off it, because the docked keyboard took every
                    // button. Only Search's docked keyboard: a keyboard asking
                    // a question (a Wi-Fi password) is modal and keeps them.
                    // Leaving clears the search, as entering any destination
                    // starts it fresh.
                    if (owner == InputOwner::Keyboard && here() == Screen::Search &&
                        (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ||
                         e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                        switchDestination(
                            e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ? -1 : +1);
                        break;
                    }
                    // X TAKES A DIGIT OFF THE PIN, as it takes a letter off
                    // the on-screen keyboard. B leaves the pad.
                    if (pinScreen.isOpen() && owner == InputOwner::UI &&
                        e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                        pinScreen.deleteDigit();
                        break;
                    }
                    if (owner == InputOwner::Keyboard) {
                        switch (e.gbutton.button) {
                            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: keyboard.moveFocus(-1, 0); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: keyboard.moveFocus(+1, 0); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_UP: keyboardUp(); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: keyboard.moveFocus(0, +1); break;
                            case SDL_GAMEPAD_BUTTON_SOUTH: keyboardResult(keyboard.pressKey()); break;
                            case SDL_GAMEPAD_BUTTON_WEST: keyboard.backspace(); break;
                            case SDL_GAMEPAD_BUTTON_NORTH: keyboard.toggleShift(); break;
                            case SDL_GAMEPAD_BUTTON_START:
                                std::fprintf(stderr, "[keyboard] committed: %s\n",
                                             keyboard.value().c_str());
                                keyboardResult(keyboard.commit());
                                break;
                            case SDL_GAMEPAD_BUTTON_EAST:
                                keyboardResult(keyboard.cancel()); break;
                            default: break;
                        }
                        break;
                    }
                    // BOTH STICK CLICKS, NOT START. Start is the pause button
                    // on nearly every system this console emulates, and taking
                    // it meant a game could never be paused — MMagTech, on the
                    // A9, 2026-09-19, before playing the first real game on it.
                    //
                    // L3+R3 is Cabinet's default and its reasoning carries:
                    // RetroArch's alternative to Select+Start, chosen because
                    // analog trigger pairs collide with real gameplay —
                    // braking and accelerating together in a racing game is
                    // the obvious case — "while clicking both sticks at once
                    // has no gameplay meaning in anything this app runs".
                    //
                    // Still hardcoded here. Cabinet makes it remappable and
                    // GLOBAL rather than per-controller, on the grounds that
                    // the risk comes from the game and not the pad model, and
                    // lets the second button be cleared for single-button
                    // mode. That belongs with the remapping screen.
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK)
                        l3Down = true;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                        r3Down = true;
                    if (l3Down && r3Down && (playing || overlayOpen)) {
                        l3Down = r3Down = false;   // one toggle per pair, not per frame
                        toggleOverlay();
                        break;
                    }
                    if (owner == InputOwner::Overlay && choiceScreen.isOpen()) {
                        switch (e.gbutton.button) {
                            case SDL_GAMEPAD_BUTTON_DPAD_UP: navigate(screens::Nav::Up); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: navigate(screens::Nav::Down); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: navigate(screens::Nav::Left); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: navigate(screens::Nav::Right); break;
                            case SDL_GAMEPAD_BUTTON_SOUTH: navigate(screens::Nav::Activate); break;
                            case SDL_GAMEPAD_BUTTON_EAST: navigate(screens::Nav::Back); break;
                            default: break;
                        }
                        break;
                    }
                    if (owner == InputOwner::Overlay) {
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) ovStep(-1);
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) ovStep(+1);
                        if ((e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT ||
                             e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) &&
                            !powerMenu && choiceAt(overlaySlot))
                            stepChoice(overlaySlot,
                                       e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? 1 : -1,
                                       false);
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) overlayActivate();
                        // East is Back, and Back from the overlay is Resume —
                        // or, from the Power menu on Home, just closing it.
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) closeOverlay();
                        // Start closes the Power menu it opened on Home.
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START && powerMenu &&
                            !playing)
                            closeOverlay();
                        break;
                    }
                    if (owner != InputOwner::UI) break;
                    // The shoulders first, because they are the one input that
                    // means the same thing on every screen that has them.
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
                        if (!shoulderOnPage(-1)) switchDestination(-1);
                        break;
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                        if (!shoulderOnPage(+1)) switchDestination(+1);
                        break;
                    }
                    // ONE DIRECTION HANDLER FOR EVERY SCREEN — 2026-09-21.
                    //
                    // There were three: Left and Right were handled early and
                    // split by screen, Up and Down were handled once for the
                    // pushed screens and again for Home, and only Home's copy
                    // ever started the hold. So `holdNav` existed and MMagTech
                    // was right that *"holding down isnt working"* — it worked
                    // on Home and nowhere else, which is the worst kind of
                    // working. Everything below is one switch, and there is now
                    // no way to add a direction to one screen and forget it on
                    // another.
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                            navigate(screens::Nav::Left);
                            holdNav(screens::Nav::Left);
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                            navigate(screens::Nav::Right);
                            holdNav(screens::Nav::Right);
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_UP:
                            navigate(screens::Nav::Up);
                            holdNav(screens::Nav::Up);
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                            navigate(screens::Nav::Down);
                            holdNav(screens::Nav::Down);
                            break;
                        case SDL_GAMEPAD_BUTTON_SOUTH:
                            navigate(screens::Nav::Activate);
                            pressing = true;
                            break;
                        // East is Back everywhere in this product, which is the
                        // rule the in-game overlay already follows. On Home it
                        // is the root and `homeKey` answers with the edge cue.
                        case SDL_GAMEPAD_BUTTON_EAST:
                            navigate(screens::Nav::Back);
                            break;
                        case SDL_GAMEPAD_BUTTON_START:
                            // START ON HOME IS THE POWER MENU — decided
                            // 2026-09-22. It used to quit the frontend, a
                            // development leftover that just got the session
                            // restarted under whoever was watching.
                            // AND ON EVERY SCREEN OUTSIDE A GAME since #223
                            // (MMagTech, 2026-10-04): it only did on Home,
                            // which nothing explained, and the menu is now
                            // Steam's door too. Not over the PIN pad, a
                            // question or the Downloads panel. NOT ON SEARCH:
                            // its keyboard is always up and is drawn over
                            // everything, so the menu opened under it (on the
                            // TV, 2026-10-04), and there Start is the
                            // keyboard's "done", as its legend says. The rule
                            // is: Start opens the menu except while typing.
                            if (here() != Screen::Search && !pinScreen.isOpen() &&
                                !choiceScreen.isOpen() && !downloadsPanel.isOpen())
                                openPowerMenu();
                            break;
                        default: break;
                    }
                    break;
                // THE TRIGGERS ARE FAST NAVIGATION BY LETTER, and they are an
                // AXIS rather than a button: a trigger reports how far it is
                // pulled, from 0 to 32767, so it has to be edge-detected here
                // or one pull would fire a hundred jumps on its way down.
                //
                // The threshold is deliberately high and the release threshold
                // deliberately lower. A trigger at rest on a worn pad does not
                // read zero, and a single threshold turns that into a jump
                // every time the pad is picked up.
                case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                    const bool left = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                    const bool right = e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                    if (!left && !right) break;
                    bool& held = left ? l2Held : r2Held;
                    const int dir = left ? -1 : +1;
                    const int v = e.gaxis.value;
                    // A RELEASE COUNTS WHOEVER OWNS INPUT. A trigger let go
                    // during a game used to stay "held", and the next pull
                    // back on the grid did nothing.
                    if (held && v < 8000) {
                        held = false;
                        if (letterHeld == dir) letterHeld = 0;
                    } else if (!held && v > 20000 && owner == InputOwner::UI) {
                        held = true;
                        if (here() == Screen::Grid) {
                            gridScreen.jumpLetter(dir);
                            letterHeld = dir;
                            letterHeldFor = 0.0f;
                            letterNext = kRepeatDelay;
                        }
                        // Person to person in Collections (#352); one jump a
                        // pull, since a household has a handful of people.
                        if (here() == Screen::Library) libraryScreen.jumpGroup(dir);
                    }
                    break;
                }
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                            releaseNav(screens::Nav::Left); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                            releaseNav(screens::Nav::Right); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_UP:
                            releaseNav(screens::Nav::Up); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                            releaseNav(screens::Nav::Down); break;
                        default: break;
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK)
                        l3Down = false;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                        r3Down = false;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) pressing = false;
                    break;
                default:
                    break;
            }
        }

        uint64_t now = SDL_GetTicksNS();
        float dt = static_cast<float>(now - previous) / 1e9f;
        previous = now;
        // A stall must not teleport an animation; the reference implementation
        // caps its own accumulator for the same reason.
        dt = std::min(dt, 0.1f);

        pollPower();
        followDrives();
        waitForUploadsToPowerDown();

        // THE SCREENSHOT gamescope is writing, whenever there is one.
        pumpStandaloneShot();
        if (watchStandalone(dt)) continue;

        // Idle, once a frame. The timers only ever deepen here; input() is
        // what lifts them, above.
        idleFrame(dt, playing && !overlayOpen && !stateHold, kScreenOff[screenOffIndex].seconds);
        // TIME PLAYED (#128): the same test the dim uses for "a game is on
        // screen". Written down once a minute, so a power cut loses at most
        // one.
        playClock.tick(dt, playing && !overlayOpen && !stateHold);
        if (playClock.active() && (playCheckpointClock += dt) >= 60.0f) {
            playCheckpointClock = 0.0f;
            playtime::checkpoint(storage::currentUser(), playClock.now(playtime::wallMs()));
        }
        // Held seats for pads that went off last only as long as the game
        // (players.h). The pause menu is still the game.
        players::setInGame(playing);
        if (playing) cab::Core::shared().setPlayers(players::count());
        if (here() == Screen::Settings && players::generation() != settingsPadsSeen)
            buildSettings();

        // The held direction, repeating and speeding up. Driven from the frame
        // loop rather than from the event queue, because the pad sends nothing
        // at all while a button is held down.
        if (navHeld && inputOwner() == InputOwner::UI && !keyboard.isOpen()) {
            heldFor += dt;
            if (heldFor >= nextRepeat) {
                navigate(heldNav);
                const float t = std::clamp(heldFor / kRepeatRamp, 0.0f, 1.0f);
                nextRepeat = heldFor + (kRepeatStart + (kRepeatFast - kRepeatStart) * t);
            }
        } else {
            navHeld = false;
        }
        // The held trigger, a letter at a time at a steady pace: each jump
        // can cross dozens of games, so it does not speed up the way a held
        // direction does.
        if (letterHeld != 0 && inputOwner() == InputOwner::UI && here() == Screen::Grid) {
            letterHeldFor += dt;
            if (letterHeldFor >= letterNext) {
                if (gridScreen.jumpLetter(letterHeld)) letterNext = letterHeldFor + kLetterRepeat;
                else letterHeld = 0;
            }
        } else {
            letterHeld = 0;
        }

        evictTestHook();
        followRemotesInGame();
        if (playing) {
            cab::Core& core = cab::Core::shared();
            const bool rewinding = readGameInput(core);
            runGameFrame(core, dt, rewinding);
        }

        downloadHook();
        takeAccountResults();
        launchHooks(dt);

        pumpLaunch();
        pumpSwitch();
        pumpLeave();
        pollSteamJob();
        pumpArrive();
        pumpExit();
        pumpStateLoad(stateLoad, session, menuNotice);
        pumpDetailStates();
        loadPendingStates();
        overlayHooks();

        pumpDiscChange();

        runDiscScript();

        stateHooks();
        images.pump(dt);
        look::tick(dt);
        // Scheduled turns dark at its hour, asked about once a minute.
        {
            static float sinceDarkCheck = 0.0f;
            sinceDarkCheck += dt;
            if (sinceDarkCheck >= 60.0f) {
                sinceDarkCheck = 0.0f;
                look::setDark(wantDark());
            }
        }
        for (auto& c : cards) {
            c.focus.tick(dt);
            c.press.tick(dt);
        }
        for (int r = 0; r < 2; ++r) {
            for (auto& a : slotFocus[r]) a.tick(dt);
            for (auto& a : slotPress[r]) a.tick(dt);
        }
        // These two are not Cards and so are not in the loop above. Forgetting
        // them cost a debugging pass: the scroll target was computed correctly
        // every frame and then discarded, because an Animated whose elapsed
        // never advances returns its start value forever.
        scrollY.tick(dt);
        backdropMix.tick(dt);
        curtain.tick(dt);
        switchText.tick(dt);
        menuNotice.tick(dt);
        tickRaToast(dt);
        // ---- System update: root's answers, the weekly check, the panel ----
        updPoll -= dt;
        tickUpdateTimer(dt);
        if (updPoll <= 0.0f) {
            updPoll = 0.5f;
            pollAccess();
            remotePlayHandover();
            pollReport();
            pollTailscale();
            pollRemotePlay();
            pollUpdate();
        }
        askReadyWhenFree();
        noticeLowBattery(dt);
        sayUpdatedTo();
        noticeGalleryHook(dt);
        retryOwedUploads(dt);
        followServer(dt);
        sayStateUploaded();
        tickLaunchBusy(dt);
        tickPauseMenu(dt);
        {
            screens::Ctx ctx{renderer, text, images, renderer.scale(), &cards};
            tickScreens(dt, ctx);
            takeNetworkAnswer();
            refreshWifiList();
            followWiiRemotes();
            followPads();
            pumpWiiPairing();
            pumpPadWindow();
            takeWifiJoin();
            takeServerCheck();
            tickSearch(dt, ctx);

            showDetailProgress();

            pumpSearch(dt);
            drainGridPages();

            drainCovers();

            sizeSearchResults();
            tickDetail(dt);
        }
        retargetHomePress();

        if (frameOverPs2Window()) continue;

        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        bool offscreen = false;
        if (renderW > 0 && renderH > 0) {
            offscreen = renderer.beginOffscreen(renderW, renderH);
            if (offscreen) {
                dw = renderW;
                dh = renderH;
            }
        }
        renderer.beginFrame(dw, dh);
        // Declared here rather than inside the Home branch: the hero's glass
        // cannot be drawn until after presentScene, which is outside it.
        // The top bar belongs to HOME. It used to be drawn inside the hero's
        // block, which meant a console with nothing playable in its recent
        // history had no way to reach Library at all.
        const float sc = renderer.scale();
        // A game is on the screen: ours, or one this window is paused over
        // (standalone.h), where nothing of Home may be drawn under the menu.
        const bool gameUp = playing || standalonePaused;
        drawGround(gameUp, dt);

        applyTabContentFade(gameUp);

        if (gameUp) {
            cab::Core& core = cab::Core::shared();
            drawGamePicture(core, dh);

            // The overlay is not composited by anything clever: the frontend
            // owns the frame loop, so the pause menu is simply drawn over the
            // game. That is the payoff of hosting cores in process rather than
            // launching them.
            if (pressing) {
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(0.55f)});
                const float panelW = 560, panelH = 260;
                renderer.draw(ui::Rect{(ui::kCanvasWidth - panelW) * 0.5f,
                                       (ui::kCanvasHeight - panelH) * 0.5f, panelW, panelH,
                                       32, ui::Color::white(0.14f)});
                const float sc2 = renderer.scale();
                const char* title = core.coreName().c_str();
                const float tw = text.measure(title, ui::TextStyle::Title2, sc2);
                text.draw(renderer, title, (ui::kCanvasWidth - tw) * 0.5f,
                          ui::kCanvasHeight * 0.5f - 40,
                          ui::TextStyle::Title2, ui::Color::white(1.0f), sc2);
                const char* sub = "Paused";
                const float sw = text.measure(sub, ui::TextStyle::Callout, sc2);
                text.draw(renderer, sub, (ui::kCanvasWidth - sw) * 0.5f,
                          ui::kCanvasHeight * 0.5f + 10, ui::TextStyle::Callout,
                          ui::Color::white(0.60f), sc2);
            }
        } else if (here() != Screen::Home) {
            drawScreen(sc);
        } else {

            drawHome(dt, sc);
        }  // end of the shelf branch

        drawStateHold(sc);

        drawPauseScrim();

        renderer.presentScene();

        drawScreenGlass(gameUp, sc);

        drawPauseMenu(sc);

        resetContentFade();

        drawBar(gameUp, sc, dt);

        // DOWNLOADS (Settings), over the top bar: it is a panel over the whole
        // screen, and the bar drawn on top of it showed "Settings" through
        // it (MMagTech on the TV, 2026-09-26). Under the pill, which says
        // what a removal did where the panel's foot is, and under the
        // question it asks ("Remove 3 games?").
        if (downloadsPanel.isOpen() && !playing) {
            screens::Ctx dctx{renderer, text, images, sc, &cards};
            downloadsPanel.draw(dctx);
            renderer.setContentAlpha(1.0f);
        }

        drawNotice(sc);

        drawRaToast(sc);

        // ---- The account switcher, over the screen and over the bar -------
        //
        // AFTER THE BAR, because it hangs from the chip the bar draws and has
        // to sit on top of it rather than under. Before the curtain, because a
        // curtain covers everything including this.
        if (accountsOpen) {
            screens::Ctx actx{renderer, text, images, sc, &cards};
            accountScreen.draw(actx);
        }
        // The PIN pad over all of it, the panel included: it can be opened
        // from the panel, and it covers the screen.
        if (choiceScreen.isOpen() && (!playing || overlayOpen)) {
            screens::Ctx cctx{renderer, text, images, sc, &cards};
            choiceScreen.draw(cctx);
            renderer.setContentAlpha(1.0f);
        }
        if (raList.isOpen() && !playing) {
            screens::Ctx lctx{renderer, text, images, sc, &cards};
            raList.draw(lctx);
            renderer.setContentAlpha(1.0f);
        }
        if (pinScreen.isOpen() && !playing) {
            screens::Ctx pctx{renderer, text, images, sc, &cards};
            pinScreen.draw(pctx);
            renderer.setContentAlpha(1.0f);
        }

        drawCurtain(sc);

        drawKeyboardAndDissolve();

        drawDim();

        ++frame;
        captureFrame(dw, dh);

        takePendingSwitch();

        if (offscreen) {
            renderer.endOffscreen();
        } else {
            SDL_GL_SwapWindow(window);
        }

        // A BLANK SCREEN DOES NOT NEED SIXTY PICTURES A SECOND. Ten keeps the
        // background jobs polled and a wake within a tenth of a second, and
        // lets the GPU stop redrawing 4K for nobody.
        if (idleShown == idle::Level::Blank && dimLayer.value() >= 0.999f) SDL_Delay(100);
    }

    wakeTvAtExit();

    leaveGameAtExit();

    closeStandaloneAtExit();
    reportAndUnloadCore();
    if (audioStream) SDL_DestroyAudioStream(audioStream);
    {
        const covercache::Stats cs = covercache::stats();
        if (cs.fromDisk || cs.fetched)
            std::fprintf(stderr, "[covers] %d from disk, %d fetched, %d stored\n",
                         cs.fromDisk, cs.fetched, cs.stored);
    }
    std::fprintf(stderr, "[image] resident %.1f MB, %d still pending\n",
                 images.bytesResident() / (1024.0 * 1024.0), images.pendingCount());
    return shutDown(argv);
}

void App::holdNav(screens::Nav n) {
    navHeld = true;
    heldNav = n;
    heldFor = 0.0f;
    nextRepeat = kRepeatDelay;
}

void App::releaseNav(screens::Nav n) {
    // Only the direction that is actually held releases it. Letting go of
    // Left while holding Down must not stop the Down.
    if (navHeld && heldNav == n) navHeld = false;
}

// What a commit or a cancel MEANS, which is not the same on every screen.
// On Search, done goes up into the results and cancel leaves the screen —
// and cancel closing the keyboard while leaving an empty Search behind it
// would be a dead end with no way out but the bar.
void App::keyboardResult(ui::KeyboardResult res) {
    // Done is a choice and Cancel a step back (#389); Search sounds its
    // own Cancel below, where it goes Home.
    if (res == ui::KeyboardResult::Committed) sound::play(sound::Cue::Activate);
    if (res == ui::KeyboardResult::Cancelled && here() != Screen::Search)
        sound::play(sound::Cue::Back);
    if (here() != Screen::Search) {
        if (res == ui::KeyboardResult::Typing || !keyboardThen) return;
        auto then = std::move(keyboardThen);
        keyboardThen = nullptr;
        then(res);
        return;
    }
    searchKeyboardResult(res);
}
