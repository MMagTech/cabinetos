// The on-screen keyboard.
//
// The baseline for all text entry, per the input model in docs/PROJECT.md: the
// controller is the only device the design may assume exists, so every field
// has to be fillable with a d-pad and two buttons. A physical keyboard types
// into the same field at the same time, and neither implementation may be the
// only one.
//
// There is nothing to copy here. Cabinet never wrote one — tvOS supplied it —
// so this is designed from the input model rather than ported.
//
// Three decisions worth stating, because they are the ones that make it
// bearable rather than merely possible:
//
// SHOW WHAT WAS TYPED, INCLUDING PASSWORDS. Every console hides a password
// field by default and every console is wrong: nobody is shoulder-surfing a
// living room, and not being able to see what you typed IS the difficulty. A
// caller can still ask for concealment, and there is a control to toggle it,
// but visible is the default.
//
// REDUCE TYPING RATHER THAN SPEEDING IT UP. A key that inserts ".com", a
// prefilled scheme, and a remembered previous value each save more than any
// amount of layout tuning.
//
// NO WRAPPING AT THE EDGES. Moving off the right of the keyboard does nothing
// rather than teleporting to the left. Wrapping reads as a glitch when you are
// holding a direction, and the whole point of a ten-foot UI is that focus goes
// where you pointed.

#pragma once

#include <functional>
#include <string>
#include <vector>

namespace ui {

class Renderer;
class TextRenderer;

enum class KeyboardResult { Typing, Committed, Cancelled };

class Keyboard {
public:
    struct Config {
        std::string title = "Enter text";
        std::string hint;          // a line under the title, or empty
        std::string initial;       // prefilled, e.g. "https://"
        std::string placeholder;   // shown greyed when the field is empty
        bool conceal = false;      // start with the value masked
        // Extra keys worth having for this particular field. A URL wants
        // ".com" and "/"; a Wi-Fi passphrase wants neither.
        std::vector<std::string> shortcuts;
    };

    void open(const Config& config);
    bool isOpen() const { return open_; }

    // Directional input, from a pad or the arrow keys — the layer above does
    // not say which, because nothing here may depend on the answer.
    void moveFocus(int dx, int dy);
    void pressKey();       // commit the focused key
    void backspace();
    void toggleShift();
    void toggleConceal();
    KeyboardResult commit();   // "done"
    KeyboardResult cancel();

    // A physical keyboard types into the same field. Not a separate path: the
    // same string, the same commit, the same screen.
    void typeText(const char* utf8);

    const std::string& value() const { return value_; }

    void draw(Renderer& r, TextRenderer& text, float scale);

    // Public only so the layout tables in the .cpp can be built by free
    // helpers. Nothing outside this class has a reason to touch it.
    struct Key {
        std::string label;   // what is drawn
        std::string insert;  // what is typed; empty for an action key
        enum Action { None, Backspace, Shift, Space, Done, Cancel, Conceal } action = None;
        float width = 1.0f;  // in key units
    };

private:
    const std::vector<std::vector<Key>>& layout() const;
    void clampFocus();

    bool open_ = false;
    Config config_;
    std::string value_;
    bool shifted_ = false;
    bool conceal_ = false;
    int row_ = 0, col_ = 0;
    std::vector<std::vector<Key>> lower_, upper_;
};

}  // namespace ui
