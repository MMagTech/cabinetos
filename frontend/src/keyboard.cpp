#include "keyboard.h"

#include <algorithm>
#include <cmath>

#include "text.h"
#include "ui.h"

namespace ui {
namespace {

// Design-system values, from docs/PROJECT.md.
constexpr float kKeyUnit = 96.0f;     // one key's width and height
constexpr float kKeyGap = 12.0f;
constexpr float kKeyRadius = 16.0f;   // the settings-row radius; a key is that size
constexpr float kPanelRadius = 32.0f; // the pause-panel radius; this is a panel
constexpr float kPanelPad = 40.0f;

// The focus scale for a small element. The design system's rule is that the
// scale shrinks as the element grows — 1.10 for a cover, 1.06 for a pill, 1.03
// for a full-width row — and a key is pill-sized.
constexpr float kFocusScale = 1.06f;

Keyboard::Key k(const char* label, const char* insert) {
    Keyboard::Key key;
    key.label = label;
    key.insert = insert;
    return key;
}
Keyboard::Key action(const char* label, Keyboard::Key::Action a, float width = 1.0f) {
    Keyboard::Key key;
    key.label = label;
    key.action = a;
    key.width = width;
    return key;
}

}  // namespace

void Keyboard::open(const Config& config) {
    config_ = config;
    value_ = config.initial;
    conceal_ = config.conceal;
    shifted_ = false;
    row_ = 1;  // the letter row, not the digits: that is where typing starts
    col_ = 0;
    open_ = true;

    // A FIXED 12-COLUMN GRID, every row totalling exactly 12 units.
    //
    // The first version sized the panel to its widest row — a long function
    // row — and left the letter rows short, so a third of the panel was empty
    // to the right of the letters. It looked unfinished, and it wasted the
    // one thing a ten-foot keyboard is short of: reach.
    //
    // The layout follows what PlayStation, Xbox and Steam all converge on,
    // which is a REAL KEYBOARD's own geography rather than an invented one:
    //
    //   * digits across the top, with BACKSPACE at their right end, where the
    //     backspace key has always been;
    //   * SHIFT at the bottom-left of the letter block, where it has always
    //     been;
    //   * SPACE spanning the bottom, with the commit key at its right.
    //
    // None of that is decoration. Somebody looking for backspace looks
    // top-right before they read anything, and a layout that rewards the
    // guess is faster than one that has to be read.
    //
    // Digits get their own row rather than living behind a shift layer,
    // because a Wi-Fi passphrase is usually being read off the underside of a
    // router and is mostly digits and symbols. Burying them costs more than
    // the row does.
    constexpr float kCols = 12.0f;

    auto build = [&](bool upper) {
        std::vector<std::vector<Key>> rows;
        auto wide = [](Key key, float w) {
            key.width = w;
            return key;
        };

        std::vector<Key> digits;
        for (const char* p = "1234567890"; *p; ++p)
            digits.push_back(k(std::string(1, *p).c_str(), std::string(1, *p).c_str()));
        digits.push_back(wide(action("del", Key::Backspace), 2.0f));   // 10 + 2 = 12
        rows.push_back(digits);

        const char* r1 = upper ? "QWERTYUIOP" : "qwertyuiop";
        const char* r2 = upper ? "ASDFGHJKL" : "asdfghjkl";
        const char* r3 = upper ? "ZXCVBNM" : "zxcvbnm";

        std::vector<Key> a;
        for (const char* p = r1; *p; ++p)
            a.push_back(k(std::string(1, *p).c_str(), std::string(1, *p).c_str()));
        a.push_back(k("/", "/"));
        a.push_back(k(":", ":"));                                       // 10 + 2 = 12
        rows.push_back(a);

        std::vector<Key> b;
        for (const char* p = r2; *p; ++p)
            b.push_back(k(std::string(1, *p).c_str(), std::string(1, *p).c_str()));
        b.push_back(k(".", "."));
        b.push_back(k("-", "-"));
        b.push_back(k("_", "_"));                                       // 9 + 3 = 12
        rows.push_back(b);

        std::vector<Key> c;
        c.push_back(wide(action(upper ? "abc" : "ABC", Key::Shift), 2.0f));
        for (const char* p = r3; *p; ++p)
            c.push_back(k(std::string(1, *p).c_str(), std::string(1, *p).c_str()));
        c.push_back(k("@", "@"));
        c.push_back(wide(action(conceal_ ? "show" : "hide", Key::Conceal), 2.0f));
        rows.push_back(c);                                              // 2 + 7 + 1 + 2 = 12

        // The bottom row absorbs whatever is left, so it fits the grid
        // whatever the field asked for. A password field passes no shortcuts
        // and simply gets a longer space bar.
        std::vector<Key> fn;
        float used = 0;
        for (const auto& sc : config_.shortcuts) {
            fn.push_back(wide(k(sc.c_str(), sc.c_str()), 2.5f));
            used += 2.5f;
        }
        const float doneW = 3.0f;
        const float spaceW = std::max(2.0f, kCols - used - doneW);
        fn.push_back(wide(action("space", Key::Space), spaceW));
        fn.push_back(wide(action("done", Key::Done), doneW));
        rows.push_back(fn);
        return rows;
    };
    lower_ = build(false);
    upper_ = build(true);
}

const std::vector<std::vector<Keyboard::Key>>& Keyboard::layout() const {
    return shifted_ ? upper_ : lower_;
}

void Keyboard::clampFocus() {
    const auto& rows = layout();
    row_ = std::clamp(row_, 0, static_cast<int>(rows.size()) - 1);
    col_ = std::clamp(col_, 0, static_cast<int>(rows[row_].size()) - 1);
}

void Keyboard::moveFocus(int dx, int dy) {
    if (!open_) return;
    const auto& rows = layout();
    if (dy != 0) {
        // Keep the horizontal POSITION across a row change, not the index.
        // Rows have different key counts and different widths, so moving down
        // from "p" should land near "l", not on whatever happens to be ninth.
        float centre = 0;
        for (int i = 0; i < col_; ++i) centre += rows[row_][i].width;
        centre += rows[row_][col_].width * 0.5f;

        row_ = std::clamp(row_ + dy, 0, static_cast<int>(rows.size()) - 1);

        float best = 1e9f;
        int bestCol = 0, i = 0;
        float x = 0;
        for (const auto& key : rows[row_]) {
            const float mid = x + key.width * 0.5f;
            if (std::fabs(mid - centre) < best) {
                best = std::fabs(mid - centre);
                bestCol = i;
            }
            x += key.width;
            ++i;
        }
        col_ = bestCol;
    }
    // HORIZONTAL MOVEMENT WRAPS. This reverses an earlier decision here, and
    // the earlier one was wrong.
    //
    // The argument against wrapping was that it reads as a glitch when you are
    // holding a direction. That holds for a shelf, where the next item is a
    // different game and landing somewhere unexpected loses your place. It does
    // not hold for a keyboard, where the grid is twelve columns wide and every
    // key is equally "where you meant to be": without wrapping, getting from
    // "1" to "del" is eleven presses when it should be one. PlayStation and
    // Xbox both wrap their keyboards for exactly this reason.
    //
    // Vertical movement still clamps. Five rows is short enough to cross
    // directly, and wrapping from the space bar up to the digits skips the
    // letters — which is where somebody pressing up is almost always going.
    if (dx != 0) {
        const int n = static_cast<int>(rows[row_].size());
        col_ = (col_ + dx % n + n) % n;
    }
    clampFocus();
}

void Keyboard::pressKey() {
    if (!open_) return;
    clampFocus();
    const Key& key = layout()[row_][col_];
    switch (key.action) {
        case Key::Backspace: backspace(); return;
        case Key::Shift: toggleShift(); return;
        case Key::Space: value_ += ' '; return;
        case Key::Conceal: toggleConceal(); return;
        case Key::Done: commit(); return;
        case Key::Cancel: cancel(); return;
        case Key::None: break;
    }
    value_ += key.insert;
    // Shift is one-shot, the way a phone keyboard behaves: capitalise a letter
    // and fall back to lowercase, because the next character almost never wants
    // the same case.
    if (shifted_) {
        shifted_ = false;
        clampFocus();
    }
}

void Keyboard::backspace() {
    if (value_.empty()) return;
    // Step back over a whole UTF-8 code point, not a byte. Deleting half of a
    // multi-byte character leaves an invalid string that will not render.
    size_t n = value_.size() - 1;
    while (n > 0 && (static_cast<unsigned char>(value_[n]) & 0xC0) == 0x80) --n;
    value_.erase(n);
}

void Keyboard::toggleShift() {
    shifted_ = !shifted_;
    clampFocus();
}

void Keyboard::toggleConceal() {
    conceal_ = !conceal_;
    // The labels live in the built layouts, so rebuild them to say the other
    // thing. Cheap, and it keeps one source of truth for the layout.
    const std::string keep = value_;
    const int r = row_, c = col_;
    Config cfg = config_;
    cfg.initial = keep;
    cfg.conceal = conceal_;
    open(cfg);
    row_ = r;
    col_ = c;
    clampFocus();
}

void Keyboard::typeText(const char* utf8) {
    if (!open_ || !utf8) return;
    value_ += utf8;
}

KeyboardResult Keyboard::commit() {
    open_ = false;
    return KeyboardResult::Committed;
}

KeyboardResult Keyboard::cancel() {
    open_ = false;
    return KeyboardResult::Cancelled;
}

void Keyboard::draw(Renderer& r, TextRenderer& text, float scale) {
    if (!open_) return;
    const auto& rows = layout();

    // Widest row decides the panel, so the panel does not jump when the layout
    // changes between shift states.
    float widest = 0;
    for (const auto& row : rows) {
        float w = -kKeyGap;
        for (const auto& key : row) w += key.width * kKeyUnit + kKeyGap;
        widest = std::max(widest, w);
    }

    const float fieldH = 96.0f;
    const float titleH = 70.0f;
    const float hintH = config_.hint.empty() ? 0.0f : 34.0f;
    const float gridH = rows.size() * kKeyUnit + (rows.size() - 1) * kKeyGap;
    const float panelW = widest + kPanelPad * 2;
    const float panelH = titleH + hintH + fieldH + 28.0f + gridH + kPanelPad * 2 + 56.0f;
    const float panelX = (kCanvasWidth - panelW) * 0.5f;
    const float panelY = (kCanvasHeight - panelH) * 0.5f;

    // Scrim, then ONE piece of frosted glass.
    //
    // Glass does not nest. Every glass surface samples the same captured
    // scene, so a key drawn as its own glass re-samples the bright cover art
    // and ignores the darkening of the panel it is sitting on — which looked
    // like stained glass and was unreadable. The panel is the glass; the keys
    // are ordinary surfaces on top of it, which is also how the reference
    // implementation layers its own materials.
    //
    // And the tint is DARK. A white tint over a blur lightens, and this is a
    // dark interface: what makes a material read as a material here is that it
    // dims what is behind it as well as softening it.
    r.draw(Rect{0, 0, kCanvasWidth, kCanvasHeight, 0, Color::black(0.45f)});
    r.drawGlass(Rect{panelX, panelY, panelW, panelH, kPanelRadius, Color::white(0)}, 6.0f,
                Color::black(0.68f));

    float y = panelY + kPanelPad;
    text.draw(r, config_.title, panelX + kPanelPad,
              y + text.ascent(TextStyle::Title2, scale), TextStyle::Title2,
              Color::white(1.0f), scale);
    if (!config_.hint.empty()) {
        // Set by every caller and drawn by none until now. A field whose title
        // is "Connect to RomM" genuinely needs the line that says WHICH
        // address, and it was being silently dropped.
        text.draw(r, config_.hint, panelX + kPanelPad,
                  y + titleH + text.ascent(TextStyle::Callout, scale) - 10.0f,
                  TextStyle::Callout, Color::white(0.55f), scale);
        y += 34.0f;
    }
    y += titleH;

    // The field.
    r.draw(Rect{panelX + kPanelPad, y, widest, fieldH, kKeyRadius, Color::black(0.45f)});
    std::string shown = value_;
    if (conceal_) shown.assign(value_.size(), '*');
    const bool empty = shown.empty();
    if (empty) shown = config_.placeholder;
    const float fieldBaseline = y + fieldH * 0.5f + text.ascent(TextStyle::Title3, scale) * 0.5f;
    // Show the tail when it overflows: what someone is typing is at the end,
    // and a field that scrolls off the right hides exactly the character they
    // just pressed.
    const float room = widest - 56.0f;
    while (text.measure(shown, TextStyle::Title3, scale) > room && shown.size() > 1) {
        size_t n = 1;
        while (n < shown.size() && (static_cast<unsigned char>(shown[n]) & 0xC0) == 0x80) ++n;
        shown.erase(0, n);
    }
    text.draw(r, shown, panelX + kPanelPad + 20.0f, fieldBaseline, TextStyle::Title3,
              empty ? Color::white(0.35f) : Color::white(1.0f), scale);
    if (!empty) {
        // A caret at the end, so the field reads as active rather than as a
        // label that happens to contain text.
        const float caretX =
            panelX + kPanelPad + 20.0f + text.measure(shown, TextStyle::Title3, scale) + 4.0f;
        r.draw(Rect{caretX, y + 22.0f, 3.0f, fieldH - 44.0f, 1.5f, Color::white(0.75f)});
    }
    y += fieldH + 28.0f;

    // The keys.
    for (size_t ri = 0; ri < rows.size(); ++ri) {
        float x = panelX + kPanelPad;
        for (size_t ci = 0; ci < rows[ri].size(); ++ci) {
            const Key& key = rows[ri][ci];
            const float kw = key.width * kKeyUnit;
            const bool focused = (static_cast<int>(ri) == row_ && static_cast<int>(ci) == col_);
            const float s = focused ? kFocusScale : 1.0f;
            const float dw = kw * s, dh = kKeyUnit * s;
            const float dx = x - (dw - kw) * 0.5f;
            const float dy = y - (dh - kKeyUnit) * 0.5f;

            Rect cap{dx, dy, dw, dh, kKeyRadius * s,
                     focused ? Color::white(0.30f) : Color::white(0.10f)};
            if (focused) {
                cap.shadowBlur = 18.0f;
                cap.shadowOffsetY = 8.0f;
                cap.shadowColor = Color::black(0.45f);
            }
            r.draw(cap);

            const TextStyle st = key.action == Key::None ? TextStyle::Title3 : TextStyle::Callout;
            const float lw = text.measure(key.label, st, scale);
            text.draw(r, key.label, dx + (dw - lw) * 0.5f,
                      dy + dh * 0.5f + text.ascent(st, scale) * 0.5f, st,
                      focused ? Color::white(1.0f) : Color::white(0.75f), scale);
            x += kw + kKeyGap;
        }
        y += kKeyUnit + kKeyGap;
    }

    // What the buttons do. A controller-only UI has to say, because there is
    // no convention to fall back on and no pointer to explore with.
    // Callout, not Caption. 25pt is inside Apple's ten-foot ramp but it is the
    // size for something glanceable, and this is a line someone has to actually
    // READ to know what the buttons do. Anything a person must read sits at
    // Callout or above.
    const char* legend = "A select     X delete     Y shift     Start done     B back";
    const float lw = text.measure(legend, TextStyle::Callout, scale);
    text.draw(r, legend, panelX + (panelW - lw) * 0.5f,
              panelY + panelH - kPanelPad + 12.0f, TextStyle::Callout, Color::white(0.55f),
              scale);
}

}  // namespace ui
