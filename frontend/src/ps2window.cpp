// The PS2 window (#226): PCSX2 drawing into a window of its own under
// gamescope, opened before the load and closed after the unload, and the
// frame over it, where this window draws only what is over the game
// (#398, restructure batch f).

#include "app.h"

#include <cstdio>
#include <cstdlib>

#include "overlaywin.h"
#include "ps2.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

// --ps2-copy: PCSX2 without a window of its own, every frame copied back for
// this window to draw, as before #226. For measuring the two side by side.
bool gPs2Copy = false;

void App::openPs2Window() {
    if (gPs2Copy || !std::getenv("GAMESCOPE_WAYLAND_DISPLAY")) return;
    const char* driver = SDL_GetCurrentVideoDriver();
    if (!driver || SDL_strcmp(driver, "x11") != 0) return;
    int w = 0, h = 0;
    SDL_GetWindowSizeInPixels(window, &w, &h);
    ps2Window = SDL_CreateWindow("PlayStation 2", w, h, SDL_WINDOW_FULLSCREEN);
    if (!ps2Window) {
        std::fprintf(stderr, "[ps2] no window of its own (%s); copying frames\n",
                     SDL_GetError());
        return;
    }
    const auto xid = static_cast<unsigned long>(SDL_GetNumberProperty(
        SDL_GetWindowProperties(ps2Window), SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    const char* display = std::getenv("DISPLAY");
    SDL_GetWindowSizeInPixels(ps2Window, &w, &h);
    if (!xid || !ps2::setWindow(display ? display : "", xid, static_cast<unsigned>(w),
                                static_cast<unsigned>(h))) {
        std::fprintf(stderr, "[ps2] this PS2 library cannot take a window; copying frames\n");
        SDL_DestroyWindow(ps2Window);
        ps2Window = nullptr;
        return;
    }
    // This window steps over it, as for an emulator of its own: hidden,
    // marked, shown, because gamescope classifies a window when it maps.
    SDL_HideWindow(window);
    cab::overlaywin::mark(window, /*takeInput=*/false);
    SDL_ShowWindow(window);
    ps2InputOurs = false;
    renderer.setTransparentBackground(true);
    ps2ClearFrames = 30;
    std::fprintf(stderr, "[ps2] its own window 0x%lx, %dx%d\n", xid, w, h);
}

// After the game has stopped, which is when PCSX2 has let go of it.
void App::closePs2Window() {
    if (!ps2Window) return;
    SDL_DestroyWindow(ps2Window);
    ps2Window = nullptr;
    SDL_HideWindow(window);
    cab::overlaywin::unmark(window);
    SDL_ShowWindow(window);
    renderer.setTransparentBackground(false);
    ps2InputOurs = false;
}

// OVER PCSX2'S OWN WINDOW (#226) this window draws only what is over
// the game: the pause menu, a notice, the curtain, the dim. With none
// of them showing it draws nothing at all, as over an emulator of its
// own, so gamescope shows PCSX2's picture straight and this loop does
// not render 4K for nobody; the game above (pads, sound) still runs
// every pass. The menu has the controllers while it is up, so a press
// meant for it never reaches the game.
// True when this frame is done: the frame loop goes straight to the next.
bool App::frameOverPs2Window() {
    if (ps2Window && playing) {
        const bool over = overlayOpen || overlayFade.value() > 0.001f ||
                          curtain.value() > 0.001f || menuNotice.alpha() > 0.001f ||
                          raToast.alpha() > 0.001f || dimLayer.value() > 0.001f;
        if (overlayOpen != ps2InputOurs) {
            cab::overlaywin::mark(window, /*takeInput=*/overlayOpen);
            ps2InputOurs = overlayOpen;
        }
        if (over) {
            ps2ClearFrames = 30;
        } else {
            if (ps2ClearFrames > 0) {
                --ps2ClearFrames;
                glBindFramebuffer(GL_FRAMEBUFFER, 0);
                glDisable(GL_SCISSOR_TEST);
                glClearColor(0, 0, 0, 0);
                glClear(GL_COLOR_BUFFER_BIT);
                SDL_GL_SwapWindow(window);
            } else {
                // Until the next press or 4 ms: the pads are read again
                // that often, which is sooner than PCSX2 asks for them.
                SDL_WaitEventTimeout(nullptr, 4);
            }
            return true;
        }
    }
    return false;
}
