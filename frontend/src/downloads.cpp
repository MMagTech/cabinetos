#include "downloads.h"

#include "sound.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>

namespace screens {

namespace {

// The question panel's look (choice.cpp), at a fixed size: moving between the
// systems and a system's games changes what is in the panel, never its shape.
constexpr float kPanelPad = 40.0f;
constexpr float kPanelW = 1200.0f;
constexpr float kRowH = 80.0f;
constexpr float kRowGap = 10.0f;
constexpr float kRowRadius = 16.0f;
constexpr float kFocusScale = 1.03f;
constexpr float kAppear = 0.280f;
// Rows shown at once, the pinned ones included. More than that and the list
// scrolls under the pinned rows, which stay put so Remove and its count are
// always in view. Seven, not eight: eight reached up over the top bar
// (MMagTech on the TV, 2026-09-26).
constexpr int kSlots = 7;
// Between the pinned rows and the list, so the two read as different things.
constexpr float kSplitGap = 28.0f;
constexpr float kTick = 36.0f;          // the tick's circle
constexpr float kScrollBarW = 6.0f;

void drawTick(Ctx& c, float x, float cy, bool on, float a) {
    ui::Rect ring{x, cy - kTick * 0.5f, kTick, kTick, kTick * 0.5f,
                  ui::Color::white(on ? 0.95f * a : 0.0f)};
    if (!on) {
        ring.border = 3.0f;
        ring.borderColor = ui::Color::white(0.55f * a);
    }
    c.r.draw(ring);
    if (!on) return;
    // THE CHECK, stamped from small dots along its two strokes: the UI font has
    // no check mark, and a filled circle alone reads as a radio button.
    const float s = kTick;
    const float ax = x + s * 0.27f, ay = cy + s * 0.02f;
    const float bx = x + s * 0.43f, by = cy + s * 0.17f;
    const float ex = x + s * 0.74f, ey = cy - s * 0.17f;
    const float d = 4.2f;
    auto stroke = [&](float x0, float y0, float x1, float y1) {
        const float len = std::hypot(x1 - x0, y1 - y0);
        const int n = std::max(2, static_cast<int>(len / 1.2f));
        for (int i = 0; i <= n; ++i) {
            const float t = static_cast<float>(i) / n;
            c.r.draw(ui::Rect{x0 + (x1 - x0) * t - d * 0.5f, y0 + (y1 - y0) * t - d * 0.5f, d, d,
                              d * 0.5f, ui::Color::black(0.85f * a)});
        }
    };
    stroke(ax, ay, bx, by);
    stroke(bx, by, ex, ey);
}

}  // namespace

std::string sizeText(int64_t bytes) {
    char buf[32];
    const double b = static_cast<double>(bytes);
    if (b >= 1e12) std::snprintf(buf, sizeof buf, "%.2f TB", b / 1e12);
    else if (b >= 1e9) std::snprintf(buf, sizeof buf, "%.1f GB", b / 1e9);
    else if (b >= 1e6) std::snprintf(buf, sizeof buf, "%.0f MB", b / 1e6);
    else std::snprintf(buf, sizeof buf, "%.0f KB", b / 1e3);
    return buf;
}

std::string countText(int games, int64_t bytes) {
    std::string s = std::to_string(games) + (games == 1 ? " game" : " games");
    if (bytes > 0) s += " \xC2\xB7 " + sizeText(bytes);
    return s;
}

void DownloadsPanel::group() {
    std::map<std::string, System> by;
    for (int i = 0; i < static_cast<int>(items_.size()); ++i) {
        System& s = by[items_[i].system];
        s.name = items_[i].system;
        s.items.push_back(i);
        s.bytes += items_[i].bytes;
    }
    systems_.clear();
    for (auto& [name, s] : by) {
        // BIGGEST FIRST, because people come here for the space.
        std::sort(s.items.begin(), s.items.end(), [&](int a, int b) {
            if (items_[a].bytes != items_[b].bytes) return items_[a].bytes > items_[b].bytes;
            return items_[a].title < items_[b].title;
        });
        systems_.push_back(std::move(s));
    }
    std::sort(systems_.begin(), systems_.end(), [](const System& a, const System& b) {
        if (a.bytes != b.bytes) return a.bytes > b.bytes;
        return a.name < b.name;
    });
}

void DownloadsPanel::open(std::vector<DownloadItem> items) {
    open_ = true;
    items_ = std::move(items);
    group();
    system_ = -1;
    ticked_.clear();
    // On the first system, not on Remove all: the harmless row.
    row_ = listCount() > 0 ? actionCount() : 0;
    top_ = 0;
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, kAppear);
    focus_.settle(1.0f);
}

bool DownloadsPanel::replace(std::vector<DownloadItem> items) {
    const std::string was = system_ >= 0 ? systems_[system_].name : std::string();
    const int wasSystemRow = system_;
    const int wasRow = row_;
    items_ = std::move(items);
    group();
    ticked_.clear();
    if (items_.empty()) return false;
    system_ = -1;
    for (int i = 0; i < static_cast<int>(systems_.size()); ++i)
        if (!was.empty() && systems_[i].name == was) system_ = i;
    if (system_ >= 0) {
        // Back on Select all, where the system opened: Remove, where focus
        // was, is greyed now that nothing is ticked.
        row_ = 0;
        top_ = 0;
    } else {
        // The system emptied: back to the systems, on the one after it.
        const int at = wasSystemRow >= 0 ? wasSystemRow : wasRow - actionCount();
        row_ = actionCount() + std::clamp(at, 0, listCount() - 1);
    }
    scrollToFocus();
    return true;
}

// A SYSTEM OPENS ON "SELECT ALL" (MMagTech on the TV, 2026-09-26): it only
// ticks, so it is harmless, and clearing a whole system is then A, down, A.
// The systems list still opens on the first system rather than on Remove all.
void DownloadsPanel::openSystem(int index) {
    if (index < 0 || index >= static_cast<int>(systems_.size())) return;
    system_ = index;
    ticked_.clear();
    row_ = 0;
    top_ = 0;
}

int DownloadsPanel::actionCount() const { return system_ < 0 ? 1 : 2; }

bool DownloadsPanel::actionEnabled(int i) const {
    if (system_ >= 0 && i == 1) return !ticked_.empty();
    return true;
}

int DownloadsPanel::listCount() const {
    return system_ < 0 ? static_cast<int>(systems_.size())
                       : static_cast<int>(systems_[system_].items.size());
}

int64_t DownloadsPanel::tickedBytes() const {
    int64_t b = 0;
    for (const DownloadItem& it : items_)
        if (ticked_.count(it.romId)) b += it.bytes;
    return b;
}

void DownloadsPanel::scrollToFocus() {
    const int visible = kSlots - actionCount();
    const int li = row_ - actionCount();
    if (li >= 0) {
        if (li < top_) top_ = li;
        if (li >= top_ + visible) top_ = li - visible + 1;
    }
    top_ = std::clamp(top_, 0, std::max(0, listCount() - visible));
}

void DownloadsPanel::moveTo(int row) {
    row_ = row;
    scrollToFocus();
    focus_.retarget(0.0f, 0.0f);
    focus_.elapsed = 0.0f;
    focus_.retarget(1.0f, design::kFocusDuration);
    sound::play(sound::Cue::Move);
}

DownloadsPanel::Outcome DownloadsPanel::key(Nav n) {
    if (!open_) return Outcome::None;
    const int actions = actionCount();
    switch (n) {
        case Nav::Up:
        case Nav::Down: {
            const int step = n == Nav::Down ? 1 : -1;
            int next = row_ + step;
            while (next >= 0 && next < rowCount() && !focusable(next)) next += step;
            if (next < 0 || next >= rowCount()) {
                sound::play(sound::Cue::Edge);
                return Outcome::None;
            }
            moveTo(next);
            return Outcome::None;
        }
        case Nav::Left:
        case Nav::Right: {
            // A PAGE AT A TIME through a long list: left and right do nothing
            // else here, and three hundred NES games is a long way by one.
            const int page = kSlots - actions;
            if (row_ < actions || listCount() <= page) {
                sound::play(sound::Cue::Edge);
                return Outcome::None;
            }
            const int next = std::clamp(row_ + (n == Nav::Right ? page : -page), actions,
                                        rowCount() - 1);
            if (next == row_) {
                sound::play(sound::Cue::Edge);
                return Outcome::None;
            }
            moveTo(next);
            return Outcome::None;
        }
        case Nav::Activate: {
            if (system_ < 0) {
                if (row_ == 0) {
                    remove_.clear();
                    int64_t bytes = 0;
                    for (const DownloadItem& it : items_) {
                        remove_.push_back(it.romId);
                        bytes += it.bytes;
                    }
                    removingAll_ = true;
                    removeTitle_ = "Remove all downloads?";
                    removeDetail_ = countText(static_cast<int>(items_.size()), bytes);
                    sound::play(sound::Cue::Activate);
                    return Outcome::Remove;
                }
                openSystem(row_ - actions);
                focus_.settle(1.0f);
                sound::play(sound::Cue::Activate);
                return Outcome::None;
            }
            const System& s = systems_[system_];
            if (row_ == 0) {
                // Select all, or Select none once everything is ticked.
                const bool all = static_cast<int>(ticked_.size()) == listCount();
                ticked_.clear();
                if (!all)
                    for (int i : s.items) ticked_.insert(items_[i].romId);
                sound::play(sound::Cue::Activate);
                return Outcome::None;
            }
            if (row_ == 1) {
                if (ticked_.empty()) { sound::play(sound::Cue::Edge); return Outcome::None; }
                remove_.assign(ticked_.begin(), ticked_.end());
                removingAll_ = false;
                const int64_t bytes = tickedBytes();
                if (remove_.size() == 1) {
                    std::string title;
                    for (const DownloadItem& it : items_)
                        if (it.romId == remove_.front()) title = it.title;
                    removeTitle_ = "Remove " + title + "?";
                    removeDetail_ = bytes > 0 ? sizeText(bytes) : std::string();
                } else {
                    removeTitle_ = "Remove " + std::to_string(remove_.size()) + " games?";
                    removeDetail_ = bytes > 0 ? sizeText(bytes) : std::string();
                }
                sound::play(sound::Cue::Activate);
                return Outcome::Remove;
            }
            const int id = items_[s.items[row_ - actions]].romId;
            if (!ticked_.erase(id)) ticked_.insert(id);
            sound::play(sound::Cue::Activate);
            return Outcome::None;
        }
        case Nav::Back:
            sound::play(sound::Cue::Back);
            if (system_ >= 0) {
                const int was = system_;
                system_ = -1;
                ticked_.clear();
                row_ = actionCount() + was;
                top_ = 0;
                scrollToFocus();
                focus_.settle(1.0f);
                return Outcome::None;
            }
            return Outcome::Closed;
    }
    return Outcome::None;
}

void DownloadsPanel::tick(float dt) {
    appear_.tick(dt);
    focus_.tick(dt);
}

void DownloadsPanel::draw(Ctx& c) {
    if (!open_) return;
    using ui::TextStyle;
    const float a = appear_.value();
    const float W = ui::kCanvasWidth, H = ui::kCanvasHeight, sc = c.sc;

    // What the panel is showing: its title, the total under it, and its rows.
    std::string title = "Downloads";
    int games = static_cast<int>(items_.size());
    int64_t bytes = 0;
    for (const DownloadItem& it : items_) bytes += it.bytes;
    if (system_ >= 0) {
        title = systems_[system_].name;
        games = static_cast<int>(systems_[system_].items.size());
        bytes = systems_[system_].bytes;
    }
    const std::string detail = countText(games, bytes);

    const float titleH = c.text.lineHeight(TextStyle::Title2, sc);
    const float lineH = c.text.lineHeight(TextStyle::Callout, sc);
    const float listH = kSlots * kRowH + (kSlots - 1) * kRowGap + kSplitGap;
    const float panelH = kPanelPad + titleH + 8.0f + lineH + 36.0f + listH + kPanelPad;
    const float px = (W - kPanelW) * 0.5f, py = (H - panelH) * 0.5f;
    const float textMax = kPanelW - kPanelPad * 2;

    c.r.setContentAlpha(1.0f);
    c.r.draw(ui::Rect{0, 0, W, H, 0, ui::Color::black(0.55f * a)});
    c.r.setContentAlpha(a);
    c.r.draw(design::menuPanel(px, py, kPanelW, panelH, 1.0f));

    auto centred = [&](const std::string& s, float base, TextStyle st, float alpha) {
        const std::string t = c.text.truncate(s, st, sc, textMax);
        const float w = c.text.measure(t, st, sc);
        c.text.draw(c.r, t, (W - w) * 0.5f, base, st, ui::Color::white(alpha), sc);
    };
    float y = py + kPanelPad;
    centred(title, y + c.text.ascent(TextStyle::Title2, sc), TextStyle::Title2, 1.0f);
    y += titleH + 8.0f;
    centred(detail, y + c.text.ascent(TextStyle::Callout, sc), TextStyle::Callout, 0.60f);
    y += lineH + 36.0f;

    const float f = focus_.value();
    const float rowW = kPanelW - kPanelPad * 2;
    const float bx = px + kPanelPad;
    const int actions = actionCount();

    auto drawRow = [&](int row, float ry, const std::string& label, const std::string& value,
                       bool enabled, int tick) {
        const bool on = row == row_;
        const float s = on ? 1.0f + (kFocusScale - 1.0f) * f : 1.0f;
        const float dw = rowW * s, dh = kRowH * s;
        const float dx = bx - (dw - rowW) * 0.5f, dy = ry - (dh - kRowH) * 0.5f;
        const float bf = on ? f : 0.0f;
        c.r.draw(design::menuButton(dx, dy, dw, dh, kRowRadius * s, bf, enabled ? 1.0f : 0.5f));
        const float base = dy + dh * 0.5f + c.text.ascent(TextStyle::Title3, sc) * 0.40f;
        float lx = dx + 24.0f;
        if (tick >= 0) {
            drawTick(c, lx, dy + dh * 0.5f, tick == 1, 1.0f);
            lx += kTick + 20.0f;
        }
        const float vw = value.empty() ? 0.0f : c.text.measure(value, TextStyle::Callout, sc);
        const float room = dx + dw - 24.0f - (value.empty() ? 0.0f : vw + 24.0f) - lx;
        const std::string t = c.text.truncate(label, TextStyle::Title3, sc, room);
        c.text.draw(c.r, t, lx, base, TextStyle::Title3,
                    enabled ? design::menuLabel(bf, 1.0f) : ui::Color::white(0.35f), sc);
        if (!value.empty())
            c.text.draw(c.r, value, dx + dw - 24.0f - vw, base, TextStyle::Callout,
                        ui::Color::white(0.60f), sc);
    };

    // THE PINNED ROWS.
    if (system_ < 0) {
        drawRow(0, y, "Remove all", "", true, -1);
    } else {
        const bool all = static_cast<int>(ticked_.size()) == listCount();
        drawRow(0, y, all ? "Select none" : "Select all", "", true, -1);
        const std::string rm = ticked_.empty()
            ? "Remove"
            : "Remove " + countText(static_cast<int>(ticked_.size()), tickedBytes());
        drawRow(1, y + kRowH + kRowGap, rm, "", !ticked_.empty(), -1);
    }
    const float listTop = y + actions * (kRowH + kRowGap) + kSplitGap;

    // THE LIST, scrolled under them.
    const int visible = kSlots - actions;
    const int n = listCount();
    float ry = listTop;
    for (int li = top_; li < std::min(n, top_ + visible); ++li) {
        const int row = actions + li;
        if (system_ < 0) {
            const System& s = systems_[li];
            drawRow(row, ry, s.name, countText(static_cast<int>(s.items.size()), s.bytes), true,
                    -1);
        } else {
            const DownloadItem& it = items_[systems_[system_].items[li]];
            std::string value = sizeText(it.bytes);
            if (!it.drive.empty()) value += " \xC2\xB7 " + it.drive;
            drawRow(row, ry, it.title, value, true, ticked_.count(it.romId) ? 1 : 0);
        }
        ry += kRowH + kRowGap;
    }

    // WHERE IN A LONG LIST, a thin bar at the panel's right edge. Without it
    // the eighth row looks like the last one.
    if (n > visible) {
        const float trackTop = listTop;
        const float trackH = visible * kRowH + (visible - 1) * kRowGap;
        // In the panel's margin, clear of a focused row, which grows.
        const float tx = px + kPanelW - kPanelPad * 0.35f - kScrollBarW * 0.5f;
        c.r.draw(ui::Rect{tx, trackTop, kScrollBarW, trackH, kScrollBarW * 0.5f,
                          ui::Color::white(0.14f)});
        const float thumbH = std::max(40.0f, trackH * visible / n);
        const float thumbY = trackTop + (trackH - thumbH) * top_ / std::max(1, n - visible);
        c.r.draw(ui::Rect{tx, thumbY, kScrollBarW, thumbH, kScrollBarW * 0.5f,
                          ui::Color::white(0.75f)});
    }
    c.r.setContentAlpha(1.0f);
}

}  // namespace screens
