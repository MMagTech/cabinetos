// The start-up screen: what the television shows from the moment the console
// starts until Home, and when the app is about to start again. The cabinet,
// the wordmark and one line saying which stage is taking the time
// (drawStartup), drawn as one still frame while the boot's blocking work runs
// (showWaiting). Its cabinet alone is How to's Wiki tile (drawCabinetIcon).
// Moved out of setup.h in the restructure (#398); setup.h includes this, for
// Deps.

#pragma once

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
void showWaiting(const Deps& d, const char* detail);

// The same startup screen, drawn into a frame somebody else is building, at
// `alpha`. For the main loop's curtain when the app is about to start again
// (Sign out, a new server address), so the screen it leaves on is the screen
// it comes back on.
// `hint`, if given, is a second line under it, brighter: something a person
// can do ("Press (A) to change the server address").
// The CabinetOS cabinet, the startup screen's icon, with its top-left at x,y
// and `h` tall (its width is h * 527 / 720). How to's wiki code carries it.
void drawCabinetIcon(ui::Renderer& r, float x, float y, float h, float a);

void drawStartup(ui::Renderer& r, ui::TextRenderer& t, const char* detail, float alpha,
                 const char* hint = nullptr);

}  // namespace setup
