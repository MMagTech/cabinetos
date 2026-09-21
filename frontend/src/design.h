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
// HOME FITS IN ONE SCREEN, and these three numbers are what buys it.
//
// MMagTech, 2026-09-19, on the A9 at native 4K: "I should not have to scroll
// down to see the favorites. Everything on home should be visible in one 4K
// screen." The same objection he had to the reference implementation, which
// scrolls.
//
// The arithmetic it replaces. A shelf block was the header (60), a 347 cover,
// a 39 caption and their gaps — about 508 — and PROJECT.md's "Home has about
// 85 points of vertical slack" only ever counted the hero and ONE shelf:
// 40 + 420 + 20 + 508 = 988 of 1080. Favourites needed another 508 and got
// 92, which is why its title was on screen and its covers were not.
//
// So about 430 points had to come out of a 1080 canvas, and it came from all
// three: the hero down from 420, the covers down from 347, and the per-card
// caption out of the layout entirely — the focused card's title now rides in
// the shelf header, where it costs no vertical space at all.
//
// Budget, and it deliberately does not fill the canvas:
//   40 top + 340 hero + 20 gap + 2 x (~310) = about 1020 of 1080
//
// REVISED the same evening, on the panel: the shelf headings at Title 2 read
// too large next to 210-point covers, so they are Title 3 and the room that
// frees goes to the hero rather than to whitespace. MMagTech, looking at it:
// "recent and favorite text seem too large. We could make them a bit smaller
// and allow the hero area to get slightly bigger."
// The 60 left over is overscan allowance. A television eats the edges, and
// this project has resized the hero three times over exactly that — once
// while a simulator showed it fitting. Check with --safe-area on a panel
// before trusting any of it.
constexpr float kHeroHeight = 340.0f;
constexpr float kShelfCoverWidth = 158.0f;
constexpr float kShelfCoverHeight = 210.0f;   // 3:4
constexpr float kShelfSpacing = 40.0f;
constexpr float kShelfHeadroom = 20.0f;
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

// THE PAUSE PANEL IS SOLID, NOT GLASS, AND THAT IS DELIBERATE — 2026-09-21.
//
// It used to be glass: it blurred the game showing through it, by sampling the
// console's own scene texture. That works only while the console is the thing
// drawing the game. It is not, for the emulators that are not libretro cores —
// PCSX2 and the heavy systems after it own their own window and gamescope
// composites our menu on top, so there is no game in our texture to blur and
// the panel came out flat on that path and frosted on this one.
//
// MMagTech's call: *"if its just about consistency then we can remove the glass
// from this menu and maybe add a little style to both to make them match."*
// Right, and it deletes work rather than adding it — the alternative was
// grabbing the screen once per pause to blur ourselves, which is a READBACK,
// the exact thing the compositing route exists to remove.
//
// So both paths now run the same code with no branch in it at all. Every OTHER
// glass surface — the Home hero, Library, Grid and Detail — is untouched,
// because those only ever appear while the console is drawing the whole scene.
//
// docs/PROJECT.md, open question 24.
// THE PANEL. A SURFACE, NOT A HOLE — and this was pure black for one build,
// which MMagTech called correctly: *"still seems a bit flat and maybe too
// black"*. It uses the console's own surface token, the dark purple the library
// tiles are made of, so the pause menu belongs to the same object as everything
// else rather than being a black rectangle borrowed from nowhere.
//
// Near-opaque, because it has to stay readable over a bright game with only the
// scrim helping. Glass used to do some of that work.
constexpr ui::Color kOverlayPanelSurface = ui::palette::kSurface;
constexpr float kOverlayPanelFill = 0.92f;
// The gradient and the top edge light, which are what stop a panel this size
// reading as a hole punched in the screen. Both are small on purpose: at 4K a
// gradient you can NAME is too strong, and one you can only feel is right.
constexpr float kOverlayPanelFillBottom = 0.96f;   // slightly denser at the foot
constexpr float kOverlayPanelBottomDarken = 0.62f; // and slightly darker
constexpr float kOverlayPanelEdgeLight = 0.22f;
// A hairline rather than a border. At 4K a 2px stroke reads as a drawn box;
// 1.5px at 14% reads as an edge catching the light, which is the intent.
constexpr float kOverlayPanelBorder = 1.5f;
constexpr float kOverlayPanelBorderAlpha = 0.14f;
// The shadow is what lifts the panel off the game now that the blur does not.
// Big and soft: a tight shadow looks like a sticker, a wide one like depth.
constexpr float kOverlayPanelShadowBlur = 64.0f;
constexpr float kOverlayPanelShadowY = 22.0f;
constexpr float kOverlayPanelShadowAlpha = 0.60f;

// THE BUTTONS. FOCUS IS A RIM, which is what focus is everywhere else in this
// console — every card, every pill, the setup boxes. A full-width light bar was
// tried for one build and MMagTech was right about it: *"not sure how i feel
// about the giant white bars"*. It also invented a second focus idiom for one
// screen, which is exactly the drift the shared-menu rule exists to prevent.
constexpr float kOverlayButtonRadius = 18.0f;
constexpr float kOverlayButtonRestFill = 0.06f;
constexpr float kOverlayButtonFocusFill = 0.16f;
constexpr float kOverlayButtonRestText = 0.62f;
// A focused row gets its own small shadow, so it sits above its neighbours
// rather than merely being paler than them.
constexpr float kOverlayButtonFocusShadowBlur = 22.0f;
constexpr float kOverlayButtonFocusShadowY = 6.0f;
constexpr float kOverlayButtonFocusShadowAlpha = 0.45f;
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
