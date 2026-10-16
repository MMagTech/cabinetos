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
    PlayState,         // value is a RomM state id — play, starting from that state
    ToggleFavorite,    // value is a rom id — the game's page heart/star (#267)
    ShowAchievements,  // value is a rom id — its RetroAchievements list (#74)
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
    // A Settings choice row moved to another choice. The value is the row's
    // id; SettingsScreen::choiceOf says which choice it is on now.
    SettingChoice,
};

struct Result {
    Action action = Action::None;
    int value = 0;
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

    // Greyed while the server is away (#88): pairing needs it. Still
    // focusable, so the list does not change shape.
    void setAddEnabled(bool on) { addEnabled_ = on; }

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);

private:
    bool addEnabled_ = true;
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
    // THE SAME SCREEN SIGNS THE CONSOLE IN TO TAILSCALE (#286): a link to
    // approve on a phone, as adding an account is. Its own title and line;
    // adding an account keeps the ones below.
    void setText(std::string title, std::vector<std::string> lines, std::string busy);

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);

private:
    std::string title_ = "Add an account";
    std::vector<std::string> lines_ = {"Sign in as the person you are adding,",
                                       "not as yourself."};
    std::string busyLine_ = "Getting a code\xE2\x80\xA6";
    std::string url_;
    std::string code_;
    std::string error_;
    bool busy_ = false;
    ui::QrTexture qr_;
    design::Animated appear_;
};

}  // namespace screens
