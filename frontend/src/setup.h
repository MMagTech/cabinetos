// First run, as a person sees it.
//
// `firstrun.h` owns the RULES — the chain, and what may be skipped. This owns
// the experience: the screens, the keyboard, and the blocking work that has to
// happen off the frame loop so the console never stops drawing while it waits.
//
// --- IT RUNS ITS OWN LOOP, AND THAT IS THE DESIGN ---------------------------
//
// Setup is not a mode inside the main frame loop; it is a loop of its own that
// runs to completion and returns. Three reasons, in the order they matter:
//
//   1. **The app cannot start without a server** and setup is what supplies
//      one. The library is fetched from RomM before the main loop exists, so a
//      first run woven into that loop would have to survive a state where the
//      thing the loop is built around does not exist yet.
//   2. **It genuinely is linear and happens once.** docs/PROJECT.md open
//      question 15b: there is no skip and no branch, because on the far side of
//      a skip there is nothing to show. A mode flag would be modelling a
//      freedom the product does not have.
//   3. Main is already five thousand lines. Threading a sixth state through it
//      would make both halves harder to read for no gain.
//
// --- NOTHING HERE BLOCKS THE FRAME ------------------------------------------
//
// A Wi-Fi scan is seconds, a Bluetooth scan is ten, and waiting for somebody to
// pick up a phone and approve a pairing is minutes. Every one of those runs on
// a worker and is polled once a frame, the same shape `LaunchJob` and
// `StateLoad` already use in main.cpp. A setup screen that freezes while it
// looks for networks is indistinguishable from a console that has crashed —
// and it would be the FIRST thing anybody ever saw it do.

#pragma once

#include <string>

struct SDL_Window;

namespace ui {
class Renderer;
class TextRenderer;
}

namespace setup {

struct Deps {
    SDL_Window* window = nullptr;
    ui::Renderer* renderer = nullptr;
    ui::TextRenderer* text = nullptr;
};

struct Options {
    // Open at a named step — network, wifi, server, pair, controller, done.
    //
    // The same reasoning as `--screen`: every screen has to be photographable
    // from a machine with no controller and nobody to describe it. What it does
    // NOT do is fake the facts, so a step opened on a machine that could not
    // reach it still draws the truth about that machine.
    const char* startStep = nullptr;

    // Capture, exactly as the rest of the frontend does it.
    const char* screenshotPath = nullptr;
    int frames = 0;

    // Render at a size this machine's window is not, so a layout can be checked
    // against a television nobody here owns. Without it a headless capture
    // comes out at the offscreen driver's 1024x768 — a 4:3 frame, on which
    // every judgement about a 16:9 layout is worthless.
    int renderWidth = 0, renderHeight = 0;

    // NEVER WRITE ANYTHING. For captures and for looking at the flow on a
    // console that is already set up, which is the only way anybody here can
    // look at it at all — the reference machine is configured and taking that
    // away to see a screen would be a silly way to lose an afternoon.
    bool dryRun = false;
};

// One frame saying the console is busy, drawn in the same language as the setup
// screens and presented immediately.
//
// WHY THIS EXISTS. Pressing "Start playing" is followed by several seconds of
// blocking work — reaching the server, adopting the user, and pulling a library
// that is sixteen hundred games on the reference machine — and until this
// existed the screen simply stopped changing. MMagTech, 2026-09-20: *"after
// hitting start playing there's a bit of a delay. It sort of seems like maybe
// it's stalled since nothing indicates the delay."*
//
// AND IT IS NOT ONLY AFTER SETUP. The same blocking work runs on EVERY boot, so
// a console has always shown a blank screen for those seconds — and for up to
// ninety of them when it is waiting for a server that is not up yet. Nobody had
// noticed because nobody watches a console boot with a stopwatch; it took
// somebody pressing a button and expecting something to happen.
//
// It is one frame, not a loop: the work it covers is synchronous, so there is
// nothing to animate against. A still sentence that says what is happening
// beats a picture that has stopped changing for no stated reason.
void showWaiting(const Deps& d, const char* title, const char* detail);

enum class Outcome {
    Completed,   // setup finished; the console is configured
    Quit,        // the person quit, or a capture run ended
};

// Runs first run to completion. On success the server address and the token are
// on disk and the marker is written, so the caller can carry on as if the
// console had always been configured.
Outcome run(const Deps& d, const Options& o);

}  // namespace setup
