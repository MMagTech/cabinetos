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
    RemoveDownload,    // value is a rom id — un-keep; the bytes stay, evictable
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
    design::Animated pillFocus_[2];
    design::Animated tabChange_;
    design::Animated scroll_;
};

// --- A grid of games --------------------------------------------------------

class GridScreen {
public:
    // `title` is drawn as ordinary content in a glass chip, never as chrome.
    void open(std::string title, std::vector<int> cards);
    void tick(float dt, Ctx& c);
    Result key(Nav n);
    void draw(Ctx& c);
    void drawGlass(Ctx& c);

private:
    int columns() const;
    float coverWidth() const;

    std::string title_;
    std::vector<int> cards_;
    int slot_ = 0;
    design::Animated scroll_;
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

private:
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
