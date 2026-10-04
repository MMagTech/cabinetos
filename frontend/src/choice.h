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
    // A value on the right of each answer ("Connected"), turning the column
    // into a list: labels go to the left. Call after open; empty for none.
    void setValues(std::vector<std::string> values) { values_ = std::move(values); }
    // New answers while open (a Wi-Fi scan landing), keeping focus on the
    // same label if it is still there.
    void replace(std::vector<std::string> options, std::vector<std::string> values,
                 std::string detail);
    // STAYS UP WHEN AN ANSWER IS CHOSEN, for a window that acts on a choice
    // in place (Add a controller pairs the pad it lists). Back still closes.
    // open() clears it.
    void setStaysOpen(bool on) { staysOpen_ = on; }
    bool staysOpen() const { return staysOpen_; }
    // ONE WIDTH FOR AS LONG AS IT IS OPEN, the content's width in canvas
    // points; longer names and lines are cut short. For a window whose list
    // and line change while it is up: Add a controller grew wider when a
    // pairing failed, and MMagTech did not like it (2026-09-26). open()
    // clears it.
    void setFixedWidth(float w) { fixedW_ = w; }
    // A LIST THAT FILLS WHILE IT IS OPEN GROWS SMOOTHLY. It opens one row
    // tall with `placeholder` said in it, and glides taller as rows arrive.
    // Add a controller first jumped from a small panel to a big one, which
    // read as a second window; then it opened six rows tall, which left an
    // empty band (MMagTech, 2026-09-26). open() clears it.
    void setGrows(std::string placeholder) {
        grows_ = true;
        placeholder_ = std::move(placeholder);
    }
    // The title alone, keeping everything else ("Paired successfully").
    void setTitle(std::string title) { title_ = std::move(title); }
    // The detail line alone, keeping the answers and focus.
    void setDetail(std::string detail) { detail_ = std::move(detail); }
    void close() { open_ = false; }
    bool isOpen() const { return open_; }
    const std::string& title() const { return title_; }
    // The answer, after Chosen.
    int chosen() const { return slot_; }

    Outcome key(Nav n);
    void tick(float dt);
    // For a capture: the resting frame.
    void settle() { appear_.settle(1.0f); focus_.settle(1.0f); }
    void draw(Ctx& c);

private:
    bool open_ = false;
    bool staysOpen_ = false;
    float fixedW_ = 0.0f;
    bool grows_ = false;
    std::string placeholder_;
    design::Animated listH_;
    bool listHSet_ = false;
    std::string title_, detail_;
    std::vector<std::string> options_;
    std::vector<std::string> values_;
    int slot_ = 0;
    int top_ = 0;              // first answer shown, once there are too many
    design::Animated appear_;
    design::Animated focus_;
};

}  // namespace screens
