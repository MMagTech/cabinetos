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

    // Opening straight onto a screen, for a capture. This walks the SAME route
    // a person would: the Library is entered, a tile is opened, the launch
    // screen is opened from a card. A capture that built a screen some other
    // way would be photographing something the product cannot reach.
    // SEARCH IS ITS OWN ROUTE, because walking to it is not walking to the
    // Library. `--screen search --query metal` types the query the way a person
    // would type it and leaves focus in the results, which is the state worth
    // photographing — an empty Search is a picture of a keyboard.
    // THE SWITCHER IS REACHED THE WAY A PERSON REACHES IT: focus the bar, walk
    // to the chip, press it. A capture that called open() directly would be
    // photographing a panel the product might not be able to get to.
    if (focusBarSlot >= 0) {
        barFocused = true;
        barSlot = std::clamp(focusBarSlot, 0, BarCount - 1);
    }
    if (initialScreen && (SDL_strcmp(initialScreen, "accounts") == 0 ||
                          SDL_strcmp(initialScreen, "add-account") == 0)) {
        barFocused = true;
        barSlot = BarAccount;
        barKey(screens::Nav::Activate);
        if (SDL_strcmp(initialScreen, "add-account") == 0) {
            // WALKS DOWN TO THE ADD ROW FIRST, and not doing so switched the
            // console to somebody else. This pressed Activate straight away,
            // which was right while the panel held only the Add row and wrong
            // the moment a second account existed — row 0 became a person, so
            // the capture route signed the console in as them.
            //
            // The lesson is the one this project keeps paying for: a route
            // that walks the way a person walks has to keep walking when the
            // screen gains a row. Down until the focus stops moving, then
            // press, which is what a person does.
            for (int guard = 0; guard < 16; ++guard)
                accountScreen.key(screens::Nav::Down);
            // Presses the Add row the way a person would. It starts a REAL
            // pairing against the real server — a device code that expires in
            // minutes and creates nothing unless somebody approves it.
            apply(accountScreen.key(screens::Nav::Activate));
            // AND THE CAPTURE HAS TO WAIT FOR THE SERVER. This loop is not
            // paced, so on the A9 four hundred frames go by in well under a
            // second and the shot comes out with no code on it — the same trap
            // `--launch-after` fell into and the same one that cost the setup
            // pairing capture. Gate the shot on the fact rather than on a
            // frame count.
            waitForPairCode = true;
        }
    } else if (initialScreen && SDL_strcmp(initialScreen, "howto") == 0) {
        // `--screen howto`: About's How to page over Home, for a capture or
        // the TV loop; Back goes Home.
        openHowTo();
    } else if (initialScreen && SDL_strcmp(initialScreen, "settings") == 0) {
        // `--screen settings --tile N` opens on category N; `--focus-row 1`
        // puts focus in its rows. The network is waited for here, because a
        // capture has no wall clock and would otherwise always say Checking.
        goToDestination(3);
        if (settingsNet.th.joinable()) settingsNet.th.join();
        if (settingsWifi.th.joinable()) settingsWifi.th.join();
        buildSettings();
        settingsScreen.focusCategory(initialTile, initialRow > 0);
    } else if (initialScreen && SDL_strcmp(initialScreen, "search") == 0) {
        goToDestination(2);
        if (searchQuery) {
            keyboard.typeText(searchQuery);
            searchTyped = keyboard.value();
            searchScreen.setQuery(searchTyped);
            // Asked straight away rather than waiting out the debounce: a
            // capture has no typing to wait for, and a screenshot of the
            // Searching… state is not what anybody asked for.
            runSearch(searchTyped);
            if (searchScreen.resultCount() > 0) searchScreen.setFocused(true);
        }
    } else if (initialScreen) {
        libraryScreen.enter();
        stack.push_back(Screen::Library);
        if (initialTab > 0) {
            // Walks the switcher the way a person would, rather than setting a
            // field: the tab change resets the grid position, and a capture
            // that skipped that would be photographing a state the product
            // cannot be in.
            libraryScreen.key(screens::Nav::Right);
            libraryScreen.key(screens::Nav::Activate);
        }
        if (SDL_strcmp(initialScreen, "library") == 0 && initialTile > 0)
            libraryScreen.focusTile(initialTile);
        if (SDL_strcmp(initialScreen, "grid") == 0 ||
            SDL_strcmp(initialScreen, "detail") == 0) {
            apply(screens::Result{screens::Action::OpenTile, initialTile});
        }
        if (SDL_strcmp(initialScreen, "detail") == 0) {
            int card = -1;
            for (size_t i = 0; i < cards.size(); ++i) {
                if (initialGame > 0 ? cards[i].id == initialGame : false) {
                    card = static_cast<int>(i);
                    break;
                }
            }
            // With no --game, the first game of the opened tile, which is what
            // somebody checking the layout wants and needs no id to hand.
            if (card < 0 && here() == Screen::Grid)
                apply(gridScreen.key(screens::Nav::Activate));
            else if (card >= 0)
                openDetailSoon(card);
        }
    }

    // BACK FROM STEAM (#223): Home, where the console starts anyway; the
    // file is only taken so it is not read again.
    if (!initialScreen) steam::takeReturned();
    {
        std::string step;
        for (char ch : discScriptText + ";") {
            if (ch != ';') { step += ch; continue; }
            while (!step.empty() && step.front() == ' ') step.erase(step.begin());
            while (!step.empty() && step.back() == ' ') step.pop_back();
            if (!step.empty()) discScript.steps.push_back(step);
            step.clear();
        }
    }

    borrowPowerButton();

    // Here rather than with --screen, because Home's keys are only wired up
    // just above; walking before that pressed nothing.
    if (navScript) {
        std::string word;
        for (const char* p = navScript;; ++p) {
            if (*p && *p != ',') { word += *p; continue; }
            screens::Nav n;
            bool ok = true;
            if (word == "up") n = screens::Nav::Up;
            else if (word == "down") n = screens::Nav::Down;
            else if (word == "left") n = screens::Nav::Left;
            else if (word == "right") n = screens::Nav::Right;
            else if (word == "a") n = screens::Nav::Activate;
            else if (word == "b") n = screens::Nav::Back;
            else ok = false;
            // The shoulders, for the manual on a game's page (#364).
            if (!ok && (word == "r1" || word == "l1")) {
                const int d = word == "r1" ? 1 : -1;
                if (!shoulderOnPage(d)) switchDestination(d);
                // A capture shows the slide finished.
                if (shotMode) manualSlide.settle(manualShown ? 1.0f : 0.0f);
                word.clear();
                if (!*p) break;
                continue;
            }
            // A digit is a PIN pad key typed on a keyboard, so a whole PIN
            // can be walked headless: "down,a,4,8,2,1,4,8,2,1".
            if (!ok && word.size() == 1 && word[0] >= '0' && word[0] <= '9') {
                pinOutcome(pinScreen.typeDigit(word[0]));
                word.clear();
                if (!*p) break;
                continue;
            }
            if (ok) navigate(n);
            else if (!word.empty()) std::fprintf(stderr, "[nav] unknown press '%s'\n", word.c_str());
            word.clear();
            if (!*p) break;
        }
        std::fprintf(stderr, "[nav] after '%s': screen %d, bar %s slot %d%s\n", navScript,
                     static_cast<int>(here()), barFocused ? "focused" : "not focused",
                     barSlot, pinScreen.isOpen() ? ", PIN pad open" : "");
        // A capture shows the pad at rest, not part-way through its fade.
        if (pinScreen.isOpen()) pinScreen.settle();
        if (choiceScreen.isOpen()) choiceScreen.settle();
    }


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

        if (evictTest && frame == 5) {
            // Five covers requested once and never asked for again: the shelf
            // has scrolled past them.
            for (int i = 0; i < 5; ++i) {
                images.get("covers/b-3x4.png#scrolled-past-" + std::to_string(i));
            }
        }
        followRemotesInGame();
        if (playing) {
            cab::Core& core = cab::Core::shared();
            const bool rewinding = readGameInput(core);
            runGameFrame(core, dt, rewinding);
        }

        // Launch on a timer when asked to. This exists so the Home-to-game
        // transition can be watched on the test machine, which has no
        // controller attached — not as a product behaviour.
        // The same errand as the launch above, for the action a person takes on
        // the launch screen. It goes through downloadById, so the floors are
        // checked exactly as they would be for a press.
        // Once Home is up, as a person's press would be: a download begun
        // behind the startup screen takes the curtain from it (pumpArrive
        // gives the curtain to any launch job) and the screen stays black.
        if (autoDownloadId > 0 && !launchJob.busy() && !arriving) {
            const int id = autoDownloadId;
            autoDownloadId = 0;
            // ASKED FOR BY ID, as --launch is (below): only Recent and
            // Favorites are in the library at boot, so any other game was not
            // found and the download never started, without a word. That is
            // how the first capture of the download ring (#145) came out as an
            // empty bar.
            if (lib.byRomId.find(id) == lib.byRomId.end()) {
                romm::Game g;
                std::string err;
                if (liveClient.fetchGame(id, &g, &err)) appendGame(lib, g);
                else std::fprintf(stderr, "[download] could not fetch rom %d: %s\n", id,
                                  err.c_str());
            }
            downloadById(id);
        }
        takeAccountResults();
        if (autoSwitchAccountId > 0) {
            const int id = autoSwitchAccountId;
            autoSwitchAccountId = 0;
            std::string why;
            if (!switchAccount(id, &why))
                std::fprintf(stderr, "[accounts] refused: %s\n", why.c_str());
            ra::useAccount(accounts::activeId());
        }
        if (autoUnkeepId > 0) {
            const int id = autoUnkeepId;
            autoUnkeepId = 0;
            removeDownload(id);
        }
        if (thenLaunchId > 0 && !overlayExitDemo && !playing && autoLaunchId == 0 &&
            !launchJob.busy()) {
            std::fprintf(stderr, "[launch] then: %d\n", thenLaunchId);
            autoLaunchId = thenLaunchId;
            thenLaunchId = 0;
            autoLaunchAfter = 1.0f;
        }
        if (autoLaunchId > 0 && !playing) {
            autoLaunchAfter -= dt;
            if (autoLaunchAfter <= 0.0f) {
                const int id = autoLaunchId;
                autoLaunchId = 0;
                // NOT ON HOME, SO NOT IN THE LIBRARY, since 2026-09-22: boot
                // stopped fetching the whole catalogue, and `--launch` quietly
                // lost every game that is not Recent or a Favorite. It said
                // "no game with id" and idled, which is how the save work of
                // 2026-09-23 found it. Asked for by id instead, the way a
                // platform grid or Search would have fetched it.
                if (lib.byRomId.find(id) == lib.byRomId.end()) {
                    romm::Game g;
                    std::string err;
                    if (liveClient.fetchGame(id, &g, &err)) {
                        appendGame(lib, g);
                        std::fprintf(stderr, "[launch] fetched %s by id\n",
                                     g.name.c_str());
                    } else {
                        std::fprintf(stderr, "[launch] could not fetch rom %d: %s\n",
                                     id, err.c_str());
                    }
                }
                launchById(id);
            }
        }

        pumpLaunch();
        pumpSwitch();
        pumpLeave();
        pollSteamJob();
        pumpArrive();
        pumpExit();
        pumpStateLoad(stateLoad, session, menuNotice);
        pumpDetailStates();
        loadPendingStates();
        if (overlayDemo && playing && !overlayOpen &&
            cab::Core::shared().framesRun() >= static_cast<uint64_t>(overlayDemoAfter)) {
            overlayDemo = false;
            toggleOverlay();
        }

        // --overlay-test: hold the pause menu open over a game this console did
        // not draw. `playing` is asserted so the whole world — backdrop, hero,
        // shelves, screens — is skipped; there is no core, so no game picture is
        // drawn either, and what reaches the screen is the scrim and the panel
        // over transparency. That is the thing being looked at.
        if (overlayTest) {
            playing = true;
            if (!overlayOpen) {
                overlayOpen = true;
                overlayFade.retarget(1.0f, overlayFadeSeconds);
            }
        }
        if (overlayExitDemo && playing) {
            static int t = 0;
            // How long the game is left running before the overlay quits it.
            // Was a fixed 120 frames, which is two seconds — long enough to
            // watch the transition and far too short for anything else. A PSP
            // game is still BOOTING at that point, and a save cannot be tested
            // at all because the game has not had time to write one.
            if (++t == overlayExitAfter) {
                toggleOverlay();
                overlaySlot = static_cast<int>(pauseItems.size()) - 1;   // Exit to Home
            }
            if (t == overlayExitAfter + 60) { overlayExitDemo = false; overlayActivate(); }
        }

        pumpDiscChange();

        // THE DISC RIG (#162, --disc-script). One step at a time, each when
        // the last is done; `wait` counts the game's own frames, so a step
        // means the same on any machine. Every step calls what the pause menu
        // calls.
        if (discScript.at < discScript.steps.size() && playing &&
            cab::Core::shared().running()) {
            cab::Core& core = cab::Core::shared();
            const uint64_t now = core.framesRun();
            // A Start over begins the game again, and its frames from zero.
            if (!discScript.started || now < discScript.from) {
                discScript.started = true;
                discScript.from = now;
                discScript.wallFrom = SDL_GetTicksNS();
            }
            const std::string& stepText = discScript.steps[discScript.at];
            std::istringstream in(stepText);
            std::string verb;
            in >> verb;
            bool done = true;
            auto say = [&](const std::string& what) {
                std::fprintf(stderr, "[disc-script] frame %llu: %s%s%s\n",
                             static_cast<unsigned long long>(now), stepText.c_str(),
                             what.empty() ? "" : " -> ", what.c_str());
            };
            if (verb == "wait") {
                uint64_t n = 0;
                in >> n;
                done = now >= discScript.from + n;
            } else if (verb == "sleep") {
                double sec = 0;
                in >> sec;
                done = (SDL_GetTicksNS() - discScript.wallFrom) / 1e9 >= sec;
            } else if (verb == "insert") {
                int n = 0;
                in >> n;
                beginInsert(n);
                say("");
            } else if (verb == "startover") {
                int n = 0;
                in >> n;
                say("");
                ++discScript.at;
                discScript.started = false;
                done = false;
                startOverOnDisc(n);
            } else if (verb == "state") {
                saveStateNow(session, uploader, menuNotice);
                say(menuNotice.text);
            } else if (verb == "load") {
                beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
                say("");
            } else if (verb == "resume") {
                // What A does once a state has loaded and the game waits: the
                // step waits for that hold (a load is fetched on a worker),
                // then ends it. Ten seconds with no hold and it moves on.
                if (stateHold) {
                    stateHold = false;
                    stateHoldJustEnded = true;
                    say("continuing");
                } else {
                    done = (SDL_GetTicksNS() - discScript.wallFrom) / 1e9 >= 10.0;
                    if (done) say("no state was holding the game");
                }
            } else if (verb == "press") {
                // press <button> <frames>: held for the game, then let go.
                std::string b;
                int frames = 6;
                in >> b >> frames;
                using cab::bit;
                const uint32_t m = b == "a" ? bit(cab::A) : b == "b" ? bit(cab::B)
                                 : b == "x" ? bit(cab::X) : b == "y" ? bit(cab::Y)
                                 : b == "start" ? bit(cab::Start) : b == "select" ? bit(cab::Select)
                                 : b == "up" ? bit(cab::Up) : b == "down" ? bit(cab::Down)
                                 : b == "left" ? bit(cab::Left) : b == "right" ? bit(cab::Right)
                                 : b == "l" ? bit(cab::L) : b == "r" ? bit(cab::R)
                                 : b == "l2" ? bit(cab::L2) : b == "r2" ? bit(cab::R2) : 0u;
                discScript.pad = m;
                discScript.padUntil = now + static_cast<uint64_t>(std::max(1, frames));
                say("");
            } else if (verb == "shot") {
                // The game's own frame, the menu never in it, as a PNG.
                std::string name;
                in >> name;
                std::vector<uint8_t> rgba, png;
                unsigned w = 0, h = 0;
                const std::string path = "/tmp/disc-" + name + ".png";
                const bool ok = core.snapshot(rgba, w, h) && ui::encodePNG(rgba, w, h, png) &&
                                cab::writeBytes(path, png);
                say(ok ? path : std::string("NO PICTURE"));
            } else if (verb == "ui") {
                // The whole screen, menus and all, as the capture signal does.
                std::string name;
                in >> name;
                discScript.captureTo = "/tmp/disc-" + name + ".bmp";
                gCaptureRequested = 1;
                say(discScript.captureTo);
            } else if (verb == "menu") {
                if (!overlayOpen) toggleOverlay();
                for (size_t k = 0; k < pauseItems.size(); ++k)
                    if (pauseItems[k] == OvDisc) overlaySlot = static_cast<int>(k);
                say(std::string("Change disc row ") +
                    (std::find(pauseItems.begin(), pauseItems.end(), OvDisc) != pauseItems.end()
                         ? "shown" : "NOT SHOWN"));
            } else if (verb == "activate") {
                overlayActivate();
                say("");
            } else if (verb == "nav") {
                // nav up|down|left|right|a|b, into whatever has the press.
                std::string k;
                in >> k;
                const screens::Nav n = k == "up" ? screens::Nav::Up : k == "down" ? screens::Nav::Down
                                     : k == "left" ? screens::Nav::Left
                                     : k == "right" ? screens::Nav::Right
                                     : k == "b" ? screens::Nav::Back : screens::Nav::Activate;
                if (choiceScreen.isOpen()) navigate(n);
                else if (n == screens::Nav::Up) ovStep(-1);
                else if (n == screens::Nav::Down) ovStep(+1);
                else if (n == screens::Nav::Activate) overlayActivate();
                else if (n == screens::Nav::Back) closeOverlay();
                say(choiceScreen.isOpen() ? "panel: " + choiceScreen.title() : "");
            } else if (verb == "close") {
                if (choiceScreen.isOpen()) choiceScreen.close();
                if (overlayOpen) closeOverlay();
                say("");
            } else if (verb == "check") {
                say("disc " + std::to_string(session.disc) + " of " +
                    std::to_string(disc::highest(session.discs)) + ", core index " +
                    std::to_string(core.discIndex()) + " of " + std::to_string(core.discCount()) +
                    (core.discEjected() ? ", lid OPEN" : ", lid closed") + ", last disc " +
                    std::to_string(disc::lastDisc(storage::currentUser(), session.romId)));
            } else if (verb == "play") {
                // play <romId>: leave this game and start another in the same
                // run, the way Home's Play would.
                int id = 0;
                in >> id;
                say("");
                ++discScript.at;
                discScript.started = false;
                done = false;
                exitToHome();
                // As --launch does: fetched by id if the library lacks it.
                autoLaunchId = id;
                autoLaunchAfter = 1.0f;
            } else if (verb == "quit") {
                say("");
                ++discScript.at;
                discScript.started = false;
                done = false;
                exitToHome();
                if (discScript.at == discScript.steps.size())
                    std::fprintf(stderr, "[disc-script] done\n");
            } else {
                say("UNKNOWN STEP");
            }
            if (done) {
                ++discScript.at;
                discScript.started = false;
                if (discScript.at == discScript.steps.size())
                    std::fprintf(stderr, "[disc-script] done\n");
            }
        }
        if (!discScript.steps.empty() && discScript.at == discScript.steps.size() &&
            !playing && !discScript.said) {
            discScript.said = true;
            std::fprintf(stderr, "[disc-script] back on Home\n");
        }

        if (stateCheckAfter > 0 && playing) {
            static std::vector<uint8_t> first;
            static uint64_t savedAt = 0;
            cab::Core& core = cab::Core::shared();
            if (first.empty() && core.framesRun() >= static_cast<uint64_t>(stateCheckAfter)) {
                if (!core.saveState(first) || first.empty()) {
                    std::fprintf(stderr, "[state-check] %s cannot make a state\n",
                                 launchJob.coreName.c_str());
                    stateCheckAfter = 0;
                } else {
                    savedAt = core.framesRun();
                }
            } else if (!first.empty() && core.framesRun() >= savedAt + 120) {
                const bool loaded = core.loadState(first);
                std::vector<uint8_t> again;
                core.saveState(again);
                size_t same = 0;
                for (size_t k = 0; k < std::min(first.size(), again.size()); ++k)
                    if (first[k] == again[k]) ++same;
                std::fprintf(stderr,
                             "[state-check] %s: %zu bytes, load %s, re-save %s "
                             "(%zu of %zu bytes equal)\n",
                             launchJob.coreName.c_str(), first.size(),
                             loaded ? "ACCEPTED" : "REFUSED",
                             again == first ? "IDENTICAL" : "DIFFERS",
                             same, first.size());
                stateCheckAfter = 0;
            }
        }

        if (loadStateAfter > 0 && playing &&
            cab::Core::shared().framesRun() >= static_cast<uint64_t>(loadStateAfter)) {
            loadStateAfter = 0;
            std::fprintf(stderr, "[load-state] loading the newest state\n");
            beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
        }

        // The round trip, once, a couple of seconds into the game so there is
        // something in memory worth snapshotting.
        if (syncTest && playing) {
            static int sinceStart = 0;
            if (++sinceStart == 150) {
                std::fprintf(stderr, "[sync-test] --- saving ---\n");
                const uint64_t t0 = SDL_GetTicksNS();
                saveStateNow(session, uploader, menuNotice);
                // Forced: at a title screen with no input the battery has not
                // changed, and "nothing to send" is the correct behaviour but
                // proves nothing about whether sending works.
                session.saveAtLaunch.clear();
                syncSave(session, uploader);
                // The same forcing for the saves the core writes as files, and
                // it is the only way to see that half send anything: those are
                // captured after the unload rather than here, and a headless
                // run cannot press the buttons that would make a game save.
                // So the baseline is dropped now and the quit-time capture
                // treats whatever the core flushed as new. `hadOne` goes with
                // it, because the freshness guard is the other thing under
                // test and a forced run must not be stopped by it.
                for (cab::FileSaveState& f : session.fileSaves) {
                    f.atLaunch.clear();
                    f.hadOne = true;
                }
                std::fprintf(stderr,
                             "[sync-test] %zu file save(s) will be sent at the quit\n",
                             session.fileSaves.size());
                // What the game actually paid. Everything after this point is
                // on the worker, so this is the whole cost to the picture.
                std::fprintf(stderr, "[sync-test] frame thread blocked %.2f ms\n",
                             (SDL_GetTicksNS() - t0) / 1e6);
                std::fprintf(stderr, "[sync-test] --- loading back ---\n");
                beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
                std::fprintf(stderr, "[sync-test] --- done ---\n");
            }
        }
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
        if (noticeGallery) {
            // Each for as long as it really stays up, a second apart, the
            // first after two so the screen has settled. A Busy one is held
            // 3.8 s, since nothing will answer it.
            static float galleryClock = -2.0f;
            static int galleryNext = 0;
            galleryClock += dt;
            constexpr int kN = static_cast<int>(sizeof kNoticeGallery / sizeof kNoticeGallery[0]);
            if (galleryClock >= 0.0f && galleryNext < kN * 100) {
                const GalleryNotice& g = kNoticeGallery[galleryNext % kN];
                std::fprintf(stderr, "[notice] gallery %d/%d: %s\n", galleryNext % kN + 1, kN,
                             g.text);
                menuNotice.say(g.text, g.tone);
                if (g.tone == Tone::Busy) menuNotice.life = 3.8f;
                ++galleryNext;
                galleryClock = -(menuNotice.life + 1.0f);
            }
        }
        // The other half of the sentence Save started. Cabinet's own words,
        // because they are better than anything invented here: it either
        // reached the server or it is waiting for signal.
        // WHAT IS OWED IS TRIED AGAIN EVERY FIVE MINUTES while any of it is:
        // the server coming back says nothing, so asking is the only way to
        // find out. A starting value; one line.
        owedClock = uploader.owing() ? owedClock + dt : 0.0f;
        if (owedClock >= 300.0f) {
            owedClock = 0.0f;
            uploader.resendOwed();
        }
        // OFFLINE AND BACK (#88). One line going, one coming back. Back, what
        // is owed goes at once rather than at the next five-minute try, and a
        // console that started offline loads its library properly, on Home
        // only and never during a game or a launch, so nothing is replaced
        // under somebody's feet; the shelves come in as an account switch's do.
        if (rommAddress) {
            const bool away = romm::serverAway();
            if (away != shownAway) {
                shownAway = away;
                if (away) {
                    std::fprintf(stderr, "[offline] can't reach the server; trying in the "
                                         "background\n");
                } else {
                    std::fprintf(stderr, "[offline] the server answered after %llds\n",
                                 static_cast<long long>(std::time(nullptr)) -
                                     romm::serverAwaySince());
                    images.forgetFailed();
                    if (!libFromDrive) {
                        owedClock = 0.0f;
                        uploader.resendOwed();
                    }
                }
            }
            onlineAgainClock += dt;
            // THE SERVER WENT WHILE THE CONSOLE WAS ON: after a minute, so a
            // blip changes nothing, Home and the Library become the games on
            // the drive, as at an offline start (MMagTech, 2026-10-05: offline
            // looks the same however you got there). On Home only, never under
            // a game, a grid or a game's page; the same arrival as coming back.
            if (away && !libFromDrive && !playing && here() == Screen::Home && !accountsOpen &&
                !launchJob.busy() &&
                static_cast<long long>(std::time(nullptr)) - romm::serverAwaySince() >= 60) {
                lib = offlineLibrary();
                libFromDrive = true;
                libraryScreen.build(platformTiles, collectionTiles);
                homeFromTheStart();
                refreshKeeps();
                tabSince = 0.0f;
                std::fprintf(stderr, "[offline] the server has been away a minute; Home is the "
                                     "games on the drive\n");
            }
            // THE LIBRARY IS FETCHED OFF THE FRAME THREAD, on a client of its
            // own, and swapped in here once it is in hand: fetched in place,
            // the picture froze for the length of the fetch and the fade below
            // was spent during the freeze, so it read as a flash (MMagTech on
            // the TV, 2026-10-05).
            if (!away && libFromDrive && !onlineLoad.running.load() && !onlineLoad.ready &&
                onlineAgainClock >= 5.0f) {
                onlineAgainClock = 0.0f;
                onlineLoad.running = true;
                onlineLoad.forAccount = accounts::activeId();
                if (onlineLoad.th.joinable()) onlineLoad.th.join();
                const std::string address = rommAddress;
                onlineLoad.th = std::thread([this, address]() {
                    romm::Client c;
                    std::string e;
                    Library fresh;
                    if (c.setAddress(address, &e) && accounts::loadActiveToken(c))
                        fresh = loadLibrary(c);
                    onlineLoad.lib = std::move(fresh);
                    onlineLoad.ready = true;
                    onlineLoad.running = false;
                });
            }
            if (onlineLoad.ready.load() && !playing && here() == Screen::Home && !accountsOpen &&
                !launchJob.busy() && uploader.pending() == 0) {
                onlineLoad.ready = false;
                if (onlineLoad.th.joinable()) onlineLoad.th.join();
                std::string e;
                if (onlineLoad.forAccount != accounts::activeId() ||
                    onlineLoad.lib.platformTiles.empty()) {
                    std::fprintf(stderr, "[offline] library not loaded; trying again\n");
                } else if (!liveClient.setAddress(rommAddress, &e)) {
                    std::fprintf(stderr, "[offline] the server went again: %s\n", e.c_str());
                } else {
                    adoptUser(liveClient);
                    lib = std::move(onlineLoad.lib);
                    libraryScreen.build(platformTiles, collectionTiles);
                    startCoverFill();
                    homeFromTheStart();
                    refreshKeeps();
                    libFromDrive = false;
                    images.forgetFailed();
                    // HOME ARRIVES AGAIN, as it does from the bar: the
                    // shelves fade in and rise (#88's agreed sequence, "the
                    // shelves fade in"). The change is the signal.
                    tabSince = 0.0f;
                    std::fprintf(stderr, "[offline] online again: %zu game(s) on Home\n",
                                 cards.size());
                    owedClock = 0.0f;
                    uploader.resendOwed();
                    backfillDrive(rommAddress);
                }
                onlineLoad.lib = Library{};
            }
        }
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

        // ---- The notification pill — see MenuNotice ------------------------
        //
        // Over the menus and over a game, under the curtain and the dim. Near
        // the foot of the screen, inside the safe area, centred: where a
        // console's confirmation is looked for, and clear of every menu's
        // buttons. It rises a little as it arrives, like the panels do.
        //
        // After the top bar since 2026-09-26, so it sits over Settings'
        // Downloads panel; the bar and the pill never meet, one at the top
        // and one at the foot.
        {
            const float na = menuNotice.alpha();
            const float keepAlpha = renderer.contentAlpha();
            renderer.setContentAlpha(1.0f);   // not part of a screen's transition
            if (na > 0.01f && !menuNotice.text.empty()) {
                const ui::TextStyle st = ui::TextStyle::Callout;
                const float tw = text.measure(menuNotice.text, st, sc);
                constexpr float kPillH = 64.0f, kPad = 30.0f, kDot = 14.0f, kGap = 16.0f;
                const float w = kPad + kDot + kGap + tw + kPad;
                const float x = (ui::kCanvasWidth - w) * 0.5f;
                const float y = ui::kCanvasHeight - ui::kSafeInset - kPillH - 24.0f +
                                (1.0f - std::min(1.0f, menuNotice.age / 0.25f)) * 12.0f;
                ui::Color fill = look::surface();
                fill.a = 0.96f * na;
                ui::Rect pill{x, y, w, kPillH, kPillH * 0.5f, fill};
                pill.border = 1.5f;
                pill.borderColor = ui::Color::white(0.14f * na);
                pill.edgeLight = ui::Color::white(0.18f * na);
                pill.shadowBlur = 26.0f;
                pill.shadowOffsetY = 10.0f;
                pill.shadowColor = ui::Color::black(0.45f * na);
                renderer.draw(pill);

                // The dot says which kind of answer it is before the words do.
                ui::Color dot = ui::Color::white(0.70f);
                switch (menuNotice.tone) {
                    case Tone::Done: dot = ui::palette::kScreenCyan; break;
                    case Tone::Busy: {
                        dot = ui::palette::kScreenCyan;
                        // A slow breath, so "working" never reads as "done".
                        const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
                        dot.a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 4.2f));
                        break;
                    }
                    case Tone::Info: break;
                    case Tone::Problem: dot = ui::palette::kMarqueeAmber; break;
                }
                dot.a *= na;
                renderer.draw(ui::Rect{x + kPad, y + (kPillH - kDot) * 0.5f, kDot, kDot,
                                       kDot * 0.5f, dot});
                text.draw(renderer, menuNotice.text, x + kPad + kDot + kGap,
                          y + (kPillH - text.lineHeight(st, sc)) * 0.5f + text.ascent(st, sc),
                          st, ui::Color::white(0.94f * na), sc);
            }
            renderer.setContentAlpha(keepAlpha);
        }

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

        // ---- The dim, over absolutely everything --------------------------
        //
        // After the keyboard and the guides, because it is not part of the
        // interface; it is the television resting. At full depth it is also
        // the blank wherever gamescope cannot put the output to sleep.
        {
            const float d = dimLayer.value();
            if (d > 0.001f) {
                const float keep = renderer.contentAlpha();
                renderer.setContentAlpha(1.0f);
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(d)});
                renderer.setContentAlpha(keep);
            }
        }

        ++frame;
        // Capture before the swap. After a swap the back buffer's contents are
        // undefined, so a readback taken there is whatever the driver left.
        if (shotMode && frame >= shotAfterFrames &&
            (!waitForPairCode || shownPairCode) &&
            // The manual's page on screen, or its refusal (#364), however
            // long fetching and drawing take.
            (!manualShown || manualView.settled())) {
            renderer.saveFrame(shotPath, dw, dh);
            // Whether the running core can produce a state AT THIS POINT, which
            // is a different question from whether the round trip is exact and
            // is the only half of it a capture can answer.
            //
            // It is here because --state-test cannot answer it for every core.
            // Its warm-up is a tight loop of retro_run with no wall clock in
            // it, and a core that emulates on a thread of its own — PPSSPP is
            // the only one — barely advances in that loop: no sound, a static
            // picture and a zero-byte state after three thousand calls, while
            // the same core reaches its attract demo on the ordinary launch
            // path. A capture has a real frame loop under it, so the number
            // here is about the core rather than about the instrument.
            if (playing) {
                cab::Core& c = cab::Core::shared();
                std::fprintf(stderr, "[state] serialize size at capture: %zu bytes\n",
                             c.stateSize());
            }
            running = false;
        }
        if (gCaptureRequested) {
            gCaptureRequested = 0;
            renderer.saveFrame(discScript.captureTo.empty() ? "/tmp/cabinetos-frame.bmp"
                                                            : discScript.captureTo.c_str(),
                               dw, dh);
            discScript.captureTo.clear();
        }

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

    // Never leave the television asleep behind us: the next thing on it —
    // this program restarting, or a person at a console — must be seen.
    if (idleShown == idle::Level::Blank) idle::setDisplayAsleep(false, /*wait=*/true);

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
