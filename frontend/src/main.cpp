// CabinetOS frontend — the foundation.
//
// What this proves, which is the whole point of it existing:
//
//   1. A Wayland client with a real GLES 3 context, under cage or gamescope,
//      with no toolkit between us and the frame.
//   2. The 1920x1080 design canvas, letterboxed to whatever panel is attached.
//   3. The design system's focus treatment, to the point: 1.10 scale, a 4pt
//      inset white rim at 85%, a black 55% shadow blurred 26 and offset 14
//      down, and the caption sliding clear of the grown card.
//   4. Motion at the right tempo — 180ms ease-out, the most-used value in the
//      reference implementation.
//   5. Controller and keyboard driving focus, with neither required.
//
// What it deliberately does not do yet: text (there is no font layer), and
// cores (there is no Linux core built). Both are next, and neither changes
// anything here.
//
// See docs/PROJECT.md, "The design system".

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "ui.h"

namespace {

// --- The design system, as numbers -----------------------------------------
// Every value here is quoted from docs/PROJECT.md. If one of them changes
// there, it changes here, and nowhere else.

constexpr float kContentInset = 60.0f;
constexpr float kShelfCoverWidth = 260.0f;
constexpr float kShelfCoverHeight = 347.0f;  // 3:4
constexpr float kShelfSpacing = 40.0f;
constexpr float kShelfHeadroom = 24.0f;  // room for the focused card to grow
constexpr float kCoverRadius = 10.0f;
constexpr float kCaptionGap = 6.0f;

constexpr float kFocusScale = 1.10f;
constexpr float kPressScale = 1.02f;
constexpr float kFocusRimWidth = 4.0f;
constexpr float kFocusShadowBlur = 26.0f;
constexpr float kFocusShadowOffsetY = 14.0f;

// 180ms ease-out. The focus tempo, and nothing about focus should be slower:
// a controller crosses a shelf faster than that and the animations must not
// queue up behind the person driving them.
constexpr float kFocusDuration = 0.180f;
constexpr float kPressDuration = 0.120f;

// The caption rides down by half of (scale - 1) times the cover height, because
// a scale about the centre advances the bottom edge by exactly that much. The
// +2 is the reference implementation's own breathing room. If kFocusScale
// changes, this follows it automatically.
float captionSlide(float focusAmount) {
    return focusAmount * (kShelfCoverHeight * (kFocusScale - 1.0f) * 0.5f + 2.0f);
}

float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// One animated scalar that behaves the way the reference implementation's
// animations do: a change re-targets from wherever the value currently is, so
// an interruption mid-flight is smooth rather than a jump.
struct Animated {
    float from = 0, to = 0, elapsed = 0, duration = kFocusDuration;

    void retarget(float target, float seconds) {
        if (target == to) return;
        from = value();
        to = target;
        duration = seconds;
        elapsed = 0;
    }
    void tick(float dt) { elapsed = std::min(elapsed + dt, duration); }
    float value() const {
        if (duration <= 0) return to;
        return from + (to - from) * easeOut(elapsed / duration);
    }
};

struct Card {
    ui::Color art;  // stands in for cover art until there is an image layer
    Animated focus;
    Animated press;
};

// Placeholder "cover art". Real covers arrive with the RomM client in Phase 4;
// until then these are just distinguishable rectangles so focus is legible.
const ui::Color kPlaceholderArt[] = {
    ui::Color::rgb(0x2484D6), ui::Color::rgb(0xEC405C), ui::Color::rgb(0x58E8F6),
    ui::Color::rgb(0xFFC457), ui::Color::rgb(0xFF7AC7), ui::Color::rgb(0x7A6BC4),
};

}  // namespace

int main(int argc, char** argv) {
    bool shotMode = false;
    const char* shotPath = "/tmp/cabinetos-frame.bmp";
    int shotAfterFrames = 30;
    int initialFocus = -1;
    // Verify the layout at a panel size this machine does not have. Most sets
    // are 4K; plenty are not; the design canvas scales to both and this is how
    // that gets checked rather than assumed.
    int renderW = 0, renderH = 0;
    for (int i = 1; i < argc; ++i) {
        if (SDL_strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotMode = true;
            shotPath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            shotAfterFrames = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--render-size") == 0 && i + 1 < argc) {
            SDL_sscanf(argv[++i], "%dx%d", &renderW, &renderH);
        } else if (SDL_strcmp(argv[i], "--focus") == 0 && i + 1 < argc) {
            // Lets a screenshot capture a chosen card already focused, so the
            // focus treatment can be checked without a controller attached.
            initialFocus = SDL_atoi(argv[++i]);
        }
    }

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        std::fprintf(stderr, "[frontend] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // GLES 3.0, which is what the libretro hardware-rendered cores ask for via
    // RETRO_ENVIRONMENT_SET_HW_RENDER. The UI and the cores share one context
    // on purpose: that is what removes the readback the Apple build cannot
    // avoid. See docs/PROJECT.md, "Video: two paths".
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "CabinetOS", 1920, 1080, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!window) {
        std::fprintf(stderr, "[frontend] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "[frontend] SDL_GL_CreateContext failed: %s\n",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Under cage on software rendering vsync throttles to whatever llvmpipe can
    // manage, which is fine — the frame loop is paced by wall clock, not by the
    // swap, exactly as the core pacing will be.
    SDL_GL_SetSwapInterval(1);

    std::fprintf(stderr, "[frontend] GL_VENDOR   %s\n", glGetString(GL_VENDOR));
    std::fprintf(stderr, "[frontend] GL_RENDERER %s\n", glGetString(GL_RENDERER));
    std::fprintf(stderr, "[frontend] GL_VERSION  %s\n", glGetString(GL_VERSION));

    ui::Renderer renderer;
    if (!renderer.init()) {
        std::fprintf(stderr, "[frontend] renderer init failed\n");
        return 1;
    }

    std::vector<Card> cards;
    for (const auto& c : kPlaceholderArt) cards.push_back(Card{c, {}, {}});

    int focused = initialFocus >= 0 ? initialFocus : 0;
    focused = std::clamp(focused, 0, static_cast<int>(cards.size()) - 1);
    // Settled, not animating: a screenshot should show the resting focused
    // state, not a frame part-way through the transition into it.
    cards[focused].focus.retarget(1.0f, kFocusDuration);
    cards[focused].focus.elapsed = kFocusDuration;

    // Any pad that is already plugged in. Hotplug is handled in the event loop,
    // so a controller connected later works without restarting anything.
    int padCount = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&padCount)) {
        for (int i = 0; i < padCount; ++i) SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
    std::fprintf(stderr, "[frontend] gamepads at startup: %d\n", padCount);

    bool running = true;
    uint64_t previous = SDL_GetTicksNS();
    int frame = 0;
    bool pressing = false;

    auto moveFocus = [&](int delta) {
        int next = std::clamp(focused + delta, 0, static_cast<int>(cards.size()) - 1);
        if (next == focused) return;
        cards[focused].focus.retarget(0.0f, kFocusDuration);
        focused = next;
        cards[focused].focus.retarget(1.0f, kFocusDuration);
    };

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    SDL_OpenGamepad(e.gdevice.which);
                    std::fprintf(stderr, "[frontend] gamepad connected\n");
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (e.key.key == SDLK_ESCAPE) running = false;
                    if (e.key.key == SDLK_LEFT) moveFocus(-1);
                    if (e.key.key == SDLK_RIGHT) moveFocus(+1);
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) pressing = true;
                    break;
                case SDL_EVENT_KEY_UP:
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) pressing = false;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) moveFocus(-1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) moveFocus(+1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) pressing = true;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) running = false;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) pressing = false;
                    break;
                default:
                    break;
            }
        }

        uint64_t now = SDL_GetTicksNS();
        float dt = static_cast<float>(now - previous) / 1e9f;
        previous = now;
        // A stall must not teleport an animation; the reference implementation
        // caps its own accumulator for the same reason.
        dt = std::min(dt, 0.1f);

        for (auto& c : cards) {
            c.focus.tick(dt);
            c.press.tick(dt);
        }
        cards[focused].press.retarget(pressing ? 1.0f : 0.0f, kPressDuration);

        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        bool offscreen = false;
        if (renderW > 0 && renderH > 0) {
            offscreen = renderer.beginOffscreen(renderW, renderH);
            if (offscreen) {
                dw = renderW;
                dh = renderH;
            }
        }
        renderer.beginFrame(dw, dh);
        renderer.drawBackdrop(ui::Gradient{ui::palette::kBackdropTop,
                                           ui::palette::kBackdropMid,
                                           ui::palette::kBackdropBottom, 0.55f});

        // The shelf header. A placeholder bar until there is a text layer —
        // its size is the space "Recent ›" will occupy at Title 2.
        renderer.draw(ui::Rect{kContentInset, 300.0f, 240.0f, 44.0f, 8.0f,
                               ui::Color::white(0.22f)});

        const float shelfTop = 300.0f + 44.0f + 12.0f + kShelfHeadroom;

        // Unfocused cards first, so a focused card's shadow and rim land on top
        // of its neighbours rather than under them.
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t i = 0; i < cards.size(); ++i) {
                const bool isFocused = (static_cast<int>(i) == focused);
                if ((pass == 0) == isFocused) continue;

                Card& card = cards[i];
                const float f = card.focus.value();
                const float p = card.press.value();

                // Pressed reads as a push INTO the screen, against the focused
                // lift, so a click still registers on a card that is already
                // raised.
                const float scale = 1.0f + f * (kFocusScale - 1.0f) -
                                    p * (kFocusScale - kPressScale);

                const float baseX = kContentInset +
                                    static_cast<float>(i) * (kShelfCoverWidth + kShelfSpacing);
                const float w = kShelfCoverWidth * scale;
                const float h = kShelfCoverHeight * scale;
                const float x = baseX - (w - kShelfCoverWidth) * 0.5f;
                const float y = shelfTop - (h - kShelfCoverHeight) * 0.5f;

                ui::Rect cover{x, y, w, h, kCoverRadius * scale, card.art};
                cover.border = f * kFocusRimWidth;
                cover.borderColor = ui::palette::kFocusRim;
                cover.shadowBlur = f * kFocusShadowBlur;
                cover.shadowOffsetY = f * kFocusShadowOffsetY;
                cover.shadowColor = ui::Color::black(0.55f * f);
                renderer.draw(cover);

                // The caption, riding down with the lift so the grown card
                // cannot bury it. A placeholder bar until there is text.
                const float capY = shelfTop + kShelfCoverHeight + kCaptionGap +
                                   captionSlide(f);
                renderer.draw(ui::Rect{baseX, capY, kShelfCoverWidth * 0.8f, 26.0f, 6.0f,
                                       ui::Color::white(isFocused ? 0.85f : 0.45f)});
            }
        }

        ++frame;
        // Capture before the swap. After a swap the back buffer's contents are
        // undefined, so a readback taken there is whatever the driver left.
        if (shotMode && frame >= shotAfterFrames) {
            renderer.saveFrame(shotPath, dw, dh);
            running = false;
        }

        if (offscreen) {
            renderer.endOffscreen();
        } else {
            SDL_GL_SwapWindow(window);
        }
    }

    renderer.shutdown();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
