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
    // Stepped: Left or Right on the stepper row (setStepper); stepped() says
    // which way. The app changes the value and calls setValues again.
    enum class Outcome { None, Chosen, Cancelled, Stepped };

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
    // GREYED ANSWERS, one flag per answer: drawn dim, never focused, never
    // chosen. Steam's Continue while the console is offline (#223): grey it
    // out, do not explain (no spoon-feeding). Call after open; focus moves
    // off a greyed answer to the next one that is not.
    void setDisabled(std::vector<bool> disabled);
    // ONE ROW WHOSE VALUE LEFT AND RIGHT CHANGE, with an arrow either side
    // of it while focused, as a Settings choice row (Steam's size, #223).
    // `canLeft`/`canRight` dim the arrow at an end. -1 for none; open()
    // clears it.
    void setStepper(int slot, bool canLeft, bool canRight) {
        stepper_ = slot;
        canLeft_ = canLeft;
        canRight_ = canRight;
    }
    int stepped() const { return stepDir_; }
    // A PANEL WITH NOTHING TO PRESS BUT A BUTTON, said rather than focused:
    // "Press (B) to cancel", the letter drawn as a button badge, in place of
    // the answers (open it with none). A focused Cancel under a run of A
    // presses gets pressed by the next one (Steam's install, #223; the
    // unearned-focus rule). Back answers Cancelled, as for any panel. open()
    // clears it.
    void setPrompt(std::string before, std::string button, std::string after) {
        promptBefore_ = std::move(before);
        promptButton_ = std::move(button);
        promptAfter_ = std::move(after);
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
    std::vector<bool> disabled_;
    std::string promptBefore_, promptButton_, promptAfter_;
    int stepper_ = -1;
    bool canLeft_ = false, canRight_ = false;
    int stepDir_ = 0;
    bool isDisabled(int i) const {
        return i >= 0 && i < static_cast<int>(disabled_.size()) && disabled_[i];
    }
    int slot_ = 0;
    int top_ = 0;              // first answer shown, once there are too many
    design::Animated appear_;
    design::Animated focus_;
};

}  // namespace screens
