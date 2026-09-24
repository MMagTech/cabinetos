// Settings: a side list of categories on the left, the chosen one's rows on the
// right, both on screen at once.
//
// THE SHAPE IS NOT CABINET'S, ON PURPOSE. MMagTech, 2026-09-24: Settings is
// *"the one screen where we can diverge a bit from the others"*. Cabinet's tvOS
// app opens each category as its own page, which costs an open and a back for
// every look. Here walking down the list changes the right side at once, and a
// television is wide enough to show both. docs/PROJECT.md, open question 31.
//
// THE SCREEN IS HANDED ITS ROWS AND HOLDS NO OPINION ABOUT THEM, the same
// division every other screen follows: the app reads the network, the drives
// and the account, builds the model, and turns a pressed row's id into
// whatever it means. Nothing here touches the disk or the network.
//
// NEVER LET FOCUS LAND ON A ROW THAT DOES NOTHING, the rule first run paid for.
// A row that only shows a value, and a row whose control is not built yet, are
// both drawn and both skipped by focus. The unbuilt ones are drawn dimmed and
// say so, because this version exists so the whole layout can be judged on the
// television before every row behind it is built.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

namespace screens {

struct SettingsRow {
    enum class Kind {
        Info,      // shows a value; cannot be focused
        Action,    // opens or does something; has a chevron
        Toggle,    // On or Off, shown as the value; pressing flips it
        Unbuilt,   // agreed, not built; drawn dimmed, cannot be focused
    };
    Kind kind = Kind::Info;
    int id = 0;               // what the app is handed back when it is pressed
    std::string title;
    std::string detail;       // the second line, or empty
    std::string value;        // right-aligned, or empty
};

struct SettingsCategory {
    std::string name;
    std::vector<SettingsRow> rows;
};

class SettingsScreen {
public:
    // Replaces the model. Where focus is survives it when it still can, so the
    // app can rebuild after a toggle without throwing the person back to the
    // top of the list.
    void setCategories(std::vector<SettingsCategory> cats);

    // Arriving is animated every time. Focus goes to the category list, on the
    // category that was last open, because a person coming back to Settings
    // usually came back for the same thing.
    void enter();
    // See LibraryScreen::settleArrival.
    void settleArrival() { appear_.settle(1.0f); }

    // For a capture: open on a category, with focus in its rows if it has one
    // that can be focused.
    void focusCategory(int index, bool intoRows);

    void tick(float dt);
    Result key(Nav n);
    void draw(Ctx& c);
    // Everything here is glass, so it all draws after presentScene.
    void drawGlass(Ctx& c);

private:
    bool focusable(int cat, int row) const;
    int firstFocusable(int cat) const;
    void retargetFocus();

    std::vector<SettingsCategory> cats_;
    int cat_ = 0;
    int row_ = -1;             // -1 while focus is in the category list
    design::Animated appear_;
    design::Animated focus_;
    design::Animated scroll_;
    design::Animated paneChange_;
};

}  // namespace screens
