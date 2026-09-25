#include "choice.h"

#include "sound.h"

#include <algorithm>

namespace screens {

namespace {

// The PIN pad's panel sizes (pin.cpp); the look is design::menuPanel.
constexpr float kPanelPad = 40.0f;
constexpr float kPanelRadius = 32.0f;
constexpr float kPanelMaxW = 1200.0f;
constexpr float kButtonW = 520.0f;
constexpr float kButtonH = 88.0f;
constexpr float kButtonGap = 12.0f;
constexpr float kButtonRadius = 16.0f;
constexpr float kFocusScale = 1.04f;
constexpr float kAppear = 0.280f;

}  // namespace

void ChoiceScreen::open(std::string title, std::string detail,
                        std::vector<std::string> options, int focus) {
    open_ = true;
    title_ = std::move(title);
    detail_ = std::move(detail);
    options_ = std::move(options);
    slot_ = options_.empty() ? 0 : std::clamp(focus, 0, static_cast<int>(options_.size()) - 1);
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, kAppear);
    focus_.settle(1.0f);
}

ChoiceScreen::Outcome ChoiceScreen::key(Nav n) {
    if (!open_) return Outcome::None;
    switch (n) {
        case Nav::Up:
        case Nav::Down: {
            const int next = slot_ + (n == Nav::Down ? 1 : -1);
            if (next < 0 || next >= static_cast<int>(options_.size())) {
                sound::play(sound::Cue::Edge);
                return Outcome::None;
            }
            slot_ = next;
            focus_.retarget(0.0f, 0.0f);
            focus_.elapsed = 0.0f;
            focus_.retarget(1.0f, design::kFocusDuration);
            sound::play(sound::Cue::Move);
            return Outcome::None;
        }
        case Nav::Left:
        case Nav::Right:
            sound::play(sound::Cue::Edge);
            return Outcome::None;
        case Nav::Activate:
            sound::play(sound::Cue::Activate);
            return Outcome::Chosen;
        case Nav::Back:
            sound::play(sound::Cue::Back);
            return Outcome::Cancelled;
    }
    return Outcome::None;
}

void ChoiceScreen::tick(float dt) {
    appear_.tick(dt);
    focus_.tick(dt);
}

void ChoiceScreen::draw(Ctx& c) {
    if (!open_) return;
    using ui::TextStyle;
    const float a = appear_.value();
    const float W = ui::kCanvasWidth, H = ui::kCanvasHeight, sc = c.sc;

    float innerW = kButtonW;
    innerW = std::max(innerW, c.text.measure(title_, TextStyle::Title2, sc));
    if (!detail_.empty())
        innerW = std::max(innerW, c.text.measure(detail_, TextStyle::Callout, sc));
    const float panelW = std::min(kPanelMaxW, innerW + kPanelPad * 2);
    const float textMax = panelW - kPanelPad * 2;
    const float titleH = c.text.lineHeight(TextStyle::Title2, sc);
    const float lineH = c.text.lineHeight(TextStyle::Callout, sc);
    const float detailH = detail_.empty() ? 0.0f : 8.0f + lineH;
    const int n = static_cast<int>(options_.size());
    const float listH = n * kButtonH + std::max(0, n - 1) * kButtonGap;
    const float panelH = kPanelPad + titleH + detailH + 36.0f + listH + kPanelPad;
    const float px = (W - panelW) * 0.5f, py = (H - panelH) * 0.5f;

    c.r.setContentAlpha(1.0f);
    c.r.draw(ui::Rect{0, 0, W, H, 0, ui::Color::black(0.55f * a)});
    c.r.setContentAlpha(a);
    // THE PAUSE MENU'S PANEL (design::menuPanel). The keyboard's black glass
    // was tried first (*"forgotten about"*), then the account panel's frosted
    // glass (*"too frosted or washed out"*). MMagTech, 2026-09-24.
    c.r.draw(design::menuPanel(px, py, panelW, panelH, 1.0f));

    auto centred = [&](const std::string& s, float base, TextStyle st, float alpha) {
        const std::string t = c.text.truncate(s, st, sc, textMax);
        const float w = c.text.measure(t, st, sc);
        c.text.draw(c.r, t, (W - w) * 0.5f, base, st, ui::Color::white(alpha), sc);
    };
    float y = py + kPanelPad;
    centred(title_, y + c.text.ascent(TextStyle::Title2, sc), TextStyle::Title2, 1.0f);
    y += titleH;
    if (!detail_.empty()) {
        centred(detail_, y + 8.0f + c.text.ascent(TextStyle::Callout, sc), TextStyle::Callout,
                0.60f);
        y += detailH;
    }
    y += 36.0f;

    const float f = focus_.value();
    const float bx = (W - kButtonW) * 0.5f;
    for (int i = 0; i < n; ++i) {
        const bool on = (i == slot_);
        const float s = on ? 1.0f + (kFocusScale - 1.0f) * f : 1.0f;
        const float dw = kButtonW * s, dh = kButtonH * s;
        const float dx = bx - (dw - kButtonW) * 0.5f, dy = y - (dh - kButtonH) * 0.5f;
        const float bf = on ? f : 0.0f;
        c.r.draw(design::menuButton(dx, dy, dw, dh, kButtonRadius * s, bf, 1.0f));
        const std::string label =
            c.text.truncate(options_[i], TextStyle::Title3, sc, kButtonW - 40.0f);
        const float lw = c.text.measure(label, TextStyle::Title3, sc);
        c.text.draw(c.r, label, dx + (dw - lw) * 0.5f,
                    dy + dh * 0.5f + c.text.ascent(TextStyle::Title3, sc) * 0.40f,
                    TextStyle::Title3, design::menuLabel(bf, 1.0f), sc);
        y += kButtonH + kButtonGap;
    }
    c.r.setContentAlpha(1.0f);
}

}  // namespace screens
