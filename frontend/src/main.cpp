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
    curtain.smooth = true;
    curtain.from = curtain.to = 0.0f;
    if (startupShown && !shotMode) {
        curtain.from = curtain.to = 1.0f;
        curtain.elapsed = curtain.duration;
        arriving = true;
    }

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

    // The button is borrowed for the life of this process — power.h. Not for a
    // capture, which is not the console and must not take its button.
    if (!shotMode) {
        power::takeButtons();
        power::takeShutdownDelay();
        restAvailable = power::canRest();
    }
    if (powerMenuDemo) {
        if (shotMode) restAvailable = power::canRest();
        openPowerMenu();
    }

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
            // THE EMULATOR HAS THE TELEVISION AND THE CONTROLLERS. Nothing here
            // may act on a press it also sees, or Home would be navigated
            // blind behind the game; a press only says somebody is playing.
            // A request to quit still goes through.
            //
            // EACH REAL PAD'S PRESSES GO TO ITS PLAYER'S VIRTUAL CONTROLLER,
            // the moment they arrive: this loop waits on events rather than
            // sleeping while the emulator runs, so nothing is added to the
            // time between a press and the game. A pad arriving or leaving
            // still reaches players.h, so the order stays the console's.
            //
            // AND THE PAUSE MENU OPENS THE WAY IT DOES FOR EVERY SYSTEM: both
            // stick clicks, always, or a tap of the shortcut button with the
            // in-game shortcuts on. The shortcut button (Home, unless set) is
            // never passed to the emulator at all: its Home is its own menu
            // and a set of Home+button shortcuts, one of which leaves
            // fullscreen, and none of them belong on a console.
            if (standaloneRun.active() && !standalonePaused && e.type != SDL_EVENT_QUIT) {
                switch (e.type) {
                    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    case SDL_EVENT_GAMEPAD_BUTTON_UP: {
                        playClock.touch();
                        const int p = players::playerOf(e.gbutton.which);
                        const auto b = static_cast<SDL_GamepadButton>(e.gbutton.button);
                        const bool down = e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN;
                        if (b == SDL_GAMEPAD_BUTTON_LEFT_STICK) l3Down = down;
                        if (b == SDL_GAMEPAD_BUTTON_RIGHT_STICK) r3Down = down;
                        if (down && l3Down && r3Down) {
                            l3Down = r3Down = false;
                            openStandaloneMenu();
                            break;
                        }
                        SDL_Gamepad* gp = players::gamepad(p);
                        const bool shortcut = gp && shortcuts::held(gp);
                        // A streaming device's Home always opens the menu, as
                        // in the built-in path (players::streaming()).
                        if (p >= 0 && p < cab::vpad::kMaxPlayers &&
                            (shortcuts::enabled() || players::streaming())) {
                            auto& sh = standaloneShortcut[p];
                            if (sh.down && sh.pad != e.gbutton.which) sh = {};
                            // THE SHORTCUT BUTTON'S OWN PRESS IS NOT A
                            // SECOND BUTTON. Sunshine's hold-Select-for-Home
                            // lets go of Select and presses Home in the same
                            // instant, so the hold began on Select's release
                            // (Home already down) and Home's own press then
                            // counted as a combination: no menu (MMagTech's
                            // phone in a PS3 game, 2026-10-08).
                            if (shortcut && !sh.down) sh = {true, false, e.gbutton.which};
                            else if (shortcut && down && !shortcuts::isShortcut(gp, b))
                                sh.used = true;
                            if (!shortcut && sh.down) {
                                const bool tap = !sh.used;
                                sh = {};
                                if (tap) {
                                    std::fprintf(stderr,
                                                 "[shortcuts] player %d: tap, opening the menu\n",
                                                 p + 1);
                                    openStandaloneMenu();
                                    break;
                                }
                            }
                            if (shortcut) break;   // held: the game hears none of it
                        }
                        if (b == SDL_GAMEPAD_BUTTON_GUIDE) break;
                        // BACK AND START TOGETHER never reach an emulator that
                        // opens its own menu on them (xemu): the second one
                        // down is held back, press and release both.
                        const cab::standalone::Emulator* running = standaloneRun.emulator();
                        if (running && running->blocksBackStart && p >= 0 &&
                            p < cab::vpad::kMaxPlayers &&
                            (b == SDL_GAMEPAD_BUTTON_BACK || b == SDL_GAMEPAD_BUTTON_START)) {
                            auto& bs = backStart[p];
                            const int me = b == SDL_GAMEPAD_BUTTON_BACK ? 0 : 1;
                            if (down && bs.passed[1 - me]) {
                                bs.held[me] = true;
                                break;
                            }
                            if (!down && bs.held[me]) {
                                bs.held[me] = false;
                                break;
                            }
                            bs.passed[me] = down;
                        }
                        cab::vpad::button(p, b, down);
                        break;
                    }
                    case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                        cab::vpad::axis(players::playerOf(e.gaxis.which),
                                        static_cast<SDL_GamepadAxis>(e.gaxis.axis),
                                        e.gaxis.value);
                        break;
                    case SDL_EVENT_GAMEPAD_ADDED:
                        players::added(e.gdevice.which);
                        break;
                    case SDL_EVENT_GAMEPAD_REMOVED:
                        players::removed(e.gdevice.which);
                        break;
                    case SDL_EVENT_KEY_DOWN:
                        playClock.touch();
                        break;
                    default:
                        break;
                }
                continue;
            }
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
        followDrives();
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

        // THE SCREENSHOT gamescope is writing, whenever there is one.
        pumpStandaloneShot();
        // PAUSED OVER AN EMULATOR OF ITS OWN, the ordinary loop below runs and
        // draws the menu; here it only keeps watch. The emulator can still end
        // (Exit to Home asked it to, or it fell over), and when the menu has
        // closed and faded right out, the controllers go back and it thaws.
        if (standaloneRun.active() && standalonePaused) {
            // THE VIRTUAL CONTROLLERS ARE STILL ANSWERED, rumble held off. An
            // emulator asks its controller to stop rumbling as it shuts down,
            // and the kernel holds that request until this process answers or
            // 30 s pass. Exit to Home is chosen from here, so unanswered, God
            // of War III took a minute to close with the console sitting on
            // it, SIGKILL included (2026-09-28: its Pad Thread in
            // uinput_request_submit).
            cab::vpad::pump(static_cast<int64_t>(SDL_GetTicks()));
            rumble::update(false);
            if (!standaloneRun.poll()) {
                finishStandalone();
            } else if (!overlayOpen && overlayFade.value() <= 0.001f &&
                       !standaloneRun.stopping()) {
                cab::overlaywin::mark(window, /*takeInput=*/false);
                standalonePaused = false;
                for (auto& sh : standaloneShortcut) sh = {};
                standaloneRun.thaw();
                std::fprintf(stderr, "[standalone] resumed\n");
            }
        }

        // WHILE AN EMULATOR OF ITS OWN HAS THE SCREEN, this loop watches it and
        // does nothing else: no idle dimming over a game this process cannot
        // see, nothing drawn, and a short sleep so a watcher does not take a
        // core from the game. Play time counts as it does for any game.
        if (standaloneRun.active() && !standalonePaused) {
            if (standaloneClearFrames > 0) {
                --standaloneClearFrames;
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glDisable(GL_SCISSOR_TEST);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                SDL_GL_SwapWindow(window);
            }
            playClock.tick(dt, true);
            if (playClock.active() && (playCheckpointClock += dt) >= 60.0f) {
                playCheckpointClock = 0.0f;
                playtime::checkpoint(storage::currentUser(), playClock.now(playtime::wallMs()));
            }
            standaloneRan += dt;
            if (standaloneFreezeAfter > 0.0f && standaloneRan >= standaloneFreezeAfter &&
                !standaloneRun.frozen() && !standaloneRun.stopping()) {
                standaloneFreezeAfter = 0.0f;
                standaloneRun.freeze();
            }
            if (standaloneExitAfter > 0.0f && !standaloneRun.stopping() &&
                standaloneRan >= standaloneExitAfter) {
                std::fprintf(stderr, "[standalone] --standalone-exit: closing it\n");
                standaloneRun.stop();
            }
            // The game's rumble, to the pads in people's hands, and never
            // while it is frozen under the pause menu.
            cab::vpad::pump(static_cast<int64_t>(SDL_GetTicks()));
            rumble::update(!standaloneRun.frozen());
            if (!standaloneRun.poll()) finishStandalone();
            // This loop skips everything below, so the handover is asked here
            // too: a PS3 game did not hand over until its menu was opened
            // (2026-10-07).
            remotePlayHandover();
            // Until the next press or a sixtieth of a second, whichever is
            // first: presses pass straight on, and a watcher costs nothing.
            SDL_WaitEventTimeout(nullptr, 16);
            continue;
        }

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
        // REAL WII REMOTES (#200): while a Wii game runs Dolphin drives them,
        // and HOME held on one is the console's own menu; a press of HOME is
        // the game's HOME menu, as on a Wii. Out of the game each Remote gets
        // its own light again.
        wiiremote::setGameRunning(playing && session.platformSlug == "wii");
        if (wiiremote::takeHomeHold() && (playing || overlayOpen)) toggleOverlay();
        if (playing) {
            cab::Core& core = cab::Core::shared();
            // Buttons first, then run: a core samples input inside retro_run,
            // so anything set afterwards is a frame late.
            cab::PadState pad;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            auto held = [&](SDL_Scancode k) { return keys && keys[k]; };
            pad.buttons = 0;
            // The RetroPad, which every core speaks whatever the real hardware
            // had. Keyboard here; a real pad is wired the same way below.
            using cab::bit;
            if (held(SDL_SCANCODE_UP)) pad.buttons |= bit(cab::Up);
            if (held(SDL_SCANCODE_DOWN)) pad.buttons |= bit(cab::Down);
            if (held(SDL_SCANCODE_LEFT)) pad.buttons |= bit(cab::Left);
            if (held(SDL_SCANCODE_RIGHT)) pad.buttons |= bit(cab::Right);
            if (held(SDL_SCANCODE_X)) pad.buttons |= bit(cab::A);
            if (held(SDL_SCANCODE_Z)) pad.buttons |= bit(cab::B);
            if (held(SDL_SCANCODE_RETURN)) pad.buttons |= bit(cab::Start);
            if (held(SDL_SCANCODE_RSHIFT)) pad.buttons |= bit(cab::Select);
            // The disc rig's presses (#162, --disc-script `press`).
            if (discScript.pad && core.framesRun() < discScript.padUntil)
                pad.buttons |= discScript.pad;

            // A real pad, mapped by SDL's own gamepad abstraction so the
            // hundreds of controllers in its database all arrive the same way.
            // The keyboard is player one as well: both OR together, neither is
            // required.
            //
            // EVERY SEATED PAD IS ITS OWN PLAYER (players.h, issue #64). Until
            // 2026-09-26 a game read only the first pad SDL listed, so a second
            // person's pad walked the menus and did nothing in a game.
            bool fastForward = false, rewinding = false;
            const bool menuWasUp = overlayOpen || stateHold || menuUpAtLastRead;
            menuUpAtLastRead = overlayOpen || stateHold;
            for (int p = 0; p < players::kMax; ++p) {
                cab::PadState st = p == 0 ? pad : cab::PadState{};
                if (SDL_Gamepad* gp = players::gamepad(p)) {
                    auto down = [&](SDL_GamepadButton b) {
                        return SDL_GetGamepadButton(gp, b);
                    };
                    if (down(SDL_GAMEPAD_BUTTON_DPAD_UP)) st.buttons |= bit(cab::Up);
                    if (down(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) st.buttons |= bit(cab::Down);
                    if (down(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) st.buttons |= bit(cab::Left);
                    if (down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) st.buttons |= bit(cab::Right);
                    // South is the bottom face button whatever it is
                    // labelled: A on Xbox, B on Nintendo, Cross on
                    // PlayStation. SDL normalises by POSITION, which is the
                    // only thing that is actually the same across pads.
                    if (down(SDL_GAMEPAD_BUTTON_SOUTH)) st.buttons |= bit(cab::B);
                    if (down(SDL_GAMEPAD_BUTTON_EAST)) st.buttons |= bit(cab::A);
                    if (down(SDL_GAMEPAD_BUTTON_WEST)) st.buttons |= bit(cab::Y);
                    if (down(SDL_GAMEPAD_BUTTON_NORTH)) st.buttons |= bit(cab::X);
                    if (down(SDL_GAMEPAD_BUTTON_START)) st.buttons |= bit(cab::Start);
                    if (down(SDL_GAMEPAD_BUTTON_BACK)) st.buttons |= bit(cab::Select);

                    // THE SHOULDERS, THE TRIGGERS AND THE RIGHT STICK.
                    // None of these were mapped until 2026-09-19 — the
                    // pad sent a d-pad, four face buttons, Start, Select
                    // and one stick, and RetroPad's other six inputs went
                    // nowhere. Found by MMagTech on the A9 trying to play
                    // Crazy Taxi, where the triggers ARE drive and
                    // reverse, so the game could not be played at all.
                    //
                    // It survived because nothing headless presses a
                    // button: every measurement to date drove the pad
                    // from code or watched an attract demo.
                    if (down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  st.buttons |= bit(cab::L);
                    if (down(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) st.buttons |= bit(cab::R);
                    // L3/R3 also open the overlay as a pair. They still
                    // reach the core individually: Cabinet's rule is that
                    // the hotkey "applies regardless of what either button
                    // is otherwise bound to", and the overlay pauses the
                    // core the instant it opens anyway.
                    if (down(SDL_GAMEPAD_BUTTON_LEFT_STICK))  st.buttons |= bit(cab::L3);
                    if (down(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) st.buttons |= bit(cab::R3);

                    // Analog triggers as digital L2/R2, which is what a
                    // RetroPad's L2/R2 are for every core in this set.
                    // Half travel: a hair-trigger fires on the spring's
                    // own slop and a full-travel one never fires on a worn
                    // pad.
                    const int kTrigger = 16384;
                    const int lt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
                    const int rt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
                    if (lt > kTrigger) st.buttons |= bit(cab::L2);
                    if (rt > kTrigger) st.buttons |= bit(cab::R2);
                    // AND HOW FAR, not just whether. A Dreamcast reads its
                    // triggers as a continuous value — they are the
                    // accelerator and the brake in a driving game — and
                    // Flycast asks for that through the analogue channel
                    // rather than the button. The digital bits above still
                    // go out for every core that wants a shoulder.
                    //
                    // A pad with switches instead of springs, which is what
                    // a Switch Pro Controller's ZL and ZR are, hands SDL a
                    // clean 0 or 32767 and arrives here as 0 or 1.
                    st.leftTrigger = std::clamp(lt / 32767.0f, 0.0f, 1.0f);
                    st.rightTrigger = std::clamp(rt / 32767.0f, 0.0f, 1.0f);

                    st.leftX = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                    st.leftY = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                    st.rightX = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
                    st.rightY = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
                }
                // THE SHORTCUT BUTTON (shortcuts.h), only with the switch on.
                // While it is held the game hears nothing from this pad's
                // buttons, so neither it nor the second button of a shortcut
                // reaches the game; on release, whatever is still held is kept
                // back until let go, the way the menu's buttons are (#103).
                // A tap opens the pause menu, or closes it. Shortcuts do
                // nothing while the menu is up, but still count as a hold.
                // WHILE A DEVICE STREAMS, ITS HOME ALWAYS OPENS THE MENU,
                // switch or not: a phone's touch controls cannot click both
                // sticks, and holding Select is Home there (Sunshine's
                // back_button_timeout), so it is the one way to pause from any
                // app (MMagTech, 2026-10-08). The shortcuts themselves stay
                // the switch's. The pads seated during a stream are only the
                // stream's, so nothing changes at the TV.
                const bool combos = shortcuts::enabled();
                if (SDL_Gamepad* gp = players::gamepad(p);
                    gp && (combos || players::streaming())) {
                    ShortcutHold& h = shortcutHold[p];
                    const SDL_JoystickID who = SDL_GetGamepadID(gp);
                    if (h.down && h.pad != who) h = {};
                    const bool down = shortcuts::held(gp);
                    // The shortcut button pressed to end a wait is that press,
                    // not a tap that opens the menu on its release.
                    if (down && !h.down) h = {true, stateHoldJustEnded, st.buttons, who};
                    if (h.down) {
                        if (combos && down && (st.buttons & bit(cab::R2)) && !overlayOpen &&
                            !stateHold && !stateHoldWaiting)
                            fastForward = true;
                        if (combos && down && (st.buttons & bit(cab::L2)) && !overlayOpen &&
                            !stateHold && !stateHoldWaiting)
                            rewinding = true;
                        const uint32_t fresh = st.buttons & ~h.before;
                        h.before = st.buttons;
                        if (fresh) {
                            h.used = true;
                            // The screenshot shortcut works in the pause menu too,
                            // on the paused frame. Nothing else does there.
                            if (combos && overlayOpen && !powerMenu && (fresh & bit(cab::Y))) {
                                std::fprintf(stderr,
                                             "[shortcuts] player %d: screenshot, paused\n", p + 1);
                                screenshotNow(session, uploader, menuNotice);
                            }
                            if (combos && !overlayOpen && !stateHold && !stateHoldWaiting) {
                                const bool save = fresh & bit(cab::R);
                                const bool load = !save && (fresh & bit(cab::L));
                                if (!save && !load && (fresh & bit(cab::Y))) {
                                    std::fprintf(stderr, "[shortcuts] player %d: screenshot\n",
                                                 p + 1);
                                    screenshotNow(session, uploader, menuNotice);
                                }
                                if ((save || load) && !session.snapshots) {
                                    menuNotice.say("No save states for this system",
                                                   Tone::Info);
                                } else if (save) {
                                    std::fprintf(stderr, "[shortcuts] player %d: save state\n",
                                                 p + 1);
                                    saveStateNow(session, uploader, menuNotice);
                                } else if (load) {
                                    std::fprintf(stderr, "[shortcuts] player %d: load newest\n",
                                                 p + 1);
                                    beginLoadLatestState(stateLoad, session, liveClient,
                                                         menuNotice);
                                }
                            }
                        }
                        if (!down) {
                            h.down = false;
                            heldThroughMenu[p] |= st.buttons;
                            if (!h.used && !stateHold && !stateHoldWaiting) {
                                std::fprintf(stderr, "[shortcuts] player %d: tap, %s the menu\n",
                                             p + 1, overlayOpen ? "closing" : "opening");
                                toggleOverlay();
                            }
                        } else {
                            st.buttons = 0;
                            st.leftTrigger = st.rightTrigger = 0;
                        }
                    }
                }
                // #103: what closed the menu is the menu's, not the game's.
                // The keyboard is in `st` for player one, so it is covered
                // too. A trigger that is swallowed goes to zero as well, or a
                // Dreamcast would still read the pull through its analogue
                // channel.
                if (menuWasUp) heldThroughMenu[p] = st.buttons;
                heldThroughMenu[p] &= st.buttons;
                st.buttons &= ~heldThroughMenu[p];
                if (heldThroughMenu[p] & bit(cab::L2)) st.leftTrigger = 0;
                if (heldThroughMenu[p] & bit(cab::R2)) st.rightTrigger = 0;
                core.setPad(p, st);
            }

            // Wall-clock pacing is the product's, and it is wrong for a
            // capture. `--frames 180` asks for 180 drawn frames, and with
            // nothing to wait for offscreen those 180 take under a tenth of a
            // second — so the core is paced against a tenth of a second and
            // emulates FIVE frames, which is a black boot screen for every
            // console ever made. The first run of Mupen64Plus reported exactly
            // that and it looked like the core had failed.
            //
            // So a capture steps one emulated frame per drawn frame: `--frames`
            // then means what it says, and the same command gives the same
            // picture on a fast machine and a slow one. The instrument being
            // the thing that is wrong has cost this project a day already.
            // THE OVERLAY PAUSES THE EMULATOR. It did not until 2026-09-19,
            // so the game carried on being played behind the menu — in Crazy
            // Taxi you would still be driving while reading it. Cabinet has
            // always done this (`openMenu` sets `renderer.paused = true`);
            // this console simply never did, because nobody had opened the
            // overlay with a game they were actually playing.
            //
            // Not the game's own pause: the core stops being stepped, which
            // works for every system whether or not it has a pause button.
            // AND FOR PLAYSTATION 2 THAT IS NOT ENOUGH, because PCSX2 is not
            // stepped by this loop at all: it runs its own machine on a thread
            // of its own and would carry on playing behind the menu exactly as
            // every core did before 2026-09-19. It has to be TOLD. A no-op for
            // all twenty-one libretro cores, so it is stated unconditionally
            // rather than behind a test somebody has to remember.
            stateHoldJustEnded = false;
            if (fastForward && !session.snapshots) {
                if (!fastForwardSaid) menuNotice.say("No fast forward for this system", Tone::Info);
                fastForwardSaid = true;
                fastForward = false;
            } else if (!fastForward) {
                fastForwardSaid = false;
            }
            if (fastForward != (core.speed() > 1.0))
                std::fprintf(stderr, "[shortcuts] fast forward %s\n", fastForward ? "on" : "off");
            core.setSpeed(fastForward && !rewinding ? kFastForward : 1.0);
            // Whether this game can rewind, decided once it is running.
            if (rewindAvailable < 0 && core.running() && core.framesRun() > 0) {
                const size_t sz = core.stateSize();
                rewindAvailable = session.snapshots && sz > 0 ? 1 : 0;
                rewindLastFrame = core.framesRun();
                std::fprintf(stderr, "[rewind] %s (state %zu KB)\n",
                             rewindAvailable ? "available" : "not offered", sz / 1024);
            }
            // WHAT REWIND IS HOLDING, in the log each time it starts: the
            // numbers the memory budget was argued from, measured.
            static bool wasRewinding = false;
            if (rewinding && !wasRewinding && rewindAvailable == 1) {
                const size_t n = rewindKeep.count(), b = rewindKeep.bytes();
                std::fprintf(stderr,
                             "[rewind] %zu states, %.1f s, %.1f MB in memory "
                             "(%.0f KB each, from %zu KB); slowest snapshot %.1f ms\n",
                             n, n * 0.5, b / 1048576.0, n ? b / 1024.0 / n : 0.0,
                             core.stateSize() / 1024, rewindWorstMs);
                rewindStepWait = 0.0f;   // the first step is at once
            }
            wasRewinding = rewinding;
            if (rewinding && rewindAvailable != 1) {
                if (!rewindSaid) menuNotice.say("No rewind for this system", Tone::Info);
                rewindSaid = true;
                rewinding = false;
            } else if (!rewinding) {
                rewindSaid = false;
            }
            const bool frozen = overlayOpen || stateHold || stateHoldWaiting;
            core.setPaused(frozen);
            // Set when a paused frame was run only to redraw it; its sound is
            // dropped below, as rewind's is.
            bool redrew = false;
            // Frames shown, repeated and skipped in play, for the [pace] line.
            static uint64_t paceShown = 0, paceRepeated = 0, paceHeld = 0, paceSkipped = 0;
            static double paceDtSum = 0.0;
            // RUMBLE (#149): what the game asked for, to each player's pad,
            // and nothing while the game is not being played. Rewind replays
            // frames backwards, which is not play either.
            if (core.isPs2()) {
                ps2::pollRumble();
                ps2::report();
            }
            rumble::update(!frozen && !rewinding);

            if (frozen && pauseRedrawFrames > 0 && !core.isPs2()) {
                // A SYSTEM ROW WAS CHANGED IN THE PAUSE MENU (#73): run the
                // game on by a frame or two, silently, so the frozen picture
                // behind the menu is redrawn with the new option. A core
                // reads the change at the start of its next frame and draws
                // it in that frame or the one after. MMagTech on the TV: the
                // colour did not change behind the menu.
                --pauseRedrawFrames;
                core.runFor(1.0 / std::max(core.avInfo().fps, 1.0));
                redrew = true;
            } else if (frozen) {
                // Nothing to step. The last frame stays uploaded, so the
                // menu sits over a frozen picture rather than a black one.
            } else if (shotMode) {
                core.runFor(1.0 / std::max(core.avInfo().fps, 1.0));
            } else if (rewinding) {
                // ONE SNAPSHOT BACK EVERY kRewindStep, held still between, so
                // it can be stopped where wanted. A frame is run from each so
                // there is a picture (its sound is dropped below). With none
                // left it holds on the oldest.
                rewindStepWait -= dt;
                if (rewindStepWait <= 0.0f) {
                    rewindStepWait = kRewindStep;
                    if (rewindKeep.takeNewest(rewindRaw)) {
                        core.loadState(rewindRaw);
                        core.runFor(1.0 / std::max(core.avInfo().fps, 1.0));
                    }
                    rewindLastFrame = core.framesRun();
                }
            } else {
                // THE SPEAKER SETS THE PACE, for every system — 2026-09-27.
                // More than kAudioSyncMs of sound already waiting and the core
                // is not due this frame. This is RetroArch's audio sync (its
                // audio write blocks when the buffer is full, default latency
                // 64 ms), and it replaced a day of per-core fixes: a core that
                // runs fast (N64 by 3 to 4%, Flycast's free-running thread,
                // PPSSPP's double speed) fills the queue and is held; one that
                // delivers sound in bursts (N64) waits instead of having the
                // top of each burst thrown away as crackle; a stall cannot
                // pile up delay. Nothing is discarded to keep time.
                //
                // THE GUARD, which Cabinet paid for (its issue #6): a gate on
                // the speaker draining freezes the game whenever the speaker
                // stops taking sound. Held for more than 250 ms with the queue
                // not going down, the gate lets go and the clock paces the
                // core as before, and says so once.
                static constexpr double kAudioSyncMs = 64.0;
                static uint64_t drainSeenNs = 0;
                static double lastWaitingMs = 0.0;
                static bool saidStuck = false;
                bool holdForSpeaker = false;
                if (audioStream && !core.isPs2() && core.speed() <= 1.0) {
                    const uint64_t nowNs = SDL_GetTicksNS();
                    const double rate = std::max(core.avInfo().sampleRate, 1.0);
                    const double waitingMs =
                        SDL_GetAudioStreamQueued(audioStream) / (4.0 * rate) * 1000.0;
                    if (waitingMs <= kAudioSyncMs || waitingMs < lastWaitingMs ||
                        drainSeenNs == 0) {
                        drainSeenNs = nowNs;
                        saidStuck = false;
                    }
                    holdForSpeaker = waitingMs > kAudioSyncMs &&
                                     nowNs - drainSeenNs < 250'000'000ull;
                    if (waitingMs > kAudioSyncMs && !holdForSpeaker && !saidStuck) {
                        std::fprintf(stderr,
                                     "[audio] the speaker is not taking sound; pacing by "
                                     "the clock\n");
                        saidStuck = true;
                    }
                    lastWaitingMs = waitingMs;
                }
                // One game frame per refresh when locked (see paceEvery),
                // counted in refreshes, not by the clock, so a wobble in the
                // clock never runs two frames or none. A refresh the console
                // was too late for (dt of two refreshes) is run on the next,
                // so the game keeps time as it did on the clock; the rate
                // control below is left with only the rates to take up.
                // Fast forward is paced by the clock, which is what makes it
                // fast.
                const bool locked = paceEvery > 0 && core.speed() <= 1.0;
                int due = 1;
                if (locked) {
                    paceCount += std::clamp(
                        static_cast<int>(std::lround(dt * paceScreenHz)), 1, 4);
                    due = std::min(paceCount / paceEvery, 3);
                    paceCount = due > 0 ? paceCount - due * paceEvery : paceCount;
                    if (paceCount >= paceEvery) paceCount %= paceEvery;
                }
                int ran = 0;
                if (!holdForSpeaker && due > 0) {
                    // RetroAchievements checks each frame run here, and only
                    // here: this is play (achievements.h).
                    core.setAfterFrame(ra::frame);
                    if (!locked) ran = core.runFor(dt);
                    else for (int i = 0; i < due; ++i) ran += core.runFrame();
                    core.setAfterFrame(nullptr);
                }
                // HOW SMOOTH IT WAS (#221): a frame shown with no new game frame
                // behind it is a repeat, and every run past the first in one
                // frame is a game frame nobody saw. Said with the [audio] line.
                if (due > 0) ++paceShown;
                if (due == 0) ran = 1;   // not a repeat: the screen is faster than the game
                if (ran == 0) ++paceRepeated;
                if (ran == 0 && holdForSpeaker) ++paceHeld;
                if (ran > 1) paceSkipped += static_cast<uint64_t>(ran - 1);
                paceDtSum += dt;
                // A SNAPSHOT EVERY HALF SECOND OF PLAY, counted in the game's
                // own frames so fast forward keeps them half a game-second
                // apart. Taken here, compressed on rewind's worker.
                const uint64_t every = static_cast<uint64_t>(
                    std::max(1.0, std::round(core.avInfo().fps * 0.5)));
                // Not while a disc change has the lid open (#162): the
                // history is dropped when it closes anyway.
                if (rewindAvailable == 1 && !session.discInserting &&
                    core.framesRun() >= rewindLastFrame + every) {
                    rewindLastFrame = core.framesRun();
                    const uint64_t t0 = SDL_GetTicksNS();
                    if (core.saveState(rewindRaw) && !rewindRaw.empty()) {
                        const double ms = (SDL_GetTicksNS() - t0) / 1e6;
                        if (ms > rewindWorstMs) rewindWorstMs = ms;
                        rewindKeep.offer(rewindRaw);
                    }
                }
            }
            // Frozen, rewinding or PlayStation 2 (which checks on PCSX2's
            // own thread): the session is kept alive without reading memory.
            if (frozen || rewinding || core.isPs2()) ra::idle();
            ra::sampleTick();
            core.uploadFrame();

            if (audioStream) {
                const std::vector<int16_t>& samples = core.drainAudio();
                // Fast forward's sound is discarded, not played fast: four
                // times the samples would only pile up behind the picture.
                // Rewind's too: it is a frame's sound played forwards.
                // A SAFETY NET, NOT THE PACING — 2026-09-27. The speaker sets
                // the pace above, and a batch bigger than the gate is QUEUED,
                // not trimmed: the gate then waits it out, which is what
                // RetroArch's blocking audio write does. Flycast's threaded
                // rendering (its default, "highly recommended") hands over a
                // loading burst of ~380 ms that drains within seconds; trimmed
                // at 250 ms it was audible skipping. So this only catches a
                // second of sound in one call, which is broken, not bursty. It
                // used to be the pacing, at 64 ms, and threw away the top of
                // every N64 burst as crackle. The history: When a game stalls, the speaker plays silence;
                // when it catches up, the sound it owed queued up behind and
                // never drained, so every stall made the sound a little later
                // for the rest of the session. Crazy Taxi 2 sat 200 ms behind
                // its picture this way even with Flycast held to the clock.
                // 64 ms is RetroArch's default audio latency
                // (DEFAULT_OUT_LATENCY), which is also what keeps RetroArch
                // from piling up. What does not fit is dropped: one small jump
                // in the sound instead of a delay that lasts. Measured against
                // this stream only, so it is the same on any machine.
                static constexpr double kMaxWaitingMs = 1000.0;
                static uint64_t droppedBytes = 0;
                static uint64_t droppedLogAt = 0;
                if (!samples.empty() && core.speed() <= 1.0 && !rewinding && !redrew) {
                    const double bytesPerMs =
                        4.0 * std::max(core.avInfo().sampleRate, 1.0) / 1000.0;
                    const int room = static_cast<int>(kMaxWaitingMs * bytesPerMs) -
                                     SDL_GetAudioStreamQueued(audioStream);
                    const int want = static_cast<int>(samples.size() * sizeof(int16_t));
                    const int take = std::max(0, std::min(want, room)) & ~3;
                    if (take > 0) SDL_PutAudioStreamData(audioStream, samples.data(), take);
                    droppedBytes += static_cast<uint64_t>(want - take);
                }
                // THE RATE CONTROL, while locked (#221). The lock sets the
                // sound's speed from the two rates; this bends it up to 0.5%
                // either way to hold the queue near kPaceTargetMs, which takes
                // up what the rates got wrong (the screen's reported rate, the
                // sound card's own clock, a missed refresh). RetroArch's
                // dynamic rate control, at its default (audio_rate_control_
                // delta 0.005), which is below what an ear can hear.
                // Measured after the frame's sound went in, so always at the
                // same point of the queue's rise and fall.
                if (paceEvery > 0 && !frozen && !rewinding && core.speed() <= 1.0) {
                    static constexpr double kPaceTargetMs = 40.0;
                    static constexpr double kRateControl = 0.005;
                    const double rate = std::max(core.avInfo().sampleRate, 1.0);
                    const double queuedMs =
                        SDL_GetAudioStreamQueued(audioStream) / (4.0 * rate) * 1000.0;
                    const double off =
                        std::clamp((queuedMs - kPaceTargetMs) / kPaceTargetMs, -1.0, 1.0);
                    paceRatio = static_cast<float>(paceBaseRatio * (1.0 + kRateControl * off));
                    SDL_SetAudioStreamFrequencyRatio(audioStream, paceRatio);
                }
                // Said at most every ten seconds, so a system that trips it
                // constantly shows up in the log rather than hiding in it.
                if (droppedBytes > 0 && SDL_GetTicksNS() >= droppedLogAt) {
                    const double rate = std::max(core.avInfo().sampleRate, 1.0);
                    std::fprintf(stderr, "[audio] dropped %.0f ms of sound to keep it in time\n",
                                 droppedBytes / (4.0 * rate) * 1000.0);
                    droppedBytes = 0;
                    droppedLogAt = SDL_GetTicksNS() + 10'000'000'000ull;
                }
                // HOW MUCH SOUND IS WAITING, every ten seconds of play, beside
                // any sound the safety net below had to drop. Together they
                // say whether a system keeps time. Added for Dreamcast on
                // 2026-09-27.
                static uint64_t audioLogAt = 0;
                const uint64_t nowNs = SDL_GetTicksNS();
                if (nowNs >= audioLogAt) {
                    if (audioLogAt != 0) {
                        const double rate = std::max(core.avInfo().sampleRate, 1.0);
                        const double queuedMs =
                            SDL_GetAudioStreamQueued(audioStream) / (4.0 * rate) * 1000.0;
                        // Only the queue: since the speaker paces every
                        // core, the core's output against runFor's clock
                        // stops meaning anything while it is being held.
                        std::fprintf(stderr, "[audio] %.0f ms waiting to play\n", queuedMs);
                        // MISSED is refreshes the console itself was too
                        // late for: the screen showed the last picture again,
                        // whatever the game did. Only known with a screen rate.
                        const long long missed =
                            paceScreenHz > 0.0
                                ? std::max(0LL, std::llround(paceDtSum * paceScreenHz) -
                                                    static_cast<long long>(paceShown) *
                                                        std::max(paceEvery, 1))
                                : 0;
                        // SOUND PER GAME FRAME against what the core's own rate
                        // and frame rate promise. A core above 0 makes more
                        // sound than real time and gets held by the speaker
                        // (N64 and Dreamcast, PROJECT.md #221).
                        static uint64_t lastAudio = 0, lastFrames = 0;
                        const uint64_t a = core.audioFramesTotal(), f = core.framesRun();
                        double perFrame = 0.0;
                        if (f > lastFrames && a >= lastAudio && lastFrames > 0 &&
                            core.avInfo().fps > 0.0)
                            perFrame = (static_cast<double>(a - lastAudio) / (f - lastFrames)) /
                                           (rate / core.avInfo().fps) -
                                       1.0;
                        lastAudio = a;
                        lastFrames = f;
                        if (paceShown > 0)
                            std::fprintf(stderr,
                                         "[pace] %llu frames shown at %.3f Hz: %llu repeated "
                                         "(%llu held for the speaker), %llu game frames "
                                         "skipped, %lld refreshes missed; sound %+.2f%%, "
                                         "sound per game frame %+.2f%%\n",
                                         static_cast<unsigned long long>(paceShown),
                                         paceShown / std::max(paceDtSum, 1e-9),
                                         static_cast<unsigned long long>(paceRepeated),
                                         static_cast<unsigned long long>(paceHeld),
                                         static_cast<unsigned long long>(paceSkipped),
                                         missed, (paceRatio - 1.0f) * 100.0f,
                                         perFrame * 100.0);
                        paceShown = paceRepeated = paceHeld = paceSkipped = 0;
                        paceDtSum = 0.0;
                    }
                    audioLogAt = nowNs + 10'000'000'000ull;
                }
            }
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
        // THE STATE PICKED ON THE LAUNCH SCREEN goes in once the core is
        // running, the same way Load latest state does, by id.
        if (pendingStateId && playing && cab::Core::shared().running() &&
            cab::Core::shared().framesRun() >= 1 && !stateLoad.running.load()) {
            beginLoadLatestState(stateLoad, session, liveClient, menuNotice, pendingStateId);
            pendingStateId = 0;
            if (stateLoad.running.load()) stateHoldWaiting = true;
        }
        // ONE OF THIS CONSOLE'S OWN STATES, picked on the game's page (#88):
        // read off the disk, so there is nothing to wait for.
        if (!pendingStatePath.empty() && playing && cab::Core::shared().running() &&
            cab::Core::shared().framesRun() >= 1 && !stateLoad.running.load()) {
            const std::vector<uint8_t> bytes = cab::readBytes(pendingStatePath);
            const bool ok = !bytes.empty() && loadStateOnItsDisc(session, pendingStatePath, bytes);
            std::fprintf(stderr, "[state] picked on the game's page: %s -> %s\n",
                         pendingStatePath.c_str(), ok ? "restored" : "REFUSED");
            menuNotice.say(ok ? "State loaded" : stateRefusedText(),
                           ok ? Tone::Done : Tone::Problem);
            stateLoad.loaded = ok;   // and it waits on that frame, as any load does
            pendingStatePath.clear();
        }
        if (pendingResume && playing && cab::Core::shared().running() &&
            cab::Core::shared().framesRun() >= 1 && !stateLoad.running.load()) {
            pendingResume = false;
            if (session.snapshots) {
                std::fprintf(stderr, "[resume] loading the newest state, if there is one\n");
                beginLoadLatestState(stateLoad, session, liveClient, menuNotice, 0, true);
                if (stateLoad.running.load()) stateHoldWaiting = true;
            }
        }
        if (stateLoad.loaded) {
            stateLoad.loaded = false;
            rewindKeep.reset();   // the history led somewhere else
            ra::stateLoaded();    // and so did what each achievement was waiting for
            rewindLastFrame = cab::Core::shared().framesRun();
            closeOverlay();
            stateHold = true;
            stateHoldWaiting = false;
        }
        // A launch-screen state that did not load: the game just goes on.
        if (stateHoldWaiting && !stateLoad.running.load() && !stateLoad.ready.load())
            stateHoldWaiting = false;
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

        // CHANGE DISC (#162): Insert's lid closes once the game has run with
        // it open; Start over starts the game again once it has gone.
        pumpInsert();
        if (relaunchRomId && !playing && !exitPending && !launchJob.busy()) {
            const int id = relaunchRomId;
            relaunchRomId = 0;
            if (!launchById(id)) gNextStartDisc = 0;
        }

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
        if (const int outcome = uploader.stateOutcome.exchange(0); outcome != 0)
            menuNotice.say(outcome == 1 ? "Saved to RomM"
                                        : "Saved. Will upload when RomM is back",
                           outcome == 1 ? Tone::Done : Tone::Info);
        // Reset the moment it stops, so the next launch starts its own clock
        // and a second press cannot inherit the first one's patience.
        launchJob.busyFor = launchJob.busy() ? launchJob.busyFor + dt : 0.0f;
        overlayFade.tick(dt);
        overlayFocus.tick(dt);
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
            // Pages of a big grid that arrived behind the first one. Applied
            // here because this thread owns the store and the screen.
            if (here() == Screen::Grid) {
                std::vector<romm::Game> got;
                {
                    std::lock_guard<std::mutex> lk(gridFill.m);
                    got.swap(gridFill.arrived);
                }
                if (!got.empty()) {
                    std::vector<int> added;
                    added.reserve(got.size());
                    screens::Tile* t = nullptr;
                    for (auto& v : {&platformTiles, &collectionTiles})
                        for (screens::Tile& x : *v)
                            if (x.query == gridFill.tileQuery) { t = &x; break; }
                    for (const auto& g : got) {
                        if (!catalog::playable(g) &&
                            !catalog::shownGreyed(catalog::coverageFor(g).support))
                            continue;
                        const int i = appendGame(lib, g);
                        added.push_back(i);
                        if (!t) continue;
                        // Playable before the greyed ones, as the first page.
                        if (cards[static_cast<size_t>(i)].unavailable.empty()) {
                            auto at = std::find_if(t->cards.begin(), t->cards.end(), [&](int c) {
                                return !cards[static_cast<size_t>(c)].unavailable.empty();
                            });
                            t->cards.insert(at, i);
                        } else {
                            t->cards.push_back(i);
                        }
                    }
                    gridScreen.append(added, cards);
                }
                gridScreen.setLoadingMore(gridFill.running.load());
            }

            drainCovers();

            sizeSearchResults();
            tickDetail(dt);
        }
        retargetHomePress();

        // OVER PCSX2'S OWN WINDOW (#226) this window draws only what is over
        // the game: the pause menu, a notice, the curtain, the dim. With none
        // of them showing it draws nothing at all, as over an emulator of its
        // own, so gamescope shows PCSX2's picture straight and this loop does
        // not render 4K for nobody; the game above (pads, sound) still runs
        // every pass. The menu has the controllers while it is up, so a press
        // meant for it never reaches the game.
        if (ps2Window && playing) {
            const bool over = overlayOpen || overlayFade.value() > 0.001f ||
                              curtain.value() > 0.001f || menuNotice.alpha() > 0.001f ||
                              raToast.alpha() > 0.001f || dimLayer.value() > 0.001f;
            if (overlayOpen != ps2InputOurs) {
                cab::overlaywin::mark(window, /*takeInput=*/overlayOpen);
                ps2InputOurs = overlayOpen;
            }
            if (over) {
                ps2ClearFrames = 30;
            } else {
                if (ps2ClearFrames > 0) {
                    --ps2ClearFrames;
                    glBindFramebuffer(GL_FRAMEBUFFER, 0);
                    glDisable(GL_SCISSOR_TEST);
                    glClearColor(0, 0, 0, 0);
                    glClear(GL_COLOR_BUFFER_BIT);
                    SDL_GL_SwapWindow(window);
                } else {
                    // Until the next press or 4 ms: the pads are read again
                    // that often, which is sooner than PCSX2 asks for them.
                    SDL_WaitEventTimeout(nullptr, 4);
                }
                continue;
            }
        }

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
            // OURS ONLY WHILE A BUILT-IN GAME RUNS (#212). Paused over a separate
            // emulator, gameUp is true and the core still holds the LAST
            // built-in game's picture: Burnout 3 was drawn behind the pause
            // menu over Mario Kart in Eden and stayed after Resume. That
            // emulator draws its own picture; this window draws none of it.
            if (playing && core.texture() && core.frameWidth() > 0) {
                // Integer-scaled and centred. A Game Boy is 160x144 and its
                // pixels were each a deliberate choice; scaling by 6.4 makes
                // some of them twice the size of their neighbours, which is
                // visible from a sofa and looks like a fault. Phase 8 can offer
                // the non-integer option; the default should be honest.
                const float srcW = static_cast<float>(core.frameWidth());
                const float srcH = static_cast<float>(core.frameHeight());
                //
                // A VERTICAL ARCADE BOARD TURNS THE PICTURE AND THE LAYOUT
                // WITH IT. Its monitor was bolted into the cabinet sideways,
                // so the board renders sideways and asks for a quarter turn.
                // After the turn the picture is TALL: the rows running down
                // the screen are the source's columns and the aspect is the
                // inverse of the stored one.
                const bool quarterTurn = core.rotatedQuarterTurn();
                //
                // AND A TURNED PICTURE IGNORES THE CORE'S DECLARED ASPECT.
                // This looks like throwing away the better answer and is the
                // opposite: FBNeo reports a vertical board's aspect ALREADY
                // turned — 0.75 for DoDonPachi DaiOuJou, which is the 3:4 of
                // the cabinet's tube on its side, not the 448x224 sitting in
                // the framebuffer. Inverting that would apply the turn twice
                // and stretch the picture. Cabinet shipped it the wrong way
                // round first and the comment in `aspectFitVertices` is the
                // record: it "stretched every vertical game".
                //
                // The declared aspect is still what a picture with square
                // pixels needs and Saturn is unplayable without it, so it
                // keeps winning everywhere else. Nothing is lost by dropping
                // it here, for the reason Cabinet gives: the platforms whose
                // pixels are not square never rotate, and the boards that
                // rotate are square-pixel.
                const float declaredAspect = quarterTurn ? 0.0f : core.avInfo().aspectRatio;
                const float aspect = declaredAspect > 0 ? declaredAspect : srcW / srcH;
                //
                // EXCEPT for a hardware-rendered core, where integer scaling
                // is the wrong idea rather than a stricter one. A Dreamcast's
                // output is already a rendering of a 3D scene at whatever
                // internal resolution the core was asked for — there is no
                // grid of deliberate pixels to preserve — and at 3x internal
                // resolution the frame is 1920x1440, where flooring to an
                // integer gives zero, clamps to one, and draws 360 rows off
                // the bottom of the screen.
                //
                // Everything from here is in terms of the picture as SHOWN
                // rather than as stored, which is the only version of it the
                // screen has an opinion about.
                const float shownAspect = quarterTurn ? 1.0f / aspect : aspect;
                const float shownRows = quarterTurn ? srcW : srcH;
                //
                // AND A TURNED PICTURE FILLS THE HEIGHT RATHER THAN INTEGER
                // SCALING. MMagTech's call, 2026-09-19, asked because nothing
                // makes a vertical game fill a horizontal screen without
                // lying and the two honest answers differ. A 240x320 board
                // integer-scaled into 1080 points gives 3x, a 720-point-tall
                // window with 180 points of dead space above and below it on
                // top of the pillarboxing that is already unavoidable; filling
                // the height gives 3.375x and the largest true-shaped picture
                // the panel can show. The dot grid argument that earns integer
                // scaling a Game Boy is worth less here than the 33% of the
                // screen it costs.
                // EVERY SYSTEM FILLS THE HEIGHT NOW — changed 2026-09-21.
                //
                // MMagTech: *"all systems should go to top and bottom of the
                // screen."* Integer scaling was the default and it cost real
                // screen: a 320x240 core on a 1080 canvas scaled by 4 rather
                // than 4.5, so a 960-point picture sat inside 120 points of
                // black bar for no reason a person watching it would accept.
                //
                // WHAT IS GIVEN UP, because it is not nothing. An integer scale
                // makes every source pixel exactly the same size; 4.5 makes
                // some of them five rows tall and some four. On a Game Boy at
                // 160x144 that is a 7.5x scale and the unevenness is visible if
                // you go looking for it. The argument for integer scaling is
                // that those pixels were each a deliberate choice; the argument
                // against is that a third of the screen is a bigger price, and
                // it is the one MMagTech is paying.
                //
                // THE VERTICAL ARCADE BOARDS ALREADY WORKED THIS WAY and the
                // reasoning recorded for them is the same one, reached first:
                // "the dot grid argument that earns integer scaling a Game Boy
                // is worth less here than the 33% of the screen it costs". This
                // is that decision applied everywhere rather than in one place.
                //
                // `--integer-scale` puts it back, for comparing the two on a
                // television, which is the only place the question can be
                // settled.
                const bool integerScale =
                    integerScaling && !core.hardwareRendered() && !quarterTurn;
                float scale = std::min(ui::kCanvasWidth / (shownRows * shownAspect),
                                       ui::kCanvasHeight / shownRows);
                if (integerScale) {
                    // COUNTED IN THE SCREEN'S OWN ROWS, not the 1080-point
                    // canvas (2026-10-02). On a 2160-line TV the canvas made
                    // the steps twice as coarse: 240 rows got 4x, 960 of 1080
                    // points with bars, where the real panel takes 9x and
                    // fills exactly. Shown to MMagTech to decide again.
                    const float perPoint =
                        dh > 0 ? static_cast<float>(dh) / ui::kCanvasHeight : 1.0f;
                    float rows = std::floor(scale * perPoint);
                    if (rows < 1.0f) rows = 1.0f;
                    scale = rows / perPoint;
                }
                // Real screen pixels per canvas point, for a screen look's
                // output size: read here, before `dh` means the picture.
                const float physPerPoint =
                    dh > 0 ? static_cast<float>(dh) / ui::kCanvasHeight : 1.0f;
                // ON WHOLE SCREEN PIXELS, edges and size. A screen look is
                // drawn at a whole number of pixels and put on screen nearest
                // texel to pixel; a rectangle a fraction wider or off by half
                // a pixel repeated one column to make up the difference. On
                // the TV that is invisible, but it moved a CRT mask by one
                // pixel from there on, and Remote Play's shrink of the 4K
                // picture turned the shift into a green left half and a
                // purple right one, split at column 1924 (Double Dragon,
                // crt-easymode, 2026-10-07, #286).
                auto snap = [physPerPoint](float v) {
                    return std::round(v * physPerPoint) / physPerPoint;
                };
                const float dh = snap(shownRows * scale);
                const float dw = snap(shownRows * scale * shownAspect);
                const float px = snap((ui::kCanvasWidth - dw) * 0.5f);
                const float py = snap((ui::kCanvasHeight - dh) * 0.5f);
                // Where the picture actually sits in that texture. A
                // software core answers "all of it, the right way up"; a
                // hardware core answers a corner of a larger target with its
                // rows the other way round, and neither the layout above nor
                // the draw below has to know which.
                float u0, v0, u1, v1;
                core.frameUV(u0, v0, u1, v1);
                // Said once per game. The picture's geometry is four numbers
                // that have to agree — what the core hands back, what it says
                // the shape is, the quad the layout builds, and the corner of
                // the texture that is sampled — and when the picture comes out
                // the wrong shape there is no way from outside to tell which
                // of the four is lying.
                {
                    static int saidFor = -1;
                    if (saidFor != session.romId) {
                        saidFor = session.romId;
                        std::fprintf(stderr,
                                     "[picture] core %gx%g aspect %.4f%s -> quad %.0fx%.0f "
                                     "(%.4f) at %.0f,%.0f  uv %.4f,%.4f..%.4f,%.4f\n",
                                     srcW, srcH, core.avInfo().aspectRatio,
                                     quarterTurn ? " TURNED" : "", dw, dh, dw / dh, px, py,
                                     u0, v0, u1, v1);
                    }
                }
                // Opaque, always. A game's frame is a picture, and whatever is
                // in its alpha channel is the emulated machine's own state
                // rather than a compositing instruction. Found on PPSSPP:
                // Lumines leaves the PSP framebuffer's alpha at nearly zero,
                // the blend took it literally, and the whole 1920x1080 capture
                // peaked at RGB (4,4,4) — a picture that was there all along
                // and read as a core that renders black.
                //
                // The turn goes to the DRAW and the shape goes to the layout,
                // and they are two different things. frameUV has already said
                // where the picture is in the texture and which way up its
                // rows are; rotation says how the picture it found is turned,
                // and the two compose — which is what a hardware-rendered
                // vertical board needs.
                // THE SCREEN LOOK (#122): RetroArch's shader files over the
                // picture, sized to the real pixels it covers on the screen,
                // in the game's own orientation and turned afterwards, as
                // RetroArch does. None chosen, or one that failed to load,
                // draws the picture as before.
                screenfx::Chain& look = screenfx::shared();
                look.use(screenfx::presetFor(session.platformSlug));
                GLuint looked = 0;
                if (look.active()) {
                    const int rot = static_cast<int>(core.rotation());
                    const int qw = static_cast<int>(std::lround(dw * physPerPoint));
                    const int qh = static_cast<int>(std::lround(dh * physPerPoint));
                    const bool turned = rot == 1 || rot == 3;
                    looked = look.run(core.texture(), u0, v0, u1, v1, static_cast<int>(srcW),
                                      static_cast<int>(srcH), turned ? qh : qw,
                                      turned ? qw : qh);
                }
                if (looked)
                    ui::drawImageTexture(renderer, looked, px, py, dw, dh, 0, 0, 1, 1, true,
                                         static_cast<int>(core.rotation()));
                else
                    ui::drawImageTexture(renderer, core.texture(), px, py, dw, dh, u0, v0, u1,
                                         v1, true, static_cast<int>(core.rotation()));

                // The glow goes over the bars, not under the picture: it is
                // drawn after, and its shader discards inside the picture rect,
                // so no game pixel is ever covered. An integer-scaled handheld
                // on a 4K set is mostly dead space, which is exactly the case
                // this exists for.
                //
                // It reshapes itself for a vertical board for free, and that
                // is worth saying because it looks like it should need work.
                // The shader ramps from the picture's edge to the screen's in
                // each direction separately, so handing it a tall rect lights
                // two wide bars at the sides and nothing above or below, where
                // a picture that fills the height leaves no room to ramp
                // across. The rect is the whole interface.
                renderer.drawBiasGlow(px, py, dw, dh, glowPeak);
            }

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

        // THE WAIT AFTER A STATE LOADS: the game's frozen picture, a little
        // darker, and the prompt low on it. Not while the pause menu is up.
        if (playing && stateHold && overlayFade.value() < 0.001f) {
            renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                   ui::Color::black(0.35f)});
            drawPressPrompt(renderer, text, sc, "Press", "to continue",
                            ui::kCanvasHeight * 0.82f, 1.0f);
        }

        // The overlay's scrim belongs to the WORLD, not to the overlay, so it
        // is drawn before the scene is presented. Put it after and the panel's
        // glass samples the UN-dimmed picture underneath: over a bright game
        // the panel comes out milky and its own labels stop being readable,
        // which is exactly what happened the first time. Dimming the picture is
        // what a scrim is for; the glass should be blurring the dimmed thing.
        if (overlayFade.value() > 0.001f) {
            renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                   ui::Color::black(0.55f * overlayFade.value())});
        }

        renderer.presentScene();

        drawScreenGlass(gameUp, sc);

        // ---- The in-game overlay -------------------------------------------
        //
        // No compositing trick: the frontend owns the frame loop, so this is a
        // scrim, a panel and some buttons drawn over whatever the game just
        // rendered. That is the direct payoff of hosting cores in process
        // rather than launching them.
        //
        // Drawn after presentScene because the panel is glass, and it reads the
        // game's own picture through itself.
        const float ovl = overlayFade.value();
        // NOT UNDER A QUESTION ASKED FROM IT (Change disc, #162): the panel
        // takes its place, and the menu's words showed through the glass.
        if (ovl > 0.001f && !(playing && choiceScreen.isOpen())) {
            // The 64 is the padding above and below the buttons. There used to
            // be 44 more, reserved for the line the menu answered with; that
            // answer is the notification pill now, so the panel is just its
            // buttons and has no empty band at the foot.
            const int ovN = ovCount();
            const float panelH = static_cast<float>(ovN) * kOverlayButtonHeight +
                                 static_cast<float>(ovN - 1) * kOverlayButtonGap + 64.0f;
            const float px = (ui::kCanvasWidth - kOverlayPanelWidth) * 0.5f;
            // Rises slightly as it arrives rather than only fading: a panel that
            // just materialises reads as a glitch.
            const float py = (ui::kCanvasHeight - panelH) * 0.5f + (1.0f - ovl) * overlayRise;
            // SOLID, NOT GLASS. See design.h, kOverlayPanelFill, for why — in
            // short, glass blurs the console's own scene texture, and on the
            // composited path the game is not in it. A panel that is frosted on
            // one path and flat on the other is the exact difference this
            // console cannot afford between a Mega Drive and a PlayStation 2.
            //
            // The shadow is what separates it from the game now that the blur
            // does not, and it costs nothing on either path.
            ui::Color panelFill = look::surface();
            panelFill.a = kOverlayPanelFill * ovl;
            ui::Rect panel{px, py, kOverlayPanelWidth, panelH,
                           kOverlayPanelRadius, panelFill};
            // Lit from above: a little lighter at the head, denser and darker
            // at the foot, with a highlight on the top edge only.
            panel.gradient = true;
            panel.fillBottom =
                ui::Color{panelFill.r * kOverlayPanelBottomDarken,
                          panelFill.g * kOverlayPanelBottomDarken,
                          panelFill.b * kOverlayPanelBottomDarken,
                          kOverlayPanelFillBottom * ovl};
            panel.edgeLight = ui::Color::white(kOverlayPanelEdgeLight * ovl);
            panel.border = kOverlayPanelBorder;
            panel.borderColor = ui::Color::white(kOverlayPanelBorderAlpha * ovl);
            panel.shadowBlur = kOverlayPanelShadowBlur;
            panel.shadowOffsetY = kOverlayPanelShadowY;
            panel.shadowColor = ui::Color::black(kOverlayPanelShadowAlpha * ovl);
            renderer.draw(panel);

            for (int i = 0; i < ovN; ++i) {
                const bool on = (i == overlaySlot);
                const float f = on ? overlayFocus.value() : 0.0f;
                // 1.04, the pause menu's own tier. A full-width button growing
                // a cover's tenth would run into its neighbours.
                const float sc2 = 1.0f + f * (kOverlayFocusScale - 1.0f);
                const float bw = (kOverlayPanelWidth - 64.0f) * sc2;
                const float bh = kOverlayButtonHeight * sc2;
                const float bx = px + (kOverlayPanelWidth - bw) * 0.5f;
                const float by = py + 32.0f +
                                 i * (kOverlayButtonHeight + kOverlayButtonGap) -
                                 (bh - kOverlayButtonHeight) * 0.5f;

                // FOCUS IS A RIM, the same one every card and pill in this
                // console uses. See design.h — a light-filled bar was tried and
                // it both shouted and invented a second focus idiom for one
                // screen.
                ui::Rect btn{bx, by, bw, bh, kOverlayButtonRadius,
                             ui::Color::white(
                                 (kOverlayButtonRestFill +
                                  f * (kOverlayButtonFocusFill - kOverlayButtonRestFill)) * ovl)};
                if (f > 0.0f) {
                    btn.border = f * kFocusRimWidth;
                    // FADED WITH THE PANEL. It was the one part of the menu
                    // that did not follow `ovl`, so the focus rim arrived at
                    // full strength on the first frame, floating over a panel
                    // that had not faded in yet — MMagTech on the Power menu,
                    // 2026-09-22: *"it opens in pieces."*
                    btn.borderColor = ui::palette::kFocusRim;
                    btn.borderColor.a *= ovl;
                    btn.shadowBlur = kOverlayButtonFocusShadowBlur;
                    btn.shadowOffsetY = kOverlayButtonFocusShadowY;
                    btn.shadowColor =
                        ui::Color::black(kOverlayButtonFocusShadowAlpha * f * ovl);
                }
                renderer.draw(btn);

                const char* label = ovLabel(i);
                // CHANGE DISC WITH ITS VALUE, written as a choice row is
                // ("Change disc: Disc 2 of 4") with no arrows: A opens the
                // question panel, Left and Right do nothing on it.
                std::string discLine;
                if (!powerMenu && pauseItems[i] == OvDisc) {
                    discLine = std::string(label) + ": " +
                               disc::ofText(session.disc, disc::highest(session.discs));
                    label = discLine.c_str();
                }
                // A CHOICE ROW, CENTRED LIKE EVERY OTHER BUTTON: "Picture
                // quality: Balanced" on one line, and under focus an arrow at
                // each edge of the button, bright where left or right still
                // goes somewhere and faint at the end, as Settings shows it.
                if (PauseChoice* choice = powerMenu ? nullptr : choiceAt(i)) {
                    const ui::TextStyle st = ui::TextStyle::Title3;
                    const ui::Color c = ui::Color::white(
                        (kOverlayButtonRestText + f * (1.0f - kOverlayButtonRestText)) * ovl);
                    const float baseY = by + (bh - text.lineHeight(st, sc)) * 0.5f +
                                        text.ascent(st, sc);
                    const std::string line =
                        choice->label + ": " + choice->values[choice->current];
                    const float lw = text.measure(line.c_str(), st, sc);
                    text.draw(renderer, line.c_str(), bx + (bw - lw) * 0.5f, baseY, st, c, sc);
                    if (f > 0.0f) {
                        const char* kNext = "\xE2\x80\xBA";
                        const char* kBack = "\xE2\x80\xB9";
                        const float aw = text.measure(kNext, st, sc);
                        const bool canRight =
                            choice->current + 1 < static_cast<int>(choice->values.size());
                        const bool canLeft = choice->current > 0;
                        text.draw(renderer, kBack, bx + 28.0f, baseY, st,
                                  ui::Color::white((canLeft ? 0.70f : 0.18f) * f * ovl), sc);
                        text.draw(renderer, kNext, bx + bw - 28.0f - aw, baseY, st,
                                  ui::Color::white((canRight ? 0.70f : 0.18f) * f * ovl), sc);
                    }
                    continue;
                }
                const float tw = text.measure(label, ui::TextStyle::Title3, sc);
                // Dark on the light row, light on the dark ones. Crossfaded by
                // the focus animation so the text does not snap between them.
                // Note Color::white(a) is WHITE AT ALPHA a, not a grey. A
                // build that wanted dark text asked for white(0.06) and got a
                // six-per-cent white, which was invisible. It reads like a
                // brightness and is not one.
                const ui::Color labelColor = ui::Color::white(
                    (kOverlayButtonRestText + f * (1.0f - kOverlayButtonRestText)) * ovl);
                text.draw(renderer, label, bx + (bw - tw) * 0.5f,
                          by + (bh - text.lineHeight(ui::TextStyle::Title3, sc)) * 0.5f +
                              text.ascent(ui::TextStyle::Title3, sc),
                          ui::TextStyle::Title3, labelColor, sc);
            }

        }

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

        // ---- The curtain, over everything --------------------------------
        //
        // Last, and over the keyboard and the in-game overlay as well: it is
        // not part of any screen, it is the screen going away. See design.h.
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

    // A SHUTDOWN MID-GAME LEAVES THE WAY EXIT TO HOME DOES — docs/PROJECT.md,
    // open question 10b. Until 2026-09-22 it did not: the loop ended, the code
    // below unloaded the core, and nothing uploaded, so pressing the power
    // button during a game threw away whatever the game had saved since it
    // started. finishExit is the one way out of a game and it carries every
    // save class there is — battery, directory, file — so going through it is
    // what makes this true for every emulator, present and future, rather
    // than for the ones somebody remembered. The uploads it queues are
    // drained below, before the process ends.
    //
    // Not while the machine is still being built: nothing has been played, and
    // finishExit's own caller waits for exactly that case rather than calling
    // it. And never for a capture, which is not a person's evening.
    if (playing && askedToStop && !shotMode && cab::Core::shared().running()) {
        std::fprintf(stderr, "[shutdown] leaving the game the way Exit to Home does\n");
        finishExit();
    }

    // An emulator of its own outlives this process unless it is closed: it has
    // a session of its own. Asked, then made, inside its grace period.
    if (standaloneRun.active()) {
        standaloneRun.stop();
        while (standaloneRun.poll()) SDL_Delay(50);
        finishStandalone();
    }
    if (playing) {
        cab::Core& core = cab::Core::shared();
        const double realtime = core.audioFramesTotal() / core.avInfo().sampleRate;
        std::fprintf(stderr,
                     "[core] %llu frames, %llu audio frames = %.2fs of emulated time\n",
                     static_cast<unsigned long long>(core.framesRun()),
                     static_cast<unsigned long long>(core.audioFramesTotal()), realtime);
        // What the bridge between Vulkan and GL cost, when there was one. The
        // whole question "does handing a Vulkan picture to a GLES UI hurt"
        // gets an answer here rather than an opinion. See vkhost.h.
        {
            uint64_t vframes = 0;
            double vseconds = 0.0;
            cab::vk::presentCost(vframes, vseconds);
            if (vframes > 0)
                std::fprintf(stderr,
                             "[vulkan] %llu pictures crossed into GL in %.3fs "
                             "= %.3f ms each\n",
                             static_cast<unsigned long long>(vframes), vseconds,
                             1000.0 * vseconds / static_cast<double>(vframes));
            if (vframes > 0) {
                double rec = 0.0, wait = 0.0;
                cab::vk::presentCostSplit(rec, wait);
                std::fprintf(stderr,
                             "[vulkan]   of which %.3f ms recording the copy and "
                             "%.3f ms waiting for the GPU\n",
                             1000.0 * rec / static_cast<double>(vframes),
                             1000.0 * wait / static_cast<double>(vframes));
            }
        }
        // Whether the second brake did anything. It is reported rather than
        // assumed, because a brake nobody can see is indistinguishable from a
        // brake that is not there — and this one has never engaged on either
        // PSP game measured. See Core::runFor.
        if (core.governorSkips() > 0)
            std::fprintf(stderr, "[core] the audio governor held it back %llu times\n",
                         static_cast<unsigned long long>(core.governorSkips()));
        core.unload();
    }
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

void App::openPs2Window() {
    if (gPs2Copy || !std::getenv("GAMESCOPE_WAYLAND_DISPLAY")) return;
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || SDL_strcmp(driver, "x11") != 0) return;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    ps2Window = SDL_CreateWindow("PlayStation 2", w, h, SDL_WINDOW_FULLSCREEN);
    if (!ps2Window) {
        std::fprintf(stderr, "[ps2] no window of its own (%s); copying frames\n",
                     SDL_GetError());
        return;
    }
    const auto xid = static_cast<unsigned long>(SDL_GetNumberProperty(
        SDL_GetWindowProperties(ps2Window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    const char* display = std::getenv("DISPLAY");
    SDL_GetWindowSizeInPixels(ps2Window, &w, &h);
    if (!xid || !ps2::setWindow(display ? display : "", xid, static_cast<unsigned>(w),
                                static_cast<unsigned>(h))) {
        std::fprintf(stderr, "[ps2] this PS2 library cannot take a window; copying frames\n");
        SDL_DestroyWindow(ps2Window);
        ps2Window = nullptr;
        return;
    }
    // This window steps over it, as for an emulator of its own: hidden,
    // marked, shown, because gamescope classifies a window when it maps.
    SDL_HideWindow(window);
    cab::overlaywin::mark(window, /*takeInput=*/false);
    SDL_ShowWindow(window);
    ps2InputOurs = false;
    renderer.setTransparentBackground(true);
    ps2ClearFrames = 30;
    std::fprintf(stderr, "[ps2] its own window 0x%lx, %dx%d\n", xid, w, h);
}

// After the game has stopped, which is when PCSX2 has let go of it.
void App::closePs2Window() {
    if (!ps2Window) return;
    SDL_DestroyWindow(ps2Window);
    ps2Window = nullptr;
    SDL_HideWindow(window);
    cab::overlaywin::unmark(window);
    SDL_ShowWindow(window);
    renderer.setTransparentBackground(false);
    ps2InputOurs = false;
}

bool App::remoteOnlyHere() {
    return playing && sessionNeedsRemote && players::streaming();
}

// The choice row at pause menu slot `slot`, or nullptr.
auto App::choiceAt(int slot) -> PauseChoice* {
    int n = 0;
    for (int i = 0; i < slot && i < static_cast<int>(pauseItems.size()); ++i)
        if (pauseItems[i] == OvChoice) ++n;
    if (slot < 0 || slot >= static_cast<int>(pauseItems.size()) ||
        pauseItems[slot] != OvChoice || n >= static_cast<int>(pauseChoices.size()))
        return nullptr;
    return &pauseChoices[n];
}

int App::ovCount() {
    return static_cast<int>(powerMenu ? powerItems.size() : pauseItems.size());
}

// UP FROM THE TOP IS THE BOTTOM, and Down from the bottom the top: the
// pause menu is short, and Exit to Home is last (MMagTech on the TV,
// 2026-10-06). The same for the Power menu, which is the same panel.
void App::ovStep(int delta) {
    const int n = ovCount();
    if (n <= 0) return;
    overlaySlot = ((overlaySlot + delta) % n + n) % n;
    // The Power menu on Home moves with a sound; the pause menu does not
    // (#389, and powerCue further down).
    if (powerMenu && !playing) sound::play(sound::Cue::Move);
}

auto App::ovLabel(int i) -> const char* {
    if (!powerMenu) return kOverlayLabels[pauseItems[i]];
    switch (powerItems[i]) {
        case PwResume: return "Resume";
        case PwCancel: return "Cancel";
        // "Sleep", not "Rest": Rest is PlayStation's word alone, and
        // Switch, Xbox, SteamOS, Windows and macOS all say Sleep.
        // MMagTech, 2026-09-22.
        case PwRest: return "Sleep";
        case PwRestart: return "Restart";
        case PwPowerOff: return "Power off";
        case PwSteam: return "Switch to Steam";
    }
    return "";
}

auto App::launchById(int romId) -> bool {
    // PRESSING PLAY ON SOMETHING ALREADY COMING JOINS IT — 2026-09-21.
    //
    // A download started by "Download and keep" runs in the background and
    // leaves the person free to browse. Pressing Play on that same game
    // while it is in flight means "and I want to play it when it lands",
    // which is one flag on the job that is already fetching it — not a
    // second fetch of the same bytes, and not a refusal.
    //
    // Any OTHER game is still refused while one is in flight: the fetch is
    // one worker and one entry path, and a second would need both.
    if (launchJob.busy()) {
        if (launchJob.romId == romId && !launchJob.playWhenReady) {
            launchJob.playWhenReady = true;
            launchJob.startedOn = static_cast<int>(here());
            std::fprintf(stderr, "[launch] joining the download of %s\n",
                         launchJob.title.c_str());
            return true;
        }
        // SAID, NOT SILENT (#184): a press that did nothing looked like a
        // broken button. One download at a time until there is a queue.
        menuNotice.say(launchJob.title + " is still downloading", Tone::Info);
        return false;    // one at a time
    }
    const romm::Game* g = nullptr;
    for (const auto& x : games) if (x.id == romId) { g = &x; break; }
    if (!g) { std::fprintf(stderr, "[launch] no game with id %d\n", romId); return false; }

    std::string lerr;
    if (!beginLaunch(launchJob, liveClient, *g, coreDir, &lerr)) {
        std::fprintf(stderr, "[launch] %s\n", lerr.c_str());
        return false;
    }
    launchJob.startedOn = static_cast<int>(here());
    std::fprintf(stderr, "[launch] %s (%s) via %s\n", g->name.c_str(),
                 g->platformName.c_str(), launchJob.coreName.c_str());
    return true;
}

// What a screen asked for, and whether the app can do it. A screen never
// reaches the disk, the network or a core; it returns one of these.
// Declared before `apply` because `apply` is what it calls, and defined
// after it for the same reason. std::function rather than a lambda, which
// is what lets the two refer to each other at all.

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

void App::apply(const screens::Result& res) {
    switch (res.action) {
        case screens::Action::None:
            break;
        case screens::Action::Back:
            if (accountsOpen) {
                // Closes the panel and puts focus back ON THE CHIP, which
                // is where it came from. Dropping focus into the screen
                // underneath would lose the place the person was at.
                accountsOpen = false;
                barFocused = true;
                barSlot = BarAccount;
                sound::play(sound::Cue::Back);
            } else if (stack.size() > 1 && here() == Screen::Detail && !shotMode) {
                pendingDetailBack = true;
                sound::play(sound::Cue::Back);
            } else if (here() == Screen::Search) {
                // B FROM SEARCH'S RESULTS goes Home the way B from its keys
                // does, keyboard and all. Popping the screen alone left the
                // keyboard, still open below the results, over Home with
                // the search in it (MMagTech on the TV, 2026-10-06). The
                // same steps as goToDestination(0), declared further down.
                if (keyboard.isOpen()) keyboard.cancel();
                searchScreen.setFocused(false);
                stack.clear();
                stack.push_back(Screen::Home);
                sound::play(sound::Cue::Back);
            } else if (stack.size() > 1) {
                stack.pop_back(); sound::play(sound::Cue::Back);
            } else sound::play(sound::Cue::Edge);
            break;
        case screens::Action::OpenTile: {
            const auto& tiles = libraryScreen.visible();
            if (res.value < 0 || res.value >= static_cast<int>(tiles.size())) break;
            // THE GAMES ARE FETCHED HERE AND NOWHERE EARLIER — open
            // question 28. Boot draws this screen having fetched no games
            // at all; a grid costs one request at the moment somebody
            // walks into it, and nothing at all for the systems they never
            // open. The second visit costs nothing: loadTileGames sees a
            // membership it already filled and returns.
            const bool isCollection = libraryScreen.tab() != 0;
            const std::string tileQuery = tiles[res.value].query;
            auto& own = isCollection ? collectionTiles : platformTiles;
            screens::Tile* t = nullptr;
            for (auto& x : own) if (x.query == tileQuery) { t = &x; break; }
            if (!t) break;
            bool more = false;
            if (t->cards.empty()) {
                // ONE PAGE, AND IT BLOCKS FOR THAT. The person has chosen
                // a system and is waiting for one request rather than for
                // the whole platform — 0.19 s for 141 games here, and the
                // same 0.19 s for a platform of thirty thousand.
                loadTileGames(liveClient, lib, *t, &more);
                libraryScreen.learnedTile(tileQuery, t->detail, t->cover);
            }
            if (t->cards.empty()) {
                // Nothing came back, or nothing in it can be played here.
                // Refusing is better than opening onto an empty grid with
                // no reason on it.
                sound::play(sound::Cue::Edge);
                break;
            }
            gridScreen.open(t->title, t->cards, cards);
            gridScreen.setLoadingMore(more);
            // ONE FILLER AT A TIME. Walking into a second platform stops
            // the first: nobody is looking at it any more, and two of
            // these would be appending into two different tiles at once.
            gridFill.stop();
            if (more) {
                gridFill.tileQuery = tileQuery;
                gridFill.running.store(true);
                const std::string filter = tileQuery;
                const size_t from = t->cards.size();
                gridFill.th = std::thread([this, filter, from]() {
                    constexpr int kPage = 500;
                    size_t offset = from;
                    for (;;) {
                        if (gridFill.quit.load()) break;
                        std::vector<romm::Game> page;
                        std::string err;
                        int total = 0;
                        if (!liveClient.fetchRoms(
                                filter + "&offset=" + std::to_string(offset),
                                kPage, &page, &err, &total))
                            break;
                        if (page.empty()) break;
                        offset += page.size();
                        {
                            std::lock_guard<std::mutex> lk(gridFill.m);
                            for (auto& g : page)
                                gridFill.arrived.push_back(std::move(g));
                        }
                        if (total > 0 && offset >= static_cast<size_t>(total)) break;
                    }
                    gridFill.running.store(false);
                });
            }
            stack.push_back(Screen::Grid);
            sound::play(sound::Cue::Activate);
            break;
        }
        case screens::Action::SwitchAccount: {
            // THE REFUSALS BELONG TO THE APP AND SO DO THEIR WORDS. The
            // screen does not know whether a game is running or a save is
            // still going up; it asked to become somebody and this decides.
            const int id = res.value;
            // NOT SWITCHED HERE. The switch asks the server who this is and
            // reloads the library, which blocks the frame loop; done here,
            // the screen froze mid-panel for as long as that took (eleven
            // seconds once, on the A9). It is handed to pumpSwitch, which
            // brings the curtain down with the person's name on it first.
            auto go = [&, id]() {
                const std::vector<accounts::Account> list = accounts::all();
                const accounts::Account* a = accounts::find(list, id);
                switchPendingId = id;
                switchCurtain = true;
                switchLabel = "Switching to " + (a ? a->name : std::string("them"));
                switchCurtainFrames = 0;
                accountsOpen = false;
                barFocused = false;
                accountScreen.setNotice("");
                curtain.retarget(1.0f, kCurtainDown);
                sound::play(sound::Cue::Activate);
            };
            // THE SAME PIN STOPS ANYBODY SWITCHING INTO THE OWNER.
            // docs/SETTINGS.md, Accounts. With no PIN, askPin just goes.
            if (id == accounts::ownerId() && accounts::activeId() != id) {
                const std::vector<accounts::Account> list = accounts::all();
                const accounts::Account* a = accounts::find(list, id);
                askPin("Enter the PIN",
                       "To switch to " + (a ? a->name : std::string("them")), go,
                       /*always=*/true);
            } else {
                go();
            }
            break;
        }
        case screens::Action::AddAccount:
            // WITH A PIN SET, ADDING SOMEBODY ASKS FOR IT, from here and
            // from Settings alike. MMagTech, 2026-09-24: without it,
            // anyone holding the controller could add themselves; with no
            // PIN, anyone can, which is the no-PIN rule everywhere.
            askPin("Enter the PIN", "To add an account", [&]() {
                // THE PANEL CLOSES AND A SCREEN OPENS. Leaving the panel
                // up behind a pairing code would put the list somebody is
                // about to change underneath the thing changing it.
                accountsOpen = false;
                barFocused = false;
                addAccountScreen.open();
                stack.push_back(Screen::AddAccount);
                startAddAccount();
            });
            sound::play(sound::Cue::Activate);
            break;
        case screens::Action::Setting:
            if (res.value == SetPads) {
                openPadsPanelAt(0);
                sound::play(sound::Cue::Activate);
            } else if (res.value >= kSetFormat) {
                const size_t i = static_cast<size_t>(res.value - kSetFormat);
                if (drives::formatting() || i >= formatable.size()) {
                    sound::play(sound::Cue::Edge);
                    break;
                }
                askFormat(formatable[i]);
                sound::play(sound::Cue::Activate);
            } else if (res.value >= kSetEject) {
                const std::vector<std::string> locs = storage::locations();
                const size_t i = static_cast<size_t>(res.value - kSetEject);
                if (drives::ejecting() || i >= locs.size() || !storage::isExternal(locs[i])) {
                    sound::play(sound::Cue::Edge);
                    break;
                }
                // The drive's row was pressed: Eject is asked, with the
                // drive named, and Cancel beside it.
                const std::string loc = locs[i];
                const std::string ejName =
                    driveNames.count(res.value) ? driveNames[res.value] : "External";
                // FORMAT BESIDE EJECT when nothing of the console's is on
                // the drive (#236). Eject stays first: it is what the row
                // says.
                std::optional<drives::Drive> fmt;
                if (auto f = ejectFormats.find(res.value);
                    f != ejectFormats.end() && f->second < formatable.size() &&
                    !drives::formatting())
                    fmt = formatable[f->second];
                std::vector<std::string> opts = {"Eject", "Cancel"};
                if (fmt) opts = {"Eject", "Format", "Cancel"};
                askChoice(ejName, "", opts, 0, [&, loc, fmt](int k) {
                if (fmt && k == 1) {
                    askFormat(*fmt);
                    return;
                }
                if (k != 0) return;
                // FINISHES OR STOPS ANYTHING WRITING TO IT. The only thing
                // that writes to a drive is a download being kept there;
                // it is stopped and waited for, so nothing holds a file
                // open on the drive. Its stage is left as the worker set
                // it, so the frame loop undoes the keep as for any failed
                // download.
                if (launchJob.busy() && launchJob.entryPath.rfind(loc + "/", 0) == 0) {
                    std::fprintf(stderr, "[drives] eject: stopping the download of %s\n",
                                 launchJob.title.c_str());
                    launchJob.stop();
                }
                drives::eject(loc);
                buildSettings();
                });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetAddController) {
                sound::play(sound::Cue::Activate);
                openPadWindow();
            } else if (res.value == SetWiiRemotes) {
                sound::play(sound::Cue::Activate);
                openWiiPanel();
            } else if (res.value == SetShortcutButton) {
                // The pad that pressed A, which the row already names.
                const std::vector<players::Pad> pads = players::connected();
                shortcutListen = pads.empty() ? 0 : pads.front().id;
                for (const players::Pad& p : pads)
                    if (p.id == settingsLastPad) shortcutListen = p.id;
                sound::play(sound::Cue::Activate);
                buildSettings();
            } else if (res.value == SetAddAccount) {
                // The same route as the chip's Add user, PIN included.
                // Back from the pairing screen returns here, because it
                // is pushed.
                askPin("Enter the PIN", "To add an account", [&]() {
                    addAccountScreen.open();
                    stack.push_back(Screen::AddAccount);
                    startAddAccount();
                });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetWifi) {
                // PIN FIRST, then either the radio on or the list of
                // networks in the question panel, scrolling past six.
                askPin("Enter the PIN", "To change Wi-Fi", [&]() {
                    bool radioOn = true;
                    {
                        std::lock_guard<std::mutex> lk(settingsNet.m);
                        if (settingsNet.have) radioOn = settingsNet.st.wifiEnabled;
                    }
                    if (!radioOn) {
                        std::string err;
                        if (!net::setRadio(true, &err))
                            menuNotice.say("Couldn't turn on Wi-Fi", Tone::Problem);
                        askNetwork();
                        askWifi();
                        buildSettings();
                        return;
                    }
                    // A password, typed on the on-screen keyboard, then the
                    // join on a worker. `forgetFirst` is Change password.
                    auto typeAndJoin = [&](const net::Network& net, bool forgetFirst) {
                        askWifiPassword(net.ssid, forgetFirst, "");
                    };
                    auto join = [&, typeAndJoin](const net::Network& net) {
                        if (net.secured && !net.known) { typeAndJoin(net, false); return; }
                        menuNotice.say("Joining " + net.ssid, Tone::Busy);
                        startWifiJoin(net.ssid, "", false);
                        buildSettings();
                    };
                    auto forget = [&](const net::Network& net) {
                        askChoice("Forget " + net.ssid + "?", "", {"Forget", "Cancel"}, 1,
                                  [&, net](int i) {
                                      if (i != 0) return;
                                      std::string err;
                                      if (net::forget(net.ssid, &err)) {
                                          menuNotice.say("Forgot " + net.ssid, Tone::Done);
                                          // AT ONCE, not when the next scan
                                          // lands: joining a stale "saved"
                                          // network sent no password.
                                          std::lock_guard<std::mutex> lk(settingsWifi.m);
                                          for (net::Network& w : settingsWifi.list)
                                              if (w.ssid == net.ssid)
                                                  w.known = w.active = false;
                                      }
                                      else
                                          menuNotice.say("Couldn't forget " + net.ssid,
                                                         Tone::Problem);
                                      std::fprintf(stderr, "[wifi] forget %s: %s\n",
                                                   net.ssid.c_str(),
                                                   err.empty() ? "done" : err.c_str());
                                      askNetwork();
                                      askWifi();
                                      buildSettings();
                                  });
                    };
                    sortWifi();
                    std::vector<std::string> names, values;
                    wifiPanelRows(&names, &values);
                    askChoice("Wi-Fi",
                              names.empty() ? "Looking for networks\xE2\x80\xA6" : "",
                              names, 0, [&, typeAndJoin, join, forget](int i) {
                                  wifiPanelOpen = false;
                                  if (i < 0 || i >= static_cast<int>(wifiShown.size()))
                                      return;
                                  const net::Network n = wifiShown[i];
                                  if (wifiJob.running.load()) {
                                      sound::play(sound::Cue::Edge);
                                      return;
                                  }
                                  if (n.active) {
                                      askChoice(n.ssid, "",
                                                {"Change password", "Forget", "Cancel"}, 0,
                                                [&, n, typeAndJoin, forget](int k) {
                                                    if (k == 0) typeAndJoin(n, true);
                                                    else if (k == 1) forget(n);
                                                });
                                  } else if (n.known) {
                                      askChoice(n.ssid, "",
                                                {"Join", "Change password", "Forget",
                                                 "Cancel"},
                                                0, [&, n, typeAndJoin, join, forget](int k) {
                                                    if (k == 0) join(n);
                                                    else if (k == 1) typeAndJoin(n, true);
                                                    else if (k == 2) forget(n);
                                                });
                                  } else {
                                      join(n);
                                  }
                              });
                    choiceScreen.setValues(values);
                    wifiPanelOpen = true;
                    askWifi();
                });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetDarkHours) {
                // From and Until, then the hours of whichever was chosen,
                // then back to From and Until with the new one showing.
                darkHoursPanel(0);
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetFiles) {
                if (filesSaidAt != 0) {
                    sound::play(sound::Cue::Edge);
                } else if (filesState.on) {
                    // The password is in the panel: the PIN first, when
                    // one is set (MMagTech, 2026-10-07).
                    askPin("Enter the PIN", "To see file access", [&]() { openFilesPanel(); });
                    sound::play(sound::Cue::Activate);
                } else {
                    // On behind the PIN, then the panel opens by itself
                    // once root says it is on.
                    askPin("Enter the PIN", "To turn on file access", [&]() {
                        filesPanelWhenOn = true;
                        setFiles(true);
                    });
                    sound::play(sound::Cue::Activate);
                }
            } else if (res.value == SetVersion) {
                // Seven presses, each within two seconds of the last,
                // show Developer access, or hide it (and turn it off).
                // Silent until the seventh: the row appearing or going
                // is the answer.
                const auto now = std::chrono::steady_clock::now();
                if (now - versionPressAt > std::chrono::seconds(2)) versionPresses = 0;
                versionPressAt = now;
                if (++versionPresses >= 7) {
                    versionPresses = 0;
                    devShown = !devShown;
                    prefs::set("developer_shown", devShown ? "on" : "off");
                    if (!devShown && (devWant || devState == "active")) setDeveloper(false);
                    std::fprintf(stderr, "[developer] %s\n", devShown ? "shown" : "hidden");
                    buildSettings();
                    sound::play(sound::Cue::Activate);
                }
            } else if (res.value == SetDeveloper) {
                if (devSaidAt != 0) {
                    sound::play(sound::Cue::Edge);
                } else if (devState == "active") {
                    askPin("Enter the PIN", "To see developer access", [&]() { openDevPanel(); });
                    sound::play(sound::Cue::Activate);
                } else {
                    askPinToOpen("To turn on developer access", [&]() {
                        devPanelWhenOn = true;
                        setDeveloper(true);
                    });
                    sound::play(sound::Cue::Activate);
                }
            } else if (res.value == SetRemotePlay) {
                // On behind the PIN when one is set; off at once, since
                // off only closes things. Never during a stream (the
                // row is greyed; this is for a press before it was).
                if (rpSaidAt != 0 || players::streaming()) {
                    sound::play(sound::Cue::Edge);
                } else if (rpState == "active") {
                    setRemotePlay(false);
                    sound::play(sound::Cue::Activate);
                } else {
                    askPin("Enter the PIN", "To turn on Remote Play",
                           [&]() { setRemotePlay(true); });
                    sound::play(sound::Cue::Activate);
                }
            } else if (res.value == SetTailscale) {
                // Off: the PIN (when set), then sign in. On: Disconnect
                // and Log out (the PIN), and Sign in again in the last
                // two weeks. Disconnected: Connect and Log out.
                const tailscale::Status st = tailscale::last();
                auto confirmLogout = [&]() {
                    askChoice("Log out of Tailscale?", "", {"Log out", "Cancel"}, 1,
                              [&](int k) {
                                  if (k != 0) return;
                                  askPin("Enter the PIN", "To log out of Tailscale",
                                         [&]() { tsLogout(); });
                              });
                };
                if (tsLoggingOut || tsSigningIn || players::streaming()) {
                    sound::play(sound::Cue::Edge);
                } else if (!tsWant && !tsSignedIn) {
                    askPin("Enter the PIN", "To turn on Tailscale",
                           [&]() { openTsSignIn(false); });
                    sound::play(sound::Cue::Activate);
                } else if (!tsWant) {
                    askChoice("Tailscale", "", {"Connect", "Log out", "Cancel"}, 0,
                              [&, confirmLogout](int c) {
                                  if (c == 0)
                                      askPin("Enter the PIN", "To turn on Tailscale",
                                             [&]() { setTailscale(true); });
                                  else if (c == 1) confirmLogout();
                              });
                    sound::play(sound::Cue::Activate);
                } else if (st.state == "Running") {
                    std::vector<std::string> opts;
                    const bool soon = tsEndsSoon(st);
                    if (soon) opts.push_back("Sign in again");
                    opts.insert(opts.end(), {"Disconnect", "Log out", "Cancel"});
                    askChoice("Tailscale", "", opts, 0, [&, soon, confirmLogout](int c) {
                        if (soon && c == 0) {
                            askPin("Enter the PIN", "To sign in to Tailscale again",
                                   [&]() { openTsSignIn(true); });
                            return;
                        }
                        if (soon) --c;
                        if (c == 0) setTailscale(false);
                        else if (c == 1) confirmLogout();
                    });
                    sound::play(sound::Cue::Activate);
                } else if (st.answered && (st.state == "NeedsLogin" || st.state == "Stopped")) {
                    // Signed out: its sign-in ended, or it was removed in
                    // Tailscale's own page. The same QR code again.
                    askPin("Enter the PIN", "To sign in to Tailscale",
                           [&]() { openTsSignIn(false); });
                    sound::play(sound::Cue::Activate);
                } else {
                    sound::play(sound::Cue::Edge);
                }
            } else if (res.value == SetPairedDevices) {
                if (players::streaming()) {
                    sound::play(sound::Cue::Edge);
                } else {
                    openPairedPanel(0);
                    sound::play(sound::Cue::Activate);
                }
            } else if (res.value == SetSteam) {
                // Grow it, or remove Steam: the PIN when set, once per
                // visit, as Downloads (it gives or takes space for the
                // whole console).
                askPin("Enter the PIN", "To change Steam", [&]() {
                    askChoice("Steam", driveSize(steam::sliceBytes()),
                              {"Adjust storage", "Remove Steam"}, 0, [&](int a) {
                        if (a == 0) {
                            auto read = std::make_shared<SteamSizes>();
                            runSteamJob("", [read]() {
                                readSteamSizes(read.get());
                            }, [&, read]() {
                                steamSizes = *read;
                                showSteamSize(true);
                            });
                            return;
                        }
                        askChoice("Remove Steam?", "", {"Remove", "Cancel"}, 1, [&](int b) {
                            if (b != 0) return;
                            auto ok = std::make_shared<bool>(false);
                            runSteamJob("Removing Steam\xE2\x80\xA6", [ok]() {
                                std::string why;
                                *ok = steam::remove(&why);
                            }, [&, ok]() {
                                buildSettings();
                                                    menuNotice.say(*ok ? "Steam removed" : "Couldn't remove Steam",
                                               *ok ? Tone::Done : Tone::Problem);
                            });
                        });
                    });
                });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetSteamShow) {
                askPin("Enter the PIN", "To show Steam", [&]() {
                    steam::setHidden(false);
                    std::fprintf(stderr, "[steam] shown\n");
                            buildSettings();
                });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetDownloads) {
                // AN ADMIN SCREEN: the PIN to open it, once per Settings
                // visit, so clearing several games is one PIN. MMagTech,
                // 2026-09-25. Removing your own needs none, from the
                // game's own screen.
                askPin("Enter the PIN", "To open Downloads", [&]() {
                    std::vector<screens::DownloadItem> dl = downloadItems();
                    if (dl.empty()) { buildSettings(); return; }
                    downloadsPanel.open(std::move(dl));
                });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetReport) {
                if (reportWorker.joinable()) {
                    sound::play(sound::Cue::Edge);
                } else {
                    report::stop();
                    reportScreen.setText("Diagnostic report", {}, "Creating\xE2\x80\xA6");
                    reportScreen.open();
                    stack.push_back(Screen::Report);
                    reportWorkerDone = false;
                    reportWorker = std::thread([&]() {
                        reportMade = report::make(storage::root());
                        std::string ip = net::status().ipv4;
                        reportAddress = ip.substr(0, ip.find('/'));
                        reportWorkerDone = true;
                    });
                    sound::play(sound::Cue::Activate);
                }
            } else if (res.value == SetHowTo) {
                openHowTo();
                sound::play(sound::Cue::Activate);

            } else if (res.value == SetCredits) {
                // A list, one line per project: what it does, and its
                // licence. Nothing to choose; A or B closes it.
                std::vector<std::string> names, values;
                for (const Credit& cr : kCredits) {
                    names.push_back(cr.name);
                    values.push_back(cr.what);
                }
                askChoice("Credits and licenses", "", names, 0, [](int) {});
                choiceScreen.setValues(values);
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetUpdate) {
                // ONE ROW, AND WHAT IT DOES IS WHAT IT SAYS: check, fetch
                // (behind the PIN: an update can be gigabytes and replaces
                // the system, so the owner decides when), restart, or
                // retry. Busy, it does nothing. docs/SETTINGS.md, System.
                const bool available = upd.state == update::State::Available ||
                                       (upd.state == update::State::None && updFoundNewer());
                const bool retryDownload = upd.state == update::State::Failed && upd.download;
                if (upd.busy()) {
                    sound::play(sound::Cue::Edge);
                } else if (upd.state == update::State::Ready) {
                    askUpdateReady();
                    sound::play(sound::Cue::Activate);
                } else if (available) {
                    askUpdateDownload();
                    sound::play(sound::Cue::Activate);
                } else if (retryDownload) {
                    askPin("Enter the PIN", "To update",
                           [&]() { startUpdate(/*download=*/true, /*automatic=*/false); });
                    sound::play(sound::Cue::Activate);
                } else {
                    updAskAfterCheck = true;
                    startUpdate(/*download=*/false, /*automatic=*/false);
                    sound::play(sound::Cue::Activate);
                }
            } else if (res.value == SetServer) {
                // PIN FIRST, then what to do: the Wi-Fi row's shape.
                // docs/SETTINGS.md, Network; issues #60 and #61.
                askPin("Enter the PIN", "To change the RomM server", [&]() {
                    const std::string addr = rommAddress ? rommAddress : "";
                    askChoice(addr, "", {"Change address", "Sign out", "Cancel"}, 0,
                              [&, addr](int i) {
                        if (i == 0) {
                            // Root's address outranks ours (firstrun.h).
                            // Only a console set up by hand has one.
                            if (!server::addressIsOurs()) {
                                menuNotice.say("Set in /etc/cabinetos/session.env",
                                               Tone::Problem);
                                return;
                            }
                            askServerAddress(addr, "");
                        } else if (i == 1) {
                            // Focus on Cancel. Then everyone's unsent
                            // saves are sent, and only what could not be
                            // is said, not counted (MMagTech, 2026-09-25),
                            // as Remove does (#194).
                            askChoice("Sign out?",
                                      "Removes every game and account from this console.",
                                      {"Sign out", "Cancel"}, 1, [&](int k) {
                                          if (k == 0) startSignOut();
                                      });
                        }
                    });
                });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetRemoveAccount) {
                // PIN (if set), then who, then are you sure. With one
                // person to remove, "who" is skipped.
                askPin("Enter the PIN", "To remove an account", [&]() {
                    auto confirm = [&](accounts::Account who) {
                        // NO EXPLANATION. MMagTech, 2026-09-24: the people
                        // using this are technical, "we don't need to spoon
                        // feed them everything with an explanation".
                        askChoice("Remove " + who.name + "?", "",
                                  {"Remove", "Cancel"}, 1, [&, who](int i) {
                                      if (i == 0) startRemove(who);
                                  });
                    };
                    const std::vector<accounts::Account> people = removableAccounts();
                    if (people.empty()) return;
                    if (people.size() == 1) { confirm(people[0]); return; }
                    std::vector<std::string> names;
                    for (const auto& p : people) names.push_back(p.name);
                    names.push_back("Cancel");
                    askChoice("Remove an account", "", names, 0,
                              [&, people, confirm](int i) {
                                  if (i >= 0 && i < static_cast<int>(people.size()))
                                      confirm(people[i]);
                              });
                });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetPinSet) {
                choosePin("Choose a PIN", "",
                          [&]() { buildSettings(); });
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetPinChange) {
                // ALWAYS ASKS (#237), even after the PIN was entered once
                // this visit: the once-per-visit rule is for Downloads, and
                // changing or removing the PIN itself must prove you know it.
                askPin("Enter your current PIN", "", [&]() {
                    choosePin("Choose a new PIN", "",
                              [&]() { buildSettings(); });
                }, true);
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetRetroAchievements) {
                if (ra::signedIn()) {
                    askChoice("RetroAchievements", "Signed in as " + ra::username(),
                              {"Sign out", "Cancel"}, 1, [&](int k) {
                                  if (k == 0) ra::signOut();
                                  buildSettings();
                              });
                } else {
                    askRaUser("");
                }
                sound::play(sound::Cue::Activate);
            } else if (res.value == SetPinOff) {
                askPin("Enter the PIN", "To turn it off", [&]() {
                    std::string err;
                    if (accounts::setPin("", &err))
                        std::fprintf(stderr, "[pin] turned off\n");
                    buildSettings();
                }, true);
                sound::play(sound::Cue::Activate);
            }
            break;
        case screens::Action::SettingChoice:
            if (res.value == SetUpdateCheck) {
                updWeekly = settingsScreen.choiceOf(SetUpdateCheck) == 1;
                prefs::set("update_check", updWeekly ? "weekly" : "manual");
                sound::play(sound::Cue::Move);
            }
            if (res.value == SetRumble) {
                rumble::setEnabled(settingsScreen.choiceOf(SetRumble) == 1);
                sound::play(sound::Cue::Move);
            }

            if (res.value == SetShortcuts) {
                shortcuts::setEnabled(settingsScreen.choiceOf(SetShortcuts) == 1);
                sound::play(sound::Cue::Move);
                buildSettings();   // the Shortcut button row comes and goes
            }
            if (res.value == SetAppearance) {
                const int i = settingsScreen.choiceOf(SetAppearance);
                if (i >= 0 && i < look::kAppearanceCount) {
                    appearance = static_cast<look::Appearance>(i);
                    prefs::set("appearance", look::appearanceWord(appearance));
                    look::setDark(wantDark());
                }
                sound::play(sound::Cue::Move);
                buildSettings();   // the From and Until rows come and go
            }
            if (res.value == SetColour) {
                const int i = settingsScreen.choiceOf(SetColour);
                if (i >= 0 && i < look::kColourCount) {
                    const auto c = static_cast<look::Colour>(i);
                    accounts::setColour(accounts::activeId(), look::colourWord(c));
                    look::setColour(c);
                }
                sound::play(sound::Cue::Move);
            }
            if (res.value == SetScreenOff) {
                const int i = settingsScreen.choiceOf(SetScreenOff);
                if (i >= 0 && i < kScreenOffCount) {
                    screenOffIndex = i;
                    prefs::set("screen_off_after", kScreenOff[i].word);
                }
                sound::play(sound::Cue::Move);
            }
            if (res.value == SetPictureQuality) {
                // From each game's next start (quality.h); nothing running
                // changes.
                const int i = settingsScreen.choiceOf(SetPictureQuality);
                if (i >= 0 && i < quality::kLevelCount)
                    quality::setConsole(static_cast<quality::Level>(i));
                sound::play(sound::Cue::Move);
            }
            if (res.value == SetAchievementSound) {
                const int i = settingsScreen.choiceOf(SetAchievementSound);
                if (i >= 0 && i < sound::kLevelCount) {
                    const auto lv = static_cast<sound::Level>(i);
                    sound::setChimeLevel(lv);
                    prefs::set("achievement_sound", sound::levelWord(lv));
                }
                // The chime itself, at the level just chosen, as the
                // clicks row plays a click.
                sound::playChime();
            }
            if (res.value == SetInterfaceSounds) {
                const int i = settingsScreen.choiceOf(SetInterfaceSounds);
                if (i >= 0 && i < sound::kLevelCount) {
                    const auto lv = static_cast<sound::Level>(i);
                    sound::setLevel(lv);
                    prefs::set("interface_sounds", sound::levelWord(lv));
                }
                // AFTER the change, so each step is heard at the level it
                // just chose, which is how a person picks one. Off is
                // silent, which says it too.
                sound::play(sound::Cue::Move);
            }
            break;
        case screens::Action::FocusKeyboard:
            // Back into the keyboard under the results. Focus does not
            // leave the screen, it moves down within it.
            searchScreen.setFocused(false);
            sound::play(sound::Cue::Move);
            break;
        case screens::Action::FocusBar:
            // Where the cursor lands in the bar: on the destination you are
            // standing in, so walking up and straight back down is a no-op
            // rather than a silent change of where you would go.
            barSlot = (here() == Screen::Library || here() == Screen::Grid) ? BarLibrary
                    : here() == Screen::Search                               ? BarSearch
                    : (here() == Screen::Settings || here() == Screen::HowTo) ? BarSettings
                                                                             : BarHome;
            barFocused = true;
            sound::play(sound::Cue::Move);
            break;
        case screens::Action::OpenGame:
            // A GAME OPENED FROM SEARCH'S RESULTS CLOSES ITS KEYBOARD. Up
            // from the keys leaves them open below the results, so typing
            // can go on; opening a game left them open over its page,
            // taking every press until B (MMagTech on the TV, 2026-10-06).
            if (here() == Screen::Search && keyboard.isOpen()) keyboard.cancel();
            // Opening a game's page, Play and a state are choices like any
            // other, and were silent (#389).
            sound::play(sound::Cue::Activate);
            openDetailSoon(res.value);
            break;
        case screens::Action::Play:
            sound::play(sound::Cue::Activate);
            launchById(res.value);
            break;
        case screens::Action::PlayState:
            sound::play(sound::Cue::Activate);
            std::fprintf(stderr, "[detail] play from state %d\n", res.value);
            // Negative: one of this console's own states, by its file.
            if (res.value < 0 &&
                static_cast<size_t>(-res.value) <= detailLocalStates.size()) {
                pendingStatePath = detailLocalStates[static_cast<size_t>(-res.value) - 1];
                // ON THE STATE'S OWN DISC (#162): no swap to wait on,
                // and the only way in for a core with no drive (3DO).
                gNextStartDisc = disc::discInStateName(pendingStatePath);
                if (!launchById(detailScreen.game().romId)) {
                    pendingStatePath.clear();
                    gNextStartDisc = 0;
                }
                break;
            }
            pendingStateId = res.value;
            if (!launchById(detailScreen.game().romId)) pendingStateId = 0;
            break;
        case screens::Action::Download:
            sound::play(sound::Cue::Activate);
            downloadById(res.value);
            break;
        case screens::Action::ShowAchievements:
            if (detailAchRom == res.value && detailAch.known && !detailAch.items.empty()) {
                raList.open(detailScreen.game().title, detailAch);
                sound::play(sound::Cue::Activate);
            }
            break;
        case screens::Action::ToggleFavorite: {
            // THE HEART ANSWERS AT ONCE (#267): the page, Home's shelf and
            // the person's list on the drive change now; the server hears
            // it from the uploader, now or when it is back.
            const int romId = res.value;
            const bool on = !detailScreen.game().favorite;
            detailScreen.setFavorite(on);
            sound::play(sound::Cue::Activate);
            const int card = detailScreen.game().cardIndex;
            auto it = std::find(favorites.begin(), favorites.end(), card);
            if (on && it == favorites.end() && card >= 0) favorites.insert(favorites.begin(), card);
            if (!on && it != favorites.end()) favorites.erase(it);
            // Home's place on the Favorites shelf, which just changed
            // under it: kept in range, or Recent if the shelf is gone.
            slotFocus[RowFavorites].clear();
            slotPress[RowFavorites].clear();
            if (focusRow == RowFavorites) {
                if (favorites.empty()) { focusRow = RowRecent; focusSlot = 0; }
                else focusSlot = std::clamp(focusSlot, 0, static_cast<int>(favorites.size()) - 1);
                if (cardAt(focusRow, focusSlot)) slotAnim(slotFocus, focusRow, focusSlot).settle(1.0f);
            }
            if (const storage::User& who = storage::currentUser(); who.valid()) {
                const std::string path = storage::userDir(who) + "/favorites.json";
                std::vector<int> ids;
                if (json_object* arr = json_object_from_file(path.c_str())) {
                    if (json_object_is_type(arr, json_type_array))
                        for (size_t i = 0; i < json_object_array_length(arr); ++i)
                            ids.push_back(json_object_get_int(json_object_array_get_idx(arr, i)));
                    json_object_put(arr);
                }
                ids.erase(std::remove(ids.begin(), ids.end(), romId), ids.end());
                if (on) ids.insert(ids.begin(), romId);
                json_object* arr = json_object_new_array();
                for (int id : ids) json_object_array_add(arr, json_object_new_int(id));
                const std::string tmp = path + ".part";
                if (json_object_to_file(tmp.c_str(), arr) == 0) ::rename(tmp.c_str(), path.c_str());
                json_object_put(arr);
                oweFavorite(who, romId, on);
                uploader.sendFavorites();
            }
            std::fprintf(stderr, "[favorite] %d %s\n", romId, on ? "on" : "off");
            break;
        }
        case screens::Action::RemoveDownload: {
            // ASKS FIRST (#188): a mis-press cost a re-download, 37 GB and
            // a reinstall for a PS3 game. The same question, words and
            // order as Settings' Downloads list, focus on Cancel.
            const int romId = res.value;
            int64_t bytes = 0;
            for (const screens::DownloadItem& d : downloadItems())
                if (d.romId == romId) bytes = d.bytes;
            askChoice("Remove " + detailScreen.game().title + "?",
                      bytes > 0 ? screens::sizeText(bytes) : std::string(),
                      {"Cancel", "Remove"}, 0, [&, romId](int answer) {
                          if (answer != 1) return;
                          removeDownload(romId);
                          refreshKeeps();
                      });
            break;
        }
    }
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

// A LAUNCH THAT CANNOT GO AHEAD says why on the game's own screen, the
// way a core that will not load does below: opened if the press came from
// somewhere with nowhere to put a message, and the curtain lifted onto it.
//
// THE MESSAGE WAITS FOR THE SCREEN when the screen is opened a frame later:
// DetailScreen::open clears its notice, so a notice set before the open
// was wiped by it, and a refusal from Home arrived on the game's screen
// saying nothing. Found 2026-09-28 by a Switch game that would not load,
// launched from Home; the core-failure path had the same order.
void App::refuseLaunch(const std::string& why) {
    bool deferred = false;
    if (here() != Screen::Detail || detailScreen.game().romId != launchJob.romId) {
        for (size_t i = 0; i < cards.size(); ++i) {
            if (cards[i].id == launchJob.romId) {
                openDetailSoon(static_cast<int>(i));
                deferred = pendingDetail >= 0;
                break;
            }
        }
    }
    if (deferred) pendingDetailNotice = why;
    else detailScreen.setNotice(why);
    sound::play(sound::Cue::Edge);
    curtain.retarget(0.0f, kCurtainUp);
}

// THE GAME IS ON THE DISK AND ITS EMULATOR IS ITS OWN APPLICATION: start
// it and let it have the television. The curtain is already down, so the
// moment between here and the emulator's first picture is black, which is
// what every launch looks like. gamescope gives the new window the screen
// by itself (measured 2026-09-28), so this window stays as it is.
void App::startStandalone(const cab::standalone::Emulator& emu) {
    cache::touch(launchJob.entryPath);
    const storage::User& user = storage::currentUser();
    const std::string saveDir = storage::savesDir(
        user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
    // ONE VIRTUAL CONTROLLER PER PLAYER, and at least one: the emulator
    // is given these and never the real pads (vpad.h). Without them the
    // game would start and nothing would move it, so that refuses here.
    const int pads = std::max(1, players::count());
    if (!cab::vpad::open(pads)) {
        refuseLaunch("Couldn't start this game");
        return;
    }
    bool missingKeys = false;
    std::string err;
    // THE GAME'S PICTURE QUALITY (#63), its own choice or the console's.
    const quality::Level level = quality::forGame(launchJob.romId);
    quality::logApplied(launchJob.coreName, launchJob.romId, level);
    if (!cab::standalone::prepare(emu, launchJob.entryPath, saveDir, user.name, pads, level,
                                  &missingKeys, &err)) {
        cab::vpad::close();
        if (!err.empty()) std::fprintf(stderr, "[standalone] %s\n", err.c_str());
        refuseLaunch(missingKeys ? "No " + launchJob.systemName + " " + emu.needs +
                                       " on your server"
                                 : std::string("Couldn't start this game"));
        return;
    }
    // THE SAVE COMES DOWN FIRST, as PSP's does: a zip of the game's save
    // folder, unpacked where the emulator will look. The folder inside is
    // named by the game's title ID, so the console never has to know it.
    const std::string root = cab::standalone::saveRoot(emu, saveDir);
    const char* tag = catalog::saveTag(launchJob.coreName.c_str());
    // A LAST GAME HERE THAT NEVER GOT ITS SAVES ZIPPED goes up first, so
    // the restore below keeps it (finishInterruptedSave).
    if (cab::standalone::hasPlayingNote(saveDir))
        finishInterruptedSave(emu, saveDir, uploader);
    if (!root.empty() && tag && liveClient.haveToken())
        restoreDirSave(liveClient, launchJob.romId, tag, root, launchJob.title);
    // AND ONTO WHERE THE EMULATOR READS THEM, when that is not a folder
    // (Xbox: its hard drive). The note says whose they are, should the
    // console go down before the game ends.
    {
        const std::string note = std::to_string(launchJob.romId) + "\n" + saveDir + "\n" +
                                 launchJob.title + "\n" + (tag ? tag : "") + "\n";
        if (!cab::standalone::beforeStart(emu, launchJob.entryPath, saveDir, user.name, note,
                                          &err)) {
            cab::vpad::close();
            std::fprintf(stderr, "[standalone] %s\n", err.c_str());
            refuseLaunch("Couldn't start this game");
            return;
        }
    }
    if (!standaloneRun.start(emu, launchJob.romPath, launchJob.entryPath, saveDir, level,
                             &err)) {
        std::string aerr;
        cab::standalone::afterEnd(emu, launchJob.entryPath, saveDir, &aerr);
        cab::standalone::finished(emu, saveDir);
        cab::vpad::close();
        std::fprintf(stderr, "[standalone] %s\n", err.c_str());
        refuseLaunch("Couldn't start this game");
        return;
    }
    session = GameSession{};
    session.romId = launchJob.romId;
    session.core = launchJob.coreName;
    session.platformSlug = launchJob.platformSlug;
    session.title = launchJob.title;
    session.fsStem = launchJob.fsStem;
    session.saveDir = saveDir;
    session.standaloneCore = emu.core;
    session.entryPath = launchJob.entryPath;
    if (tag) session.saveTag = tag;
    // No save states: the pause menu is Resume, Screenshot and Exit to
    // Home (open question 25; Switch fails its rule).
    session.snapshots = catalog::snapshotsAllowed(launchJob.coreName.c_str());
    // The baseline: what changes between here and the end is what the
    // game saved. See GameSession.
    if (!root.empty()) {
        session.dirSaveRoot = root;
        session.dirAtLaunch = cab::listTree(root);
        std::fprintf(stderr, "[save] %s holds %zu file(s) at launch\n", root.c_str(),
                     session.dirAtLaunch.size());
    }
    standaloneRan = 0.0f;
    if (session.romId > 0) {
        playClock.begin(session.romId, playtime::wallMs());
        playCheckpointClock = 0.0f;
    }
    // THIS WINDOW STEPS OUT OF THE WAY AND WAITS ON TOP. It becomes
    // gamescope's overlay (overlaywin.h), draws nothing, and leaves the
    // controllers with the game, so the emulator has the screen; the pause
    // menu is this same window drawing again. gamescope classifies a window
    // when it maps, hence hidden, marked, shown.
    SDL_HideWindow(window);
    cab::overlaywin::mark(window, /*takeInput=*/false);
    SDL_ShowWindow(window);
    renderer.setTransparentBackground(true);
    // The launch curtain is down, and nothing lifts it while the game runs
    // because nothing is drawn. Up at once, or the first paused frame is
    // the curtain's black rather than the frozen game.
    curtain.from = curtain.to = 0.0f;
    curtain.elapsed = curtain.duration;
    standaloneClearFrames = 30;
    standalonePaused = false;
}

// AND WHEN IT ENDS, however it ends, the console is where it was: Home,
// or the game's screen, under the curtain lifting. A game that could not
// load says so where the press came from; one that fell over says so in
// the pill, because the person was playing, not choosing.
// A PRESS THAT ARRIVED WHILE A GAME CLOSED IS DROPPED, from every
// controller, on both ways home (a core here, an emulator of its own).
// Closing blocks for a moment (the unload, the saves), and a press queued
// meanwhile, a second press of A or a Remote's button bouncing, reached
// Home as a fresh one and started the game it had just left: A9
// 2026-10-03 21:53:49, Geometry Wars again 34 ms after Exit. The same
// family as #103, on the way out. Releases stay, and the power and Sleep
// keys always go through.
void App::dropPressesFromClosing() {
    SDL_PumpEvents();
    SDL_FilterEvents([](void*, SDL_Event* ev) {
        if (ev->type == SDL_EVENT_GAMEPAD_BUTTON_DOWN) return false;
        if (ev->type == SDL_EVENT_KEY_DOWN)
            return ev->key.key == SDLK_POWER || ev->key.key == SDLK_SLEEP;
        return true;
    }, nullptr);
}

void App::finishStandalone() {
    // AFTER THE EMULATOR HAS GONE, which is when everything it wrote is
    // on the disk. Whatever changed is zipped and sent, crash or not: a
    // game that saved and then fell over still saved.
    if (const cab::standalone::Emulator* ran =
            cab::standalone::find(session.standaloneCore)) {
        std::string aerr;
        if (!cab::standalone::afterEnd(*ran, session.entryPath, session.saveDir, &aerr))
            std::fprintf(stderr, "[standalone] saves stay on the drive: %s\n", aerr.c_str());
    }
    if (!session.dirSaveRoot.empty()) syncDirSave(session, uploader);
    session.dirSaveRoot.clear();
    // The zip is on the disk and owed to the server: nothing left for a
    // restart to finish.
    if (const cab::standalone::Emulator* ran = cab::standalone::find(session.standaloneCore))
        cab::standalone::finished(*ran, session.saveDir);
    cab::vpad::close();
    if (playClock.active()) {
        playtime::close(storage::currentUser(), playClock.finish(playtime::wallMs()));
        uploader.sendPlay();
        playedJustNow(session.romId);
    }
    curtain.from = curtain.to = 1.0f;
    curtain.elapsed = curtain.duration;
    curtain.retarget(0.0f, kCurtainUp);
    // Nothing reached this loop while the emulator had the screen, so the
    // idle timers think nobody has touched anything since the launch.
    idleWatch.input(clockSeconds());
    switch (standaloneRun.ended()) {
        case cab::standalone::Run::End::CouldNotLoad:
            refuseLaunch("Couldn't start this game");
            break;
        case cab::standalone::Run::End::KeysTooOld:
            refuseLaunch("Needs newer " + launchJob.systemName + " keys on your server");
            break;
        case cab::standalone::Run::End::NoLicence:
            refuseLaunch("No license for this game on your server");
            break;
        case cab::standalone::Run::End::Crashed:
            menuNotice.say("The game closed unexpectedly", Tone::Problem);
            break;
        default:
            break;
    }
    detailStates.stale = true;
    // And Home again: an ordinary window, opaque, with the controllers.
    if (cab::overlaywin::active()) {
        SDL_HideWindow(window);
        cab::overlaywin::unmark(window);
        SDL_ShowWindow(window);
    }
    renderer.setTransparentBackground(false);
    standalonePaused = false;
    overlayOpen = false;
    overlayFade.from = overlayFade.to = 0.0f;
    overlayFade.elapsed = overlayFade.duration;
    dropPressesFromClosing();
    std::fprintf(stderr, "[standalone] back to Home\n");
}

// Picks up a finished job. Loading the game happens HERE, on the frame
// thread, because the core is not thread-safe and the worker only ever
// moved bytes.
void App::pumpLaunch() {
    const LaunchJob::Stage st = launchJob.stage.load();
    if (st == LaunchJob::Stage::Failed) {
        std::fprintf(stderr, "[launch] failed: %s\n", launchJob.message.c_str());
        // A promise the console could not deliver is not a promise. The
        // record went in before the download so the download would be safe
        // from eviction; it comes out again if the download never finished,
        // or the disk fills with reserved space holding nothing.
        // UNDOING A PROMISE, NOT RECLAIMING SPACE, so the bytes stay and
        // become evictable. The file may be a game that was already on the
        // disk before anybody pressed Download, and throwing that away
        // because a firmware fetch failed would be its own small disaster.
        if (launchJob.keepWhenReady)
            cache::unkeep(storage::currentUser(), launchJob.romId,
                          /*keepTheBytes=*/true);
        if (here() == Screen::Detail &&
            detailScreen.game().romId == launchJob.romId) {
            detailScreen.setKept(
                cache::isKeptBy(storage::currentUser(), launchJob.romId));
            detailScreen.setNotice(launchJob.message);
        }
        refreshKeeps();
        launchJob.stop();
        launchJob.stage = LaunchJob::Stage::Idle;
        return;
    }
    if (st != LaunchJob::Stage::Ready) return;

    // WHETHER THE PERSON MOVED ON is decided BEFORE the curtain, not after
    // it — 2026-09-27. It used to be checked below, after the curtain had
    // come all the way down, and that branch returned without lifting it:
    // the console sat on a black screen with no way out but restarting
    // the app (#85, Dynamite Cop). Asked every frame, so a screen change
    // while the curtain is on its way down lifts it again as well.
    const bool movedOn = launchJob.playWhenReady && launchJob.startedOn >= 0 &&
                         launchJob.startedOn != static_cast<int>(here());

    // THE CURTAIN COMES DOWN BEFORE THE BLOCKING WORK, not after it. See
    // design::kCurtainDown. The stage is deliberately NOT cleared here, so
    // this runs again next frame and the frames in between are spent
    // animating something a person can see rather than waiting inside
    // Core::loadGame with nothing on the screen.
    //
    // Only for a launch. A download that was merely asked for must not
    // black the screen out — the person is still browsing, and its progress
    // is on the row they pressed.
    if (launchJob.playWhenReady && !movedOn) {
        curtain.retarget(1.0f, kCurtainDown);
        launchJob.loweredCurtain = true;
        if (curtain.value() < 0.995f) return;
    }

    launchJob.stop();
    launchJob.stage = LaunchJob::Stage::Idle;
    // THE GAME'S MANUAL COMES WITH IT (#364), beside it on the drive, on
    // a worker of its own: a manual that does not come never holds up the
    // game.
    manualBeside(launchJob.romId, launchJob.entryPath);

    // A download that was asked for rather than needed stops here. The game
    // is on the disk, prepared exactly as a launch would have prepared it,
    // so playing it later costs nothing — and nothing about it is guessed
    // at a second time.
    if (!launchJob.playWhenReady) {
        std::fprintf(stderr, "[download] %s is here%s\n", launchJob.title.c_str(),
                     launchJob.keepWhenReady ? " and kept" : "");
        if (here() == Screen::Detail &&
            detailScreen.game().romId == launchJob.romId)
            detailScreen.setKept(
                cache::isKeptBy(storage::currentUser(), launchJob.romId));
        refreshKeeps();
        // THE ONE EARLY WARNING. Only a Download can fill the drives (the
        // cache clears itself, saves are small), so this is the one moment
        // to say it: kept games are close to leaving no room to play
        // something new. MMagTech, 2026-09-25.
        if (launchJob.keepWhenReady && cache::almostFull())
            menuNotice.say("Storage almost full", Tone::Info);
        return;
    }

    // AND IT ONLY LAUNCHES ITSELF IF NOBODY WALKED AWAY — 2026-09-21.
    //
    // MMagTech, on the download no longer blocking the screen: *"wait
    // should it auto launch after complete."* It is a question that could
    // not be asked while a modal panel was up, because there was nowhere to
    // walk to. Now there is: press Play on a 1.78 GB arcade set, get bored,
    // go and look at something else, and four minutes later the console
    // would drop you into a game you had stopped waiting for.
    //
    // So: still on the screen you pressed it from, you are visibly waiting
    // and it launches — "one action from cold to playing" holds. Moved on,
    // and it finishes quietly. The game is on the disk, prepared exactly as
    // a launch would have prepared it, and its row says Play.
    if (movedOn) {
        // Which screens, because the one time this was seen it fired two
        // seconds after the press, too soon to be somebody walking away,
        // and the log could not say what had moved.
        std::fprintf(stderr,
                     "[launch] %s is ready, and not starting: the screen moved on "
                     "(pressed on screen %d, now on %d)\n",
                     launchJob.title.c_str(), launchJob.startedOn,
                     static_cast<int>(here()));
        if (launchJob.loweredCurtain) curtain.retarget(0.0f, kCurtainUp);
        if (here() == Screen::Detail &&
            detailScreen.game().romId == launchJob.romId)
            detailScreen.setKept(
                cache::isKeptBy(storage::currentUser(), launchJob.romId));
        refreshKeeps();
        return;
    }

    if (const cab::standalone::Emulator* emu =
            cab::standalone::find(launchJob.coreName)) {
        startStandalone(*emu);
        return;
    }

    cab::Core& core = cab::Core::shared();
    // THIS PERSON'S SAVES, FOR THIS GAME, FROM THIS CORE. It is also the
    // directory the core itself is handed, so a Sega CD's .brm and a PSP's
    // memory stick land in the same place as the battery snapshot this
    // frontend takes — which is the whole of what "two save directories"
    // and "one flat pile" cost.
    const storage::User& user = storage::currentUser();
    const std::string saveDir = storage::savesDir(
        user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
    storage::makeDirs(saveDir);

    // A DIRECTORY save comes down BEFORE the game is loaded, unlike the
    // battery below, which the core can be handed afterwards. PPSSPP mounts
    // the memory stick while the game boots, so a folder that arrives later
    // is a folder the game has already decided is not there.
    const char* dirSub = catalog::directorySaveRoot(launchJob.coreName.c_str(),
                                                    launchJob.platformSlug,
                                                    launchJob.titleId);
    const char* launchTag = catalog::saveTag(launchJob.coreName.c_str());
    // GameCube's raw card from before, which Dolphin reads into the folder
    // (writeDolphinConfig): `card.raw` with the region Dolphin stamps in.
    std::string oldGameCubeCard;
    if (launchJob.platformSlug == "ngc") {
        if (const char* region = catalog::gameCubeRegion(launchJob.titleId))
            oldGameCubeCard = saveDir + "/card." + region + ".raw";
        else
            std::fprintf(stderr, "[save] RomM has no game code for this GameCube game, so its "
                         "region and save folder are unknown; saves stay on this console\n");
    }
    if (dirSub && launchTag && liveClient.haveToken())
        restoreDirSave(liveClient, launchJob.romId, launchTag, saveDir + "/" + dirSub,
                       launchJob.title, oldGameCubeCard);

    // AND THE SAVES THE CORE WRITES AS A FILE, which is most of the ones
    // on a real server and none of the ones this console used to handle.
    // Before the load for the same reason the directory save is: these
    // cores read their file once while the machine is being built. See
    // filesave.h.
    //
    // The stem is the basename of what the core is handed, without its
    // extension, because that is what these cores name the save after —
    // `lethalen.nv`, `Lunar - The Silver Star.brm`.
    // A GAME ON SEVERAL DISCS (#162): the disc it starts on, the last one
    // this person was on (disc 1 the first time) or the one Start over
    // asked for, and how that disc is put first on this system.
    const disc::System discSys = disc::systemFor(launchJob.platformSlug);
    disc::Boot discBoot = discSys.boot;
    if (gDiscBootOverride >= 0 && discSys.change == disc::Change::Insert)
        discBoot = static_cast<disc::Boot>(gDiscBootOverride);
    int startDisc = 0;
    std::vector<int> discOrder;
    // Taken whatever this game is, so a single-disc launch never leaves it set.
    const int askedDisc = gNextStartDisc;
    gNextStartDisc = 0;
    if (!launchJob.discs.empty()) {
        const std::vector<disc::Disc>& discs = launchJob.discs;
        const int count = disc::highest(discs);
        startDisc = askedDisc > 0 ? askedDisc : disc::lastDisc(user, launchJob.romId);
        if (!disc::byNumber(discs, startDisc)) startDisc = discs.front().number;
        // GAMECUBE ALWAYS STARTS AT THE TOP OF THE LIST: the game asks for
        // the disc it needs and Dolphin puts it in; which one was in last
        // time is not known (syncDiscFromCore).
        if (discSys.change == disc::Change::Automatic) startDisc = discs.front().number;
        const std::string dir = launchJob.entryPath;
        const std::string first = dir + "/" + disc::byNumber(discs, startDisc)->file;
        auto writeList = [&](int top) {
            const std::string t = disc::listText(discs, top);
            if (!cab::writeBytes(launchJob.romPath, std::vector<uint8_t>(t.begin(), t.end())))
                std::fprintf(stderr, "[disc] could not write %s\n", launchJob.romPath.c_str());
            discOrder = disc::listOrder(discs, top);
        };
        const char* how = "";
        switch (discBoot) {
            case disc::Boot::File:
                // No list for this core: the disc's own file.
                launchJob.romPath = first;
                discOrder = {startDisc};
                how = "its own file";
                break;
            case disc::Boot::ListFirst:
                writeList(startDisc);
                how = "first in the list";
                break;
            case disc::Boot::InitialImage:
                writeList(0);
                // Disc 1 as well: Flycast keeps the last start disc it
                // was told across games, and would start on it.
                core.setInitialDisc(startDisc - 1, first);
                how = "the core's start disc";
                break;
            case disc::Boot::SwapFirst:
                writeList(0);
                how = "put in before the first frame";
                break;
        }
        std::fprintf(stderr, "[disc] starts on disc %d of %d (%s)\n", startDisc, count, how);
    }

    std::vector<catalog::SaveFile> saveSpecs;
    {
        std::string stem = launchJob.romPath;
        if (const size_t slash = stem.find_last_of('/'); slash != std::string::npos)
            stem.erase(0, slash + 1);
        if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos)
            stem.erase(dot);
        saveSpecs = catalog::saveFiles(launchJob.platformSlug,
                                       launchJob.coreName, stem);
    }
    // Before the core loads: it reads Dolphin.ini on startup.
    if (launchJob.platformSlug == "ngc") writeDolphinConfig(saveDir);
    // This person's Miis into every Wii game but the Mii Channel, whose
    // own are the master copy (#275).
    if (launchJob.platformSlug == "wii" && launchJob.coreName == "dolphin" &&
        !wii::isMiiChannel(launchJob.titleId))
        copyMiisInto(liveClient, launchJob.platformId, launchJob.platformFsSlug,
                     launchJob.coreName, saveDir);
    if (launchJob.coreName == "dolphin") shareDolphinCache(saveDir);
    // What each player's port holds. Only Wii says anything but a joypad:
    // a Classic Controller, or a GameCube pad for a game that takes only
    // that, in the pads' ports; and, while a Wii Remote is paired, a real
    // Remote in every port after them (#200). A game that takes neither
    // pad gets Remotes only: pads have no player in it, and it is greyed
    // out while no Remote is paired (coverageFor).
    sessionNeedsRemote = false;
    if (launchJob.platformSlug == "wii") {
        const unsigned device = wii::padDevice(wii::codeFromTitleId(launchJob.titleId));
        sessionNeedsRemote = device == 0;
        core.setPadDevice(device);
        if (wiiremote::anyPaired())
            core.setRealRemotesFrom(device == 0 ? 0 : static_cast<int>(players::connected().size()));
    }
    const std::string romForCore = launchJob.coreName == "dolphin"
                                       ? dolphinReadablePath(launchJob.romPath, launchJob.romId)
                                       : launchJob.romPath;

    std::vector<cab::FileSaveState> restored =
        restoreFileSaves(saveSpecs, launchJob.fsStem, saveDir, launchJob.romId,
                         launchTag, liveClient);

    core.setPlayers(players::count());
    if (core.isPs2()) openPs2Window();
    if (!core.loadGame(romForCore, storage::biosDir(), saveDir)) {
        closePs2Window();
        // AND SAY IT ON THE SCREEN, NOT ONLY TO STDERR — 2026-09-21.
        //
        // MMagTech: *"dreamcast game downloaded and didnt auto launch and
        // clicking play didnt launch it either."* The console knew exactly
        // why and had said so, to a log nobody on a sofa can read:
        //
        //   [launch] this core wants Vulkan and this device will not export
        //   memory as a file descriptor, so nothing it draws could reach
        //   the screen
        //
        // A refusal the person cannot see is indistinguishable from a
        // console that has stopped responding — and it is worse than a
        // crash, because pressing the button again does the same nothing
        // forever. The launch screen already has somewhere to put this:
        // `setNotice` is the same place a full disk reports itself.
        std::fprintf(stderr, "[launch] %s\n", core.error().c_str());
        // If the press came from somewhere with nowhere to put a message —
        // Home's Resume card, for instance — the console goes to the
        // game's own screen and says it there, rather than inventing a
        // second place for refusals to live. See refuseLaunch.
        refuseLaunch(launchJob.biosMissing.load()
                         ? "No " + launchJob.systemName + " BIOS on your server"
                         : std::string("Couldn't start this game"));
        return;
    }
    // Played now, so it is the LAST thing eviction should take rather than
    // whatever its download time says. The ENTRY's own mtime is the record —
    // see cache.h — so a game downloaded and never started stays oldest, and
    // it works the same whether the entry is one file or a folder.
    cache::touch(launchJob.entryPath);
    // The session, and the game's own save restored into it BEFORE the
    // first frame. A battery save is the game's progress; it has to be in
    // place when the game boots, not offered as a choice afterwards.
    // The game is loaded and the next frame is its first. Up it goes.
    curtain.retarget(0.0f, kCurtainUp);

    session = GameSession{};
    session.romId = launchJob.romId;
    session.raHash = launchJob.raHash;
    session.core = launchJob.coreName;
    session.platformSlug = launchJob.platformSlug;
    session.title = launchJob.title;
    session.saveDir = saveDir;
    session.fsStem = launchJob.fsStem;
    session.stateDir = storage::statesDir(
        user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
    session.fileSaves = std::move(restored);
    if (launchTag) session.saveTag = launchTag;
    session.snapshots = catalog::snapshotsAllowed(launchJob.coreName.c_str());
    if (!session.snapshots)
        std::fprintf(stderr, "[state] no save states on this system, true to the console\n");
    // THE DISCS, and the start disc checked against what the core says:
    // a core that took no start disc, or one put in before the first
    // frame by design (Saturn), gets it now, before any frame has run.
    if (!launchJob.discs.empty()) {
        session.discs = launchJob.discs;
        session.discOrder = discOrder;
        session.discChange = discSys.change;
        session.disc = startDisc;
        const int want = discIndexOf(session, startDisc);
        const int idx = core.discIndex();
        std::fprintf(stderr, "[disc] the core has %d disc(s), index %d (%s)\n",
                     core.discCount(), idx, core.discPath(idx).c_str());
        if (discSys.change == disc::Change::Automatic) {
            // Nothing to check or remember: see syncDiscFromCore.
        } else if (discSys.change == disc::Change::Insert && core.hasDiscControl() && idx != want) {
            if (discBoot == disc::Boot::InitialImage)
                std::fprintf(stderr, "[disc] the core started on index %d, not %d\n", idx,
                             want);
            discSwapNow(session, startDisc, "before the first frame");
        } else if (core.hasDiscControl()) {
            syncDiscFromCore(session);
        } else {
            disc::remember(user, session.romId, session.disc, disc::highest(session.discs));
        }
    }
    if (const char* tag = catalog::emulatorTag(launchJob.coreName.c_str())) {
        session.stateTag = tag;
    } else {
        std::fprintf(stderr,
                     "[sync] no settled tag for %s — states stay local%s\n",
                     launchJob.coreName.c_str(),
                     launchTag ? ", saves travel" : ", and so do saves");
    }
    // The baseline for a directory save, taken AFTER the restore above and
    // BEFORE the game has had a chance to write anything. What changes
    // between here and the quit is what this game saved. See GameSession.
    if (dirSub) {
        session.dirSaveRoot = saveDir + "/" + dirSub;
        session.dirSaveCore = launchJob.coreName;
        session.dirSavePlatform = launchJob.platformSlug;
        session.dirSaveTitleId = launchJob.titleId;
        session.dirAtLaunch = listDirSave(session);
        std::fprintf(stderr, "[save] %s holds %zu file(s) at launch\n",
                     session.dirSaveRoot.c_str(), session.dirAtLaunch.size());
    }

    // THE BATTERY SAVE: RomM's newest, unless this console's own copy is
    // still owed, and this console's copy whenever RomM has none to give
    // (#259). Until 2026-10-05 the local `.srm` syncSave writes at every
    // quit was never read back, so a game started with the server away
    // began with an empty battery, and one started while its upload was
    // still owed got RomM's OLDER copy over the newer one. The same rule
    // restoreFileSaves and restoreDirSave keep (cache.h, isPending), and
    // what RetroArch does: the frontend loads the local `.srm`.
    if (core.saveRamSize() > 0) {
        const std::string name = saveRowName(session.fsStem, std::string(), "srm");
        const std::vector<uint8_t> local = cab::readBytes(saveDir + "/" + name);
        const bool owed =
            !session.saveTag.empty() && cache::isPending(user, session.romId, name);
        bool fromServer = false;
        if (owed) {
            std::fprintf(stderr, "[save] %s has not reached the server yet - it wins\n",
                         name.c_str());
        } else if (!session.saveTag.empty()) {
            std::vector<romm::Asset> saves;
            std::string serr;
            if (savesFor(liveClient, session.romId, &saves, &serr)) {
                const romm::Asset* newest = nullptr;
                for (const auto& a : saves) {
                    if (a.emulator != session.saveTag) continue;
                    // THE BATTERY ROW ONLY. Cabinet files a Game Boy
                    // clock as its own 8-byte `.rtc` row under the same
                    // tag; taken as the newest, its bytes went over the
                    // start of the save (#346, found in the audit).
                    if (regionOfRow(a.fileName) != "srm") continue;
                    if (!newest || a.updatedAt > newest->updatedAt) newest = &a;
                }
                if (newest) {
                    std::vector<uint8_t> data =
                        saveBytesFor(liveClient, session.romId, *newest);
                    if (!data.empty() && core.writeSaveRam(data)) {
                        session.saveAtLaunch = data;
                        fromServer = true;
                        std::fprintf(stderr, "[save] restored %s (%zu bytes)\n",
                                     newest->fileName.c_str(), data.size());
                    }
                }
            } else {
                std::fprintf(stderr, "[save] could not ask the server: %s\n", serr.c_str());
            }
        }
        if (!fromServer && !local.empty() && core.writeSaveRam(local)) {
            session.saveAtLaunch = local;
            std::fprintf(stderr, "[save] restored this console's %s (%zu bytes)\n",
                         name.c_str(), local.size());
        }
    }
    if (session.saveAtLaunch.empty()) core.readSaveRam(session.saveAtLaunch);
    std::fprintf(stderr, "[save] battery is %zu bytes\n", core.saveRamSize());

    // Options again, AFTER the game is loaded. Some cores declare nothing
    // until they know what they are running: FBNeo's and MAME's options are
    // per-driver dipswitches, so their table does not exist until a machine
    // is chosen. A count taken only at core-load time reports zero for them
    // and looks like a core with nothing to configure.
    {
        const std::vector<cab::Core::OptionReport> opts = core.options();
        int asked = 0;
        for (const auto& o : opts) if (o.asked) ++asked;
        std::fprintf(stderr, "[options] %zu declared, %d asked for so far\n",
                     opts.size(), asked);
        // AND THE WHOLE TABLE, when asked for. --core-options-detail
        // cannot see these at all: a core that declares nothing until it
        // knows what it is running is invisible to an audit taken at core
        // load, and that is Dolphin, FBNeo and MAME — three of the most
        // configurable things this console ships.
        if (coreOptionsDetail) {
            for (const auto& o : opts) {
                std::fprintf(stderr, "  %s\n", o.key.c_str());
                if (!o.desc.empty())
                    std::fprintf(stderr, "      what   %s\n", o.desc.c_str());
                std::fprintf(stderr, "      now    %s%s\n", o.chosen.c_str(),
                             o.overridden ? "   (ours)" : "");
                if (!o.values.empty()) {
                    std::fprintf(stderr, "      takes ");
                    for (size_t i = 0; i < o.values.size(); ++i)
                        std::fprintf(stderr, "%s%s", i ? " | " : " ",
                                     o.values[i].c_str());
                    std::fprintf(stderr, "\n");
                }
            }
        }
        for (const std::string& k : core.undeclaredOptionAsks())
            std::fprintf(stderr, "[options] asked but never declared: %s\n",
                         k.c_str());
    }

    // OPEN THE AUDIO DEVICE. Until 2026-09-19 this happened ONLY on the
    // `--core` developer path, so **a game launched from the library had
    // no sound at all** — `audioStream` stayed null and nothing was ever
    // opened. Found on the A9 Pro by MMagTech simply listening, with
    // Crazy Taxi 2 running and PipeWire reporting zero streams.
    //
    // It survived this long because every audio claim in this project was
    // made by COUNTING SAMPLES out of drainAudio() rather than by hearing
    // anything — "2,384 frames, zero audio" is a sample count. A headless
    // VM has nothing to listen with, so the one test nobody could run is
    // the one that would have caught it.
    //
    // Opened here rather than at startup because the rate comes from the
    // core: av_info is not known until a game is loaded.
    //
    // AND SET FOR EVERY GAME, NOT ONLY THE FIRST. Until 2026-09-27 the
    // stream kept the rate of the first game played, so every later game
    // at another rate was played too fast or too slow. MMagTech played
    // Mortal Kombat II (SNES, 32040 Hz) and then DoDonPachi (FBNeo, 47997
    // Hz): its sound went into a stream playing two-thirds as fast, fell
    // further behind every second, and was still playing on Home after he
    // quit, which read as the game running on in the background. Emptied
    // here and at every exit, so no game hears another's leftovers.
    SDL_AudioSpec src{};
    src.format = SDL_AUDIO_S16;
    src.channels = 2;
    src.freq = static_cast<int>(core.avInfo().sampleRate);
    if (audioStream) {
        SDL_ClearAudioStream(audioStream);
        if (SDL_SetAudioStreamFormat(audioStream, &src, nullptr))
            std::fprintf(stderr, "[frontend] audio in at %d Hz\n", src.freq);
        else
            std::fprintf(stderr, "[frontend] could not set audio to %d Hz: %s\n", src.freq,
                         SDL_GetError());
    } else {
        audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                &src, nullptr, nullptr);
        if (audioStream) {
            SDL_ResumeAudioStreamDevice(audioStream);
            std::fprintf(stderr, "[frontend] audio out at %d Hz\n", src.freq);
        } else {
            // A console with no sound card is still a console.
            std::fprintf(stderr, "[frontend] no audio device: %s\n", SDL_GetError());
        }
    }

    {
        // THE SCREEN'S OWN RATE, read for every game, so the console does
        // the right thing on whatever television it is plugged into. Off
        // by a little (gamescope says 59.980 for a 60.000 TV) is fine: the
        // rate control below takes up the difference.
        //
        // 5% IS RETROARCH'S LIMIT (video_max_timing_skew, default 0.05):
        // the furthest a game's sound may be sped up or slowed to lock it.
        // Further than that and it is paced by the clock instead, with a
        // repeated frame now and then, which is what RetroArch does too.
        static constexpr double kMaxSkew = 0.05;
        const SDL_DisplayMode* dm =
            SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        const double hz = dm ? dm->refresh_rate : 0.0;
        paceScreenHz = hz;
        paceRatio = 1.0f;
        const double fps = core.avInfo().fps;
        paceEvery = 0;
        paceBaseRatio = 1.0;
        paceCount = 0;
        // Headless (offscreen) there is no refresh to lock to: nothing
        // waits for one, so a frame per loop would run the game flat out.
        const char* vd = SDL_GetCurrentVideoDriver();
        const bool realScreen =
            vd && std::strcmp(vd, "offscreen") != 0 && std::strcmp(vd, "dummy") != 0;
        // CABINETOS_PACE=clock: the old pacing, for comparing on the TV.
        const char* paceEnv = std::getenv("CABINETOS_PACE");
        const bool clockAsked = paceEnv && std::strcmp(paceEnv, "clock") == 0;
        if (realScreen && !clockAsked && !core.isPs2() && hz >= 20.0 && fps > 0.0) {
            const int k = std::max(1, static_cast<int>(std::lround(hz / fps)));
            const double ratio = hz / k / fps;
            if (std::fabs(ratio - 1.0) <= kMaxSkew) {
                paceEvery = k;
                paceBaseRatio = ratio;
            }
        }
        if (audioStream) SDL_SetAudioStreamFrequencyRatio(audioStream, 1.0f);
        if (paceEvery > 0)
            std::fprintf(stderr,
                         "[pace] screen %.3f Hz, game %.4f fps: locked, one game frame "
                         "every %d refresh%s, sound %+.2f%%\n",
                         hz, fps, paceEvery, paceEvery == 1 ? "" : "es",
                         (paceBaseRatio - 1.0) * 100.0);
        else
            std::fprintf(stderr, "[pace] screen %.3f Hz, game %.4f fps: paced by the "
                         "clock\n", hz, fps);
    }
    std::fprintf(stderr, "[launch] running %s\n", core.coreName().c_str());
    playing = true;
    // RetroAchievements, when this person signed in (achievements.h):
    // after the battery save and before the first frame it checks.
    ra::beginGame(session.raHash, session.platformSlug, core.isPs2());
    if (session.romId > 0) {
        playClock.begin(session.romId, playtime::wallMs());
        playCheckpointClock = 0.0f;
    }
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

void App::finishExit() {
    // TIME PLAYED (#128): the session ends as the game does, and goes to
    // RomM with the save.
    if (playClock.active()) {
        playtime::close(storage::currentUser(), playClock.finish(playtime::wallMs()));
        uploader.sendPlay();
        playedJustNow(session.romId);
    }
    syncSave(session, uploader);
    // The disc in the drive, kept for this person's next Play (#162);
    // GameCube changes its own discs, so the core is asked.
    if (!session.discs.empty()) {
        // LEFT DURING INSERT'S WAIT: the disc goes in and the lid closes
        // before anything else, as it would have a moment later.
        if (session.discInserting) {
            cab::Core& c = cab::Core::shared();
            c.setDiscIndex(discIndexOf(session, session.discInserting));
            c.setDiscEjected(false);
            std::fprintf(stderr, "[disc] left during the change: disc %d in, lid closed\n",
                         session.discInserting);
            session.discInserting = 0;
        }
        syncDiscFromCore(session);
        if (session.discChange != disc::Change::Automatic)
            std::fprintf(stderr, "[disc] left on disc %d of %d\n", session.disc,
                         disc::highest(session.discs));
        // THE DRIVE BACK ON THE LIST'S FIRST ENTRY BEFORE THE UNLOAD, for
        // every core with a drive. Flycast (a172e00) keeps its disc index
        // across games: retro_unload_game never resets it, and read_m3u
        // reads no line unless it is 0, so after a change to disc 2 the
        // NEXT Dreamcast game's list read as empty and Flycast tried the
        // .m3u itself as a disc ("Unknown disk format"), refusing every
        // Dreamcast game until the app restarted. Measured 2026-10-11
        // (#162). RetroArch does not do this; it has the same exposure.
        // Reported upstream: flyinghead/flycast#2541. THE LID IS CLOSED
        // AGAIN: Flycast keeps the lid too, and a lid left open started
        // the next Dreamcast game with it open (measured 2026-10-11).
        cab::Core& core = cab::Core::shared();
        if (core.hasDiscControl() && core.discIndex() > 0) {
            const bool opened = core.setDiscEjected(true);
            const bool set = core.setDiscIndex(0);
            const bool closed = core.setDiscEjected(false);
            std::fprintf(stderr, "[disc] the drive put back on the first entry before the "
                                 "unload: open %s, set %s, close %s\n",
                         opened ? "yes" : "no", set ? "yes" : "no", closed ? "yes" : "no");
        }
    }
    // Before the unload: rcheevos reads the core's memory until told not to.
    ra::endGame();
    // And the same curtain on the way out — `unloadGame` blocks too, and a
    // game vanishing into Home mid-frame is the same cut in the other
    // direction.
    curtain.from = curtain.to = 1.0f;
    curtain.elapsed = curtain.duration;
    curtain.retarget(0.0f, kCurtainUp);
    cab::Core::shared().unloadGame();
    closePs2Window();
    // AFTER the unload, for the one platform whose save is a tree. The
    // battery above is read out of the core's own memory and is finished
    // the moment the game stopped writing to it; a directory save is files
    // on a disk, and retro_unload_game is where a core flushes them. The
    // reference implementation captures this class after shutdown for
    // exactly that reason, and it is the same rule that lost saves here
    // once already when it was ignored.
    //
    // Safe to run twice: syncDirSave compares against its own baseline and
    // sends nothing when nothing moved.
    if (!session.dirSaveRoot.empty()) syncDirSave(session, uploader);
    // The HOME Menu's rumble switch, read back from the game's SYSCONF
    // now that Dolphin has written it (#274, wii.h). Every Wii game has
    // its NAND under its save directory.
    if (session.platformSlug == "wii" && session.core == "dolphin")
        wii::keepRumbleFrom(session.saveDir + "/User/Wii/shared2/sys/SYSCONF");
    // And the same trigger for the same reason, for the class that is one
    // file rather than a tree. This is where a Dreamcast's card is read
    // back out of the system directory and taken out of `bios/`.
    syncFileSaves(session, uploader);
    // Whatever sound the game had queued goes with it, rather than playing
    // on over Home (see where the stream's rate is set, at launch).
    if (audioStream) SDL_ClearAudioStream(audioStream);
    pendingStateId = 0;
    pendingStatePath.clear();
    pendingResume = false;
    stateHold = stateHoldWaiting = false;
    rewindKeep.reset();
    rewindAvailable = -1;
    rewindLastFrame = 0;
    rewindWorstMs = 0.0;
    detailStates.stale = true;   // Continue from may have changed
    playing = false;
    overlayOpen = false;
    overlayFade.retarget(0.0f, overlayFadeSeconds);
    dropPressesFromClosing();
    std::fprintf(stderr, "[overlay] exited to Home\n");
}

void App::exitToHome() {
    if (!cab::Core::shared().running()) {
        // Still building the machine. Close the overlay so the quit looks
        // like it was accepted — it has been — and let the loop finish it.
        exitPending = true;
        exitWaitStart = SDL_GetTicksNS();
        overlayOpen = false;
        overlayFade.retarget(0.0f, overlayFadeSeconds);
        std::fprintf(stderr, "[overlay] quit accepted; waiting for the core to boot\n");
        return;
    }
    finishExit();
}

void App::pumpExit() {
    if (!exitPending || !playing) return;
    // WALL CLOCK, NOT FRAMES, and that distinction is the whole fix. The
    // first version capped the wait at 120 FRAMES, which crashed in one
    // configuration and not another for a reason that looked like magic: a
    // capture at 1920x1080 and one at the 1024x768 window differ threefold
    // in fill rate on a software rasteriser, so the same 120 frames are
    // seconds in one and an instant in the other. The core's boot takes the
    // time it takes.
    const double waited = (SDL_GetTicksNS() - exitWaitStart) / 1e9;
    const bool up = cab::Core::shared().running();
    if (up || waited > 15.0) {
        std::fprintf(stderr, "[overlay] quitting after %.1fs%s\n", waited,
                     up ? "" : " — the core never booted, unloading anyway");
        exitPending = false;
        finishExit();
    }
}

// A WII REMOTE GAME DURING A STREAM STAYS PAUSED (MMagTech, 2026-10-07):
// the person streaming has no Remote, so Back does not resume it either.
// Exit to Home, and saving where there are states, still work; when the
// stream ends, Resume is back for whoever has the Remote.
// THE POWER MENU ON HOME SOUNDS LIKE EVERY OTHER MENU (#389): open is
// Activate, a move is Move, closing it is Back, and a choice in it is
// Activate. Over a game it stays silent, like the pause menu it shares a
// panel with (MMagTech: the pause menu may be the one exception).
void App::powerCue(sound::Cue c) {
    if (powerMenu && !playing) sound::play(c);
}

void App::closeOverlay(bool cue) {
    if (remoteOnlyHere() && !powerMenu) {
        sound::play(sound::Cue::Edge);
        return;
    }
    if (cue && overlayOpen) powerCue(sound::Cue::Back);
    overlayOpen = false;
    overlayFade.retarget(0.0f, overlayFadeSeconds);
    if (playing && choiceScreen.isOpen()) choiceScreen.close();
}

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

// A SCREENSHOT OF A GAME THIS WINDOW DID NOT DRAW: gamescope's own, of the
// game's plane only, which leaves the menu out (open question 24, and
// measured: about two seconds for a 4K PNG, written by gamescope after
// the command returns). The menu says so once the file is whole, and it
// travels to RomM's gallery the way every other screenshot does.
void App::standaloneScreenshot() {
    if (!standaloneShot.path.empty()) return;   // one at a time
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm utc{};
    gmtime_r(&ts.tv_sec, &utc);
    char stamp[48];
    const size_t n = std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &utc);
    std::snprintf(stamp + n, sizeof stamp - n, "-%03ld", ts.tv_nsec / 1000000L);
    std::string base = session.fsStem.empty() ? sanitisedStem(session.title) : session.fsStem;
    std::replace(base.begin(), base.end(), '/', '_');
    const std::string dir = storage::screenshotsDir(storage::currentUser()) + "/" +
                            std::to_string(session.romId);
    storage::makeDirs(dir);
    standaloneShot.name = base + " [" + stamp + "].png";
    standaloneShot.path = dir + "/" + standaloneShot.name;
    standaloneShot.romId = session.romId;
    standaloneShot.size = -1;
    const proc::Result r = proc::run({"gamescopectl", "screenshot", standaloneShot.path}, 5);
    if (!r.ok()) {
        std::fprintf(stderr, "[screenshot] gamescopectl: %s\n", proc::trimmed(r.err).c_str());
        standaloneShot.path.clear();
        menuNotice.say("Couldn't save the screenshot", Tone::Problem);
    }
}

// Twice a second or so: the file is whole when its size stops changing.
void App::pumpStandaloneShot() {
    if (standaloneShot.path.empty()) return;
    struct stat st;
    if (::stat(standaloneShot.path.c_str(), &st) != 0 || st.st_size <= 0) return;
    if (st.st_size != standaloneShot.size) {
        standaloneShot.size = st.st_size;
        return;
    }
    std::vector<uint8_t> png = cab::readBytes(standaloneShot.path);
    std::fprintf(stderr, "[screenshot] %zu bytes, %s\n", png.size(),
                 standaloneShot.name.c_str());
    menuNotice.say("Screenshot saved", Tone::Done);
    Uploader::Job job;
    job.romId = standaloneShot.romId;
    job.emulator = "screenshot";
    job.fileName = standaloneShot.name;
    job.data = std::move(png);
    job.localPath = standaloneShot.path;
    job.isScreenshot = true;
    uploader.push(std::move(job));
    standaloneShot.path.clear();
}

// AS A SETTINGS CHOICE ROW BEHAVES (settings.cpp): left and right stop at
// the ends with the edge sound, and A walks forward and wraps, so the row
// also works from A alone.
void App::stepChoice(int slot, int delta, bool wrap) {
    PauseChoice* c = choiceAt(slot);
    if (!c) return;
    const int count = static_cast<int>(c->values.size());
    int next = c->current + delta;
    if (wrap && next >= count) next = 0;
    if (next < 0 || next >= count) {
        sound::play(sound::Cue::Edge);
        return;
    }
    c->current = next;
    if (c->apply) c->apply(next);
    if (c->nextStart) menuNotice.say("Exit and relaunch to apply", Tone::Done);
    sound::play(sound::Cue::Move);
}

// ---- Change disc (#162) ----------------------------------------------
//
// ONE ROW IN THE PAUSE MENU, ONLY ON A GAME WITH SEVERAL DISCS, and
// everything else inside the question panel: every disc listed, earlier
// ones too, the one in the drive marked, focus on the next. Picking one
// asks Insert (the game keeps running) or Insert and restart.
//
// INSERT IS EJECT, LET THE GAME RUN, SET THE DISC, CLOSE: RetroArch
// waits 100 frames, and a Dreamcast game needs the tray seen open
// (flycast#1914). The menu closes at once so the wait runs in the game;
// a core that is paused never sees the lid (beetle-psx#694).
void App::beginInsert(int n) {
    if (session.discs.empty() || session.discInserting) return;
    cab::Core& core = cab::Core::shared();
    const bool opened = core.setDiscEjected(true);
    const double fps = core.avInfo().fps > 0 ? core.avInfo().fps : 60.0;
    session.discInserting = n;
    session.discCloseAt =
        core.framesRun() + static_cast<uint64_t>(std::max(1.0, fps * gDiscWaitSeconds));
    std::fprintf(stderr, "[disc] insert disc %d: lid open %s, closing in %.1fs of play\n", n,
                 opened ? "yes" : "no (already open?)", gDiscWaitSeconds);
    menuNotice.say("Changing disc\xE2\x80\xA6", Tone::Busy);
}

void App::pumpInsert() {
    if (!playing || !session.discInserting) return;
    cab::Core& core = cab::Core::shared();
    if (core.framesRun() < session.discCloseAt) return;
    const int n = session.discInserting;
    const int idx = discIndexOf(session, n);
    const bool set = core.setDiscIndex(idx);
    const bool closed = core.setDiscEjected(false);
    session.discInserting = 0;
    syncDiscFromCore(session);
    // A REWIND across a disc change would put the machine back on a disc
    // that is no longer in it.
    rewindKeep.reset();
    rewindLastFrame = core.framesRun();
    std::fprintf(stderr, "[disc] disc %d in: index %d %s, lid closed %s, the core says "
                         "index %d\n",
                 n, idx, set ? "set" : "REFUSED", closed ? "yes" : "no", core.discIndex());
    if (set && session.disc == n)
        menuNotice.say("Disc " + std::to_string(n) + " in", Tone::Done);
    else
        menuNotice.say("Couldn't change the disc", Tone::Problem);
}

void App::startOverOnDisc(int n) {
    if (session.discs.empty()) return;
    std::fprintf(stderr, "[disc] start over on disc %d\n", n);
    disc::remember(storage::currentUser(), session.romId, n, disc::highest(session.discs));
    gNextStartDisc = n;
    relaunchRomId = session.romId;
    if (choiceScreen.isOpen()) choiceScreen.close();
    exitToHome();
    // The exit just remembered the disc that was in; this one wins.
    disc::remember(storage::currentUser(), relaunchRomId, n, disc::highest(session.discs));
}

void App::pickDisc(int n) {
    // INSERT ONLY WHERE THE CORE HAS A DRIVE, and not for the disc
    // already in it.
    const bool canInsert =
        session.discChange == disc::Change::Insert && n != session.disc;
    // ONE THING TO DO IS SAID, NOT FOCUSED: a lone button nobody moved
    // onto does not read as selected.
    std::vector<std::string> options;
    if (canInsert) {
        options.push_back("Insert");
        options.push_back("Insert and restart");
    }
    const disc::Disc* picked = disc::byNumber(session.discs, n);
    askChoice(picked ? picked->label : "Disc " + std::to_string(n), "", options, 0,
              [&, n, canInsert](int answer) {
                  if (canInsert && answer == 0) {
                      closeOverlay();
                      beginInsert(n);
                  } else {
                      startOverOnDisc(n);
                  }
              });
    if (!canInsert) choiceScreen.setPrompt("Press", "A", "to insert and restart");
    choiceBack = [&]() { openDiscPanel(); };
}

void App::openDiscPanel() {
    std::vector<std::string> names, values;
    for (const disc::Disc& d : session.discs) {
        names.push_back(d.label);
        values.push_back(d.number == session.disc ? "In the drive" : "");
    }
    // Focus on the next disc in the drive's order, or the last.
    const int count = static_cast<int>(session.discs.size());
    int at = 0;
    for (int k = 0; k < count; ++k)
        if (session.discs[static_cast<size_t>(k)].number == session.disc) at = k;
    const int focus = at + 1 < count ? at + 1 : count - 1;
    // A row is a disc by its own number, not its place: with disc 2
    // missing on RomM the second row is disc 3.
    askChoice("Change disc", "", names, focus, [&](int k) {
        if (k >= 0 && k < static_cast<int>(session.discs.size()))
            pickDisc(session.discs[static_cast<size_t>(k)].number);
    });
    choiceScreen.setValues(values);
}

void App::overlayActivate() {
    if (powerMenu) {
        if (overlaySlot >= 0 && overlaySlot < static_cast<int>(powerItems.size()))
            powerActivate(powerItems[overlaySlot]);
        return;
    }
    if (overlaySlot < 0 || overlaySlot >= static_cast<int>(pauseItems.size())) return;
    switch (pauseItems[overlaySlot]) {
        case OvResume:
            overlayOpen = false;
            overlayFade.retarget(0.0f, overlayFadeSeconds);
            break;
        case OvSaveState: saveStateNow(session, uploader, menuNotice); break;
        case OvLoadState: beginLoadLatestState(stateLoad, session, liveClient, menuNotice); break;
        // THE PAUSED MOMENT, the menu not in it: a screenshot reads the
        // game's frame, not the screen. MMagTech's idea, 2026-09-27: pause
        // in an intense scene and take it without letting go of the game.
        // The menu stays open, saying "Screenshot saved".
        case OvScreenshot:
            if (standaloneRun.active()) standaloneScreenshot();
            else screenshotNow(session, uploader, menuNotice);
            break;
        case OvChoice: stepChoice(overlaySlot, +1, /*wrap=*/true); break;
        case OvDisc:
            if (session.discInserting) menuNotice.say("Not while the disc is changing", Tone::Info);
            else openDiscPanel();
            break;
        case OvExit:
            // An emulator of its own is asked to close (thawed first) and
            // the menu goes; finishStandalone takes the rest of the way
            // Home once it has gone, saves and all.
            if (standaloneRun.active()) {
                overlayOpen = false;
                overlayFade.retarget(0.0f, overlayFadeSeconds);
                standaloneRun.stop();
            } else {
                exitToHome();
            }
            break;
        default: break;
    }
}

void App::toggleOverlay() {
    if (!playing && !standalonePaused) return;
    if (overlayOpen && remoteOnlyHere() && !powerMenu) {
        sound::play(sound::Cue::Edge);
        return;
    }
    overlayOpen = !overlayOpen;
    // A question asked over the menu goes with it (Change disc).
    if (!overlayOpen && playing && choiceScreen.isOpen()) choiceScreen.close();
    // Only on the way IN: the list must not change under a panel that is
    // still fading out.
    if (overlayOpen) {
        powerMenu = false;
        pauseItems.clear();
        if (!remoteOnlyHere()) pauseItems.push_back(OvResume);
        if (session.snapshots) {
            pauseItems.push_back(OvSaveState);
            pauseItems.push_back(OvLoadState);
        }
        // In every game, as the shortcut is (MMagTech: screenshots go
        // wherever the shortcut button is recognised). PS2 reads PCSX2's
        // frame since #130; GameCube is a libretro core like the rest.
        pauseItems.push_back(OvScreenshot);
        // CHANGE DISC (#162), only on a game with several discs and only
        // where the console changes them (not GameCube: Dolphin does).
        // After the three one-press rows and beside Look, the other
        // "Name: value" row (MMagTech on the TV, 2026-10-11).
        if (session.discs.size() >= 2 &&
            (session.discChange == disc::Change::Insert ||
             session.discChange == disc::Change::StartOver))
            pauseItems.push_back(OvDisc);
        // PICTURE QUALITY, on the nine systems where a level changes
        // something, and with a PIN set only for the owner, as in
        // Settings (MMagTech, 2026-10-02).
        pauseChoices.clear();
        if (quality::hasLevels(session.core) &&
            (!accounts::pinIsSet() || accounts::activeId() == accounts::ownerId())) {
            PauseChoice c;
            c.label = "Picture";
            c.values.push_back("Console");
            for (int l = 0; l < quality::kLevelCount; ++l)
                c.values.push_back(quality::levelName(static_cast<quality::Level>(l)));
            const std::optional<quality::Level> own = quality::gameChoice(session.romId);
            c.current = own ? static_cast<int>(*own) + 1 : 0;
            c.nextStart = true;
            const int romId = session.romId;
            c.apply = [romId](int i) {
                quality::setGameChoice(romId, i == 0 ? std::nullopt
                                                     : std::optional<quality::Level>(
                                                           static_cast<quality::Level>(i - 1)));
            };
            pauseChoices.push_back(std::move(c));
        }
        // WIDESCREEN (#368), for everyone, only on a PS2 game whose patch
        // file has a "Widescreen 16:9" section: a row that does nothing
        // is worse than none. Some patches leave the HUD stretched, and
        // some players want 4:3. From the next start, as Picture is.
        if (session.core == "pcsx2" && ps2::hasWidescreenPatch()) {
            PauseChoice c;
            c.label = "Widescreen";
            c.values = {"On", "Off"};
            c.current = ps2::widescreenOn(session.romId) ? 0 : 1;
            c.nextStart = true;
            const int romId = session.romId;
            c.apply = [romId](int i) { ps2::setWidescreenOn(romId, i == 0); };
            pauseChoices.push_back(std::move(c));
        }
        // THE SCREEN LOOK (#122), for everyone, on the systems this
        // console draws itself. Changes at once, behind the menu.
        if (session.standaloneCore.empty() &&
            !screenfx::looksFor(session.platformSlug).empty()) {
            PauseChoice c;
            c.label = "Look";
            for (const screenfx::Look& l : screenfx::looksFor(session.platformSlug))
                c.values.push_back(l.label);
            c.current = screenfx::chosen(session.platformSlug);
            const std::string slug = session.platformSlug;
            c.apply = [slug](int i) { screenfx::choose(slug, i); };
            pauseChoices.push_back(std::move(c));
        }
        // THE SYSTEM'S OWN ROWS (#73), for everyone: they are looks.
        // Built-in cores only; none of the separate emulators has any.
        if (session.standaloneCore.empty()) {
            for (const sysopts::Option& o : sysopts::forSystem(session.platformSlug)) {
                PauseChoice c;
                c.label = o.label;
                for (const sysopts::Choice& ch : o.choices) c.values.push_back(ch.label);
                c.current = sysopts::chosen(session.platformSlug, o);
                const std::string slug = session.platformSlug;
                const sysopts::Option* opt = &o;
                c.apply = [slug, opt, &pauseRedrawFrames = pauseRedrawFrames](int i) {
                    sysopts::choose(slug, *opt, i);
                    for (const auto& kv : opt->choices[i].sets)
                        cab::Core::shared().setOptionLive(kv.first, kv.second);
                    pauseRedrawFrames = 2;
                };
                pauseChoices.push_back(std::move(c));
            }
        }
        for (size_t i = 0; i < pauseChoices.size(); ++i) pauseItems.push_back(OvChoice);
        pauseItems.push_back(OvExit);
    }
    overlaySlot = 0;
    overlayFade.retarget(overlayOpen ? 1.0f : 0.0f, overlayFadeSeconds);
    overlayFocus.retarget(1.0f, kOverlayFocusDuration);
    overlayFocus.elapsed = kOverlayFocusDuration;
}

// THE PAUSE MENU OVER A GAME THIS WINDOW DID NOT DRAW (#169). The same
// menu as every other system, from the same code; what differs is only
// what "pause" means. The emulator is frozen where it is, its controllers
// are let go, and this window, already the overlay, takes the controllers
// and draws the menu over the frozen picture. Resume hands them back.
void App::openStandaloneMenu() {
    if (!standaloneRun.active() || standaloneRun.stopping() || standalonePaused) return;
    standaloneRun.freeze();
    cab::vpad::releaseAll();
    rumble::update(false);
    cab::overlaywin::mark(window, /*takeInput=*/true);
    standalonePaused = true;
    for (auto& sh : standaloneShortcut) sh = {};
    for (auto& bs : backStart) bs = {};
    toggleOverlay();
    std::fprintf(stderr, "[standalone] paused\n");
}

void App::remotePlayHandover() {
    const bool on = rpWant && remoteplay::streaming();
    if (on == players::streaming()) return;
    players::setStreaming(on);
    // A DEVICE ARRIVING COUNTS AS SOMEBODY THERE (#294): the idle timer
    // starts again and a dim or dark screen lifts, so the phone sees
    // the console, not the black layer, and its first press is not
    // spent waking it. Only the timer: no press reaches the menus. The
    // screen itself was already woken by the start step, before Sunshine
    // looked (cabinetos-remoteplay).
    if (on) idleWatch.input(clockSeconds());
    // Remote Play's section greys out for the stream, and back after. A
    // panel already open from it (Paired devices, Tailscale's menu)
    // closes, so nothing in it can be pressed from the stream either.
    if (here() == Screen::Settings) {
        if (on && choiceScreen.isOpen()) choiceScreen.close();
        buildSettings();
    }
    if (playing && !overlayOpen) toggleOverlay();
    else if (standaloneRun.active()) openStandaloneMenu();
    // A Remote game's pause menu already open loses or gets back its
    // Resume with the handover.
    if (playing && overlayOpen && !powerMenu) {
        const bool has = !pauseItems.empty() && pauseItems.front() == OvResume;
        if (remoteOnlyHere() && has) pauseItems.erase(pauseItems.begin());
        else if (!remoteOnlyHere() && !has) pauseItems.insert(pauseItems.begin(), OvResume);
        overlaySlot = 0;
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
