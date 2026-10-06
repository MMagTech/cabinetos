#include "achlist.h"

#include "sound.h"

#include <algorithm>
#include <cstdio>

namespace screens {

namespace {

// The choice panel's padding and fade (choice.cpp), wider: a description
// needs the room a button label does not.
constexpr float kPanelPad = 40.0f;
constexpr float kPanelW = 1400.0f;
constexpr float kPanelMaxH = 920.0f;
constexpr float kAppear = 0.280f;
constexpr float kRowH = 116.0f;
constexpr float kRowGap = 10.0f;
constexpr float kBadge = 88.0f;
constexpr float kScroll = 0.18f;

}  // namespace

void AchievementList::open(std::string title, ra::GameList list) {
    open_ = true;
    title_ = std::move(title);
    list_ = std::move(list);
    top_ = 0;
    scroll_.settle(0.0f);
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = appear_.duration;
    appear_.retarget(1.0f, kAppear);
}

bool AchievementList::key(Nav n) {
    if (!open_) return false;
    if (n == Nav::Back) {
        open_ = false;
        sound::play(sound::Cue::Back);
        return true;
    }
    if (n == Nav::Up || n == Nav::Down) {
        const int last = std::max(0, static_cast<int>(list_.items.size()) - shown_);
        const int next = std::clamp(top_ + (n == Nav::Down ? 1 : -1), 0, last);
        if (next == top_) {
            sound::play(sound::Cue::Edge);
        } else {
            top_ = next;
            scroll_.retarget(static_cast<float>(top_), kScroll);
            sound::play(sound::Cue::Move);
        }
    }
    return false;
}

void AchievementList::tick(float dt) {
    appear_.tick(dt);
    scroll_.tick(dt);
}

void AchievementList::draw(Ctx& c) {
    if (!open_) return;
    using ui::TextStyle;
    const float a = appear_.value();
    const float W = ui::kCanvasWidth, H = ui::kCanvasHeight, sc = c.sc;
    const int n = static_cast<int>(list_.items.size());

    const float titleH = c.text.lineHeight(TextStyle::Title2, sc);
    const float lineH = c.text.lineHeight(TextStyle::Callout, sc);
    const float headH = titleH + 8.0f + lineH + 32.0f;
    const float roomH = kPanelMaxH - kPanelPad * 2 - headH;
    shown_ = std::max(1, static_cast<int>((roomH + kRowGap) / (kRowH + kRowGap)));
    const int rows = std::min(n, shown_);
    const float listH = rows * kRowH + std::max(0, rows - 1) * kRowGap;
    const float panelH = kPanelPad + headH + listH + kPanelPad;
    const float px = (W - kPanelW) * 0.5f, py = (H - panelH) * 0.5f;

    c.r.setContentAlpha(1.0f);
    c.r.draw(ui::Rect{0, 0, W, H, 0, ui::Color::black(0.55f * a)});
    c.r.setContentAlpha(a);
    c.r.draw(design::menuPanel(px, py, kPanelW, panelH, 1.0f));

    const float textMax = kPanelW - kPanelPad * 2;
    auto centred = [&](const std::string& s, float base, TextStyle st, float alpha) {
        const std::string t = c.text.truncate(s, st, sc, textMax);
        const float w = c.text.measure(t, st, sc);
        c.text.draw(c.r, t, (W - w) * 0.5f, base, st, ui::Color::white(alpha), sc);
    };
    float y = py + kPanelPad;
    centred(title_, y + c.text.ascent(TextStyle::Title2, sc), TextStyle::Title2, 1.0f);
    y += titleH + 8.0f;
    centred(std::to_string(list_.unlocked) + " of " + std::to_string(list_.total) + " unlocked",
            y + c.text.ascent(TextStyle::Callout, sc), TextStyle::Callout, 0.60f);
    y += lineH + 32.0f;

    // The rows, scrolled smoothly between whole rows and clipped to the list.
    const float listTop = y;
    const float offset = scroll_.value() * (kRowH + kRowGap);
    const float rowX = px + kPanelPad, rowW = kPanelW - kPanelPad * 2 - 24.0f;
    const int first = std::max(0, static_cast<int>(scroll_.value()) - 1);
    for (int i = first; i < n && i <= first + shown_ + 2; ++i) {
        const float ry = listTop + i * (kRowH + kRowGap) - offset;
        if (ry + kRowH <= listTop - 1.0f || ry >= listTop + listH + 1.0f) continue;
        // A row half out of the list fades rather than being cut through.
        float vis = 1.0f;
        if (ry < listTop) vis = std::max(0.0f, 1.0f - (listTop - ry) / kRowH);
        if (ry + kRowH > listTop + listH) vis = std::max(0.0f, 1.0f - (ry + kRowH - listTop - listH) / kRowH);
        const ra::Achievement& it = list_.items[static_cast<size_t>(i)];
        const float on = it.unlocked ? 1.0f : 0.0f;
        c.r.draw(ui::Rect{rowX, ry, rowW, kRowH, 16.0f,
                          ui::Color::white((0.04f + 0.04f * on) * vis)});
        const float bx = rowX + 14.0f, by = ry + (kRowH - kBadge) * 0.5f;
        const std::string& badge = it.unlocked || it.lockedBadgeUrl.empty() ? it.badgeUrl
                                                                            : it.lockedBadgeUrl;
        const ui::Image* img = badge.empty() ? nullptr : &c.images.get(badge);
        if (img && img->ready) {
            c.r.drawTextured(bx, by, kBadge, kBadge, img->texture, 0, 0, 1, 1,
                             ui::Color{1, 1, 1, vis * img->fade * (it.unlocked ? 1.0f : 0.75f)},
                             false, 0.0f, bx, by, kBadge, kBadge, 12.0f);
        } else {
            c.r.draw(ui::Rect{bx, by, kBadge, kBadge, 12.0f, ui::Color::white(0.08f * vis)});
        }
        const float tx = bx + kBadge + 24.0f;
        const std::string pts = std::to_string(it.points);
        const float pw = c.text.measure(pts, TextStyle::Callout, sc);
        const float textW = rowX + rowW - 24.0f - pw - 24.0f - tx;
        const float t3 = c.text.lineHeight(TextStyle::Title3, sc);
        const float ty = ry + (kRowH - (t3 + lineH)) * 0.5f;
        c.text.draw(c.r, c.text.truncate(it.title, TextStyle::Title3, sc, textW), tx,
                    ty + c.text.ascent(TextStyle::Title3, sc), TextStyle::Title3,
                    ui::Color::white((it.unlocked ? 0.96f : 0.55f) * vis), sc);
        c.text.draw(c.r, c.text.truncate(it.description, TextStyle::Callout, sc, textW), tx,
                    ty + t3 + c.text.ascent(TextStyle::Callout, sc), TextStyle::Callout,
                    ui::Color::white((it.unlocked ? 0.62f : 0.40f) * vis), sc);
        c.text.draw(c.r, pts, rowX + rowW - 24.0f - pw,
                    ry + kRowH * 0.5f + c.text.ascent(TextStyle::Callout, sc) * 0.4f,
                    TextStyle::Callout, ui::Color::white((it.unlocked ? 0.70f : 0.40f) * vis), sc);
    }

    // WHERE IN THE LIST, when it is longer than the panel: a thin bar on the
    // right, the length of what is in view.
    if (n > shown_) {
        const float trackX = px + kPanelW - kPanelPad + 4.0f;
        const float barH = std::max(40.0f, listH * static_cast<float>(shown_) / n);
        const float travel = listH - barH;
        const float frac = scroll_.value() / static_cast<float>(std::max(1, n - shown_));
        c.r.draw(ui::Rect{trackX, listTop, 6.0f, listH, 3.0f, ui::Color::white(0.08f)});
        c.r.draw(ui::Rect{trackX, listTop + travel * std::clamp(frac, 0.0f, 1.0f), 6.0f, barH,
                          3.0f, ui::Color::white(0.45f)});
    }
    c.r.setContentAlpha(1.0f);
}

}  // namespace screens
