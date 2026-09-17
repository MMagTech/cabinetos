#include "screens.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace screens {
namespace {

using design::Animated;
using design::Card;

// Two caption lines with reserved space, so rows stay aligned whether a title
// wraps or not. One line truncated almost every real title at these widths —
// the reference implementation records that as a mistake already made.
//
// Greedy, and it breaks on spaces only. A CJK title has none, so it falls
// through to the truncating path below, which is the right answer for it: a
// Japanese title is short in characters and long in pixels.
std::vector<std::string> wrapTwoLines(ui::TextRenderer& text, const std::string& s,
                                      ui::TextStyle style, float sc, float width) {
    std::vector<std::string> lines;
    if (text.measure(s, style, sc) <= width) {
        lines.push_back(s);
        return lines;
    }

    // Break opportunities: after a space, and AFTER A HYPHEN. The hyphen is not
    // typographic pedantry — this library holds "TurboGrafx-16" and
    // "TurboGrafx-CD", which have no space between them and their only
    // distinguishing part, so a space-only wrap rendered both tiles as
    // "TurboGraf..." and made them the same tile to look at.
    //
    // A CJK title has neither, and falls through to the truncating path below,
    // which is the right answer for it: those are short in characters and long
    // in pixels.
    size_t brk = std::string::npos;
    std::string line;
    for (size_t i = 1; i <= s.size(); ++i) {
        const bool afterSpace = (i < s.size() && s[i] == ' ');
        const bool afterHyphen = (s[i - 1] == '-');
        if (!afterSpace && !afterHyphen) continue;
        const std::string candidate = s.substr(0, i);
        if (text.measure(candidate, style, sc) > width) break;
        // A space is dropped at the break; a hyphen stays on the line it ends.
        brk = afterSpace ? i + 1 : i;
        line = candidate;
    }
    if (brk == std::string::npos || brk >= s.size()) {
        // One unbreakable run. Truncate it rather than overflowing the column.
        lines.push_back(text.truncate(s, style, sc, width));
        return lines;
    }
    lines.push_back(line);
    lines.push_back(text.truncate(s.substr(brk), style, sc, width));
    return lines;
}

// The artwork focus treatment: lift, shadow, and a rim unless the element is a
// composite one. A rectangle drawn around a button whose label mixes art with
// its own text always crosses the text somewhere, which is why a platform tile
// and the hero card get the scale and the shadow and no rim.
void applyArtworkFocus(ui::Rect& rect, float f, bool rim) {
    rect.shadowBlur = f * design::kFocusShadowBlur;
    rect.shadowOffsetY = f * design::kFocusShadowOffsetY;
    rect.shadowColor = ui::Color::black(0.55f * f);
    if (rim) {
        rect.border = f * design::kFocusRimWidth;
        rect.borderColor = ui::palette::kFocusRim;
    }
}

// A cover drawn into a box: the coloured panel that shows while the art decodes
// or if there is none, the art over it, and the rim on top — the rim is the
// focus indicator and nothing may sit above it.
void drawCover(Ctx& c, const Card& card, float x, float y, float w, float h,
               float radius, float f, bool rim) {
    ui::Rect panel{x, y, w, h, radius, card.art};
    applyArtworkFocus(panel, f, rim);
    c.r.draw(panel);
    if (card.cover.empty()) return;
    ui::drawImage(c.r, c.images.get(card.cover), x, y, w, h, ui::Fit::Fill, 1.0f, radius);
    if (f > 0.0f && rim) {
        ui::Rect edge{x, y, w, h, radius, ui::Color::white(0)};
        edge.border = f * design::kFocusRimWidth;
        edge.borderColor = ui::palette::kFocusRim;
        c.r.draw(edge);
    }
}

// A capsule pill, treatment 2 in the design system: tinted blur, text to full
// white, scale 1.06. `selected` and `focused` are INDEPENDENT and both visible
// at once — a controller UI where focus can move away from the current
// selection has to show both or the person loses their place.
float pillWidth(ui::TextRenderer& text, const std::string& label, float sc) {
    return text.measure(label, ui::TextStyle::Title3, sc) + design::kPillPadX * 2.0f;
}

float pillHeight(ui::TextRenderer& text, float sc) {
    return text.lineHeight(ui::TextStyle::Title3, sc) + design::kPillPadY * 2.0f;
}

void drawPill(Ctx& c, const std::string& label, float x, float y, bool selected,
              float f) {
    const float w0 = pillWidth(c.text, label, c.sc);
    const float h0 = pillHeight(c.text, c.sc);
    const float s = 1.0f + f * (design::kPillFocusScale - 1.0f);
    const float w = w0 * s, h = h0 * s;
    const float px = x - (w - w0) * 0.5f;
    const float py = y - (h - h0) * 0.5f;

    const float tint = (selected ? design::kSelectedTint : 0.0f) +
                       f * design::kFocusedTint;
    c.r.drawGlass(ui::Rect{px, py, w, h, h * 0.5f, ui::Color::white(0)},
                  design::kHeroPillBlur, ui::Color::white(tint));
    const float tw = c.text.measure(label, ui::TextStyle::Title3, c.sc);
    c.text.draw(c.r, label, px + (w - tw) * 0.5f,
                py + design::kPillPadY * s + c.text.ascent(ui::TextStyle::Title3, c.sc),
                ui::TextStyle::Title3,
                ui::Color::white(selected || f > 0.5f ? 1.0f : 0.60f), c.sc);
}

}  // namespace

// ---------------------------------------------------------------------------
// The Library
// ---------------------------------------------------------------------------

void LibraryScreen::build(std::vector<Tile> platforms, std::vector<Tile> collections) {
    platforms_ = std::move(platforms);
    collections_ = std::move(collections);
}

void LibraryScreen::enter() {
    if (entered_) return;    // and ONLY the first time
    entered_ = true;
    row_ = 0;
    slot_ = 0;
    pillFocus_[0].settle(1.0f);
}

void LibraryScreen::focusTile(int index) {
    auto& tiles = tab_ == 0 ? platforms_ : collections_;
    if (tiles.empty()) return;
    if (row_ == 0) pillFocus_[slot_].settle(0.0f);
    row_ = 1;
    slot_ = std::clamp(index, 0, static_cast<int>(tiles.size()) - 1);
    rememberedTileSlot_ = slot_;
    tiles[slot_].focus.settle(1.0f);
    entered_ = true;
}

int LibraryScreen::columns() const {
    // Adaptive: as many columns of at least the minimum width as fit, then
    // stretched to fill. Four on a 1920 canvas at an 80pt inset.
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const int n = static_cast<int>((usable + design::kTileSpacing) /
                                   (design::kTileMinWidth + design::kTileSpacing));
    return std::max(1, n);
}

float LibraryScreen::tileWidth() const {
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const int n = columns();
    return (usable - design::kTileSpacing * static_cast<float>(n - 1)) /
           static_cast<float>(n);
}

int LibraryScreen::tileRows() const {
    const int n = static_cast<int>(visible().size());
    if (n == 0) return 0;
    return (n + columns() - 1) / columns();
}

void LibraryScreen::tick(float dt) {
    for (auto& p : pillFocus_) p.tick(dt);
    tabChange_.tick(dt);
    scroll_.tick(dt);
    for (auto& t : platforms_) t.focus.tick(dt);
    for (auto& t : collections_) t.focus.tick(dt);
}

void LibraryScreen::moveFocus(int dx, int dy) {
    auto& tiles = tab_ == 0 ? platforms_ : collections_;
    const int cols = columns();
    const int count = static_cast<int>(tiles.size());

    auto leave = [&]() {
        if (row_ == 0) pillFocus_[slot_].retarget(0.0f, design::kFocusDuration);
        else if (slot_ < count) tiles[slot_].focus.retarget(0.0f, design::kFocusDuration);
    };
    auto arrive = [&]() {
        if (row_ == 0) pillFocus_[slot_].retarget(1.0f, design::kFocusDuration);
        else if (slot_ < count) tiles[slot_].focus.retarget(1.0f, design::kFocusDuration);
    };

    if (row_ == 0) {
        if (dx != 0) {
            const int next = std::clamp(slot_ + dx, 0, 1);
            if (next == slot_) return;
            leave();
            slot_ = next;
            arrive();
            return;
        }
        if (dy > 0 && count > 0) {
            leave();
            row_ = 1;
            slot_ = std::clamp(rememberedTileSlot_, 0, count - 1);
            arrive();
        }
        return;
    }

    // In the grid.
    if (dy < 0 && slot_ < cols) {
        // Off the top row and back to the switcher, which is where the person
        // came in. Remembered, so coming back down lands where they left.
        leave();
        rememberedTileSlot_ = slot_;
        row_ = 0;
        arrive();
        return;
    }
    int next = slot_;
    if (dx != 0) {
        // Rows do not wrap. Running off the right of one row and appearing at
        // the left of the next is disorienting with a d-pad, and the reference
        // implementation's grids do not do it.
        const int rowStart = (slot_ / cols) * cols;
        const int rowEnd = std::min(rowStart + cols - 1, count - 1);
        next = std::clamp(slot_ + dx, rowStart, rowEnd);
    } else if (dy != 0) {
        next = slot_ + dy * cols;
        if (next < 0 || next >= count) return;
    }
    if (next == slot_) return;
    leave();
    slot_ = next;
    rememberedTileSlot_ = slot_;
    arrive();
}

Result LibraryScreen::key(Nav n) {
    auto& tiles = tab_ == 0 ? platforms_ : collections_;
    switch (n) {
        case Nav::Left:  moveFocus(-1, 0); break;
        case Nav::Right: moveFocus(+1, 0); break;
        case Nav::Up:    moveFocus(0, -1); break;
        case Nav::Down:  moveFocus(0, +1); break;
        case Nav::Back:  return {Action::Back, 0};
        case Nav::Activate:
            if (row_ == 0) {
                if (slot_ == tab_) return {};
                // Changing tab resets the grid position: the remembered slot
                // belonged to a different list and pointing it at this one
                // means nothing.
                tab_ = slot_;
                rememberedTileSlot_ = 0;
                tabChange_.retarget(0.0f, 0.0f);
                tabChange_.elapsed = 0.0f;
                tabChange_.retarget(1.0f, 0.220f);   // a segmented choice: 220 ms
                scroll_.retarget(0.0f, design::kFocusDuration);
                return {};
            }
            if (slot_ >= 0 && slot_ < static_cast<int>(tiles.size())) {
                // An unplayable platform is drawn and says why, and does not
                // open. Offering a screen of games that cannot start is the
                // promise this whole catalog exists to avoid breaking.
                if (!tiles[slot_].enterable) return {};
                return {Action::OpenTile, slot_};
            }
            break;
    }
    return {};
}

void LibraryScreen::draw(Ctx& c) {
    const auto& tiles = visible();
    const int cols = columns();
    const float tw = tileWidth();
    const float pillH = pillHeight(c.text, c.sc);
    const float gridTop = design::kSwitcherTop + pillH + design::kSwitcherGapBelow;
    const float rowPitch = design::kTileHeight + design::kTileSpacing;

    // Scroll to keep the focused row on screen, and never past the end of the
    // content: pinning the last row to the top leaves half a screen of nothing
    // under it, which reads as the layout having broken.
    const float contentH = gridTop + static_cast<float>(tileRows()) * rowPitch +
                           design::kSwitcherTop;
    const float maxScroll = std::max(0.0f, contentH - ui::kCanvasHeight);
    float want = 0.0f;
    if (row_ > 0) {
        const int r = slot_ / cols;
        const float rowTop = gridTop + static_cast<float>(r) * rowPitch;
        const float rowBottom = rowTop + design::kTileHeight;
        const float current = scroll_.to;
        if (rowBottom - current > ui::kCanvasHeight - design::kSwitcherTop)
            want = rowBottom - (ui::kCanvasHeight - design::kSwitcherTop);
        else if (rowTop - current < gridTop)
            want = std::max(0.0f, rowTop - gridTop);
        else
            want = current;
        want = std::clamp(want, 0.0f, maxScroll);
    }
    if (std::fabs(want - scroll_.to) > 0.5f)
        scroll_.retarget(want, design::kFocusDuration);
    const float scroll = scroll_.value();

    if (tiles.empty()) {
        // An honest empty state rather than a blank grid. A library with no
        // collections is ordinary, not broken.
        const char* line = tab_ == 0 ? "No systems on this server"
                                     : "No collections on this server";
        const float w = c.text.measure(line, ui::TextStyle::Title3, c.sc);
        c.text.draw(c.r, line, (ui::kCanvasWidth - w) * 0.5f, ui::kCanvasHeight * 0.5f,
                    ui::TextStyle::Title3, ui::Color::white(0.60f), c.sc);
        return;
    }

    // Unfocused first, so a focused tile's shadow lands on top of its
    // neighbours rather than under them.
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < tiles.size(); ++i) {
            const bool isFocused = (row_ > 0 && static_cast<int>(i) == slot_);
            if ((pass == 0) == isFocused) continue;

            const int col = static_cast<int>(i) % cols;
            const int r = static_cast<int>(i) / cols;
            const float bx = design::kLibraryInset +
                             static_cast<float>(col) * (tw + design::kTileSpacing);
            const float by = gridTop + static_cast<float>(r) * rowPitch - scroll;
            // Cull to what is on screen. A grid is a window onto a list, not a
            // drawing of the list — and the cover cache must not be asked for
            // art that is nowhere near the screen.
            if (by + design::kTileHeight < -rowPitch) continue;
            if (by > ui::kCanvasHeight + rowPitch) break;

            const Tile& t = tiles[i];
            const float f = t.focus.value();
            const float s = 1.0f + f * (design::kFocusScale - 1.0f);
            const float w = tw * s, h = design::kTileHeight * s;
            const float x = bx - (w - tw) * 0.5f;
            const float y = by - (h - design::kTileHeight) * 0.5f;

            // The tile's ground: the one solid panel colour in the design
            // system. Dimmed when the platform cannot be played, which is the
            // difference a person has to be able to see from a sofa.
            ui::Rect plate{x, y, w, h, design::kTileRadius,
                           t.enterable ? ui::palette::kSurface
                                       : ui::Color::white(0.06f)};
            // NO RIM: a platform tile is a composite element, art mixed with
            // its own text, and a rectangle drawn round it crosses the text.
            applyArtworkFocus(plate, f, /*rim=*/false);
            c.r.draw(plate);

            // The cover, right-hand side, inset by the tile's padding. A cover
            // thumbnail inside another element takes the 8pt radius.
            const float pad = design::kTilePadding * s;
            // SMALLER THAN THE TILE IS TALL, and the width it gives back goes to
            // the label. A thumbnail filling the full height left 227pt for a
            // name, which is less than "Sega Master" needs at Title 3 — so the
            // artwork was winning an argument against the thing the tile is
            // actually for. A tile with no artwork at all gives the label the
            // whole width, rather than reserving a column for a picture that
            // does not exist: that is the unplayable ones, which have the most
            // to say and had the least room to say it in.
            const float artH = t.cover.empty() ? 0.0f : design::kTileArtHeight * s;
            const float artW = artH * 0.75f;      // 3:4
            const float artX = x + w - pad - artW;
            if (!t.cover.empty()) {
                const ui::Image& img = c.images.get(t.cover);
                // The blurred echo behind everything, so the tile is the art's
                // own colours rather than a flat panel. Clipped to the tile.
                if (img.ready) {
                    c.r.drawTextured(x, y, w, h, img.texture, 0, 0, 1, 1,
                                     ui::Color{1, 1, 1, 0.35f * img.fade}, false,
                                     design::kBackdropBlur, x, y, w, h,
                                     design::kTileRadius);
                }
                // Centred against the tile rather than pinned to the padding,
                // now that it no longer spans the full height.
                ui::drawImage(c.r, img, artX, y + (h - artH) * 0.5f, artW, artH,
                              ui::Fit::Fill, t.enterable ? 1.0f : 0.45f, 8.0f);
            }

            // The label, left. Title 3 for the name, Footnote for the count —
            // the ramp's own roles for a tile title and a game count.
            //
            // TWO LINES FOR THE TITLE, for the same reason grid captions get
            // two: one line truncated almost every real name at this width, and
            // here it did worse than look untidy. "Nintendo 64" and "Nintendo
            // DS" both came out as "Nintendo ..." on adjacent tiles, and
            // "Arcade (FinalBurn Neo)" and "Arcade (MAME 2003-Plus)" — the two
            // this console goes to some trouble to tell apart — both came out as
            // "Arcade (...". A tile nobody can identify is not a tile.
            const float textW = artW > 0 ? (artX - x - pad - design::kTileArtGap)
                                         : (w - pad * 2.0f);
            const std::vector<std::string> titleLines =
                wrapTwoLines(c.text, t.title, ui::TextStyle::Title3, c.sc, textW);
            const float titleH = c.text.lineHeight(ui::TextStyle::Title3, c.sc) *
                                 static_cast<float>(titleLines.size());
            const float labelH = titleH + c.text.lineHeight(ui::TextStyle::Footnote, c.sc);
            // Centred against the artwork rather than pinned to the top, so a
            // one-line tile and a two-line tile both sit square in the row.
            float baseline = y + (h - labelH) * 0.5f +
                             c.text.ascent(ui::TextStyle::Title3, c.sc);
            for (const std::string& line : titleLines) {
                c.text.draw(c.r, line, x + pad, baseline, ui::TextStyle::Title3,
                            ui::Color::white(t.enterable ? 1.0f : 0.60f), c.sc);
                baseline += c.text.lineHeight(ui::TextStyle::Title3, c.sc);
            }
            c.text.draw(c.r, c.text.truncate(t.detail, ui::TextStyle::Footnote, c.sc, textW),
                        x + pad, baseline, ui::TextStyle::Footnote,
                        ui::Color::white(0.60f), c.sc);
        }
    }
}

void LibraryScreen::drawGlass(Ctx& c) {
    // The switcher: Platforms and Collections as capsule pills. It is the only
    // heading this screen gets — the navigation bar already says "Library", and
    // a screen title that repeats the tab is chrome the reference
    // implementation deliberately removed.
    //
    // IT SCROLLS WITH THE TILES, which is what "a pushed page carries its title
    // as ordinary content at the top of its own SCROLL VIEW, never as system
    // chrome" means. Pinned, it sat over the grid as the grid moved under it,
    // and a tile's name ran behind a pill that was not part of the same surface.
    static const char* kLabels[2] = {"Platforms", "Collections"};
    const float y = design::kSwitcherTop - scroll_.value();
    if (y + pillHeight(c.text, c.sc) < 0.0f) return;
    float x = design::kLibraryInset;
    for (int i = 0; i < 2; ++i) {
        const float f = (row_ == 0 && slot_ == i) ? pillFocus_[i].value() : 0.0f;
        drawPill(c, kLabels[i], x, y, tab_ == i, f);
        x += pillWidth(c.text, kLabels[i], c.sc) + design::kPillGap;
    }
}

// ---------------------------------------------------------------------------
// A grid of games
// ---------------------------------------------------------------------------

void GridScreen::open(std::string title, std::vector<int> cards) {
    title_ = std::move(title);
    cards_ = std::move(cards);
    slot_ = 0;
    scroll_.retarget(0.0f, 0.0f);
    scroll_.elapsed = scroll_.duration;
}

int GridScreen::columns() const {
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const int n = static_cast<int>((usable + design::kGridColumnSpacing) /
                                   (design::kGridCoverMin + design::kGridColumnSpacing));
    return std::max(1, n);
}

float GridScreen::coverWidth() const {
    const float usable = ui::kCanvasWidth - design::kLibraryInset * 2.0f;
    const int n = columns();
    return (usable - design::kGridColumnSpacing * static_cast<float>(n - 1)) /
           static_cast<float>(n);
}

void GridScreen::tick(float dt, Ctx& c) {
    scroll_.tick(dt);
    if (!c.cards) return;
    for (int i : cards_) {
        if (i >= 0 && i < static_cast<int>(c.cards->size())) {
            (*c.cards)[i].focus.tick(dt);
            (*c.cards)[i].press.tick(dt);
        }
    }
}

Result GridScreen::key(Nav n) {
    const int count = static_cast<int>(cards_.size());
    if (n == Nav::Back) return {Action::Back, 0};
    if (count == 0) return {};
    if (n == Nav::Activate) return {Action::OpenGame, cards_[slot_]};

    const int cols = columns();
    int next = slot_;
    if (n == Nav::Left || n == Nav::Right) {
        const int d = (n == Nav::Right) ? 1 : -1;
        const int rowStart = (slot_ / cols) * cols;
        const int rowEnd = std::min(rowStart + cols - 1, count - 1);
        next = std::clamp(slot_ + d, rowStart, rowEnd);
    } else {
        const int d = (n == Nav::Down) ? 1 : -1;
        next = slot_ + d * cols;
        // The last row is ragged. Stepping down from a column that row does not
        // have should land on its last entry rather than refusing to move.
        if (d > 0 && next >= count && slot_ / cols < (count - 1) / cols)
            next = count - 1;
        if (next < 0 || next >= count) return {};
    }
    slot_ = next;
    return {};
}

void GridScreen::draw(Ctx& c) {
    if (!c.cards) return;
    auto& cards = *c.cards;
    const int cols = columns();
    const float cw = coverWidth();
    const float ch = cw * 4.0f / 3.0f;
    const float captionH = c.text.lineHeight(ui::TextStyle::Callout, c.sc) *
                           static_cast<float>(design::kGridCaptionLines);
    const float rowPitch = ch + design::kGridCaptionGap + captionH + design::kGridRowSpacing;

    // The screen's title is ordinary content at the top of its own scroll view,
    // never system chrome: on tvOS the system version painted over the artwork.
    const float chipH = c.text.lineHeight(ui::TextStyle::ScreenTitle, c.sc) +
                        design::kScreenChipPadY * 2.0f;
    const float gridTop = design::kSwitcherTop + chipH + design::kSwitcherGapBelow;

    const int count = static_cast<int>(cards_.size());
    const int rows = count == 0 ? 0 : (count + cols - 1) / cols;
    const float contentH = gridTop + static_cast<float>(rows) * rowPitch;
    const float maxScroll = std::max(0.0f, contentH - ui::kCanvasHeight);

    const int focusedRow = count == 0 ? 0 : slot_ / cols;
    const float rowTop = gridTop + static_cast<float>(focusedRow) * rowPitch;
    // HEADROOM FOR THE FOCUSED CARD, which grows by a tenth: without it the
    // lifted card is clipped against the top of the screen on the first row it
    // scrolls to. Half of (scale - 1) times the height, which is exactly how
    // far a scale about the centre advances an edge.
    const float lift = ch * (design::kFocusScale - 1.0f) * 0.5f + 8.0f;
    float want = scroll_.to;
    if (rowTop - want < gridTop) want = std::max(0.0f, rowTop - gridTop);
    const float rowBottom = rowTop + ch + design::kGridCaptionGap + captionH + lift;
    if (rowBottom - want > ui::kCanvasHeight - design::kSwitcherTop)
        want = rowBottom - (ui::kCanvasHeight - design::kSwitcherTop);
    want = std::clamp(want, 0.0f, maxScroll);
    if (std::fabs(want - scroll_.to) > 0.5f)
        scroll_.retarget(want, design::kFocusDuration);
    const float scroll = scroll_.value();

    for (int pass = 0; pass < 2; ++pass) {
        for (int i = 0; i < count; ++i) {
            const bool isFocused = (i == slot_);
            if ((pass == 0) == isFocused) continue;
            const int idx = cards_[i];
            if (idx < 0 || idx >= static_cast<int>(cards.size())) continue;

            const int col = i % cols;
            const int r = i / cols;
            const float bx = design::kLibraryInset +
                             static_cast<float>(col) * (cw + design::kGridColumnSpacing);
            const float by = gridTop + static_cast<float>(r) * rowPitch - scroll;
            if (by + ch + captionH < -rowPitch) continue;
            if (by > ui::kCanvasHeight + rowPitch) break;

            Card& card = cards[idx];
            card.focus.retarget(isFocused ? 1.0f : 0.0f, design::kFocusDuration);
            const float f = card.focus.value();
            const float p = card.press.value();
            // Pressed reads as a push INTO the screen against the focused lift,
            // so a click still registers on a card that is already raised.
            const float s = 1.0f + f * (design::kFocusScale - 1.0f) -
                            p * (design::kFocusScale - design::kPressScale);
            const float w = cw * s, h = ch * s;
            const float x = bx - (w - cw) * 0.5f;
            const float y = by - (h - ch) * 0.5f;

            drawCover(c, card, x, y, w, h, design::kGridCoverRadius * s, f, /*rim=*/true);

            // Two lines with the space reserved either way, and riding down
            // with the lift so the grown card cannot bury them.
            const std::vector<std::string> lines =
                wrapTwoLines(c.text, card.title, ui::TextStyle::Callout, c.sc, cw);
            float baseline = by + ch + design::kGridCaptionGap +
                             c.text.ascent(ui::TextStyle::Callout, c.sc) +
                             design::captionSlide(f, ch);
            for (const std::string& line : lines) {
                c.text.draw(c.r, line, bx, baseline, ui::TextStyle::Callout,
                            ui::Color::white(isFocused ? 1.0f : 0.60f), c.sc);
                baseline += c.text.lineHeight(ui::TextStyle::Callout, c.sc);
            }
        }
    }
}

void GridScreen::drawGlass(Ctx& c) {
    // A STATIC GLASS CAPSULE, NOT A BUTTON. It is the screen's title carried as
    // ordinary content at the top of its own scroll view; on tvOS the system
    // version of this painted straight over the artwork.
    //
    // 40 bold is the one hardcoded size in the whole reference UI, and it sits
    // between Title 2 and Large Title deliberately: a grid title should not
    // shout as loudly as a game's own name does.
    const float tw = c.text.measure(title_, ui::TextStyle::ScreenTitle, c.sc);
    const float w = tw + design::kScreenChipPadX * 2.0f;
    const float h = c.text.lineHeight(ui::TextStyle::ScreenTitle, c.sc) +
                    design::kScreenChipPadY * 2.0f;
    // Scrolls away with the covers, for the same reason the Library's switcher
    // does: it is content at the top of a scroll view, not chrome over it.
    const float y = design::kSwitcherTop - scroll_.value();
    if (y + h < 0.0f) return;
    c.r.drawGlass(ui::Rect{design::kLibraryInset, y, w, h, h * 0.5f,
                           ui::Color::white(0)},
                  design::kHeroPillBlur, ui::Color::white(0.18f));
    c.text.draw(c.r, title_, design::kLibraryInset + design::kScreenChipPadX,
                y + design::kScreenChipPadY +
                    c.text.ascent(ui::TextStyle::ScreenTitle, c.sc),
                ui::TextStyle::ScreenTitle, ui::Color::white(1.0f), c.sc);
}

// ---------------------------------------------------------------------------
// The launch screen
// ---------------------------------------------------------------------------

void DetailScreen::open(GameDetail d) {
    game_ = std::move(d);
    notice_.clear();
    slot_ = 0;
    rebuildRows();
    focus_.settle(1.0f);
    appear_.retarget(0.0f, 0.0f);
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);   // the launch transition's own duration
}

void DetailScreen::rebuildRows() {
    rows_.clear();
    if (game_.playable) {
        // The primary action first, and reachable without travelling through
        // the secondary ones.
        rows_.push_back({Action::Play, "Play", true});
        // DOWNLOAD IS THE DELIBERATE ONE. The cache is invisible — pressing
        // Play fetches the game if it is not here and says nothing about it —
        // so this row is not "is it cached", it is "put this game on the
        // machine and do not take it away again". That is what makes it a keep,
        // and what makes it the one action the console may refuse.
        if (game_.kept)
            rows_.push_back({Action::RemoveDownload, "Remove download", true});
        else
            rows_.push_back({Action::Download, "Download and keep", true});
    }
    // A different save state, a different core and an export belong here too.
    // They are not built yet and a row that does nothing is worse than no row.
}

void DetailScreen::tick(float dt) {
    focus_.tick(dt);
    appear_.tick(dt);
}

Result DetailScreen::key(Nav n) {
    if (n == Nav::Back) return {Action::Back, 0};
    if (rows_.empty()) return {};
    if (n == Nav::Up || n == Nav::Down) {
        const int d = (n == Nav::Down) ? 1 : -1;
        const int next = std::clamp(slot_ + d, 0, static_cast<int>(rows_.size()) - 1);
        if (next != slot_) {
            slot_ = next;
            notice_.clear();
            focus_.retarget(0.0f, 0.0f);
            focus_.elapsed = 0.0f;
            focus_.retarget(1.0f, design::kFocusDuration);
        }
        return {};
    }
    if (n == Nav::Activate && rows_[slot_].enabled)
        return {rows_[slot_].action, game_.romId};
    return {};
}

void DetailScreen::draw(Ctx& c) {
    const float a = appear_.value();

    // THE ARTWORK IS ITS OWN BACKDROP. A full-screen cover rather than a push,
    // with the game's cover filled and blurred behind everything and a scrim
    // over it — so the leftovers are the art's own colours rather than a flat
    // panel, and the text still has something to be read against.
    const ui::Image* art = nullptr;
    if (!game_.cover.empty()) {
        const ui::Image& img = c.images.get(game_.cover);
        if (img.ready) art = &img;
    }
    if (art) {
        // Filled means CROPPED, not stretched: mapping a 3:4 cover across a
        // 16:9 screen by UV 0..1 smears it into a band that is no longer the
        // art's colours at all, which is the whole point of the backdrop.
        const float boxAspect = ui::kCanvasWidth / ui::kCanvasHeight;
        const float imgAspect = art->aspect();
        float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
        if (imgAspect < boxAspect) {
            const float span = imgAspect / boxAspect;
            v0 = (1.0f - span) * 0.5f;
            v1 = v0 + span;
        } else {
            const float span = boxAspect / imgAspect;
            u0 = (1.0f - span) * 0.5f;
            u1 = u0 + span;
        }
        c.r.drawTextured(0, 0, ui::kCanvasWidth, ui::kCanvasHeight, art->texture,
                         u0, v0, u1, v1, ui::Color{1, 1, 1, art->fade * a}, false,
                         design::kBackdropBlur);
    }
    c.r.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                      ui::Color::black(design::kScrimOverlay * a)});

    // The cover itself, at the detail size, on the left.
    const float coverX = design::kLibraryInset;
    const float coverY = (ui::kCanvasHeight - design::kDetailCoverHeight) * 0.5f;
    if (c.cards && game_.cardIndex >= 0 &&
        game_.cardIndex < static_cast<int>(c.cards->size())) {
        drawCover(c, (*c.cards)[game_.cardIndex], coverX, coverY,
                  design::kDetailCoverWidth, design::kDetailCoverHeight,
                  design::kDetailRadius, 0.0f, /*rim=*/false);
    } else {
        c.r.draw(ui::Rect{coverX, coverY, design::kDetailCoverWidth,
                          design::kDetailCoverHeight, design::kDetailRadius, game_.art});
    }

    // The title, Large Title — the one place in the product that size is used
    // for a game rather than a settings page.
    const float textX = coverX + design::kDetailCoverWidth + 60.0f;
    const float textW = std::min(design::kRowColumnMaxWidth,
                                 ui::kCanvasWidth - textX - design::kLibraryInset);
    float y = coverY + c.text.ascent(ui::TextStyle::LargeTitle, c.sc);
    c.text.draw(c.r, c.text.truncate(game_.title, ui::TextStyle::LargeTitle, c.sc, textW),
                textX, y, ui::TextStyle::LargeTitle, ui::Color::white(a), c.sc);
    y += c.text.lineHeight(ui::TextStyle::LargeTitle, c.sc) * 0.55f +
         c.text.ascent(ui::TextStyle::Callout, c.sc);

    std::string meta = game_.platform;
    if (game_.sizeBytes > 0) {
        // A unit that suits the number. A library holds a 19 KB Game Boy ROM
        // and a 1.78 GB arcade set, and megabytes flatter neither: the first
        // reads as "0 MB", which looks like the server failed to say.
        char buf[64];
        const double b = static_cast<double>(game_.sizeBytes);
        if (b >= 1e9) std::snprintf(buf, sizeof buf, "  ·  %.1f GB", b / 1e9);
        else if (b >= 1e6) std::snprintf(buf, sizeof buf, "  ·  %.0f MB", b / 1e6);
        else std::snprintf(buf, sizeof buf, "  ·  %.0f KB", b / 1e3);
        meta += buf;
    }
    c.text.draw(c.r, meta, textX, y, ui::TextStyle::Callout,
                ui::Color::white(0.60f * a), c.sc);

    rowsX_ = textX;
    rowsW_ = textW;
    rowsY_ = y + c.text.lineHeight(ui::TextStyle::Callout, c.sc) * 1.6f;

    // A game this console cannot play gets the screen and the reason, and no
    // Play button. Saying it here is the whole point of knowing it.
    if (!game_.playable) {
        c.text.draw(c.r, c.text.truncate(game_.reason, ui::TextStyle::Title3, c.sc, textW),
                    textX, rowsY_ + c.text.ascent(ui::TextStyle::Title3, c.sc),
                    ui::TextStyle::Title3, ui::Color::white(0.60f * a), c.sc);
    }
}

void DetailScreen::drawGlass(Ctx& c) {
    const float a = appear_.value();
    // Treatment 3, the row one: a surface that is always there. Blur untinted
    // at rest, white 22% focused, scale 1.03 — a full-width row growing a
    // cover's tenth would collide with its neighbours.
    const float rowH = c.text.lineHeight(ui::TextStyle::Title3, c.sc) +
                       design::kRowPadY * 2.0f;
    float y = rowsY_;
    for (size_t i = 0; i < rows_.size(); ++i) {
        const bool on = (static_cast<int>(i) == slot_);
        const float f = on ? focus_.value() : 0.0f;
        const float s = 1.0f + f * (design::kRowFocusScale - 1.0f);
        const float w = rowsW_ * s, h = rowH * s;
        const float x = rowsX_ - (w - rowsW_) * 0.5f;
        const float ry = y - (h - rowH) * 0.5f;

        c.r.drawGlass(ui::Rect{x, ry, w, h, design::kRowRadius, ui::Color::white(0)},
                      design::kHeroBandBlur,
                      ui::Color::white((0.08f + 0.14f * f) * a));
        c.text.draw(c.r, rows_[i].label, x + design::kRowPadX,
                    ry + design::kRowPadY * s + c.text.ascent(ui::TextStyle::Title3, c.sc),
                    ui::TextStyle::Title3,
                    ui::Color::white((on ? 1.0f : 0.60f) * a), c.sc);
        y += rowH + design::kDetailRowGap;
    }

    // A refusal, and the number that makes it actionable. docs/PROJECT.md:
    // "the one failure the person ever sees is 'the disk is full of things you
    // asked me to keep'" — which is only useful with an amount beside it.
    if (!notice_.empty()) {
        // WRAPPED, NOT TRUNCATED. On one line this refusal came out as "...the
        // disk is full of things you asked me to keep. Remo..." — cutting off
        // the amount, which is the only part of it a person can act on. A
        // message that loses its own point is worse than no message.
        float baseline = y + c.text.ascent(ui::TextStyle::Callout, c.sc);
        for (const std::string& line :
             wrapTwoLines(c.text, notice_, ui::TextStyle::Callout, c.sc, rowsW_)) {
            c.text.draw(c.r, line, rowsX_, baseline, ui::TextStyle::Callout,
                        ui::Color::white(0.75f * a), c.sc);
            baseline += c.text.lineHeight(ui::TextStyle::Callout, c.sc);
        }
    }
}

}  // namespace screens
