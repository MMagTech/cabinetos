#include "pin.h"

#include "sound.h"

#include <algorithm>
#include <cmath>

namespace screens {

namespace {

constexpr int kLength = 4;

// The pad, as a phone draws it. The bottom row's two action keys are known by
// their position (isCancel, isDelete) rather than by their label, so a change
// of wording, such as Cancel becoming "Not now", cannot change what they do.
constexpr const char* kKeys[4][3] = {
    {"1", "2", "3"},
    {"4", "5", "6"},
    {"7", "8", "9"},
    {"Cancel", "0", "Delete"},
};
bool isCancel(int r, int c) { return r == 3 && c == 0; }
bool isDelete(int r, int c) { return r == 3 && c == 2; }

// Layout, in canvas points. Starting values, judged on the television.
constexpr float kTitleBase = 190.0f;
constexpr float kDetailBase = 248.0f;
constexpr float kDotsY = 322.0f;         // centre line of the dots
constexpr float kDotSize = 30.0f;
constexpr float kDotSpacing = 64.0f;
constexpr float kMessageBase = 400.0f;
constexpr float kPadTop = 440.0f;
constexpr float kKeyW = 200.0f;
constexpr float kKeyH = 100.0f;
constexpr float kKeyGap = 16.0f;
constexpr float kKeyRadius = 16.0f;      // the keyboard's key radius
constexpr float kKeyFocusScale = 1.06f;  // the keyboard's key focus
constexpr float kLegendBase = 960.0f;

constexpr float kAppear = 0.280f;        // Settings' own arrival
constexpr float kShake = 0.40f;

}  // namespace

void PinScreen::open(Mode mode, std::string title, std::string detail,
                     std::string cancel) {
    open_ = true;
    cancel_ = std::move(cancel);
    mode_ = mode;
    title_ = std::move(title);
    detail_ = std::move(detail);
    typed_.clear();
    first_.clear();
    entered_.clear();
    message_.clear();
    row_ = 0;
    col_ = 0;
    shake_ = 0.0f;
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, kAppear);
    focus_.settle(1.0f);
}

void PinScreen::close() {
    open_ = false;
    typed_.clear();
    first_.clear();
}

void PinScreen::reject(const std::string& why) {
    typed_.clear();
    entered_.clear();
    message_ = why;
    shake_ = kShake;
}

void PinScreen::lockFor(float seconds) {
    lockLeft_ = seconds;
    typed_.clear();
}

PinScreen::Outcome PinScreen::add(char digit) {
    if (locked()) { sound::play(sound::Cue::Edge); return Outcome::None; }
    if (static_cast<int>(typed_.size()) >= kLength) return Outcome::None;
    typed_ += digit;
    // The message was about the last attempt; a new digit is a new attempt.
    message_.clear();
    sound::play(sound::Cue::Move);
    if (static_cast<int>(typed_.size()) < kLength) return Outcome::None;

    // FOUR IN, AND IT GOES AT ONCE. No OK key: a PIN of a fixed length is
    // finished when it is long enough, which is how a phone does it.
    if (mode_ == Mode::Check) {
        entered_ = typed_;
        return Outcome::Entered;
    }
    if (first_.empty()) {
        // The title changes to "Enter it again"; see draw.
        first_ = typed_;
        typed_.clear();
        return Outcome::None;
    }
    if (typed_ != first_) {
        first_.clear();
        reject("Those did not match. Choose a PIN again");
        sound::play(sound::Cue::Edge);
        return Outcome::None;
    }
    entered_ = typed_;
    return Outcome::Entered;
}

PinScreen::Outcome PinScreen::deleteOne() {
    if (typed_.empty()) return Outcome::None;
    typed_.pop_back();
    sound::play(sound::Cue::Back);
    return Outcome::None;
}

PinScreen::Outcome PinScreen::typeDigit(char c) {
    if (!open_ || c < '0' || c > '9') return Outcome::None;
    return add(c);
}

PinScreen::Outcome PinScreen::key(Nav n) {
    if (!open_) return Outcome::None;
    auto move = [&](int dr, int dc) {
        const int r = row_ + dr, c = col_ + dc;
        // NO WRAPPING. Three columns is short enough to cross, and a pad
        // that wraps puts Delete one press from Cancel.
        if (r < 0 || r > 3 || c < 0 || c > 2) { sound::play(sound::Cue::Edge); return; }
        row_ = r;
        col_ = c;
        focus_.retarget(0.0f, 0.0f);
        focus_.elapsed = 0.0f;
        focus_.retarget(1.0f, design::kFocusDuration);
        sound::play(sound::Cue::Move);
    };
    switch (n) {
        case Nav::Up: move(-1, 0); return Outcome::None;
        case Nav::Down: move(1, 0); return Outcome::None;
        case Nav::Left: move(0, -1); return Outcome::None;
        case Nav::Right: move(0, 1); return Outcome::None;
        case Nav::Activate:
            if (isCancel(row_, col_)) {
                sound::play(sound::Cue::Back);
                return Outcome::Cancelled;
            }
            if (isDelete(row_, col_)) return deleteOne();
            return add(kKeys[row_][col_][0]);
        case Nav::Back:
            // B TAKES BACK A DIGIT FIRST, and leaves only when there is none,
            // so a mistyped digit is one press to fix rather than a restart.
            if (!typed_.empty()) return deleteOne();
            sound::play(sound::Cue::Back);
            return Outcome::Cancelled;
    }
    return Outcome::None;
}

void PinScreen::tick(float dt) {
    appear_.tick(dt);
    focus_.tick(dt);
    shake_ = std::max(0.0f, shake_ - dt);
    if (lockLeft_ > 0.0f) {
        lockLeft_ = std::max(0.0f, lockLeft_ - dt);
        if (lockLeft_ <= 0.0f) message_.clear();
    }
}

void PinScreen::draw(Ctx& c) {
    if (!open_) return;
    const float a = appear_.value();
    c.r.setContentAlpha(1.0f);
    const float W = ui::kCanvasWidth, H = ui::kCanvasHeight;

    // THE CONSOLE'S PLAIN PURPLE, the one first run and Settings stand on,
    // faded in over whatever was there. Drawn as the backdrop's two bands so
    // it matches it exactly and can still fade.
    {
        const float mid = H * 0.55f;
        ui::Color top = ui::palette::kBackdropTop, m = ui::palette::kBackdropMid,
                  bot = ui::palette::kBackdropBottom;
        top.a = m.a = bot.a = a;
        // The bands overlap by a point: two anti-aliased edges meeting
        // exactly leave a faint dark seam across the middle of the pad.
        ui::Rect upper{0, 0, W, mid + 1.0f, 0, top};
        upper.gradient = true;
        upper.fillBottom = m;
        c.r.draw(upper);
        ui::Rect lower{0, mid, W, H - mid, 0, m};
        lower.gradient = true;
        lower.fillBottom = bot;
        c.r.draw(lower);
    }
    c.r.setContentAlpha(a);

    auto centred = [&](const std::string& s, float base, ui::TextStyle st, float alpha) {
        const float w = c.text.measure(s, st, c.sc);
        c.text.draw(c.r, s, (W - w) * 0.5f, base, st, ui::Color::white(alpha), c.sc);
    };
    std::string title = title_;
    if (mode_ == Mode::Choose && !first_.empty()) title = "Enter it again";
    centred(title, kTitleBase, ui::TextStyle::Title2, 1.0f);
    if (!detail_.empty()) centred(detail_, kDetailBase, ui::TextStyle::Callout, 0.60f);

    // The dots: filled for a digit typed, faint for one still to come. The
    // digits themselves are never drawn, because a PIN is typed with the
    // person it keeps out sitting on the same sofa.
    float shakeX = 0.0f;
    if (shake_ > 0.0f) {
        const float t = kShake - shake_;
        shakeX = std::sin(t * 50.0f) * 18.0f * (shake_ / kShake);
    }
    const float dotsW = kDotSpacing * (kLength - 1);
    for (int i = 0; i < kLength; ++i) {
        const float cx = (W - dotsW) * 0.5f + i * kDotSpacing + shakeX;
        const bool on = i < static_cast<int>(typed_.size());
        c.r.draw(ui::Rect{cx - kDotSize * 0.5f, kDotsY - kDotSize * 0.5f, kDotSize, kDotSize,
                          kDotSize * 0.5f, ui::Color::white(on ? 1.0f : 0.30f)});
    }

    std::string msg = message_;
    if (locked()) {
        const int s = static_cast<int>(std::ceil(lockLeft_));
        msg = "Too many tries. Try again in " + std::to_string(s) +
              (s == 1 ? " second" : " seconds");
    }
    if (!msg.empty()) centred(msg, kMessageBase, ui::TextStyle::Callout, 0.85f);

    // The pad. Ordinary surfaces, not glass: the keyboard's own treatment,
    // brighter and a little larger under focus.
    const float f = focus_.value();
    const float padW = kKeyW * 3 + kKeyGap * 2;
    const float padX = (W - padW) * 0.5f;
    for (int r = 0; r < 4; ++r) {
        for (int col = 0; col < 3; ++col) {
            const float x = padX + col * (kKeyW + kKeyGap);
            const float y = kPadTop + r * (kKeyH + kKeyGap);
            const bool focused = (r == row_ && col == col_);
            const float s = focused ? 1.0f + (kKeyFocusScale - 1.0f) * f : 1.0f;
            const float dw = kKeyW * s, dh = kKeyH * s;
            const float dx = x - (dw - kKeyW) * 0.5f, dy = y - (dh - kKeyH) * 0.5f;
            ui::Rect cap{dx, dy, dw, dh, kKeyRadius * s,
                         ui::Color::white(focused ? 0.10f + 0.20f * f : 0.10f)};
            if (focused) {
                cap.shadowBlur = 18.0f;
                cap.shadowOffsetY = 8.0f;
                cap.shadowColor = ui::Color::black(0.45f * f);
            }
            c.r.draw(cap);
            const bool action = isCancel(r, col) || isDelete(r, col);
            const ui::TextStyle st = action ? ui::TextStyle::Callout : ui::TextStyle::Title2;
            const std::string label = isCancel(r, col) ? cancel_ : kKeys[r][col];
            const float lw = c.text.measure(label, st, c.sc);
            c.text.draw(c.r, label, dx + (dw - lw) * 0.5f,
                        dy + dh * 0.5f + c.text.ascent(st, c.sc) * 0.5f, st,
                        ui::Color::white(focused ? 1.0f : 0.75f), c.sc);
        }
    }

    centred("A select     B delete, or go back", kLegendBase, ui::TextStyle::Callout, 0.55f);
    c.r.setContentAlpha(1.0f);
}

}  // namespace screens
