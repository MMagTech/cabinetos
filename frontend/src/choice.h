// A question with a few answers, in a panel in the middle of the screen.
//
// "Who do you want to remove?", "Remove vivian from this console?" and, when
// they are built, sign out and forgetting a network. A scrim, the pause
// menu's panel, and a column of its buttons (design::menuPanel).
//
// A LAYER, LIKE THE PIN PAD. It is opened over whatever is showing, takes
// input first while it is open, and says which answer was chosen. What the
// answer means is the app's.

#pragma once

#include <string>
#include <vector>

#include "screens.h"

namespace screens {

class ChoiceScreen {
public:
    enum class Outcome { None, Chosen, Cancelled };

    // `focus` is the answer focus starts on. For a question whose first
    // answer destroys something, start on the harmless one.
    void open(std::string title, std::string detail, std::vector<std::string> options,
              int focus = 0);
    void close() { open_ = false; }
    bool isOpen() const { return open_; }
    // The answer, after Chosen.
    int chosen() const { return slot_; }

    Outcome key(Nav n);
    void tick(float dt);
    // For a capture: the resting frame.
    void settle() { appear_.settle(1.0f); focus_.settle(1.0f); }
    void draw(Ctx& c);

private:
    bool open_ = false;
    std::string title_, detail_;
    std::vector<std::string> options_;
    int slot_ = 0;
    design::Animated appear_;
    design::Animated focus_;
};

}  // namespace screens
