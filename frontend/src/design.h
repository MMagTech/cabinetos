// The design system, as numbers.
//
// Every value here is quoted from docs/PROJECT.md, "The design system". If one
// of them changes there, it changes here, and nowhere else. It lives in a
// header rather than in main.cpp because the screens are separate files now and
// two copies of a focus scale is how a shelf and a grid end up disagreeing
// about what focus looks like.
//
// The numbers that are NOT arbitrary, and that a tidy-up will otherwise undo:
//
//   THE FOCUS SCALE SHRINKS AS THE ELEMENT GROWS — 1.10 for a cover, 1.06 for a
//   pill, 1.03 for a full-width row. A row growing a tenth collides with its
//   neighbours; a pill growing a thirtieth does not read at all.
//
//   RESERVED HEADROOM IS A LAYOUT OBLIGATION. A shelf carries 24pt of vertical
//   padding for no reason except that its cards grow when focused. Every
//   container holding focusable elements has to budget for their focused size.
//
//   180 ms IS THE FOCUS TEMPO and nothing about focus is slower. A controller
//   crosses a shelf faster than that and the animations must not queue up
//   behind the person driving them.

#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "ui.h"

namespace design {

// --- The canvas -------------------------------------------------------------

// Home insets by 60, everything pushed insets by 80. Both are the reference
// implementation's own `contentInset` and the difference is deliberate: Home is
// a wall of artwork and the pushed screens are content to be read.
constexpr float kContentInset = 60.0f;
constexpr float kLibraryInset = 80.0f;

// --- Focus ------------------------------------------------------------------

constexpr float kFocusScale = 1.10f;       // artwork
constexpr float kPillFocusScale = 1.06f;   // a text control
constexpr float kRowFocusScale = 1.03f;    // a full-width row
constexpr float kPressScale = 1.02f;
constexpr float kFocusRimWidth = 4.0f;
constexpr float kFocusShadowBlur = 26.0f;
constexpr float kFocusShadowOffsetY = 14.0f;
constexpr float kFocusDuration = 0.180f;
constexpr float kPressDuration = 0.120f;

// SELECTION IS NOT FOCUS, and both are visible at once. A selected switcher
// pill is tinted white 35%; a focused one is tinted white 25% and scaled. A
// controller UI where focus can move away from the current selection has to
// show both or the person loses their place.
constexpr float kSelectedTint = 0.35f;
constexpr float kFocusedTint = 0.25f;

// --- Home -------------------------------------------------------------------

constexpr float kHeroTop = 40.0f;
constexpr float kHeroRadius = 18.0f;
constexpr float kHeroArtInsetTop = 14.0f;
constexpr float kHeroBandPadX = 12.0f;
constexpr float kHeroBandPadY = 10.0f;
constexpr float kHeroGapBelow = 20.0f;
constexpr float kHeroPillBlur = 4.0f;    // a pill is the thin material
constexpr float kHeroBandBlur = 6.0f;    // a panel is the regular one
constexpr float kShelfCoverWidth = 260.0f;
constexpr float kShelfCoverHeight = 347.0f;   // 3:4
constexpr float kShelfSpacing = 40.0f;
constexpr float kShelfHeadroom = 24.0f;
constexpr float kCoverRadius = 10.0f;
constexpr float kCaptionGap = 6.0f;

// --- Library ----------------------------------------------------------------
//
// A TILE GRID, NOT A LIST, and the reason is in docs/CABINET.md: a full-width
// row on a 1920pt canvas leaves a name at the far left and a count at the far
// right with a third of the screen empty between them. A grid also gives the
// focus engine a real two-dimensional field to move in.
constexpr float kTileMinWidth = 380.0f;   // adaptive: as many columns as fit
constexpr float kTileHeight = 200.0f;
constexpr float kTileSpacing = 36.0f;     // both axes
constexpr float kTileRadius = 18.0f;
constexpr float kTilePadding = 24.0f;     // inside a tile, around its label
// The thumbnail on the right of a tile. Deliberately shorter than the tile:
// a cover spanning the full height left the name too little room to be read,
// and a tile whose name cannot be read is not doing its job.
constexpr float kTileArtHeight = 132.0f;
constexpr float kTileArtGap = 20.0f;
// The switcher's pills: Platforms / Collections.
constexpr float kPillPadX = 14.0f;
constexpr float kPillPadY = 8.0f;
constexpr float kPillGap = 16.0f;
constexpr float kSwitcherTop = 40.0f;
constexpr float kSwitcherGapBelow = 28.0f;

// --- A grid of games --------------------------------------------------------
//
// `TenFoot` declares a 240 minimum and the grid that uses it hardcodes 260.
// Take 260: it is the value that shipped and was looked at.
constexpr float kGridCoverMin = 260.0f;
constexpr float kGridColumnSpacing = 48.0f;
constexpr float kGridRowSpacing = 44.0f;
constexpr float kGridCaptionGap = 10.0f;
constexpr float kGridCoverRadius = 12.0f;
// A list of covers gets TWO caption lines with reserved space, so rows stay
// aligned whether a title wraps or not. One line truncated almost every real
// title at these widths.
constexpr int kGridCaptionLines = 2;
constexpr float kScreenChipPadX = 24.0f;
constexpr float kScreenChipPadY = 10.0f;

// --- The launch screen ------------------------------------------------------

constexpr float kDetailCoverWidth = 340.0f;
constexpr float kDetailCoverHeight = 460.0f;
constexpr float kDetailRadius = 16.0f;
// A row, which is the settings shape and what the launch screen's actions are.
// Its height is COMPUTED from the type and this padding rather than hardcoded,
// the same way the hero's band is, so it grows with the ramp instead of
// clipping it.
constexpr float kRowRadius = 16.0f;
constexpr float kRowPadX = 32.0f;
constexpr float kRowPadY = 22.0f;
constexpr float kDetailRowGap = 16.0f;
// The reference implementation's settings column. A row stretched to the full
// 1920 leaves a label at one end and a value at the other with a third of the
// screen empty between them.
constexpr float kRowColumnMaxWidth = 1100.0f;
// The backdrop under a full-screen cover: the artwork itself, filled and
// blurred, because the leftovers should be the art's own colours rather than
// letterbox bars.
// The hero's own value, and for the same reason: one level coarser averages a
// cover down to a single muddy colour, which is not "the art's own colours",
// it is a brown rectangle where the artwork used to be.
constexpr float kBackdropBlur = 5.0f;
constexpr float kScrimOverlay = 0.55f;

// --- The in-game overlay ----------------------------------------------------

constexpr float kOverlayFade = 0.350f;
constexpr float kOverlayFocusScale = 1.04f;
constexpr float kOverlayFocusDuration = 0.150f;
constexpr float kOverlayPanelRadius = 32.0f;
constexpr float kOverlayPanelWidth = 720.0f;
constexpr float kOverlayButtonHeight = 92.0f;
constexpr float kOverlayButtonGap = 14.0f;

// --- Motion -----------------------------------------------------------------

// Ease-out is the default: things arrive quickly and settle. Ease-in-out is for
// a change of state the person asked for. Ease-in is used nowhere.
inline float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

inline float easeInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

// One animated scalar that behaves the way the reference implementation's
// animations do: a change re-targets from wherever the value currently is, so
// an interruption mid-flight is smooth rather than a jump.
struct Animated {
    float from = 0, to = 0, elapsed = 0, duration = kFocusDuration;
    // The overlay sets this for the design system's ease-IN-out: a panel that
    // covers the game should leave as deliberately as it arrives, and an
    // ease-out exit snaps away at the end.
    bool smooth = false;

    void retarget(float target, float seconds) {
        if (target == to) return;
        from = value();
        to = target;
        duration = seconds;
        elapsed = 0;
    }
    void tick(float dt) { elapsed = std::min(elapsed + dt, duration); }
    // Jumps straight to the target. A screenshot should show the resting
    // focused state, not a frame part-way through the transition into it.
    void settle(float target) {
        retarget(target, kFocusDuration);
        elapsed = duration;
    }
    float value() const {
        if (duration <= 0) return to;
        const float t = elapsed / duration;
        return from + (to - from) * (smooth ? easeInOut(t) : easeOut(t));
    }
};

// The caption rides down by half of (scale - 1) times the cover height, because
// a scale about the centre advances the bottom edge by exactly that much, which
// otherwise buries the caption underneath it. The +2 is the reference
// implementation's own breathing room. If kFocusScale changes, this follows.
inline float captionSlide(float focusAmount, float coverHeight) {
    return focusAmount * (coverHeight * (kFocusScale - 1.0f) * 0.5f + 2.0f);
}

// --- A card -----------------------------------------------------------------

struct Card {
    // The RomM ROM id, and the only safe way to match a card to anything else.
    // Titles collide: "Altered Beast" is a Game & Watch entry AND a Genesis one
    // in the reference library, so matching Recent to the library by name can
    // show the wrong platform's cover for the game that was actually played.
    int id = 0;
    ui::Color art;        // shown until the cover arrives, and if it never does
    std::string title;
    // A local path in the sample library, a RomM cover path with live data.
    // Empty means there is no art, which is a normal state and not a failure:
    // arcade sets often have none, and Game & Watch has none at all.
    std::string cover;
    Animated focus;
    Animated press;
};

// A stable colour for a card with no art, from its title. Better than one grey
// for everything: a shelf of coverless games stays distinguishable, and the
// same game is the same colour every time the library is opened.
inline ui::Color colorForTitle(const std::string& title) {
    uint32_t h = 2166136261u;
    for (unsigned char c : title) { h ^= c; h *= 16777619u; }
    // Fixed saturation and value, hue from the hash: keeps every generated
    // colour inside the design system's range instead of producing mud.
    const float hue = static_cast<float>(h % 360u);
    const float s = 0.45f, v = 0.62f;
    const float c2 = v * s;
    const float x = c2 * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float m = v - c2;
    float r = 0, g = 0, b = 0;
    if (hue < 60)       { r = c2; g = x; }
    else if (hue < 120) { r = x; g = c2; }
    else if (hue < 180) { g = c2; b = x; }
    else if (hue < 240) { g = x; b = c2; }
    else if (hue < 300) { r = x; b = c2; }
    else                { r = c2; b = x; }
    return ui::Color{r + m, g + m, b + m, 1.0f};
}

}  // namespace design
