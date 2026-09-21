/*
 * overlay-probe — can this console draw its menu over a window it does not own?
 *
 * WHAT IT IS FOR. PCSX2, and every other emulator that is not a libretro core,
 * makes its own graphics device and presents its own picture. Today CabinetOS
 * copies that picture off the GPU and back on so it can draw the pause menu
 * over it, which costs 6.1 ms of a 16.7 ms frame at 4x. The alternative is to
 * let the emulator present straight to the screen and have gamescope composite
 * our menu on top. This program is the smallest thing that answers whether
 * gamescope will do that — it is a TEST INSTRUMENT and not a step towards the
 * product.
 *
 * It draws nothing but a pattern chosen so that LOOKING AT THE TELEVISION
 * answers the question, rather than suggesting an answer:
 *
 *   - an OPAQUE magenta bar across the top     -> composited at all?
 *   - a 50% GREEN band across the middle       -> does the game show through?
 *   - everything else fully TRANSPARENT        -> is the game covered?
 *
 * If the game is visible THROUGH the green band, alpha blending is real and the
 * overlay can be a pause menu. If the band is flat green, an overlay would black
 * the game out and the whole route is worthless.
 *
 * YOU MUST LOOK AT THE TELEVISION. `gamescopectl screenshot` DOES NOT CAPTURE
 * EITHER OVERLAY PLANE — not even with type 2, "all_real_layers". Every capture
 * taken while proving this came back showing only the game, and that cost most
 * of a session before MMagTech looked up and said the gears were on the screen.
 * There is no capture path that shows this. The television is the instrument.
 *
 * THE TWO SLOTS ARE NOT THE SAME, and the difference decides the design:
 *
 *   --external   GAMESCOPE_EXTERNAL_OVERLAY. Composites. CANNOT EVER TAKE
 *                INPUT — steamcompmgr.cpp grants input focus only under
 *                `if (w->isOverlay && w->inputFocusMode)`, and isOverlay is
 *                the OTHER atom. This is the HUD slot; mangoapp uses it.
 *
 *   --steam      STEAM_OVERLAY, plus STEAM_INPUT_FOCUS. Composites AND takes
 *                the pad, while the GAME KEEPS THE SCREEN. This is the slot
 *                Steam's own overlay uses and the only one a pause menu can
 *                live in.
 *
 * Both are only ever read from the ROOT Xwayland context:
 *   pFocus->externalOverlayWindow = root_ctx->focus.externalOverlayWindow;
 * A window on any other server carries the atom and is silently never
 * consulted, which looks exactly like a compositor that refused.
 *
 * gamescope paints the overlay with PaintWindowFlag::NoScale, so it is drawn at
 * its own pixel size and NOT stretched to the output. Pass the output's real
 * resolution — 3840x2160 on the reference console — or the menu lands in a
 * corner at a quarter size, which is what glxgears did.
 *
 * Build (the console has no X11 headers; the PCSX2 builder container has them):
 *   podman run --rm -v "$PWD":/w:Z -w /w localhost/cabinetos-pcsx2-builder \
 *     bash -c 'gcc -O2 -o overlay-probe overlay-probe.c $(pkg-config --cflags --libs x11)'
 *
 * Run it against a gamescope that is already showing something else, on that
 * gamescope's root X display:
 *   DISPLAY=:0 ./overlay-probe 3840 2160 --steam
 *
 * tools/gamescope-overlay-test.sh does the whole thing, including standing up
 * a game to sit underneath.
 */
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xatom.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void set_card(Display *d, Window w, const char *name, unsigned long v)
{
    Atom a = XInternAtom(d, name, False);
    XChangeProperty(d, w, a, XA_CARDINAL, 32, PropModeReplace,
                    (unsigned char *)&v, 1);
}

int main(int argc, char **argv)
{
    int W = argc > 1 ? atoi(argv[1]) : 1920;
    int H = argc > 2 ? atoi(argv[2]) : 1080;
    int steam_slot = 0;
    for (int i = 3; i < argc; i++)
        if (!strcmp(argv[i], "--steam")) steam_slot = 1;

    if (W <= 0 || H <= 0) {
        fprintf(stderr, "usage: overlay-probe <width> <height> [--steam]\n");
        return 1;
    }

    Display *d = XOpenDisplay(NULL);
    if (!d) { fprintf(stderr, "no display\n"); return 1; }
    int scr = DefaultScreen(d);

    /* Depth 32 or there is no alpha to blend and the middle band proves
     * nothing. */
    XVisualInfo vi;
    if (!XMatchVisualInfo(d, scr, 32, TrueColor, &vi)) {
        fprintf(stderr, "no 32-bit visual — cannot test alpha\n");
        return 1;
    }

    XSetWindowAttributes at;
    memset(&at, 0, sizeof at);
    at.colormap = XCreateColormap(d, RootWindow(d, scr), vi.visual, AllocNone);
    at.border_pixel = 0;
    at.background_pixel = 0;
    at.event_mask = ExposureMask | StructureNotifyMask;

    Window w = XCreateWindow(d, RootWindow(d, scr), 0, 0, W, H, 0,
                             32, InputOutput, vi.visual,
                             CWColormap | CWBorderPixel | CWBackPixel | CWEventMask,
                             &at);

    XStoreName(d, w, "cabinetos overlay probe");

    /* SET THE ATOMS BEFORE MAPPING. gamescope classifies a window when it maps.
     * Setting them afterwards only sets a flag on a window nothing re-examines,
     * and the overlay never appears — unless something else forces the rescan
     * (see the opacity poke in gamescope-overlay-test.sh). */
    if (steam_slot) {
        set_card(d, w, "STEAM_OVERLAY", 1);
        set_card(d, w, "STEAM_INPUT_FOCUS", 1);
        set_card(d, w, "GAMESCOPE_NO_FOCUS", 0);
    } else {
        set_card(d, w, "GAMESCOPE_EXTERNAL_OVERLAY", 1);
        set_card(d, w, "GAMESCOPE_NO_FOCUS", 1);
    }

    XMapWindow(d, w);
    XFlush(d);

    unsigned int *px = malloc((size_t)W * H * 4);
    if (!px) { fprintf(stderr, "out of memory\n"); return 1; }

    XImage *img = XCreateImage(d, vi.visual, 32, ZPixmap, 0,
                               (char *)px, W, H, 32, W * 4);
    GC gc = XCreateGC(d, w, 0, NULL);

    printf("overlay window 0x%lx  %dx%d  depth 32  slot=%s\n",
           w, W, H, steam_slot ? "STEAM_OVERLAY+INPUT" : "GAMESCOPE_EXTERNAL_OVERLAY");
    fflush(stdout);

    /* REDRAWN EVERY FRAME WITH A MOVING TICK. An unchanging image makes
     * Xwayland hand gamescope the same buffer twice; gamescope logs "got the
     * same buffer committed twice, ignoring" and drops it, which is
     * indistinguishable from an overlay it refused to composite. */
    for (int frame = 0;; frame++) {
        int slide = (frame * 17) % W;
        for (int y = 0; y < H; y++) {
            unsigned int row;
            if (y < H / 9)                           row = 0xFFFF00FF; /* opaque magenta */
            else if (y > H * 4 / 9 && y < H * 5 / 9) row = 0x80008000; /* 50% green, PREMULTIPLIED */
            else                                     row = 0x00000000; /* transparent */
            for (int x = 0; x < W; x++) {
                unsigned int v = row;
                if (y < H / 9 && x > slide && x < slide + 60) v = 0xFFFFFFFF;
                px[(size_t)y * W + x] = v;
            }
        }
        while (XPending(d)) { XEvent e; XNextEvent(d, &e); }
        XPutImage(d, w, gc, img, 0, 0, 0, 0, W, H);
        XFlush(d);
        usleep(33000);
    }
    return 0;
}
