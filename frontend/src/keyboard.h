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

        // A PANEL THAT SHARES THE SCREEN RATHER THAN COVERING IT — new
        // 2026-09-21, for Search.
        //
        // Everything that opened this before was a question with one answer:
        // an address, a passphrase. Nothing else needed to be visible, so the
        // panel sat in the middle of a dimmed screen and owned it.
        //
        // Search is the opposite. The results are the point and they change on
        // every key, so the keyboard has to live at the bottom of the screen
        // with the results above it, and nothing may be dimmed. Its title and
        // hint go too: the field IS the title when you can see what it is
        // filtering.
        bool dockedBottom = false;
    };

    // Whether focus is on the top row of keys, so a caller can decide what UP
    // means. It means nothing here — this keyboard deliberately does not wrap —
    // and on Search it means "leave the keyboard and go to the results".
    bool atTopRow() const { return row_ == 0; }

    // Where the panel's top edge is, in canvas points, from the last time it
    // was drawn. A docked keyboard shares the screen, so whatever is above it
    // has to know where "above it" ends — and the panel's height depends on the
    // layout, the shift state and whether it is docked, so nothing outside can
    // work it out for itself.
    float panelTop() const { return panelTop_; }

    void open(const Config& config);
    bool isOpen() const { return open_; }

    // Directional input, from a pad or the arrow keys — the layer above does
    // not say which, because nothing here may depend on the answer.
    void moveFocus(int dx, int dy);
    // Presses the focused key. RETURNS A RESULT, because two of the keys on
    // this layout end the whole session — "done" and "cancel" — and until
    // 2026-09-20 this returned void and handled them silently. So somebody
    // driving the keyboard with a CONTROLLER, pressing A on "done", closed the
    // panel and had what they had typed thrown away: the caller was watching
    // for its own Start button and never learned. The physical keyboard's
    // Return worked, which is exactly why nobody noticed.
    KeyboardResult pressKey();
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

    // --- A pointer, which is a THIRD way in and takes nothing away ----------
    //
    // docs/PROJECT.md open question 16 gives the mouse focus and click and
    // nothing else, and MMagTech's condition on extending it here, 2026-09-20,
    // was that *"typing with keyboard still works as well or the on screen
    // keyboard can still be driven by a wired controller."* Both do: this moves
    // the same `row_`/`col_` the d-pad moves and presses the same key `A`
    // presses. There is no pointer-only affordance and no key that can only be
    // reached with a mouse.
    //
    // WHY THE KEYBOARD NEEDS IT AT ALL. A mouse that drives the list behind
    // this panel and then goes dead the moment a password field opens is worse
    // than no mouse: it teaches somebody it works and abandons them at the
    // hardest typing in the whole flow. And people arrive here straight out of
    // a pointer-driven installer — MMagTech's argument, and a better one than
    // the document had.
    //
    // Coordinates are CANVAS POINTS, because that is what the layout is in and
    // the letterbox is the caller's problem to undo.

    // Moves focus to the key under the point. Returns false when there is no
    // key there, which includes the panel's own margins — nothing is focused
    // by pointing at the gap between keys.
    bool focusAt(float canvasX, float canvasY);

    // Focuses the key under the point and presses it, returning what that
    // press meant. `hit` says whether there was a key there at all — a click on
    // the scrim does nothing rather than pressing whatever was focused last.
    KeyboardResult pressAt(float canvasX, float canvasY, bool* hit = nullptr);

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

    // Where `draw` last put each key, in canvas points. The draw pass fills it
    // and the event pass reads it on the frame after — the same order the rest
    // of this flow already relies on. Empty until the first frame is drawn, so
    // a click that somehow arrives first simply finds nothing.
    struct KeyRect { float x = 0, y = 0, w = 0, h = 0; int row = 0, col = 0; };
    std::vector<KeyRect> keyRects_;

    bool open_ = false;
    Config config_;
    std::string value_;
    bool shifted_ = false;
    bool conceal_ = false;
    int row_ = 0, col_ = 0;
    float panelTop_ = 0.0f;
    std::vector<std::vector<Key>> lower_, upper_;
};

}  // namespace ui
