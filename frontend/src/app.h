// The App: everything main() held, as one struct (#398, restructure batch c).
//
// main() was one function: its locals were the console's whole state and its
// lambdas reached them through [&]. They are the members and member functions
// of this struct now, in main()'s order and unchanged, so the next batches can
// move one feature at a time into its own file. The bodies stay in main.cpp;
// main.cpp's main() builds one App and calls run(), which is main()'s old
// body: the start-up in today's order, the frame loop, the teardown, the
// restart.
//
// MEMBER ORDER IS DESTRUCTION ORDER, backwards. It is main()'s declaration
// order with three moves: resolvedAddress beside rommAddress, which points
// into it; liveClient before images, whose workers call it, and before every
// job whose thread uses it; kManualTemp before manualFetch. Every thread's
// stop is declared after the job it stops, so it runs first.
//
// NOT COPYABLE OR MOVABLE: members hold references and pointers to other
// members, and threads and stored callbacks hold `this`.

#pragma once

#include <SDL3/SDL.h>

#include <unistd.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "accounts.h"
#include "achievements.h"
#include "achlist.h"
#include "battery.h"
#include "bluetooth.h"
#include "choice.h"
#include "design.h"
#include "disc.h"
#include "downloads.h"
#include "drives.h"
#include "files.h"
#include "idle.h"
#include "image.h"
#include "keyboard.h"
#include "look.h"
#include "manualview.h"
#include "net.h"
#include "pin.h"
#include "players.h"
#include "playtime.h"
#include "remoteplay.h"
#include "report.h"
#include "rewind.h"
#include "romm.h"
#include "screens.h"
#include "server.h"
#include "settings.h"
#include "setup.h"
#include "sound.h"
#include "standalone.h"
#include "steam.h"
#include "tailscale.h"
#include "text.h"
#include "ui.h"
#include "update.h"
#include "vpad.h"
#include "wiiremote.h"
#include "wikiqr.h"

// Moved out of main.cpp earlier in the restructure (#398).
#include "gamesession.h"
#include "launch.h"
#include "library.h"
#include "notice.h"
#include "states.h"
#include "uploader.h"

struct App {
    App() = default;
    App(const App&) = delete;
    App& operator=(const App&) = delete;
    App(App&&) = delete;
    App& operator=(App&&) = delete;

    // main()'s body (main.cpp).
    int run(int argc, char** argv);


    // ---- Focus -------------------------------------------------------------
    //
    // Home is rows, not one list. Focus is therefore a ROW and a SLOT within it,
    // never a single index: `focused` used to index the shelf, and that is only
    // the same number while the shelf is the only thing on screen.
    //
    //   row 0   the hero      slot 0 the card, slot 1 the Resume pill
    //   row 1   Recent
    //   row 2   Favorites, when there are any
    //
    // The hero takes focus on arrival, which is what "resume-first" means: the
    // thing you were playing is already under the cursor.
    // THE TOP BAR IS A ROW, above the hero. It is drawn OVER the hero's top
    // edge rather than above it, because Home has 60 points of slack and a bar
    // costs about 105 — docs/PROJECT.md priced the three options as "either
    // the bar fits, or Home's hero comes down, or the bar goes elsewhere", and
    // overlaying it is "elsewhere". It is the only one that does not undo
    // Home fitting on one screen.
    //
    // Always visible rather than revealed by pressing Up. Up from the hero is
    // a dead input today and it is tempting to spend it, but a console that
    // hides Settings behind an undiscoverable gesture is constraint 3 — the
    // one about leaving somebody stuck — and showing it costs nothing.
    //
    // No "Home" item: Home is the root and Back returns to it, so a
    // destination that does nothing when you are already there only teaches
    // people the bar is decorative. Cabinet's iOS reaches the same shape from
    // the other side, three destinations with Settings demoted, on the rule
    // that "reach should track frequency".
    // THE HERO ROW IS GONE — 2026-09-21.
    //
    // Home was bar / hero / Recent / Favorites, where the hero was the most
    // recently played game lifted out of Recent and given an 1800x340 card of
    // its own. The card was mostly not artwork: a 3:4 cover fitted in the
    // middle of a 16:5 box, with the same cover blurred either side to fill the
    // space it could not. MMagTech, looking at it: *"the homepage needs some
    // work"*, and that band was the thing.
    //
    // It is now three rows, and the most recent game is simply the first card
    // on Recent — which is what it was before it was taken out. Resume-first is
    // unchanged and is now cheaper: focus opens on that card, and A launches it
    // rather than opening its launch screen. See `activateHome`.
    //
    // WHAT THE 284 POINTS BOUGHT: the shelf covers are back at the reference
    // implementation's own 260x347. They were cut to 158x210 to fit two shelves
    // under the hero — design.h records that as "about 430 points had to come
    // out of a 1080 canvas". Most of it has come back.
    // THE BAR IS NOT ONE OF HOME'S ROWS — changed 2026-09-21.
    //
    // It was, back when it was drawn on Home and nowhere else. Now it is drawn
    // over every browsing screen, and MMagTech asked the obvious question:
    // *"if the library has the top bar in view shouldnt i be able to up and
    // access it."* Yes. Chrome that is on screen and cannot be reached is worse
    // than chrome that is hidden, because it reads as the console having
    // stopped responding rather than as a thing that is not there.
    //
    // So the bar's focus belongs to the APP — `barFocused` and `barSlot` — and
    // every screen hands focus up to it the same way, through
    // screens::Action::FocusBar. Two mechanisms for one piece of chrome is how
    // a shelf and a grid end up disagreeing about what focus looks like, which
    // is the same reason design.h exists.
    enum Row { RowRecent = 0, RowFavorites = 1 };
    // THE CHIP IS A BAR SLOT NOW, and it is the only one that is not a
    // capsule: it is drawn as the avatar disc at the far right, so the label
    // loop below stops at BarSettings and the chip takes its own focus rim.
    // Everything else about it — L1/R1 walking onto it, Down leaving the bar —
    // it gets for free by being in this list.
    //
    // HOME IS THE FIRST ITEM — #127, MMagTech 2026-09-27. It was left out on
    // 2026-09-21 because a bar item that does nothing where you stand teaches
    // people the bar is decorative. Since 2026-09-24 moving across the bar
    // switches the screen, so the lit item is always where you stand, and
    // Home is simply one of them. A bar slot is now its destination's number.
    enum BarItem { BarHome = 0, BarLibrary, BarSearch, BarSettings, BarAccount, BarCount };

    // ---- Where we are -------------------------------------------------------
    //
    // A stack, not a mode flag. Each tab owns a navigation stack and Library
    // pushes to a grid; backing out has to land where the person came from
    // rather than at a fixed home. The launch screen is a full-screen COVER
    // over whatever was behind it, and the player is a cover over that — which
    // is what makes quitting a game return to the launch screen and backing out
    // again return to the browsing.
    enum class Screen { Home, Library, Grid, Detail, Search, AddAccount, TailscaleSignIn, Report,
                        HowTo, Settings };

    // THE TILE COVERS, FETCHED BEHIND HOME RATHER THAN BEFORE IT.
    //
    // A platform tile's artwork is the first cover among its games, and until
    // open question 28 it came for free because boot had fetched every game
    // anyway. It is one request per platform on its own: 1.55 s against the
    // reference server, measured, which is more than the whole of the rest of
    // a boot. Paying it at startup would have handed most of the win back for
    // thumbnails.
    //
    // MMagTech's call, 2026-09-22, given the choice between paying it, using
    // RomM's platform logo instead, and this: the tiles come up in their
    // colour — which the Library screen already treats as a normal state, not
    // a fault — and the covers arrive a second or so later. The finished
    // screen looks exactly as it did before.
    //
    // IT HAPPENS ON EVERY BOOT, because nothing about a library is kept
    // between them. Within a session it happens once.
    //
    // THE WORKER TOUCHES NOTHING THE FRAME THREAD OWNS. It returns cover
    // PATHS — two ints and a string — and the frame loop is what applies them.
    // Appending to the store from another thread would be a data race against
    // every screen that draws from it.
    struct CoverFill {
        std::mutex m;
        std::vector<std::pair<int, std::string>> done;   // platform id -> cover
        std::vector<int> want;
        std::atomic<size_t> next{0};
        std::atomic<bool> quit{false};
        std::vector<std::thread> workers;
    };
    // THE REST OF A BIG GRID, BEHIND THE FIRST PAGE.
    //
    // A grid is proportional to its own platform — about 1 ms a game — so the
    // first page is drawn at once and the remainder arrives behind it. The
    // worker returns GAMES and the frame loop is what puts them in the store
    // and on the screen, for the same reason the cover fill returns paths:
    // appending to the store from another thread would race every screen that
    // draws from it.
    struct GridFill {
        std::mutex m;
        std::vector<romm::Game> arrived;
        std::atomic<bool> running{false};
        std::atomic<bool> quit{false};
        std::string tileQuery;   // the tile being filled (Tile::query)
        std::thread th;
        void stop() {
            quit.store(true);
            if (th.joinable()) th.join();
            quit.store(false);
            std::lock_guard<std::mutex> lk(m);
            arrived.clear();
        }
    };
    // The library being fetched for a console that started offline (#88).
    struct OnlineLoad {
        std::thread th;
        std::atomic<bool> running{false}, ready{false};
        int forAccount = 0;
        Library lib;
        ~OnlineLoad() { if (th.joinable()) th.join(); }
    };
    // THE BATTERY ROW'S ENTRIES AS THEY MOVE (#293, MMagTech 2026-10-08): a
    // pad switching on fades in where it goes while the others slide aside to
    // make room; one switching off fades out first, then the rest close up.
    // Each is the pad itself (its key) or the Remote (its address), so a number
    // closing up (P3 becoming P2) slides rather than swaps. The bar coming
    // back (from a game, a game's page) lays it out at once: nothing slides
    // that was not seen to change.
    struct BatteryEntry {
        std::string id;   // "pad:<key>" or "wii:<address>"
        std::string label;
        battery::Reading r;
    };
    struct BatterySlot {
        BatteryEntry e;
        design::Animated offset;   // its left edge, leftward from the row's right end
        design::Animated alpha;
        bool leaving = false;
    };
    // THE SHORTCUT BUTTON, per player, while a game plays (shortcuts.h).
    // `before` is what was already held when it went down, so only buttons
    // pressed DURING the hold count; `used` is whether one was, which is what
    // tells a tap (the pause menu) from a hold (a shortcut) on release.
    //
    // A HOLD BELONGS TO THE PAD THAT PRESSED IT. A pad is switched off by
    // holding its Home button, so it leaves with its shortcut button down and
    // never sends the release. The hold used to stay with the player, and the
    // next pad to be that player (the same one, switched back on) read as
    // letting go: a tap, which closed the pause menu the pad's going off had
    // opened (2026-10-07, every reconnect on Genesis). `pad` is whose hold it
    // is; another pad in the seat drops it unread.
    struct ShortcutHold {
        bool down = false, used = false;
        uint32_t before = 0;
        SDL_JoystickID pad = 0;
    };
    enum OverlayItem { OvResume = 0, OvSaveState, OvLoadState, OvScreenshot, OvChoice, OvExit,
                       OvDisc, OvCount };
    // CHOICE ROWS in a list of buttons: "Picture: Console" (#63) and a
    // system's own options, "3D glasses: Off" (#73). Built when the menu
    // opens, in the order they appear, between Screenshot and Exit to Home.
    // Left and right change one; A steps it on and wraps.
    struct PauseChoice {
        // "Picture", not "Picture quality": "Picture quality: Quality" says it
        // twice (MMagTech, 2026-10-02).
        std::string label;
        std::vector<std::string> values;
        int current = 0;
        // Says "Exit and relaunch to apply" when changed (MMagTech,
        // 2026-10-12, #368: "Exit" points at Exit to Home, the way to do it;
        // it was "From the next start"): Picture and Widescreen do, a system
        // row changes in front of the game and needs no word.
        bool nextStart = false;
        std::function<void(int)> apply;
    };
    enum PowerItem { PwResume, PwCancel, PwRest, PwRestart, PwPowerOff, PwSteam };

    // Opening the launch screen for a card. Everything it shows is decided
    // here, so the screen holds no opinion about where any of it came from.
    // CONTINUE FROM, fetched when a game's page opens: the newest three states
    // RomM holds for the emulator Play would run, with their pictures. Network
    // work, so on a worker, one at a time; a page opened while one is out
    // waits its turn in `wantRom` rather than blocking the frame on a join.
    struct DetailStates {
        std::mutex m;
        std::thread worker;
        std::atomic<bool> busy{false}, ready{false};
        int romId = 0;
        std::vector<screens::StateChoice> list;
        std::string saveWhen;
        screens::DetailScreen::Facts facts;
        int wantRom = 0;
        std::string wantTag, wantSaveTag, wantStateDir, wantSlug;
        // The console's own states in `list` (#88): a StateChoice id of -(i+1)
        // is localPaths[i]. Offline, they are the states; online, only the
        // ones still waiting to upload are added to RomM's.
        std::vector<std::string> localPaths;
        // Asked again once the uploads are done: a game just left may have
        // saved a state that is still on its way to RomM.
        bool stale = false;
        ~DetailStates() { if (worker.joinable()) worker.join(); }
    };

    // A GAME'S MANUAL, FETCHED (#364): into the `Manuals` folder beside the
    // game when the game is on a drive (cache.h), so it is there offline and
    // follows the game from then on; for a game that is not, into /tmp for as
    // long as it is being read. One at a time, on a worker. A manual that
    // will not come never stops anything: it is said in the log, and a game
    // downloading or starting beside it carries on.
    struct ManualFetch {
        struct Job {
            int romId = 0;
            std::string from, to;
            bool forViewer = false;
        };
        std::mutex m;
        std::condition_variable cv;
        std::deque<Job> jobs;
        std::thread th;
        bool stop = false;
        // The viewer's answer: which game, where, and whether it came.
        bool done = false;
        int doneRom = 0;
        std::string donePath;
        bool doneOk = false;
        ~ManualFetch() {
            {
                std::lock_guard<std::mutex> lk(m);
                stop = true;
            }
            cv.notify_all();
            if (th.joinable()) th.join();
            // A manual fetched only to be read is not kept past the app.
            ::unlink("/tmp/cabinetos-manual.pdf");
        }
    };

    // ADDING SOMEBODY, ON A CLIENT OF ITS OWN.
    //
    // THE SEPARATE CLIENT IS THE WHOLE POINT and it is the reference
    // implementation's rule too: pairing on the live client would swap the
    // token under whoever is playing, and a half-finished pairing would leave
    // `firstrun` looking at a console it cannot classify. Nothing here touches
    // `liveClient` until `recordPairing` has succeeded — and even then it only
    // writes the account, because adding somebody must not sign anybody out.
    struct AddJob {
        std::thread th;
        std::mutex m;
        std::atomic<bool> running{false};
        romm::Pairing pairing;
        bool haveCode = false;
        bool finished = false;
        bool ok = false;
        accounts::Paired who;
        std::string err;
    };
    // LEAVING: Sign out, or a new server address saved. The curtain is the
    // startup screen with this line under the name, and once it is down (and
    // uploads are through) the app starts itself again; see pumpLeave.
    enum class Leave { None, SignOut, NewAddress, Steam };

    // ---- Settings' rows -----------------------------------------------------
    //
    // docs/PROJECT.md, open question 31. The screen is handed rows and hands
    // back an id; everything a row SHOWS is read here, fresh, each time
    // Settings opens or a row changes.
    enum SettingId { SetAddAccount = 1, SetInterfaceSounds, SetPinSet, SetPinChange,
                     SetPinOff, SetRemoveAccount, SetScreenOff, SetWifi, SetServer,
                     SetUpdate, SetUpdateCheck, SetCredits, SetFiles, SetDownloads,
                     SetAddController, SetShortcuts, SetShortcutButton, SetAppearance,
                     SetDarkHours, SetColour, SetRumble, SetPictureQuality,
                     SetWiiRemotes, SetSteam, SetSteamShow, SetRetroAchievements,
                     SetAchievementSound, SetDeveloper, SetVersion, SetRemotePlay,
                     SetPairedDevices, SetTailscale, SetReport, SetHowTo, SetPads };
    // THE NETWORK IS ASKED OFF THE FRAME THREAD. net::status() is three or four
    // nmcli round trips, which is a visible hitch if the screen waits for it,
    // so the row says "Checking" until the answer lands.
    struct SettingsNet {
        std::mutex m;
        bool have = false;
        bool fresh = false;       // arrived and not yet shown
        net::Status st;
        std::thread th;
        ~SettingsNet() { if (th.joinable()) th.join(); }
    };

    // ---- Wi-Fi, in Settings > Network (issue #59) -------------------------
    //
    // WHAT IS ON THE AIR, OFF THE FRAME THREAD: what NetworkManager already
    // knows first (quick), then a real scan (seconds on the A9). Each answer
    // rebuilds the rows if Settings is still showing. A scan still running is
    // left to finish rather than waited on.
    struct SettingsWifi {
        std::mutex m;
        bool fresh = false;
        std::vector<net::Network> list;
        std::atomic<bool> running{false};
        std::thread th;
        ~SettingsWifi() { if (th.joinable()) th.join(); }
    };

    // ---- Add a controller, a window over Settings (issue #65) --------------
    //
    // A WINDOW LIKE WI-FI'S, NOT FIRST RUN'S SCREEN. MMagTech, 2026-09-26:
    // first run's full screen felt like leaving Settings. While it is open a
    // worker listens in short rounds and the list is only what it has HEARD
    // since opening, so a pad's stale identities from other modes never show.
    // Choosing one pairs it in place; the new pad's first button press closes
    // it. Pairing and listening take turns on the one worker: bluetoothctl
    // does not want two at once.
    struct PadWindow {
        std::mutex m;
        std::map<std::string, bt::Device> heard;   // by address, since opening
        bool fresh = false;
        bool noBluetooth = false;
        std::string pairAddress;                   // asked for, not started
        bool pairing = false;
        bool pairDone = false, pairOk = false;
        std::string pairError;
        std::atomic<bool> stop{false};
        std::atomic<bool> cut{false};              // ends a listening round early
        std::thread th;
        void quit() {
            stop = true;
            cut = true;
            if (th.joinable()) th.join();
        }
        ~PadWindow() { quit(); }
    };
    // ---- Pair a Wii Remote (#200) ---------------------------------------------
    //
    // The Wii's own search, over and over, until a Remote answers or the
    // window is closed; then it is paired, trusted and connected, and the
    // sensor bar is asked, as a Wii asks it. One pass of the search is about
    // 12 seconds and a Remote's red button keeps it findable for longer.
    struct WiiPairJob {
        std::mutex m;
        std::string detail;   // the line under the title, when it changes
        std::string address;  // the Remote paired, once done
        bool fresh = false, done = false;
        std::atomic<bool> stop{false};
        std::thread th;
        void quit() {
            stop = true;
            if (th.joinable()) th.join();
        }
        ~WiiPairJob() { quit(); }
    };
    // JOINING BLOCKS FOR UP TO 45 SECONDS (net::join), so it is a job too. The
    // row being joined says so; the answer arrives in the notice pill.
    struct WifiJob {
        std::mutex m;
        std::thread th;
        std::atomic<bool> running{false};
        bool done = false, ok = false;
        std::string ssid, err;
        ~WifiJob() { if (th.joinable()) th.join(); }
    };
    // ---- Change server address (issue #60) -------------------------------
    //
    // IS IT THE SAME SERVER, off the frame thread: reaching an address that
    // is not there costs curl's connect timeout. The keyboard stays up saying
    // "Checking…" while it runs, the Wi-Fi password's shape.
    struct ServerJob {
        std::mutex m;
        std::thread th;
        std::atomic<bool> running{false};
        bool done = false;
        server::Check result = server::Check::Failed;
        std::string address, detail;
        Uint64 startedAt = 0;
        ~ServerJob() { if (th.joinable()) th.join(); }
    };

    // THE CURTAIN COMES DOWN ON THE STARTUP SCREEN, and pumpLeave does the
    // rest once it is down.
    // REMOVING A PERSON DELETES THEIR FOLDER (#194, MMagTech 2026-10-04). A
    // person deleted and made again on RomM gets a new id, so to RomM and to
    // this console they are someone new, and a folder kept "just in case" is
    // left behind for good. So Remove sends what they still owe RomM, with
    // their own login (they are never the person signed in: accounts::remove
    // refuses that), and only then deletes. Off the frame thread, because a
    // server that does not answer costs curl's whole timeout.
    struct RemoveJob {
        std::mutex m;
        std::atomic<bool> running{false};
        bool finished = false;
        int left = 0;   // still owed after trying
        accounts::Account who;
        bool everyone = false;   // Sign out: every account's, then leave
    };

    // ---- Steam (#223) ------------------------------------------------------
    //
    // The tile, its first-pick screen, its size and its removal. Everything
    // that runs the root helper, walks the cache or asks the network runs in
    // ONE background job at a time (steamJob), and its answer is acted on here,
    // on the frame thread, by pollSteamJob. A pill says what is happening
    // meanwhile. The handover itself is the leave curtain: "Starting Steam",
    // then the frontend quits and the session runs Steam (cabinetos-session).
    struct SteamJob {
        std::thread th;
        std::atomic<bool> done{false};
        std::function<void()> finish;
        std::string busy;        // the pill while it runs, said again until done
        Uint64 saidAt = 0;
    };
    // Steam's install in progress, for the panel's line (installSteam).
    struct SteamInstall {
        std::atomic<bool> cancel{false};
        std::atomic<int64_t> bytes{-1};
        bool ok = false;
        std::string why;
    };

    // What the first-pick screen and the size panel show, read off the frame
    // thread: what the main drive could give Steam without touching a kept
    // game, the default, and whether the console is online (Steam downloads
    // itself the first time).
    struct SteamSizes {
        int64_t room = 0;      // the most Steam can have
        int64_t size = 0;      // what is chosen; 0 = does not fit
        int64_t drive = 0;     // the main drive, Steam's slice included
        int64_t current = 0;   // the slice now, 0 when not set up
        bool online = false;
    };

    // WHO OWNS THE INPUT, asked once per event rather than decided again at
    // every call site.
    //
    // docs/PROJECT.md states the rule architecturally: while a game runs the
    // controller belongs to the core exclusively; while an overlay is open it
    // belongs to the UI; never both. It also says to implement the RULE rather
    // than the routing, because "any design where a button can mean two things
    // at once is the same bug" — the one reported from real hardware as
    // "controllers work on the homescreen but in game b exits the game".
    //
    // This was exactly that bug: arrow keys moved the Tetris piece AND shifted
    // focus on the Home screen behind it, so leaving the game landed somewhere
    // nobody chose. Found by someone actually playing it.
    enum class InputOwner { Keyboard, Overlay, Game, UI };
};
