// Marking this console's window as a gamescope overlay, for the one case where
// it is not the only thing on the screen.
//
// WHAT THIS IS FOR. An emulator that is not a libretro core — PCSX2, and PS3,
// Switch, Xbox and Wii U after it — makes its own graphics device and presents
// its own picture. Today CabinetOS copies that picture off the GPU and back on
// so it can draw the pause menu over it. gamescope will instead composite our
// menu on top of a window the emulator owns, which removes the copy entirely.
// docs/PROJECT.md, open question 24, has the measurements and the decision.
//
// THE SLOT IS NOT THE OBVIOUS ONE. `GAMESCOPE_EXTERNAL_OVERLAY` composites and
// can NEVER take input: gamescope grants input focus only under
// `w->isOverlay && w->inputFocusMode`, and `isOverlay` is the STEAM_OVERLAY
// atom. External is the HUD slot — mangoapp lives there and a HUD never needs a
// controller. A pause menu has to be STEAM_OVERLAY, which is the slot Steam's
// own overlay uses to draw over a game it does not own.
//
// It is a SEPARATE FILE because X11's headers typedef `Screen`, `Window` and
// `Font`, and this codebase has an `enum class Screen`. Including Xlib.h in
// main.cpp turns every `Screen::Grid` into a compile error.

#pragma once

struct SDL_Window;

namespace cab::overlaywin {

// Marks the window so gamescope composites it over whatever else is on screen,
// and gives it the pad.
//
// `takeInput` is Pause and Resume: true routes keyboard and controller to this
// window while the GAME KEEPS THE SCREEN, false hands input straight back. It
// can be called again at any time to flip it.
//
// Returns false when this is not an X11 window or libX11 is not present, which
// are the only ways it can fail. Callers should say so rather than carrying on
// as though the window were an overlay.
bool mark(SDL_Window* window, bool takeInput);

// Whether mark() has ever succeeded on this window, for reporting.
bool active();

} // namespace cab::overlaywin
