// The account panel (AccountScreen) and the rows it is handed (AccountRow,
// built by accountsflow.cpp). The app draws it over the bar. Moved out of
// screens.h in the restructure (#398); what every screen shares is in
// screens.h.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

namespace screens {

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

}  // namespace screens
