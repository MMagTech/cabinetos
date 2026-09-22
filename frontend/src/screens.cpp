#include "screens.h"

#include "sound.h"

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
// The kept mark, top-right of a cover. See design::kKeptMark*.
//
// TOP-RIGHT AND NOT BOTTOM-RIGHT: box art puts its title at the top and its
// publisher logos and rating badges along the bottom, so the bottom corners are
// where a mark is most likely to land on something that matters. The top right
// is nearly always sky, or a border, or nothing.
void drawKeptMark(Ctx& c, const Card& card, float x, float y, float w, float radius) {
    if (!card.kept) return;
    const float d = design::kKeptMarkSize;
    const float mx = x + w - design::kKeptMarkInset - d;
    const float my = y + design::kKeptMarkInset;
    // The ring first and wider, so the dot reads against pale artwork too.
    const float r = design::kKeptMarkRing;
    c.r.draw(ui::Rect{mx - r, my - r, d + r * 2.0f, d + r * 2.0f,
                      (d + r * 2.0f) * 0.5f, ui::Color::black(0.55f)});
    c.r.draw(ui::Rect{mx, my, d, d, d * 0.5f, ui::palette::kScreenCyan});
}

void drawCover(Ctx& c, const Card& card, float x, float y, float w, float h,
               float radius, float f, bool rim, bool large = false) {
    ui::Rect panel{x, y, w, h, radius, card.art};
    applyArtworkFocus(panel, f, rim);
    c.r.draw(panel);
    // `large` asks for the 810x1080 original instead of the 162x216 thumbnail,
    // and only the launch screen does: it draws a 340x460 cover, which is 680
    // real pixels wide on a 4K panel. A shelf or a grid cover is small enough
    // that the thumbnail holds up and cheap enough that dozens stay resident.
    const std::string& key =
        (large && !card.coverLarge.empty()) ? card.coverLarge : card.cover;
    if (key.empty()) return;
    ui::drawImage(c.r, c.images.get(key), x, y, w, h, ui::Fit::Fill, 1.0f, radius);
    // Unfocused artwork sits back. See design::kRestArtDim.
    if (f < 1.0f)
        c.r.draw(ui::Rect{x, y, w, h, radius,
                          ui::Color::black(design::kRestArtDim * (1.0f - f))});
    drawKeptMark(c, card, x, y, w, radius);
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

// THE SWITCHER IS TEXT, AND THE CAPSULE ONLY APPEARS UNDER FOCUS — 2026-09-21.
//
// It used to be a filled capsule at rest, which made the Library's first row a
// pair of controls where Home's first row is a pair of headings. MMagTech:
// *"the library and collection text dont seem to match home now either."*
// Home's "Recent" and "Favorites" are Title 3 on the backdrop with nothing
// behind them; these are the same words doing the same job and they are Title 3
// on the backdrop now too.
//
// It also settles the margin. A capsule cannot be aligned two ways at once —
// box-aligned puts its label 14 points inside everything else on the screen,
// text-aligned hangs the capsule 14 points outside the tiles below it — and
// text with no box behind it simply starts where the margin is, the way the
// heading on Home does.
//
// SELECTION IS STILL NOT FOCUS AND BOTH ARE STILL VISIBLE AT ONCE, which is the
// rule this has to keep: the SELECTED tab is full white and the other is dimmed
// to 45%, so which tab you are looking at survives focus walking away from it;
// FOCUS is the capsule, which appears only under the thing the cursor is on.
// The two signals are now different in kind rather than different in strength,
// which reads better from a sofa than two tints of the same white did.
void drawPill(Ctx& c, const std::string& label, float x, float y, bool selected,
              float f) {
    const float w0 = pillWidth(c.text, label, c.sc);
    const float h0 = pillHeight(c.text, c.sc);
    const float s = 1.0f + f * (design::kPillFocusScale - 1.0f);
    const float w = w0 * s, h = h0 * s;
    // `x` is the TEXT's left edge; the capsule is drawn a padding-width outside
    // it, so it grows around the label rather than pushing it along.
    const float px = x - design::kPillPadX - (w - w0) * 0.5f;
    const float py = y - (h - h0) * 0.5f;

    if (f > 0.001f)
        c.r.drawGlass(ui::Rect{px, py, w, h, h * 0.5f, ui::Color::white(0)},
                      design::kThinMaterialBlur,
                      ui::Color::white(f * design::kFocusedTint));
    // The label does not move when the capsule grows: a focus scale that shoved
    // the words along would make the pair jitter as focus crossed them.
    c.text.draw(c.r, label, x,
                py + design::kPillPadY * s + c.text.ascent(ui::TextStyle::Title3, c.sc),
                ui::TextStyle::Title3,
                ui::Color::white(selected ? 1.0f : 0.45f), c.sc);
}

}  // namespace

// ---------------------------------------------------------------------------
// The Library
// ---------------------------------------------------------------------------

void LibraryScreen::build(std::vector<Tile> platforms, std::vector<Tile> collections) {
    platforms_ = std::move(platforms);
    collections_ = std::move(collections);
}

void LibraryScreen::learnedTile(int id, const std::string& detail,
                                const std::string& cover) {
    for (auto* v : {&platforms_, &collections_}) {
        for (Tile& t : *v) {
            if (t.id != id) continue;
            if (!detail.empty()) t.detail = detail;
            // A cover already on the tile is left alone: a collection carries
            // its own from the server and must not be overwritten by whichever
            // game happened to sort first inside it.
            if (t.cover.empty()) t.cover = cover;
            return;
        }
    }
}

void LibraryScreen::enter() {
    // ARRIVING IS ANIMATED EVERY TIME, and it is outside the guard below on
    // purpose: coming back from a platform's grid is an arrival too, and a
    // screen that faded in the first time and cut in afterwards would be worse
    // than one that always cut. 280ms ease-out is the design system's own
    // transition duration.
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);

    if (entered_) return;    // focus lands here ONLY the first time
    entered_ = true;
    row_ = 0;
    slot_ = 0;
    pillFocus_[0].settle(1.0f);
}

std::string LibraryScreen::focusedCover() const {
    const auto& tiles = visible();
    if (tiles.empty()) return {};
    const int i = (row_ == 0) ? 0 : slot_;
    if (i < 0 || i >= static_cast<int>(tiles.size())) return {};
    return tiles[i].cover;
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
    appear_.tick(dt);
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
        // THE SCREENS PLAY THEIR OWN MOVE CUE. The app cannot tell a focus move
        // from a press that did nothing — both come back as Action::None — so
        // the only place that knows a move happened is the place that made it.
        sound::play(sound::Cue::Move);
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
        // Up out of the switcher is the bar, which is drawn above this screen
        // and belongs to the app rather than to it.
        case Nav::Up:    if (row_ == 0) return {Action::FocusBar, 0};
                         moveFocus(0, -1);
                         break;
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
    // Everything this screen draws is multiplied by the arrival. The backdrop
    // underneath is NOT — it belongs to the app and stays put, which is what
    // the content is fading in against.
    c.r.setContentAlpha(appear_.value());
    // Everything below scrolls, so it is clipped to the window under the bar.
    // See design::kScrollClipTop.
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
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
                           design::kScreenBottomPad;
    const float maxScroll = std::max(0.0f, contentH - ui::kCanvasHeight);
    float want = 0.0f;
    if (row_ > 0) {
        const int r = slot_ / cols;
        const float rowTop = gridTop + static_cast<float>(r) * rowPitch;
        const float rowBottom = rowTop + design::kTileHeight;
        const float current = scroll_.to;
        if (rowBottom - current > ui::kCanvasHeight - design::kScreenBottomPad)
            want = rowBottom - (ui::kCanvasHeight - design::kScreenBottomPad);
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
    c.r.setContentAlpha(appear_.value());
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
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
    // OPTICALLY ALIGNED, not box-aligned: the capsule starts a padding-width
    // LEFT of the margin so that "Platforms" begins exactly where the bar's
    // "Library" and the tiles below do. See kContentInset.
    // The TEXT starts on the margin, the same as Home's shelf headings and the
    // bar above. drawPill puts its focus capsule outside that.
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

void GridScreen::open(std::string title, std::vector<int> cards,
                      const std::vector<design::Card>& all) {
    title_ = std::move(title);
    cards_ = std::move(cards);
    slot_ = 0;

    // The letter index, built once. The list arrives sorted by name — that is
    // the Library's doing, not this screen's — so the distinct initials come
    // out in order by walking it.
    //
    // ANYTHING THAT IS NOT A LETTER IS '#', which is one bucket rather than
    // several: a library holds "1080 Snowboarding", "3D Lemmings" and
    // "@Home", and three buckets of one game each at the top of the index
    // would be three targets nobody wants to land on separately.
    letters_.clear();
    letterFirst_.clear();
    for (size_t i = 0; i < cards_.size(); ++i) {
        const int idx = cards_[i];
        if (idx < 0 || idx >= static_cast<int>(all.size())) continue;
        const std::string& n = all[static_cast<size_t>(idx)].title;
        char c = n.empty() ? '#' : static_cast<char>(std::toupper(
            static_cast<unsigned char>(n[0])));
        if (c < 'A' || c > 'Z') c = '#';
        if (letters_.empty() || letters_.back() != c) {
            letters_.push_back(c);
            letterFirst_.push_back(static_cast<int>(i));
        }
    }
    index_.from = index_.to = 0.0f;
    index_.elapsed = index_.duration;
    appear_.from = appear_.to = 0.0f;
    appear_.elapsed = 0.0f;
    appear_.retarget(1.0f, 0.280f);
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

int GridScreen::letterOf(int slot) const {
    int at = -1;
    for (size_t i = 0; i < letterFirst_.size(); ++i)
        if (letterFirst_[i] <= slot) at = static_cast<int>(i);
    return at;
}

void GridScreen::jumpLetter(int dir) {
    if (letters_.empty()) { sound::play(sound::Cue::Edge); return; }
    const int at = letterOf(slot_);
    // GOING BACK FROM THE MIDDLE OF A LETTER GOES TO ITS OWN START FIRST, the
    // way a track-skip button does: pressing back inside the M's should reach
    // the first M, not jump past every one of them into the L's.
    int want = at;
    if (dir > 0) {
        want = at + 1;
    } else {
        if (at >= 0 && letterFirst_[static_cast<size_t>(at)] < slot_) want = at;
        else want = at - 1;
    }
    if (want < 0 || want >= static_cast<int>(letters_.size())) {
        sound::play(sound::Cue::Edge);
        // Still show the index: hitting the end of the alphabet is an answer,
        // and the index is what makes it a legible one.
        index_.retarget(1.0f, 0.150f);
        indexHold_ = 1.600f;
        return;
    }
    slot_ = letterFirst_[static_cast<size_t>(want)];
    sound::play(sound::Cue::Move);
    index_.retarget(1.0f, 0.150f);
    indexHold_ = 1.600f;
}

void GridScreen::tick(float dt, Ctx& c) {
    appear_.tick(dt);
    scroll_.tick(dt);
    index_.tick(dt);
    // It stays up while it is being used and goes when it is not.
    if (indexHold_ > 0.0f) {
        indexHold_ -= dt;
        if (indexHold_ <= 0.0f) index_.retarget(0.0f, 0.600f);
    }
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
    // Up out of the top row of covers is the bar, the same as it is on every
    // other screen the bar is drawn over.
    if (n == Nav::Up && slot_ < cols) return {Action::FocusBar, 0};
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
        if (next < 0 || next >= count) { sound::play(sound::Cue::Edge); return {}; }
    }
    if (next == slot_) sound::play(sound::Cue::Edge);
    else sound::play(sound::Cue::Move);
    slot_ = next;
    return {};
}

void GridScreen::draw(Ctx& c) {
    c.r.setContentAlpha(appear_.value());
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
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
    if (rowBottom - want > ui::kCanvasHeight - design::kScreenBottomPad)
        want = rowBottom - (ui::kCanvasHeight - design::kScreenBottomPad);
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

            // NO CAPTION HERE. The focused card's title is drawn once, beside
            // the screen's heading — see drawGlass and design::kGridCaptionLines
            // for what that bought and what it cost. Kept behind the constant
            // rather than deleted, so putting it back is one number.
            if (design::kGridCaptionLines > 0) {
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
}

void GridScreen::drawGlass(Ctx& c) {
    c.r.setContentAlpha(appear_.value());
    c.r.setScissor(0, design::kScrollClipTop, ui::kCanvasWidth,
                   ui::kCanvasHeight - design::kScrollClipTop);
    // THE TITLE IS TEXT, NOT A CHIP — changed 2026-09-21, and this is a
    // DEPARTURE FROM docs/PROJECT.md, which describes it as "a static glass
    // capsule, not a button". That description was right when it was written:
    // on tvOS this title painted straight over scrolling artwork and the glass
    // was what kept it readable.
    //
    // Two things changed underneath it. The screen now has a scrimmed backdrop
    // of its own, so a title on it is legible without a surface; and the
    // Library's switcher lost its capsule for consistency with Home's shelf
    // headings, which left this the only capsule in the product hanging off the
    // left margin to keep its own label aligned. A heading is a heading on all
    // three screens now. PROJECT.md's design system section needs this.
    //
    // 40 bold stays. It is the one hardcoded size in the whole reference UI and
    // it sits between Title 2 and Large Title deliberately: a grid title should
    // not shout as loudly as a game's own name does.
    const float h = c.text.lineHeight(ui::TextStyle::ScreenTitle, c.sc) +
                    design::kScreenChipPadY * 2.0f;
    // Scrolls away with the covers, for the same reason the Library's switcher
    // does: it is content at the top of a scroll view, not chrome over it.
    const float y = design::kSwitcherTop - scroll_.value();
    if (y + h < 0.0f) return;
    const float baseline = y + design::kScreenChipPadY +
                           c.text.ascent(ui::TextStyle::ScreenTitle, c.sc);
    c.text.draw(c.r, title_, design::kLibraryInset, baseline,
                ui::TextStyle::ScreenTitle, ui::Color::white(1.0f), c.sc);

    // THE FOCUSED GAME'S NAME, BESIDE THE HEADING, which is where Home puts the
    // focused card's title and for the same reason: it costs no vertical space
    // and it is the only one of forty names anybody is reading. With it, the
    // count — the Library's tile knew how many games a platform holds and this
    // screen used to forget it on the way in.
    if (!c.cards || cards_.empty()) return;
    float x = design::kLibraryInset +
              c.text.measure(title_, ui::TextStyle::ScreenTitle, c.sc) + 28.0f;
    char count[48];
    std::snprintf(count, sizeof count, "%zu game%s", cards_.size(),
                  cards_.size() == 1 ? "" : "s");
    c.text.draw(c.r, count, x, baseline, ui::TextStyle::Callout,
                ui::Color::white(0.45f), c.sc);
    x += c.text.measure(count, ui::TextStyle::Callout, c.sc) + 24.0f;

    const int idx = focusedCard();
    if (idx >= 0 && idx < static_cast<int>(c.cards->size())) {
        const std::string& name = (*c.cards)[idx].title;
        const float room = ui::kCanvasWidth - design::kLibraryInset - x -
                           design::kLetterIndexWidth;
        c.text.draw(c.r, c.text.truncate(name, ui::TextStyle::Callout, c.sc, room), x,
                    baseline, ui::TextStyle::Callout, ui::Color::white(0.95f), c.sc);
    }

    // --- The letter index, down the right ---------------------------------
    //
    // OUTSIDE THE SCROLL WINDOW, because it is not part of the list: it is a
    // readout of where you are in it. It is also the one thing on this screen
    // that may sit beside the bar rather than under it.
    const float a = index_.value();
    if (a <= 0.01f || letters_.empty()) return;
    c.r.clearScissor();
    const int here = letterOf(slot_);
    const float lineH = c.text.lineHeight(ui::TextStyle::Callout, c.sc);
    const float colH = lineH * static_cast<float>(letters_.size());
    const float top = (ui::kCanvasHeight - colH) * 0.5f;
    const float cx = ui::kCanvasWidth - design::kLibraryInset -
                     design::kLetterIndexWidth * 0.5f;
    // A soft plate behind it, so letters stay legible over pale artwork.
    c.r.draw(ui::Rect{cx - design::kLetterIndexWidth * 0.5f, top - 20.0f,
                      design::kLetterIndexWidth, colH + 40.0f,
                      design::kLetterIndexWidth * 0.5f,
                      ui::Color::black(0.45f * a)});
    for (size_t i = 0; i < letters_.size(); ++i) {
        const bool on = (static_cast<int>(i) == here);
        const std::string ch(1, letters_[i]);
        const float lw = c.text.measure(ch, ui::TextStyle::Callout, c.sc);
        c.text.draw(c.r, ch, cx - lw * 0.5f,
                    top + lineH * static_cast<float>(i) +
                        c.text.ascent(ui::TextStyle::Callout, c.sc),
                    ui::TextStyle::Callout,
                    ui::Color::white((on ? 1.0f : 0.40f) * a), c.sc);
    }
}

// ---------------------------------------------------------------------------
// Search
// ---------------------------------------------------------------------------

void SearchScreen::open() {
    query_.clear();
    results_.clear();
    slot_ = 0;
    focused_ = false;
    scroll_.from = scroll_.to = 0.0f;
    scroll_.elapsed = scroll_.duration;
}

void SearchScreen::setQuery(const std::string& q) {
    if (q == query_) return;
    query_ = q;
    slot_ = 0;
    scroll_.retarget(0.0f, design::kFocusDuration);
    if (query_.empty()) {
        // Back to nothing typed, which is a different screen from "nothing
        // matched" and has to drop the previous answer with it — results left
        // standing under an empty box are the last query's, silently.
        results_.clear();
        resultsFor_.clear();
        total_ = 0;
        state_ = State::Empty;
        failure_.clear();
    }
}

void SearchScreen::setWaiting() {
    if (query_.empty()) return;
    state_ = State::Waiting;
    failure_.clear();
}

void SearchScreen::setFailed(const std::string& why) {
    state_ = State::Failed;
    failure_ = why;
    results_.clear();
    resultsFor_.clear();
    total_ = 0;
    slot_ = 0;
}

void SearchScreen::setResults(const std::string& forQuery, std::vector<int> results,
                              int total) {
    // AN ANSWER TO A QUESTION NOBODY IS ASKING ANY MORE IS DROPPED. Requests
    // can land out of order, and showing the results for "mar" under a box
    // that now reads "mario" is the failure this guard exists for.
    if (forQuery != query_) return;
    results_ = std::move(results);
    resultsFor_ = forQuery;
    total_ = total;
    failure_.clear();
    state_ = results_.empty() ? State::NoMatches : State::Results;
    slot_ = 0;
    scroll_.retarget(0.0f, design::kFocusDuration);
}

void SearchScreen::tick(float dt, Ctx& c) {
    scroll_.tick(dt);
    if (!c.cards) return;
    for (int i : results_) {
        if (i >= 0 && i < static_cast<int>(c.cards->size())) {
            (*c.cards)[i].focus.tick(dt);
            (*c.cards)[i].press.tick(dt);
        }
    }
}

Result SearchScreen::key(Nav n) {
    const int count = static_cast<int>(results_.size());
    if (n == Nav::Back) return {Action::Back, 0};
    if (count == 0) return {};
    if (n == Nav::Activate) return {Action::OpenGame, results_[slot_]};
    // Up out of the results is the bar, the same as every other screen.
    if (n == Nav::Up) return {Action::FocusBar, 0};
    // Down goes back to the keyboard, which is the app's to hand focus to.
    if (n == Nav::Down) return {Action::FocusKeyboard, 0};
    const int d = (n == Nav::Right) ? 1 : -1;
    const int next = std::clamp(slot_ + d, 0, count - 1);
    if (next == slot_) { sound::play(sound::Cue::Edge); return {}; }
    slot_ = next;
    sound::play(sound::Cue::Move);
    return {};
}

void SearchScreen::draw(Ctx& c) {
    if (!c.cards) return;
    const float top = design::kContentTop;
    const float headingH = c.text.lineHeight(ui::TextStyle::Title3, c.sc) + 12.0f;

    // The covers are sized to the room the keyboard leaves, not to a constant.
    // The panel's height depends on its layout and whether it is docked, so a
    // hardcoded cover size would be correct until somebody added a row of keys.
    const float room = resultsBottom_ - (top + headingH) - design::kShelfBreathing * 2.0f;
    const float ch = std::clamp(room, 120.0f, 320.0f);
    const float cw = ch * 0.75f;
    const float coversTop = top + headingH + design::kShelfBreathing;

    // The heading says what happened, because an empty screen that says nothing
    // is indistinguishable from one that is broken.
    // FIVE STATES AND FIVE SENTENCES. "Nothing matches" used to cover all of
    // them, which was honest while the answer was a local substring match and
    // became a lie the moment the answer came from a server: a console that
    // could not ASK would have said the library held nothing like it.
    char line[96];
    switch (state_) {
        case State::Empty:
            std::snprintf(line, sizeof line, "Search");
            break;
        case State::Waiting:
            std::snprintf(line, sizeof line, "Searching\xE2\x80\xA6");
            break;
        case State::NoMatches:
            std::snprintf(line, sizeof line, "Nothing matches");
            break;
        case State::Failed:
            // Names the server, not the library. The person typed a word and
            // the console could not go and look.
            std::snprintf(line, sizeof line, "Could not reach your server");
            break;
        case State::Results:
            // `total` is the server's count of what matched, which is larger
            // than what came back whenever the first page was not the whole of
            // it. Saying "20 games" for a search that found four hundred is
            // the kind of quiet wrongness this project keeps paying for.
            if (total_ > static_cast<int>(results_.size()))
                std::snprintf(line, sizeof line, "%zu of %d games",
                              results_.size(), total_);
            else
                std::snprintf(line, sizeof line, "%zu game%s", results_.size(),
                              results_.size() == 1 ? "" : "s");
            break;
    }
    c.text.draw(c.r, line, design::kContentInset,
                top + c.text.ascent(ui::TextStyle::Title3, c.sc),
                ui::TextStyle::Title3, ui::Color::white(1.0f), c.sc);

    // The focused game's name beside it, the way Home's shelf header and the
    // grid's heading both do it.
    const int fi = focusedCard();
    if (focused_ && fi >= 0 && fi < static_cast<int>(c.cards->size())) {
        const float x = design::kContentInset +
                        c.text.measure(line, ui::TextStyle::Title3, c.sc) + 24.0f;
        const float roomW = ui::kCanvasWidth - design::kContentInset - x;
        c.text.draw(c.r, c.text.truncate((*c.cards)[fi].title, ui::TextStyle::Callout,
                                         c.sc, roomW),
                    x, top + c.text.ascent(ui::TextStyle::Title3, c.sc),
                    ui::TextStyle::Callout, ui::Color::white(0.60f), c.sc);
    }

    if (results_.empty()) return;

    // One row, scrolling sideways under the focus. A grid would need the height
    // the keyboard is standing on.
    const float pitch = cw + design::kShelfSpacing;
    const float usable = ui::kCanvasWidth - design::kContentInset * 2.0f;
    const float focusX = static_cast<float>(slot_) * pitch;
    float want = scroll_.to;
    if (focusX - want < 0.0f) want = focusX;
    if (focusX + cw - want > usable) want = focusX + cw - usable;
    want = std::max(0.0f, want);
    if (std::fabs(want - scroll_.to) > 0.5f)
        scroll_.retarget(want, design::kFocusDuration);
    const float scroll = scroll_.value();

    for (int pass = 0; pass < 2; ++pass) {
        for (size_t i = 0; i < results_.size(); ++i) {
            const bool isFocused = focused_ && static_cast<int>(i) == slot_;
            if ((pass == 0) == isFocused) continue;
            const int idx = results_[i];
            if (idx < 0 || idx >= static_cast<int>(c.cards->size())) continue;
            const float bx = design::kContentInset +
                             static_cast<float>(i) * pitch - scroll;
            if (bx + cw < -pitch) continue;
            if (bx > ui::kCanvasWidth + pitch) break;

            design::Card& card = (*c.cards)[static_cast<size_t>(idx)];
            card.focus.retarget(isFocused ? 1.0f : 0.0f, design::kFocusDuration);
            const float f = card.focus.value();
            const float s = 1.0f + f * (design::kFocusScale - 1.0f);
            const float w = cw * s, h = ch * s;
            drawCover(c, card, bx - (w - cw) * 0.5f, coversTop - (h - ch) * 0.5f, w, h,
                      design::kGridCoverRadius * s, f, /*rim=*/true);
        }
    }
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
    // THE BACKDROP IS THE THUMBNAIL, AT THE ORIGINAL BLUR, and that is a
    // decision rather than an oversight — 2026-09-21.
    //
    // It was changed to the 810x1080 original, which made it a legible picture
    // of the game rather than a wash of its colours. Two attempts at it, sharp
    // and softened, and MMagTech on both: *"no that is not what was there for
    // the launch screen backdrop before you started."*
    //
    // So it keeps the small cover and `kBackdropBlur`'s fixed mip bias. Five
    // texels across a 162-wide source is exactly the muddy average the rest of
    // this session has been removing elsewhere — and here it is what the screen
    // wants, because everything in FRONT of it is the same artwork sharp: the
    // cover, at the original resolution, and the title. The backdrop's job here
    // is to be a colour, not a second copy of the picture.
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
                  design::kDetailRadius, /*f=*/1.0f, /*rim=*/false, /*large=*/true);
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
                      design::kRegularMaterialBlur,
                      ui::Color::white((0.08f + 0.14f * f) * a));

        // THE ROW IS THE PROGRESS BAR. Not a bar drawn inside the row — the
        // row's own surface fills from the left, so the thing you pressed is
        // the thing that is loading rather than a widget that appeared on it.
        const bool busy = progress_.active && progress_.action == rows_[i].action;
        std::string label = rows_[i].label;
        if (busy) {
            if (progress_.total > 0) {
                const float frac = std::clamp(
                    static_cast<float>(progress_.got) /
                        static_cast<float>(progress_.total), 0.0f, 1.0f);
                // Clipped to the row's own rounded rectangle, so the fill has
                // the row's corners rather than square ones poking out of them.
                ui::Rect fill{x, ry, w * frac, h, design::kRowRadius,
                              ui::Color::white(0.22f * a)};
                c.r.draw(fill);
            }
            char buf[96];
            const double g = static_cast<double>(progress_.got);
            const double t = static_cast<double>(progress_.total);
            if (progress_.unpacking)
                std::snprintf(buf, sizeof buf, "Unpacking\xE2\x80\xA6");
            else if (progress_.total > 0)
                std::snprintf(buf, sizeof buf, "%.0f of %.0f MB", g / 1e6, t / 1e6);
            else
                std::snprintf(buf, sizeof buf, "%.0f MB", g / 1e6);
            label = buf;
        }

        c.text.draw(c.r, label, x + design::kRowPadX,
                    ry + design::kRowPadY * s + c.text.ascent(ui::TextStyle::Title3, c.sc),
                    ui::TextStyle::Title3,
                    ui::Color::white((on || busy ? 1.0f : 0.60f) * a), c.sc);
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


// --- Accounts ---------------------------------------------------------------

void AccountScreen::setRows(std::vector<AccountRow> rows) {
    rows_ = std::move(rows);
    slot_ = firstSelectable();
}

void AccountScreen::setAnchor(float rightX, float topY) {
    anchorRight_ = rightX;
    anchorTop_ = topY;
}

int AccountScreen::firstSelectable() const {
    // The first other account, or the Add row when there is nobody else —
    // which is every console with one account, so it is the common case and
    // the reason this panel is never a page of things you cannot press.
    return rows_.empty() ? 0 : 0;
}

void AccountScreen::open() {
    slot_ = firstSelectable();
    notice_.clear();
    appear_.retarget(0.0f, 0.0f);
    appear_.retarget(1.0f, 0.24f);
    focus_.retarget(1.0f, design::kFocusDuration);
}

void AccountScreen::setNotice(std::string s) { notice_ = std::move(s); }

void AccountScreen::tick(float dt) {
    focus_.tick(dt);
    appear_.tick(dt);
}

Result AccountScreen::key(Nav n) {
    auto step = [&](int d) -> Result {
        int at = slot_;
        for (int tries = 0; tries < rowCount(); ++tries) {
            at += d;
            if (at < 0 || at >= rowCount()) return {};
            {
                if (at != slot_) {
                    slot_ = at;
                    focus_.retarget(0.0f, 0.0f);
                    focus_.retarget(1.0f, design::kFocusDuration);
                }
                return {};
            }
        }
        return {};
    };

    switch (n) {
        case Nav::Up:   return step(-1);
        case Nav::Down: return step(1);
        case Nav::Back: return {Action::Back, 0};
        case Nav::Activate:
            if (slot_ < 0 || slot_ >= rowCount()) return {};
            if (isAddRow(slot_)) return {Action::AddAccount, 0};
            // THE ID, NOT THE ROW. A list that changed underneath this screen
            // must not be able to switch the console to the wrong person.
            return {Action::SwitchAccount, rows_[slot_].id};
        default: return {};
    }
}

void AccountScreen::draw(Ctx& c) {
    const float a = appear_.value();
    // **SIZED AGAINST THE CHIP, because it hangs off it.** MMagTech, 2026-09-22:
    // *"that box for adding users and switching is way too big especially
    // compared to the logged user."* It was — the rows were Title3 at 38pt
    // with 60pt discs, opening from a chip that is Caption1 at 25pt with a
    // 26pt disc. The thing you pressed was less than half the scale of the
    // thing it produced, which is what made it read as a box landing on the
    // screen rather than as the chip opening.
    //
    // A row is a list item and the chip is an indicator, so a row is allowed
    // to be a little larger — but a little. Callout against the chip's
    // Caption1 is one step on the ramp, and a 36pt disc against its 26.
    const ui::TextStyle rowStyle = ui::TextStyle::Callout;
    const float rowH = c.text.lineHeight(rowStyle, c.sc) + 26.0f;
    const float discD = rowH - 22.0f;
    const float w = 400.0f;
    // HUNG FROM THE CHIP, not centred. The panel's right edge lines up with the
    // chip's, so it reads as the chip opening rather than as a screen arriving.
    const float x = anchorRight_ - w;
    const float top = anchorTop_;

    // The panel grows downward as it appears, which is what makes it read as an
    // expansion. Everything inside is clipped to it by being drawn after.
    const int rows = rowCount();
    const float bodyH = rows * rowH + (rows - 1) * 8.0f + 18.0f * 2.0f +
                        (notice_.empty() ? 0.0f : 46.0f);
    c.r.drawGlass(ui::Rect{x, top, w, bodyH * a, design::kRowRadius,
                           ui::Color::white(0)},
                  design::kRegularMaterialBlur, ui::Color::white(0.10f * a));

    float y = top + 18.0f;

    for (int i = 0; i < rows; ++i) {
        const bool on = (i == slot_);
        const float f = on ? focus_.value() : 0.0f;
        const float rw = w - 16.0f;
        const float rx = x + 8.0f;
        c.r.draw(ui::Rect{rx, y, rw, rowH, design::kRowRadius,
                          ui::Color::white((0.04f + 0.16f * f) * a)});

        if (isAddRow(i)) {
            // A PLUS ON A DISC, so it sits in the same column as the faces and
            // reads as one more entry in the same list rather than as a button
            // bolted underneath it.
            const float dx = rx + 12.0f, dy = y + (rowH - discD) * 0.5f;
            c.r.draw(ui::Rect{dx, dy, discD, discD, discD * 0.5f,
                              ui::Color::white(0.14f * a)});
            const float pw = c.text.measure("+", rowStyle, c.sc);
            c.text.draw(c.r, "+", dx + (discD - pw) * 0.5f,
                        dy + (discD - c.text.lineHeight(rowStyle, c.sc)) * 0.5f +
                            c.text.ascent(rowStyle, c.sc),
                        rowStyle, ui::Color::white(0.8f * a), c.sc);
            c.text.draw(c.r, "Add user", dx + discD + 14.0f,
                        y + (rowH - c.text.lineHeight(rowStyle, c.sc)) * 0.5f +
                            c.text.ascent(rowStyle, c.sc),
                        rowStyle, ui::Color::white(0.92f * a), c.sc);
            y += rowH + 8.0f;
            continue;
        }

        const AccountRow& row = rows_[i];
        // The disc is drawn either way: the ground under a picture with
        // transparency, and the fallback when there is none. Same rule as the
        // chip in the bar, and the same reason.
        const float dx = rx + 16.0f, dy = y + (rowH - discD) * 0.5f;
        c.r.draw(ui::Rect{dx, dy, discD, discD, discD * 0.5f, ui::Color::white(0.22f * a)});
        const ui::Image* face = nullptr;
        if (!row.avatar.empty()) {
            const ui::Image& img = c.images.get(row.avatar);
            if (img.ready) face = &img;
        }
        if (face) {
            ui::drawImage(c.r, *face, dx, dy, discD, discD, ui::Fit::Fill,
                          face->fade * a, discD * 0.5f);
        } else if (!row.name.empty()) {
            const std::string initial(1, static_cast<char>(std::toupper(
                static_cast<unsigned char>(row.name[0]))));
            const float iw = c.text.measure(initial, rowStyle, c.sc);
            c.text.draw(c.r, initial, dx + (discD - iw) * 0.5f,
                        dy + (discD - c.text.lineHeight(rowStyle, c.sc)) * 0.5f +
                            c.text.ascent(rowStyle, c.sc),
                        rowStyle, ui::Color::white(0.85f * a), c.sc);
        }

        c.text.draw(c.r, row.name, dx + discD + 14.0f,
                    y + (rowH - c.text.lineHeight(rowStyle, c.sc)) * 0.5f +
                        c.text.ascent(rowStyle, c.sc),
                    rowStyle, ui::Color::white(0.92f * a), c.sc);
        y += rowH + 8.0f;
    }

    if (!notice_.empty())
        c.text.draw(c.r, notice_, x + 18.0f,
                    y + 8.0f + c.text.ascent(ui::TextStyle::Callout, c.sc),
                    ui::TextStyle::Callout, ui::Color::white(0.85f * a), c.sc);
}

// --- Adding an account ------------------------------------------------------

void AddAccountScreen::open() {
    url_.clear();
    code_.clear();
    error_.clear();
    busy_ = true;
    appear_.retarget(0.0f, 0.0f);
    appear_.retarget(1.0f, 0.3f);
}

void AddAccountScreen::setPairing(const std::string& url, const std::string& code) {
    url_ = url;
    code_ = code;
    busy_ = false;
    std::string err;
    // THE QR IS THE SERVER'S URL AND NOT A SHAPE WE GUESSED. `romm.h` records
    // what happens otherwise: a fabricated one scans perfectly and lands on a
    // page saying the code does not exist.
    const qr::Code c = qr::encode(url, &err);
    if (c.valid()) qr_.set(c);
    else error_ = err;   // the address and the code below are still usable
}

void AddAccountScreen::setBusy(bool on) { busy_ = on; }
void AddAccountScreen::setError(const std::string& err) { error_ = err; busy_ = false; }

void AddAccountScreen::tick(float dt) { appear_.tick(dt); }

Result AddAccountScreen::key(Nav n) {
    // ONE WAY OUT AND IT IS BACK. Nothing here is a choice — it is a code
    // somebody is copying onto a phone — so a focus ring would have nowhere to
    // go and would only invite a press that does nothing.
    if (n == Nav::Back) return {Action::Back, 0};
    return {};
}

void AddAccountScreen::draw(Ctx& c) {
    const float a = appear_.value();

    // THE SAME SHAPE AS FIRST RUN'S PAIRING STEP, and the first version of this
    // screen was not. MMagTech, 2026-09-22: *"the add user screen seemed way
    // too jarring visually in presentation and text."* It was bare text on the
    // gradient — the only screen in the console with no material under it —
    // with a 76pt title and a 76pt code competing, and a raw URL at 38pt bold
    // dominating the middle of it.
    //
    // setup.cpp already solved this screen: prose on the left, the thing you
    // act on in a panel on the right, one line along the bottom. Its own
    // comment says why the shape is constant — *"so the flow does not appear
    // to jump between five unrelated screens"* — and adding somebody is the
    // same job as pairing the first somebody. The numbers below are its
    // numbers, deliberately.
    constexpr float kInset = 80.0f;
    constexpr float kTitleTop = 150.0f;
    constexpr float kProseWidth = 760.0f;
    constexpr float kPanelX = 1020.0f;
    constexpr float kPanelY = 168.0f;
    constexpr float kPanelW = ui::kCanvasWidth - kPanelX - kInset;
    constexpr float kPanelH = 660.0f;
    constexpr float kFooterY = 900.0f;

    // --- the left column: what is happening and why --------------------------
    float y = kTitleTop;
    c.text.draw(c.r, "Add a user", kInset,
                y + c.text.ascent(ui::TextStyle::LargeTitle, c.sc),
                ui::TextStyle::LargeTitle, ui::Color::white(0.96f * a), c.sc);
    y += c.text.lineHeight(ui::TextStyle::LargeTitle, c.sc) + 28.0f;

    // PROSE IS `Body`, WHICH IS WHAT setup.cpp USES AND WHAT THIS SCREEN DID
    // NOT. It was Title3 — 38pt against the 76pt title — and two near-headline
    // sizes stacked is most of why it read as shouting.
    //
    // Written as short lines rather than wrapped, because `wrap` lives in
    // setup.cpp behind `hardWrap` and this does not need either: the strings
    // are fixed, they are mine, and they are well inside a 760pt column at
    // 29pt. **If a line here ever grows, measure it** — that is the rule a
    // truncated tile caption already bought once.
    const char* lines[3] = {nullptr, nullptr, nullptr};
    if (busy_) {
        lines[0] = "Asking the server for a code.";
    } else {
        lines[0] = "Scan the code with a phone.";
        lines[1] = "Sign in as the person you are adding,";
        lines[2] = "not as yourself.";
    }
    for (const char* line : lines) {
        if (!line) continue;
        c.text.draw(c.r, line, kInset, y + c.text.ascent(ui::TextStyle::Body, c.sc),
                    ui::TextStyle::Body, ui::Color::white(0.72f * a), c.sc);
        y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
    }

    // THE ADDRESS AND THE CODE GO IN THE PROSE COLUMN, and putting them in the
    // panel was the second mistake on this screen. setup.cpp says why in its
    // own words: *"the pairing step puts the address and the code in the prose
    // column, because they are the things somebody reads out or types — the QR
    // is only a shortcut past typing them."* In the panel they were also white
    // text over a light glass card, which is the contrast the QR's own white
    // background creates, and the address was touching the panel's bottom edge.
    if (!code_.empty()) {
        y += 30.0f;
        if (!url_.empty()) {
            // Without the code repeated on the end of it: it is printed below,
            // at four times the size, and once is enough.
            std::string shown = url_;
            if (const size_t q = shown.find("?user_code="); q != std::string::npos)
                shown = shown.substr(0, q);
            if (shown.rfind("http://", 0) == 0) shown = shown.substr(7);
            c.text.draw(c.r, shown, kInset,
                        y + c.text.ascent(ui::TextStyle::Body, c.sc),
                        ui::TextStyle::Body, ui::palette::kScreenCyan, c.sc);
            y += c.text.lineHeight(ui::TextStyle::Body, c.sc);
        }
        y += 20.0f;
        // THE BIGGEST THING AFTER THE TITLE, for setup.cpp's reason: it is what
        // somebody reads off the screen and checks against their phone, and
        // RomM shows the same characters on the page they are approving.
        c.text.draw(c.r, "Code " + code_, kInset,
                    y + c.text.ascent(ui::TextStyle::Title1, c.sc),
                    ui::TextStyle::Title1, ui::Color::white(0.97f * a), c.sc);
        y += c.text.lineHeight(ui::TextStyle::Title1, c.sc);
    }

    // AN OUTCOME BELONGS IN THE PROSE COLUMN, not over the code. This is where
    // "nobody was added" lands, and it is the whole reason that case stays on
    // this screen instead of returning to a panel that looks unchanged.
    if (!error_.empty()) {
        y += 30.0f;
        std::string rest = error_;
        for (int guard = 0; guard < 6 && !rest.empty(); ++guard) {
            size_t cut = rest.size();
            while (cut > 0 &&
                   c.text.measure(rest.substr(0, cut), ui::TextStyle::Callout, c.sc) > kProseWidth) {
                const size_t sp = rest.rfind(' ', cut - 1);
                if (sp == std::string::npos) break;
                cut = sp;
            }
            c.text.draw(c.r, rest.substr(0, cut), kInset,
                        y + c.text.ascent(ui::TextStyle::Callout, c.sc),
                        ui::TextStyle::Callout, ui::Color::white(0.92f * a), c.sc);
            y += c.text.lineHeight(ui::TextStyle::Callout, c.sc);
            rest = (cut >= rest.size()) ? std::string() : rest.substr(cut + 1);
        }
    }

    // --- the right column: the thing you act on ------------------------------
    //
    // **NO GLASS PANEL BEHIND THE CODE, AND THERE WAS ONE.** MMagTech, looking
    // at it: *"why the giant white box around the qrcode."* Because there were
    // two boxes — a white card inside a light glass panel — and only one of
    // them earns its place.
    //
    // THE WHITE CARD IS NOT DECORATION. It is the quiet zone: four modules of
    // real white around the symbol, which `qr.h` records as measured rather
    // than assumed — the same code drawn flush to its edge does not decode at
    // all, and with the margin it decodes every time. It cannot go.
    //
    // The panel could, and did. It was copied from setup.cpp's shape, where
    // the same panel also holds rows of networks and controllers at the other
    // steps; here it only ever holds the QR, so it was a box around a card
    // holding nothing else. The card is the material on this side now.

    if (code_.empty()) return;

    // Everything that is words lives in the column on the left; this side is
    // the shortcut past typing them. Bigger than it was, because it no longer
    // has to leave room around itself inside something else — and a code that
    // is photographed from a sofa cannot be too large.
    const float side = 560.0f;
    const float qx = kPanelX + (kPanelW - side) * 0.5f;
    const float qy = kPanelY + (kPanelH - side) * 0.5f;
    if (qr_.valid()) qr_.draw(c.r, qx, qy, side);

    // --- NOTHING ALONG THE BOTTOM, and both lines that were here are gone ---
    //
    // "They are added to this console. Switching to them is separate." was
    // dropped at MMagTech's request: it answers a question nobody has asked
    // yet, at the moment they are trying to scan a code, and the panel names
    // who was added the instant it happens.
    //
    // **"B to go back" WAS WORSE — IT WAS WRONG.** MMagTech: *"b doesnt let me
    // go back like it suggests."* It does not, because SDL maps face buttons
    // by POSITION: `SDL_GAMEPAD_BUTTON_EAST` is Back everywhere in this
    // product, and on a Switch Pro Controller the east button is physically
    // **A**. South is Activate, and south is B. The screen was naming the one
    // button that does the opposite of what it claimed.
    //
    // **SO NOTHING HERE NAMES A PHYSICAL BUTTON.** No other screen in this
    // product does, and this is why: the letter depends on the pad, and a
    // console that will meet Switch, Xbox and PlayStation controllers cannot
    // put one in a string. If a hint is ever wanted here it has to come from
    // the pad SDL actually reports, not from a constant.
}

}  // namespace screens
