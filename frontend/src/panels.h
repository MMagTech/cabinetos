// The in-game overlay's numbers and the small panels' recipe (menuPanel,
// menuButton, menuLabel): the pause and Power menus, the PIN pad and the
// question panel are built from them, and the grid's letter index and the
// account panel take their edge and shadow. Moved out of design.h in the
// restructure (#398).

#pragma once

#include "design.h"
#include "look.h"
#include "ui.h"

namespace design {

// --- The in-game overlay ----------------------------------------------------

// 620 ms, TUNED ON THE PANEL 2026-09-22 with the Power menu. The design
// system's 350 ms read as the menu arriving "in pieces" — MMagTech could not
// name it until the fade was slowed to four seconds and it turned out to be
// speed: the slide up and the fade were over before the eye took the panel in
// as one thing. 550 was still quick and 700 a touch slow; this is between.
// Both the pause menu and the Power menu use it, so they cannot drift apart.
constexpr float kOverlayFade = 0.620f;
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
// The panel colour follows the signed-in person's colour and dark mode:
// look::surface(), in place of the constant this used to be.
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

// THE PAUSE MENU'S PANEL AND BUTTONS, as a recipe the console's other small
// panels share: the PIN pad (pin.cpp) and the question panel (choice.cpp).
// Both tried the keyboard's black glass (*"forgotten about"*) and the account
// panel's frosted glass (*"too frosted or washed out"*) first; MMagTech,
// 2026-09-24. `a` fades the whole thing. The pause menu itself still builds
// its own in main.cpp, from these same constants.
inline ui::Rect menuPanel(float x, float y, float w, float h, float a) {
    ui::Color fill = look::surface();
    fill.a = kOverlayPanelFill * a;
    ui::Rect p{x, y, w, h, kOverlayPanelRadius, fill};
    p.gradient = true;
    p.fillBottom = ui::Color{fill.r * kOverlayPanelBottomDarken,
                             fill.g * kOverlayPanelBottomDarken,
                             fill.b * kOverlayPanelBottomDarken,
                             kOverlayPanelFillBottom * a};
    p.edgeLight = ui::Color::white(kOverlayPanelEdgeLight * a);
    p.border = kOverlayPanelBorder;
    p.borderColor = ui::Color::white(kOverlayPanelBorderAlpha * a);
    p.shadowBlur = kOverlayPanelShadowBlur;
    p.shadowOffsetY = kOverlayPanelShadowY;
    p.shadowColor = ui::Color::black(kOverlayPanelShadowAlpha * a);
    return p;
}
// A button on it; `f` is focus, 0 to 1. Focus is the rim, as everywhere.
inline ui::Rect menuButton(float x, float y, float w, float h, float radius, float f,
                           float a) {
    ui::Rect b{x, y, w, h, radius,
               ui::Color::white((kOverlayButtonRestFill +
                                 f * (kOverlayButtonFocusFill - kOverlayButtonRestFill)) * a)};
    if (f > 0.0f) {
        b.border = f * kFocusRimWidth;
        b.borderColor = ui::palette::kFocusRim;
        b.borderColor.a *= a;
        b.shadowBlur = kOverlayButtonFocusShadowBlur;
        b.shadowOffsetY = kOverlayButtonFocusShadowY;
        b.shadowColor = ui::Color::black(kOverlayButtonFocusShadowAlpha * f * a);
    }
    return b;
}
// Its label: dim at rest, full white focused.
inline ui::Color menuLabel(float f, float a) {
    return ui::Color::white((kOverlayButtonRestText + f * (1.0f - kOverlayButtonRestText)) * a);
}
constexpr float kOverlayButtonHeight = 92.0f;
constexpr float kOverlayButtonGap = 14.0f;

}  // namespace design
