#include "choice.h"

#include "sound.h"

#include <algorithm>

namespace screens {

namespace {

// The PIN pad's panel sizes (pin.cpp); the look is design::menuPanel.
constexpr float kPanelPad = 40.0f;
constexpr float kPanelRadius = 32.0f;
constexpr float kPanelMaxW = 1200.0f;
constexpr float kButtonW = 600.0f;
constexpr float kButtonH = 88.0f;
constexpr float kButtonGap = 12.0f;
constexpr float kButtonRadius = 16.0f;
constexpr float kFocusScale = 1.04f;
constexpr float kAppear = 0.280f;
// MORE THAN THIS SCROLLS, for a Wi-Fi list in a crowded building.
constexpr int kMaxVisible = 6;

}  // namespace

void ChoiceScreen::open(std::string title, std::string detail,
                        std::vector<std::string> options, int focus) {
    open_ = true;
    title_ = std::move(title);
    detail_ = std::move(detail);
    options_ = std::move(options);
    values_.clear();
    slot_ = options_.empty() ? 0 : std::clamp(focus, 0, static_cast<int>(options_.size()) - 1);
    top_ = std::max(0, slot_ - (kMaxVisible - 1));
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, kAppear);
    focus_.settle(1.0f);
}

void ChoiceScreen::replace(std::vector<std::string> options, std::vector<std::string> values,
                           std::string detail) {
    const std::string was = (slot_ >= 0 && slot_ < static_cast<int>(options_.size()))
                                ? options_[slot_] : std::string();
    options_ = std::move(options);
    values_ = std::move(values);
    detail_ = std::move(detail);
    slot_ = 0;
    for (int i = 0; i < static_cast<int>(options_.size()); ++i)
        if (options_[i] == was) slot_ = i;
    top_ = std::clamp(top_, 0, std::max(0, static_cast<int>(options_.size()) - kMaxVisible));
    if (slot_ < top_) top_ = slot_;
    if (slot_ >= top_ + kMaxVisible) top_ = slot_ - kMaxVisible + 1;
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
            if (slot_ < top_) top_ = slot_;
            if (slot_ >= top_ + kMaxVisible) top_ = slot_ - kMaxVisible + 1;
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
            if (options_.empty()) { sound::play(sound::Cue::Edge); return Outcome::None; }
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

    // The detail may be more than one line, split at '\n' (Sign out's two).
    std::vector<std::string> lines;
    for (size_t at = 0; !detail_.empty() && at <= detail_.size();) {
        size_t nl = detail_.find('\n', at);
        if (nl == std::string::npos) nl = detail_.size();
        lines.push_back(detail_.substr(at, nl - at));
        at = nl + 1;
    }
    // A LIST GROWS TO ITS LONGEST LINE, name and value, up to the panel's
    // limit, so "Universal Blue ... Image tooling" is not cut to "Unive...".
    // A question keeps its fixed buttons. Found on Credits and licences,
    // 2026-09-25; long Wi-Fi names get the same room.
    const bool list = !values_.empty();
    float rowW = kButtonW;
    if (list) {
        for (size_t i = 0; i < options_.size(); ++i) {
            const float vw = i < values_.size() && !values_[i].empty()
                ? c.text.measure(values_[i], TextStyle::Callout, sc) + 24.0f : 0.0f;
            rowW = std::max(rowW, c.text.measure(options_[i], TextStyle::Title3, sc) + vw + 48.0f);
        }
        rowW = std::min(rowW, kPanelMaxW - kPanelPad * 2);
    }
    float innerW = rowW;
    innerW = std::max(innerW, c.text.measure(title_, TextStyle::Title2, sc));
    for (const std::string& l : lines)
        innerW = std::max(innerW, c.text.measure(l, TextStyle::Callout, sc));
    const float panelW = std::min(kPanelMaxW, innerW + kPanelPad * 2);
    const float textMax = panelW - kPanelPad * 2;
    const float titleH = c.text.lineHeight(TextStyle::Title2, sc);
    const float lineH = c.text.lineHeight(TextStyle::Callout, sc);
    const float detailH = lines.empty() ? 0.0f : 8.0f + lineH * lines.size();
    const int n = static_cast<int>(options_.size());
    const int shown = std::min(n, kMaxVisible);
    const float listH = shown * kButtonH + std::max(0, shown - 1) * kButtonGap;
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
    for (size_t i = 0; i < lines.size(); ++i)
        centred(lines[i], y + 8.0f + lineH * i + c.text.ascent(TextStyle::Callout, sc),
                TextStyle::Callout, 0.60f);
    y += detailH;
    y += 36.0f;

    const float f = focus_.value();
    const float bx = (W - rowW) * 0.5f;
    for (int i = top_; i < std::min(n, top_ + kMaxVisible); ++i) {
        const bool on = (i == slot_);
        const float s = on ? 1.0f + (kFocusScale - 1.0f) * f : 1.0f;
        const float dw = rowW * s, dh = kButtonH * s;
        const float dx = bx - (dw - rowW) * 0.5f, dy = y - (dh - kButtonH) * 0.5f;
        const float bf = on ? f : 0.0f;
        c.r.draw(design::menuButton(dx, dy, dw, dh, kButtonRadius * s, bf, 1.0f));
        const float base = dy + dh * 0.5f + c.text.ascent(TextStyle::Title3, sc) * 0.40f;
        // A LIST (answers with values) reads left to right, "name ... state",
        // like a Settings row; a plain question keeps its answers centred.
        const std::string value =
            (list && i < static_cast<int>(values_.size())) ? values_[i] : std::string();
        const float vw = value.empty() ? 0.0f : c.text.measure(value, TextStyle::Callout, sc);
        const float room = rowW - 48.0f - (value.empty() ? 0.0f : vw + 24.0f);
        const std::string label = c.text.truncate(options_[i], TextStyle::Title3, sc, room);
        const float lw = c.text.measure(label, TextStyle::Title3, sc);
        const float lx = list ? dx + 24.0f : dx + (dw - lw) * 0.5f;
        c.text.draw(c.r, label, lx, base, TextStyle::Title3, design::menuLabel(bf, 1.0f), sc);
        if (!value.empty())
            c.text.draw(c.r, value, dx + dw - 24.0f - vw, base, TextStyle::Callout,
                        ui::Color::white(0.60f), sc);
        y += kButtonH + kButtonGap;
    }
    c.r.setContentAlpha(1.0f);
}

}  // namespace screens
