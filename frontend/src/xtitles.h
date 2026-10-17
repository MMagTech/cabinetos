// The emulators' window titles, read from the X server the console and the
// emulators share (gamescope's Xwayland): how Run::poll sees a game end or an
// error box come up, and how Run::stop closes a window as a person would.
// Moved out of standalone.cpp in the restructure (#398).

#pragma once

#include <dlfcn.h>

#include <algorithm>
#include <cstring>
#include <string>
#include <type_traits>

namespace cab::standalone::detail {

// THE EMULATOR'S WINDOW TITLE, read from the X server the console and the
// emulator share (gamescope's Xwayland). libX11 is opened at run time, as
// overlaywin.cpp does, so a console without it only loses this check.
struct XTitles {
    using Display = void;
    using Window = unsigned long;
    using Atom = unsigned long;
    void* lib = nullptr;
    Display* dpy = nullptr;
    Display* (*OpenDisplay)(const char*) = nullptr;
    int (*CloseDisplay)(Display*) = nullptr;
    Window (*DefaultRootWindow)(Display*) = nullptr;
    int (*QueryTree)(Display*, Window, Window*, Window*, Window**, unsigned*) = nullptr;
    Atom (*InternAtom)(Display*, const char*, int) = nullptr;
    int (*GetWindowProperty)(Display*, Window, Atom, long, long, int, Atom, Atom*, int*,
                             unsigned long*, unsigned long*, unsigned char**) = nullptr;
    int (*Free)(void*) = nullptr;
    int (*SendEvent)(Display*, Window, int, long, void*) = nullptr;
    int (*Flush)(Display*) = nullptr;

    bool open() {
        if (dpy) return true;
        if (!lib) lib = dlopen("libX11.so.6", RTLD_LAZY | RTLD_LOCAL);
        if (!lib) return false;
        auto sym = [&](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(lib, name));
            return fn != nullptr;
        };
        if (!(sym(OpenDisplay, "XOpenDisplay") && sym(CloseDisplay, "XCloseDisplay") &&
              sym(DefaultRootWindow, "XDefaultRootWindow") && sym(QueryTree, "XQueryTree") &&
              sym(InternAtom, "XInternAtom") && sym(GetWindowProperty, "XGetWindowProperty") &&
              sym(Free, "XFree") && sym(SendEvent, "XSendEvent") && sym(Flush, "XFlush")))
            return false;
        dpy = OpenDisplay(nullptr);
        return dpy != nullptr;
    }
    void close() {
        if (dpy) CloseDisplay(dpy);
        dpy = nullptr;
    }
    // The longest title among the top-level windows that start with `prefix`,
    // counted in " | " parts; 0 when there is none.
    int parts(const char* prefix) {
        if (!open()) return 0;
        const Atom netName = InternAtom(dpy, "_NET_WM_NAME", 0);
        const Atom utf8 = InternAtom(dpy, "UTF8_STRING", 0);
        Window rootRet = 0, parent = 0;
        Window* kids = nullptr;
        unsigned n = 0;
        if (!QueryTree(dpy, DefaultRootWindow(dpy), &rootRet, &parent, &kids, &n)) return 0;
        int best = 0;
        for (unsigned i = 0; i < n; ++i) {
            Atom type = 0;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            if (GetWindowProperty(dpy, kids[i], netName, 0, 1024, 0, utf8, &type, &format,
                                  &count, &after, &data) != 0 || !data)
                continue;
            const std::string title(reinterpret_cast<char*>(data), count);
            Free(data);
            if (title.compare(0, std::strlen(prefix), prefix) != 0) continue;
            int p = 1;
            for (size_t at = title.find(" | "); at != std::string::npos;
                 at = title.find(" | ", at + 3))
                ++p;
            best = std::max(best, p);
        }
        if (kids) Free(kids);
        return best;
    }
    // THE CLOSE A PERSON WOULD MAKE: WM_DELETE_WINDOW to every top-level
    // window of process `pid` (by _NET_WM_PID, which Qt sets). The number of
    // windows asked, so a caller can fall back when there were none.
    int closeWindowsOf(int pid) {
        if (!open()) return 0;
        const Atom wmPid = InternAtom(dpy, "_NET_WM_PID", 0);
        const Atom protocols = InternAtom(dpy, "WM_PROTOCOLS", 0);
        const Atom del = InternAtom(dpy, "WM_DELETE_WINDOW", 0);
        const Atom cardinal = 6;   // XA_CARDINAL
        Window rootRet = 0, parent = 0;
        Window* kids = nullptr;
        unsigned n = 0;
        if (!QueryTree(dpy, DefaultRootWindow(dpy), &rootRet, &parent, &kids, &n)) return 0;
        int asked = 0;
        for (unsigned i = 0; i < n; ++i) {
            Atom type = 0;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            if (GetWindowProperty(dpy, kids[i], wmPid, 0, 1, 0, cardinal, &type, &format, &count,
                                  &after, &data) != 0 || !data)
                continue;
            const long owner = count == 1 ? *reinterpret_cast<long*>(data) : -1;
            Free(data);
            if (owner != pid) continue;
            // XClientMessageEvent, laid out as Xlib declares it, inside the
            // 24 longs every XEvent is.
            struct {
                int type;
                unsigned long serial;
                int sendEvent;
                Display* display;
                Window window;
                Atom messageType;
                int format;
                long l[5];
            } msg{};
            long ev[24] = {};
            static_assert(sizeof msg <= sizeof ev, "XEvent is 24 longs");
            msg.type = 33;   // ClientMessage
            msg.sendEvent = 1;
            msg.display = dpy;
            msg.window = kids[i];
            msg.messageType = protocols;
            msg.format = 32;
            msg.l[0] = static_cast<long>(del);
            std::memcpy(ev, &msg, sizeof msg);
            if (SendEvent(dpy, kids[i], 0, 0, ev)) ++asked;
        }
        if (kids) Free(kids);
        Flush(dpy);
        return asked;
    }
    // Whether process `pid` has a top-level window called exactly `title`:
    // the emulator's own error box (`errorBox`).
    bool hasWindowTitled(int pid, const char* title) {
        if (!open()) return false;
        const Atom wmPid = InternAtom(dpy, "_NET_WM_PID", 0);
        const Atom netName = InternAtom(dpy, "_NET_WM_NAME", 0);
        const Atom utf8 = InternAtom(dpy, "UTF8_STRING", 0);
        const Atom cardinal = 6;   // XA_CARDINAL
        Window rootRet = 0, parent = 0;
        Window* kids = nullptr;
        unsigned n = 0;
        if (!QueryTree(dpy, DefaultRootWindow(dpy), &rootRet, &parent, &kids, &n)) return false;
        bool found = false;
        for (unsigned i = 0; i < n && !found; ++i) {
            Atom type = 0;
            int format = 0;
            unsigned long count = 0, after = 0;
            unsigned char* data = nullptr;
            if (GetWindowProperty(dpy, kids[i], wmPid, 0, 1, 0, cardinal, &type, &format, &count,
                                  &after, &data) != 0 || !data)
                continue;
            const long owner = count == 1 ? *reinterpret_cast<long*>(data) : -1;
            Free(data);
            if (owner != pid) continue;
            data = nullptr;
            if (GetWindowProperty(dpy, kids[i], netName, 0, 1024, 0, utf8, &type, &format,
                                  &count, &after, &data) != 0 || !data)
                continue;
            found = std::string(reinterpret_cast<char*>(data), count) == title;
            Free(data);
        }
        if (kids) Free(kids);
        return found;
    }
};

// The one there is, in xtitles.cpp.
extern XTitles gTitles;

}  // namespace cab::standalone::detail
