// The screens past Home: Library, a grid of games, and the launch screen.
//
// Home is resume-first and it is one screen. Everything else in the product is
// reached from the Library, and until this existed 1100 playable games had
// exactly fifty of them reachable — the ones Home happened to show.
//
// WHY THESE ARE NOT IN main.cpp. Each screen owns focus, scroll and remembered
// state that only it understands, and the alternative is one function holding
// four screens' worth of indices at once. What they deliberately do NOT own is
// anything that touches the network, the disk or a core: a screen returns an
// Action and the app decides what that means. So a screen can be drawn in a
// test, and nothing here can start a download by accident.
//
// THE NAVIGATION MODEL, from docs/PROJECT.md:
//
//   - Each tab owns a navigation stack. Library pushes to a platform's grid.
//   - The launch screen is a FULL-SCREEN COVER, not a push. It replaces the
//     screen entirely, with the artwork as its own backdrop.
//   - The player is a full-screen cover over that. So quitting a game returns
//     to the launch screen, and backing out again returns to where the person
//     was browsing.
//   - No screen titles that repeat the tab: Library has no "Library" heading.
//     A pushed page carries its title as ordinary content at the top of its own
//     scroll view, never as system chrome.

#pragma once

#include <string>
#include <vector>

#include "design.h"
#include "image.h"
#include "text.h"
#include "ui.h"

namespace screens {

// Everything a screen needs to draw itself. References, because the app owns
// all of it and a screen outlives none of it.
struct Ctx {
    ui::Renderer& r;
    ui::TextRenderer& text;
    ui::ImageCache& images;
    float sc = 1.0f;                       // device pixels per design point
    std::vector<design::Card>* cards = nullptr;
};

// One keypress, already translated out of SDL so a screen never sees an event.
enum class Nav { Left, Right, Up, Down, Activate, Back };

// What a screen asks the app to do. The app decides whether it can.
enum class Action {
    None,
    Back,
    OpenTile,          // value is the tile index within the visible switcher tab
    OpenGame,          // value is a card index
    Play,              // value is a rom id
    Download,          // value is a rom id — fetch it AND keep it
    RemoveDownload,    // value is a rom id — release the keep AND delete the game
    // UP OUT OF THE TOP ROW, INTO THE BAR. MMagTech: *"if the library has the
    // top bar in view shouldnt i be able to up and access it."* Yes — chrome
    // that is on screen and cannot be reached is worse than chrome that is
    // hidden, because it looks like the console stopped responding.
    //
    // The screen says "focus left me upwards" and the app decides what is up
    // there, which is the same division every other Action here follows: the
    // screen knows its own rows and nothing about the product around it.
    FocusBar,
    // Down out of the search results, back into the keyboard docked under
    // them. Only Search sends this, and only the app can act on it: the
    // keyboard belongs to the app, not to a screen.
    FocusKeyboard,
    // Become this account. The value is a RomM user id, not a row index —
    // the screen is handed ids and hands one back, so a list that changed
    // underneath it cannot switch the console to the wrong person.
    SwitchAccount,
    // Pair somebody new against the server this console already uses. The app
    // owns it because pairing needs a client and a worker, and because it must
    // happen on a SEPARATE client: adding an account mid-session must not be
    // able to throw the console back into setup.
    AddAccount,
    // A Settings row was pressed. The value is the row's own id, which the
    // app assigned when it built the rows; see settings.h.
    Setting,
};

struct Result {
    Action action = Action::None;
    int value = 0;
};

// --- The Library ------------------------------------------------------------

// One tile: a platform, or a collection.
//
// A PLATFORM THIS CONSOLE CANNOT PLAY STILL GETS A TILE. docs/PROJECT.md is
// explicit that the answer is "neither show everything nor hide quietly — it is
// to KNOW, per platform, and to say so", and until now the saying-so went to
// stderr. Somebody who owns Switch games and sees none will reasonably conclude
// the scan failed; somebody who sees "no core for this system" has been told
// the truth. So an unplayable tile is drawn, dimmed, carrying its reason where
// a playable one carries its count, and it cannot be entered.
struct Tile {
    int id = 0;               // platform id, or collection id
    std::string title;        // "Arcade (FinalBurn Neo)", "SHMUP"
    std::string detail;       // "141 games", or why it cannot be played
    std::string cover;        // a cover path for the tile's artwork
    ui::Color art;            // shown until that arrives, and if it never does
    bool enterable = true;
    std::vector<int> cards;   // indices into Ctx::cards
    design::Animated focus;
};

class LibraryScreen {
public:
    void build(std::vector<Tile> platforms, std::vector<Tile> collections);

    // FOCUS LANDS ON THE SWITCHER THE FIRST TIME AND ONLY THE FIRST TIME.
    // Re-entering from a pushed screen must leave focus where back-navigation
    // put it. The reference implementation got this wrong first: forcing focus
    // on every appearance yanked it away whenever somebody came back.
    void enter();
    // Arrived already, with no fade of its own: a top-bar dissolve is doing
    // the arriving, and two fades at once dip through the background.
    void settleArrival() { appear_.settle(1.0f); }

    // Puts focus on a given tile, for a capture. The unplayable systems sort
    // last, so without this the only part of this screen a screenshot can ever
    // show is the part that works.
    void focusTile(int index);

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);
    // Glass reads the scene through itself, so anything frosted has to be drawn
    // AFTER Renderer::presentScene. That is a property of the renderer rather
    // than of these screens, and the split is the same one Home already makes
    // for the hero's band.
    void drawGlass(Ctx& c);

    // Which tab is showing, so the app can resolve an OpenTile.
    const std::vector<Tile>& visible() const {
        return tab_ == 0 ? platforms_ : collections_;
    }
    // Platforms or collections, for the same reason: the two are fetched from
    // different endpoints, and OpenTile has to know which one it is resolving.
    int tab() const { return tab_; }

    // What a tile learned the first time somebody walked into it — its cover,
    // and a count corrected to what is actually playable here.
    //
    // WHY THE SCREEN HAS TO BE TOLD. It holds its OWN copies of the tiles, so
    // the app mutating the vectors it built them from changes nothing on
    // screen. That is fine while a tile is fixed at boot and wrong the moment
    // a tile can learn something — open question 28, where a platform's games
    // are not fetched until it is opened.
    void learnedTile(int id, const std::string& detail, const std::string& cover);

    // The cover of whatever focus is on, so the app can keep lighting the room
    // with it. The backdrop follows focus on Home and it follows focus here for
    // the same reason and to the same rule — and the continuity is most of what
    // makes arriving at this screen read as a move rather than a cut.
    //
    // On the switcher row it answers the first tile rather than nothing: the
    // grid below is still what the screen is about, and a backdrop that blanked
    // whenever focus touched a pill would flicker on the way past.
    std::string focusedCover() const;

private:
    int columns() const;
    int tileRows() const;
    float tileWidth() const;
    void moveFocus(int dx, int dy);

    std::vector<Tile> platforms_;
    std::vector<Tile> collections_;
    int tab_ = 0;              // 0 platforms, 1 collections
    bool entered_ = false;     // has this screen ever been arrived at
    // Row 0 is the switcher; rows 1.. are the tile grid.
    int row_ = 0;
    int slot_ = 0;
    int rememberedTileSlot_ = 0;
    design::Animated appear_;
    design::Animated pillFocus_[2];
    design::Animated tabChange_;
    design::Animated scroll_;
};

// --- A grid of games --------------------------------------------------------

class GridScreen {
public:
    // `title` is drawn as ordinary content at the top of the scroll view,
    // never as chrome. `all` is the card table the indices point into — the
    // screen needs the NAMES once, at open, to build its letter index, and
    // holding a reference to the table would outlive the call.
    void open(std::string title, std::vector<int> cards,
              const std::vector<design::Card>& all);

    // More of the same grid, arriving after it was opened.
    //
    // A GRID IS PROPORTIONAL TO ITS OWN PLATFORM and boot is not — the fault
    // open question 28 fixed one level down. About 1 ms a game, so 141 is
    // fine and a full MAME set in one platform is ten seconds of staring at
    // nothing. The first page is drawn and the rest arrive behind it.
    //
    // APPENDED, NEVER RE-SORTED. RomM returns roms in title order, so pages
    // concatenate already in order; re-sorting here would move every card out
    // from under the slot the person is focused on.
    void append(const std::vector<int>& more, const std::vector<design::Card>& all);

    // Whether more is still coming, so the heading can say so rather than
    // showing a count that is about to change.
    void setLoadingMore(bool on) { loadingMore_ = on; }

    // FAST NAVIGATION BY LETTER — new 2026-09-21.
    //
    // MMagTech: *"when holding down or maybe r2 and l2 it should be fast nav
    // with the letters showing on the right side of the screen."* A platform
    // with 141 games is 21 rows, and reaching the S's a row at a time is the
    // kind of thing that makes a person stop browsing their own library.
    //
    // The TRIGGERS rather than a held direction, and both were on the table.
    // A held d-pad already means "keep moving one at a time" and giving it a
    // second meaning after some interval makes the first one feel broken while
    // you wait for it. The triggers mean nothing else anywhere in this product.
    //
    // `dir` is -1 or +1. Moves focus to the first game of the previous or next
    // letter, and shows the index for as long as somebody is using it.
    void jumpLetter(int dir);

    // The letters this list actually contains, for the index on the right, and
    // which of them focus is inside. Empty when there is nothing to show.
    const std::vector<char>& letters() const { return letters_; }

    // The Ctx::cards index focus is on, or -1. The app turns it into a cover
    // for the backdrop; this screen holds indices and no opinion about art.
    int focusedCard() const {
        return (slot_ >= 0 && slot_ < static_cast<int>(cards_.size())) ? cards_[slot_] : -1;
    }
    void tick(float dt, Ctx& c);
    Result key(Nav n);
    void draw(Ctx& c);
    void drawGlass(Ctx& c);

private:
    bool loadingMore_ = false;
    int columns() const;
    float coverWidth() const;

    int letterOf(int slot) const;

    std::string title_;
    std::vector<int> cards_;
    // The distinct initials, in order, and the first slot of each. Built once
    // at open: a 141-game platform is scanned once rather than per keypress.
    std::vector<char> letters_;
    std::vector<int> letterFirst_;
    int slot_ = 0;
    design::Animated appear_;
    design::Animated scroll_;
    // The index fades in when it is used and out when it is not. It is a
    // navigation aid, not chrome, and a permanent alphabet down the side of a
    // screen of artwork is a menu bar nobody asked for.
    design::Animated index_;
    // Seconds the index stays up after the last jump, before it starts to go.
    // Separate from the animation so a run of jumps holds it steady instead of
    // restarting a fade that has not begun.
    float indexHold_ = 0.0f;
};

// --- Search -----------------------------------------------------------------

// MMagTech, 2026-09-21: *"we also need to implement the search as well."* The
// bar has said "Search" since the bar existed and pressing it printed a line to
// stderr saying it was not built.
//
// IT ASKS THE SERVER — changed 2026-09-22, open question 28.
//
// It used to filter a catalogue held in memory, on the grounds that the whole
// library was loaded at boot anyway so a substring match was free. That was
// true, and the reason it was true is exactly what open question 28 removed:
// boot no longer fetches sixteen hundred games, so there is nothing here to
// filter. `/api/roms` takes `search_term`, checked against the live server.
//
// THE OLD COMMENT ALSO ARGUED THAT ASKING THE SERVER WOULD BE "THE ONE SCREEN
// IN THE PRODUCT THAT STOPS WORKING OFFLINE", AND THAT WAS WRONG. Nothing is
// kept between boots but three config files, so an offline boot has no
// catalogue in memory either and this screen has always filtered an empty
// vector with no server. Nobody noticed because the startup screen waits for
// the server and Search is never reached. See open question 28's offline
// section, and 29 for what offline actually means here.
//
// THE OTHER HALF OF THAT COMMENT STANDS: this needs debouncing, which the
// substring filter did not. The keyboard is docked and the results are live,
// so without it every keystroke is a request.
//
// THE KEYBOARD IS DOCKED AND THE RESULTS ARE LIVE. Typing blind and pressing
// Done to find out what you got is the thing that makes console search
// miserable; see ui::Keyboard::Config::dockedBottom for what that cost.
class SearchScreen {
public:
    void open();

    // What the person has typed. Recorded immediately so the screen can say
    // what it is doing; the results arrive separately and later.
    void setQuery(const std::string& q);
    const std::string& query() const { return query_; }

    // The answer, once the server has given one. `total` is what the server
    // says matched, which is not the same as how many came back — a search
    // that matched four hundred shows the first page and says so.
    void setResults(const std::string& forQuery, std::vector<int> results, int total);

    // Three states that must never read as one, which is the rule this file
    // keeps running into: nothing typed yet, waiting for the server, and the
    // server answered with nothing. A fourth — the server could not be asked —
    // is `failed`, and it is NOT the same as "no matches".
    enum class State { Empty, Waiting, Results, NoMatches, Failed };
    State state() const { return state_; }
    void setWaiting();
    void setFailed(const std::string& why);

    // Where the results have to stop, in canvas points: the top of the docked
    // keyboard. The covers are sized to the room that leaves rather than to a
    // constant, so the layout survives the keyboard changing height.
    void setResultsBottom(float y) { resultsBottom_ = y; }

    // Whether focus is in the results rather than in the keyboard. The app owns
    // this because the app owns the keyboard.
    void setFocused(bool on) { focused_ = on; }
    bool focused() const { return focused_; }

    void tick(float dt, Ctx& c);
    Result key(Nav n);
    void draw(Ctx& c);

    int focusedCard() const {
        return (slot_ >= 0 && slot_ < static_cast<int>(results_.size())) ? results_[slot_] : -1;
    }
    size_t resultCount() const { return results_.size(); }

private:
    std::string query_;
    std::vector<int> results_;
    // The query the results in hand actually answer, which lags `query_` by
    // one round trip. Drawing results against a query somebody has since typed
    // past is how a search screen comes to show the wrong thing confidently.
    std::string resultsFor_;
    int total_ = 0;
    State state_ = State::Empty;
    std::string failure_;
    int slot_ = 0;
    bool focused_ = false;
    float resultsBottom_ = ui::kCanvasHeight;
    design::Animated scroll_;
};

// --- Accounts ---------------------------------------------------------------

// One account as the switcher needs to draw it. Handed over whole, like
// GameDetail: the screen looks nothing up and holds no opinion about where any
// of this came from.
struct AccountRow {
    int id = 0;                  // RomM's user id — see accounts.h on the key
    std::string name;
    std::string avatar;          // an image key, or empty for a lettered disc
};

// WHO THIS CONSOLE IS, AND HOW TO BECOME SOMEBODY ELSE.
//
// **THE CHIP EXPANDS — it is not a separate screen.** MMagTech, 2026-09-21:
// *"when a user goes on the user and activates it, it expands and has add
// user."* So this draws as a panel hanging from the account chip in the bar,
// anchored to its right edge, rather than as a centred page with a title. The
// first version of this was a centred "Who is playing?" screen and it was
// wrong: the chip is the thing you pressed, so the chip is where the answer
// has to come from.
//
// THE FIRST ACCOUNT IS WHOEVER FIRST RUN PAIRED, which needs no code here —
// `accounts::recordPairing` makes the first account active because there is
// nobody for it to take over from.
//
// **THE ACTIVE ACCOUNT IS NOT IN THE LIST, AND THAT IS THE POINT.** MMagTech,
// looking at the first capture: *"seems redundant to show my login twice."* It
// was — the chip says who you are and the panel hanging off it said so again
// sixty points below. **The chip IS the active account's row.** So this holds
// everybody else, plus Add user, and every row in it can be pressed. Nothing
// here is dimmed, because there is nothing here you are not allowed to choose.
//
// STILL NOT HERE: removing an account and the PIN. Both are built underneath
// (`accounts::remove`, `accounts::setPin`) and neither has a control. They are
// absent rather than present-and-dead, because of the rule first run paid for:
// **never let focus land on a row that does nothing.**
class AccountScreen {
public:
    // Re-read from the store each time it opens; the list is small and this is
    // never hot. Resets focus to the first row that can be chosen.
    void setRows(std::vector<AccountRow> rows);

    // Where the chip is, in canvas points: the right edge to line the panel up
    // with, and the y to hang it from. The app knows, because the app draws the
    // bar; the screen must not guess, or the panel drifts the day the bar moves.
    void setAnchor(float rightX, float topY);

    void open();

    // Why a switch did not happen. The app owns the refusals — a running game,
    // a save still going up — so it owns the words for them too.
    void setNotice(std::string s);

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);

private:
    // Everything is selectable now, so this is only here to keep the Add row
    // focused on a console with one account.
    int firstSelectable() const;
    // Rows plus the one Add row underneath them.
    int rowCount() const { return static_cast<int>(rows_.size()) + 1; }
    bool isAddRow(int i) const { return i == static_cast<int>(rows_.size()); }

    std::vector<AccountRow> rows_;
    int slot_ = -1;
    std::string notice_;
    float anchorRight_ = ui::kCanvasWidth - 60.0f;
    float anchorTop_ = 150.0f;
    design::Animated focus_;
    design::Animated appear_;
};

// --- Adding an account ------------------------------------------------------

// PAIRING SOMEBODY NEW, AND IT IS A SCREEN RATHER THAN PART OF THE PANEL.
// MMagTech agreed the split, 2026-09-21: *"when you click add user i agree to
// another screen."* The reason is the QR — a code has to be big enough to
// photograph from a sofa, and a 520-point panel hanging off the corner cannot
// hold one. The list and the switch stay in the panel; this takes the screen.
//
// IT SHOWS, IT DOES NOT PAIR. The app owns the client, the worker and the
// polling, for the same reason it owns the switch's refusals: this file does
// no networking and starts no threads.
class AddAccountScreen {
public:
    void open();

    // The server has issued a code. Encodes the QR here, on the frame thread,
    // because that is where the GL context is.
    void setPairing(const std::string& url, const std::string& code);
    // Before the server has answered, and again if it never does.
    void setBusy(bool on);
    void setError(const std::string& err);

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);

private:
    std::string url_;
    std::string code_;
    std::string error_;
    bool busy_ = false;
    ui::QrTexture qr_;
    design::Animated appear_;
};

// --- The launch screen ------------------------------------------------------

// What the app knows about a game when it opens this screen. Handed over whole
// rather than looked up, so the screen holds no opinion about where any of it
// came from.
struct GameDetail {
    int romId = 0;
    int cardIndex = -1;
    std::string title;
    std::string platform;
    std::string cover;
    // THE BIG ONE, because this screen draws the biggest cover in the product.
    // 340x460 design points is 680x920 real pixels on a 4K panel, and it was
    // being drawn from RomM's 162x216 thumbnail — a 4.2x upscale, the worst
    // anywhere. The backdrop behind it was the same 162 pixels blurred, which
    // is why it came out as mud rather than as the art's own colours.
    std::string coverLarge;
    ui::Color art;
    int64_t sizeBytes = 0;
    // A game on a platform this console cannot play gets the screen and not the
    // Play button, with the reason where the button was. That is the fourth
    // thing `catalog::coverageFor` distinguishes, finally said out loud.
    bool playable = true;
    std::string reason;
    // Kept means downloaded deliberately: it is not part of the cache and
    // eviction may never take it. Unlike the cache, this IS visible — the cache
    // is invisible by decision, a kept game is a promise the person made.
    bool kept = false;
};

class DetailScreen {
public:
    void open(GameDetail d);
    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);
    void drawGlass(Ctx& c);

    const GameDetail& game() const { return game_; }
    void setKept(bool kept) { game_.kept = kept; }

    // A refusal, or anything else the person needs to read once. The screen
    // shows it under the actions until they do something else.
    void setNotice(std::string notice) { notice_ = std::move(notice); }

    // PROGRESS BELONGS ON THE ROW THAT STARTED IT — 2026-09-21.
    //
    // MMagTech asked whether a download should be its own window or shown on
    // the normal screen. It was a centred panel over a dimmed screen, and the
    // fact that settled it is that the panel never blocked anything: input
    // still reached the screen underneath, so it was obstruction with no
    // behaviour behind it — the worst of both.
    //
    // Now the row fills. Press Play and the Play row becomes the progress; press
    // Download and keep and that one does. Nothing is covered, nothing is
    // blocked, and the thing that is loading is the thing you pressed.
    //
    // `action` says which row, so that a background download started by Download
    // does not light up Play. `total` of zero means the server did not say how
    // big it is, which is common — the row then says what has arrived and draws
    // no bar, because a progress bar that invents its own total is a lie.
    struct Progress {
        bool active = false;
        Action action = Action::Play;
        int64_t got = 0, total = 0;
        bool unpacking = false;
    };
    void setProgress(const Progress& p) { progress_ = p; }

private:
    Progress progress_;

    struct Row {
        Action action = Action::None;
        std::string label;
        bool enabled = true;
    };
    void rebuildRows();

    GameDetail game_;
    std::vector<Row> rows_;
    std::string notice_;
    // Where draw() put the action column, so the glass pass can put the rows in
    // the same place without computing the layout twice and drifting from it.
    // draw() runs before drawGlass() every frame, which is what makes this safe.
    float rowsX_ = 0, rowsY_ = 0, rowsW_ = 0;
    int slot_ = 0;
    design::Animated focus_;
    design::Animated appear_;
};

}  // namespace screens
