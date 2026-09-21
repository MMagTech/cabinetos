#include "overlaywin.h"

#include <SDL3/SDL.h>

#include <cstdio>
#include <dlfcn.h>

namespace cab::overlaywin {
namespace {

// libX11 IS OPENED AT RUNTIME, NOT LINKED, which is the same choice gpu.cpp
// makes for the Vulkan loader and for the same two reasons: this binary must
// still start on a machine without it, and adding a build dependency for an
// instrument is a poor trade. Xwayland is always present under gamescope, so
// in practice it is always there — but "in practice" is not a reason to fail
// to load.
//
// The three calls are declared by hand rather than included, because X11's
// headers typedef `Screen`, `Window` and `Font`, and this codebase has an
// `enum class Screen`. The types below are X11's own: XID and Atom are
// `unsigned long`, and a Display* is opaque to us.
using XDisplay = void;
using XID = unsigned long;
using XAtom = unsigned long;

constexpr int kPropModeReplace = 0;   // X.h
constexpr XAtom kXA_Cardinal = 6;     // Xatom.h

using PFN_XInternAtom = XAtom (*)(XDisplay*, const char*, int);
using PFN_XChangeProperty = int (*)(XDisplay*, XID, XAtom, XAtom, int, int,
                                    const unsigned char*, int);
using PFN_XFlush = int (*)(XDisplay*);

struct Xlib {
    void* handle = nullptr;
    PFN_XInternAtom InternAtom = nullptr;
    PFN_XChangeProperty ChangeProperty = nullptr;
    PFN_XFlush Flush = nullptr;
    bool ok = false;
};

Xlib& xlib() {
    static Xlib x = [] {
        Xlib r;
        r.handle = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
        if (!r.handle) {
            std::fprintf(stderr, "[overlay] no libX11.so.6 on this machine\n");
            return r;
        }
        r.InternAtom = reinterpret_cast<PFN_XInternAtom>(dlsym(r.handle, "XInternAtom"));
        r.ChangeProperty =
            reinterpret_cast<PFN_XChangeProperty>(dlsym(r.handle, "XChangeProperty"));
        r.Flush = reinterpret_cast<PFN_XFlush>(dlsym(r.handle, "XFlush"));
        r.ok = r.InternAtom && r.ChangeProperty && r.Flush;
        if (!r.ok) std::fprintf(stderr, "[overlay] libX11 is missing a symbol\n");
        return r;
    }();
    return x;
}

bool g_active = false;

} // namespace

bool active() { return g_active; }

bool mark(SDL_Window* window, bool takeInput) {
    Xlib& x = xlib();
    if (!x.ok) return false;

    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    auto* dpy = static_cast<XDisplay*>(
        SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, nullptr));
    const auto xid =
        static_cast<XID>(SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0));
    if (!dpy || !xid) {
        std::fprintf(stderr, "[overlay] this is not an X11 window — gamescope's "
                             "overlay slots are X11 properties\n");
        return false;
    }

    auto set = [&](const char* name, unsigned long value) {
        const XAtom a = x.InternAtom(dpy, name, 0);
        x.ChangeProperty(dpy, xid, a, kXA_Cardinal, 32, kPropModeReplace,
                         reinterpret_cast<const unsigned char*>(&value), 1);
    };

    // THE SLOT THAT CAN HOLD THE PAD. See overlaywin.h for why it is not
    // GAMESCOPE_EXTERNAL_OVERLAY.
    set("STEAM_OVERLAY", 1);
    // Pause and Resume, in one property. With it set, keyboard and controller
    // come here and the GAME KEEPS THE SCREEN; cleared, input goes straight
    // back to the game.
    set("STEAM_INPUT_FOCUS", takeInput ? 1 : 0);
    set("GAMESCOPE_NO_FOCUS", 0);
    x.Flush(dpy);

    if (!g_active) {
        std::fprintf(stderr,
                     "[overlay] window 0x%lx marked STEAM_OVERLAY, input=%s\n",
                     xid, takeInput ? "ours" : "game's");
    }
    g_active = true;
    return true;
}

} // namespace cab::overlaywin
