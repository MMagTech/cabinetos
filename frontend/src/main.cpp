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

#include <csignal>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core.h"
#include "image.h"
#include "text.h"
#include "ui.h"

namespace {

// Photograph the running console on demand, without stopping it.
//
//   kill -USR1 $(pgrep -f cabinetos-frontend)
//
// This is not a debugging convenience, it is how anyone ever finds out what a
// CabinetOS machine actually has on its screen. The test VM has no way to show
// a person a picture, and later the machines that matter are in other people's
// living rooms — where "send me a photo of the telly" is the whole bug report
// channel (see *HDMI-CEC*, which has exactly this problem).
//
// A signal handler may do almost nothing safely, so it sets a flag and the
// frame loop does the work, on the thread that owns the GL context.
volatile std::sig_atomic_t gCaptureRequested = 0;
void requestCapture(int) { gCaptureRequested = 1; }

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
    ui::Color art;      // shown until the cover arrives, and if it never does
    const char* title;
    const char* cover;  // a path today, a RomM URL in Phase 4
    Animated focus;
    Animated press;
};

// Stand-in library. Real covers and names arrive with the RomM client in Phase
// 4; these exist so the layout is exercised against the shapes real data has —
// a long title that has to truncate, and a Japanese one, which a ROM library is
// full of and which is the reason the font stack has a CJK fallback at all.
const Card kSampleLibrary[] = {
    {ui::Color::rgb(0x2484D6), "Sonic the Hedgehog 2", "covers/a-3x4.jpg", {}, {}},
    {ui::Color::rgb(0xEC405C), "Super Metroid", "covers/b-3x4.png", {}, {}},
    {ui::Color::rgb(0x58E8F6), "Castlevania: Symphony of the Night", "covers/c-3x4.jpg", {}, {}},
    // Deliberately the wrong shape: a squarish arcade flyer. This is the
    // odd-aspect case, and it must letterbox onto a blurred echo of itself
    // rather than crop the title off the top of the art.
    {ui::Color::rgb(0xFFC457), "\xE3\x83\x89\xE3\x83\xA9\xE3\x82\xAD\xE3\x83\xA5\xE3\x83\xBC\xE3\x82\xB7\xE3\x83\xA5", "covers/d-square.png", {}, {}},
    // A wide one, for the same reason in the other direction.
    {ui::Color::rgb(0xFF7AC7), "Streets of Rage 2", "covers/e-wide.jpg", {}, {}},
    // No cover at all. Arcade sets often have none, and the coloured panel with
    // the title under it is the honest answer rather than a grey box.
    {ui::Color::rgb(0x7A6BC4), "Chrono Trigger", nullptr, {}, {}},
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
    // Deliberately settable, so eviction can be exercised on a machine with
    // plenty of memory. An unbounded texture cache on a 4 GB console is a real
    // failure mode and it must be testable, not merely intended.
    size_t imageBudget = 192u * 1024 * 1024;
    // Loads covers that are then never drawn again, the way scrolling a shelf
    // leaves the covers behind it. That is the only situation in which anything
    // is evictable at all, so it is the only way to test that eviction works.
    bool evictTest = false;
    // Running a core. Both are needed: a core without a ROM has nothing to do.
    const char* corePath = nullptr;
    const char* romPath = nullptr;
    for (int i = 1; i < argc; ++i) {
        if (SDL_strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotMode = true;
            shotPath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            shotAfterFrames = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
            corePath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            romPath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--evict-test") == 0) {
            evictTest = true;
        } else if (SDL_strcmp(argv[i], "--image-budget-mb") == 0 && i + 1 < argc) {
            imageBudget = static_cast<size_t>(SDL_atoi(argv[++i])) * 1024 * 1024;
        } else if (SDL_strcmp(argv[i], "--render-size") == 0 && i + 1 < argc) {
            SDL_sscanf(argv[++i], "%dx%d", &renderW, &renderH);
        } else if (SDL_strcmp(argv[i], "--focus") == 0 && i + 1 < argc) {
            // Lets a screenshot capture a chosen card already focused, so the
            // focus treatment can be checked without a controller attached.
            initialFocus = SDL_atoi(argv[++i]);
        }
    }

    std::signal(SIGUSR1, requestCapture);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
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

    ui::TextRenderer text;
    // Regular, Medium, SemiBold, Bold, then the CJK fallback. All five are
    // already in the Bazzite base, so the type costs the image nothing.
    if (!text.init({"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
                    "/usr/share/fonts/google-noto/NotoSans-Medium.ttf",
                    "/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf",
                    "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
                    "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc"})) {
        std::fprintf(stderr, "[frontend] text init failed\n");
        return 1;
    }

    // 192 MB of covers resident. A shelf holds a handful; a library grid holds
    // a screenful; anything past that is re-decoded on the way back, which is
    // cheap and bounded. Four workers, so a fast scroll keeps up without
    // starving the frame thread on a four-core box.
    ui::ImageCache images;
    // A key is not a path. Everything after '#' is stripped before reading, so
    // one file can stand in for many distinct entries here — and so a real RomM
    // URL with a query string is already the shape this expects.
    images.init(imageBudget, 4, [](const std::string& key) {
        const size_t hash = key.find('#');
        return ui::ImageCache::readFile(hash == std::string::npos ? key
                                                                 : key.substr(0, hash));
    });

    std::vector<Card> cards(std::begin(kSampleLibrary), std::end(kSampleLibrary));

    // --- The core, if one was asked for --------------------------------------
    bool playing = false;
    SDL_AudioStream* audioStream = nullptr;
    if (corePath && romPath) {
        cab::Core& core = cab::Core::shared();
        if (!core.load(corePath)) {
            std::fprintf(stderr, "[frontend] core: %s\n", core.error().c_str());
            return 1;
        }
        // The save directory must outlive the session. Per-game, alongside the
        // ROM for now; Phase 4 moves it under the chosen storage location.
        const std::string saveDir = "saves";
        SDL_CreateDirectory(saveDir.c_str());
        if (!core.loadGame(romPath, "system", saveDir)) {
            std::fprintf(stderr, "[frontend] %s\n", core.error().c_str());
            return 1;
        }
        playing = true;

        // Audio: SDL owns the device thread and we push from the frame loop.
        // The rule from the reference implementation is that the callback must
        // never block — so there is no callback, and nothing to block.
        SDL_AudioSpec src{};
        src.format = SDL_AUDIO_S16;
        src.channels = 2;
        src.freq = static_cast<int>(core.avInfo().sampleRate);
        audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &src,
                                                nullptr, nullptr);
        if (audioStream) {
            SDL_ResumeAudioStreamDevice(audioStream);
            std::fprintf(stderr, "[frontend] audio out at %d Hz\n", src.freq);
        } else {
            // A console with no sound card is still a console. Say so and play on.
            std::fprintf(stderr, "[frontend] no audio device: %s\n", SDL_GetError());
        }
    }

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

        if (evictTest && frame == 5) {
            // Five covers requested once and never asked for again: the shelf
            // has scrolled past them.
            for (int i = 0; i < 5; ++i) {
                images.get("covers/b-3x4.png#scrolled-past-" + std::to_string(i));
            }
        }
        if (playing) {
            cab::Core& core = cab::Core::shared();
            // Buttons first, then run: a core samples input inside retro_run,
            // so anything set afterwards is a frame late.
            cab::PadState pad;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            auto held = [&](SDL_Scancode k) { return keys && keys[k]; };
            pad.buttons = 0;
            // The RetroPad, which every core speaks whatever the real hardware
            // had. Keyboard here; a real pad is wired the same way below.
            using cab::bit;
            if (held(SDL_SCANCODE_UP)) pad.buttons |= bit(cab::Up);
            if (held(SDL_SCANCODE_DOWN)) pad.buttons |= bit(cab::Down);
            if (held(SDL_SCANCODE_LEFT)) pad.buttons |= bit(cab::Left);
            if (held(SDL_SCANCODE_RIGHT)) pad.buttons |= bit(cab::Right);
            if (held(SDL_SCANCODE_X)) pad.buttons |= bit(cab::A);
            if (held(SDL_SCANCODE_Z)) pad.buttons |= bit(cab::B);
            if (held(SDL_SCANCODE_RETURN)) pad.buttons |= bit(cab::Start);
            if (held(SDL_SCANCODE_RSHIFT)) pad.buttons |= bit(cab::Select);

            // A real pad, mapped by SDL's own gamepad abstraction so the
            // hundreds of controllers in its database all arrive the same way.
            // Both paths OR together: neither is required, both work.
            int padCountNow = 0;
            if (SDL_JoystickID* ids = SDL_GetGamepads(&padCountNow)) {
                if (padCountNow > 0) {
                    if (SDL_Gamepad* gp = SDL_GetGamepadFromID(ids[0])) {
                        auto down = [&](SDL_GamepadButton b) {
                            return SDL_GetGamepadButton(gp, b);
                        };
                        if (down(SDL_GAMEPAD_BUTTON_DPAD_UP)) pad.buttons |= bit(cab::Up);
                        if (down(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) pad.buttons |= bit(cab::Down);
                        if (down(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) pad.buttons |= bit(cab::Left);
                        if (down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) pad.buttons |= bit(cab::Right);
                        // South is the bottom face button whatever it is
                        // labelled: A on Xbox, B on Nintendo, Cross on
                        // PlayStation. SDL normalises by POSITION, which is the
                        // only thing that is actually the same across pads.
                        if (down(SDL_GAMEPAD_BUTTON_SOUTH)) pad.buttons |= bit(cab::B);
                        if (down(SDL_GAMEPAD_BUTTON_EAST)) pad.buttons |= bit(cab::A);
                        if (down(SDL_GAMEPAD_BUTTON_WEST)) pad.buttons |= bit(cab::Y);
                        if (down(SDL_GAMEPAD_BUTTON_NORTH)) pad.buttons |= bit(cab::X);
                        if (down(SDL_GAMEPAD_BUTTON_START)) pad.buttons |= bit(cab::Start);
                        if (down(SDL_GAMEPAD_BUTTON_BACK)) pad.buttons |= bit(cab::Select);
                        pad.leftX = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                        pad.leftY = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                    }
                }
                SDL_free(ids);
            }
            core.setPad(0, pad);

            core.runFor(dt);
            core.uploadFrame();

            if (audioStream) {
                const std::vector<int16_t>& samples = core.drainAudio();
                if (!samples.empty()) {
                    SDL_PutAudioStreamData(audioStream, samples.data(),
                                           static_cast<int>(samples.size() * sizeof(int16_t)));
                }
            }
        }

        images.pump(dt);
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

        if (playing) {
            cab::Core& core = cab::Core::shared();
            if (core.texture() && core.frameWidth() > 0) {
                // Integer-scaled and centred. A Game Boy is 160x144 and its
                // pixels were each a deliberate choice; scaling by 6.4 makes
                // some of them twice the size of their neighbours, which is
                // visible from a sofa and looks like a fault. Phase 8 can offer
                // the non-integer option; the default should be honest.
                const float srcW = static_cast<float>(core.frameWidth());
                const float srcH = static_cast<float>(core.frameHeight());
                const float aspect = core.avInfo().aspectRatio > 0
                                         ? core.avInfo().aspectRatio
                                         : srcW / srcH;
                float scale = std::floor(
                    std::min(ui::kCanvasWidth / (srcH * aspect), ui::kCanvasHeight / srcH));
                if (scale < 1.0f) scale = 1.0f;
                const float dh = srcH * scale;
                const float dw = dh * aspect;
                ui::drawImageTexture(renderer, core.texture(),
                                     (ui::kCanvasWidth - dw) * 0.5f,
                                     (ui::kCanvasHeight - dh) * 0.5f, dw, dh);
            }

            // The overlay is not composited by anything clever: the frontend
            // owns the frame loop, so the pause menu is simply drawn over the
            // game. That is the payoff of hosting cores in process rather than
            // launching them.
            if (pressing) {
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(0.55f)});
                const float panelW = 560, panelH = 260;
                renderer.draw(ui::Rect{(ui::kCanvasWidth - panelW) * 0.5f,
                                       (ui::kCanvasHeight - panelH) * 0.5f, panelW, panelH,
                                       32, ui::Color::white(0.14f)});
                const float sc2 = renderer.scale();
                const char* title = core.coreName().c_str();
                const float tw = text.measure(title, ui::TextStyle::Title2, sc2);
                text.draw(renderer, title, (ui::kCanvasWidth - tw) * 0.5f,
                          ui::kCanvasHeight * 0.5f - 40,
                          ui::TextStyle::Title2, ui::Color::white(1.0f), sc2);
                const char* sub = "Paused";
                const float sw = text.measure(sub, ui::TextStyle::Callout, sc2);
                text.draw(renderer, sub, (ui::kCanvasWidth - sw) * 0.5f,
                          ui::kCanvasHeight * 0.5f + 10, ui::TextStyle::Callout,
                          ui::Color::white(0.60f), sc2);
            }
        } else {

        // The shelf header: Title 2 bold, with the chevron that says the row
        // continues into a screen of its own.
        const float sc = renderer.scale();
        const float headerBaseline = 300.0f + text.ascent(ui::TextStyle::Title2, sc);
        text.draw(renderer, "Recent", kContentInset, headerBaseline,
                  ui::TextStyle::Title2, ui::Color::white(1.0f), sc);
        const float headerWidth =
            text.measure("Recent", ui::TextStyle::Title2, sc);
        // Title 3 semibold at tertiary, not Title 2: the chevron says "this row
        // continues", it is not part of the heading, and at heading weight it
        // competes with it.
        text.draw(renderer, "\xE2\x80\xBA", kContentInset + headerWidth + 10.0f,
                  headerBaseline, ui::TextStyle::Title3, ui::Color::white(0.30f), sc);

        const float shelfTop =
            300.0f + text.lineHeight(ui::TextStyle::Title2, sc) + 12.0f + kShelfHeadroom;

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

                // The coloured panel under the art: it is what shows while a
                // cover is still decoding, what stays if there is none, and
                // what the art fades in over.
                ui::Rect cover{x, y, w, h, kCoverRadius * scale, card.art};
                cover.border = f * kFocusRimWidth;
                cover.borderColor = ui::palette::kFocusRim;
                cover.shadowBlur = f * kFocusShadowBlur;
                cover.shadowOffsetY = f * kFocusShadowOffsetY;
                cover.shadowColor = ui::Color::black(0.55f * f);
                renderer.draw(cover);

                if (card.cover) {
                    // Fill, which the drawing code turns into fit-over-a-
                    // blurred-echo by itself when the cover is the wrong shape.
                    ui::drawImage(renderer, images.get(card.cover), x, y, w, h,
                                  ui::Fit::Fill, 1.0f, kCoverRadius * scale);
                    // The rim again, over the art: it is the focus indicator and
                    // nothing may sit on top of it.
                    if (f > 0.0f) {
                        ui::Rect rim{x, y, w, h, kCoverRadius * scale, ui::Color::white(0)};
                        rim.border = f * kFocusRimWidth;
                        rim.borderColor = ui::palette::kFocusRim;
                        renderer.draw(rim);
                    }
                }

                // The caption, riding down with the lift so the grown card
                // cannot bury it. One line, truncated with a real ellipsis:
                // game titles are long and at this width most of them are.
                const float capBaseline = shelfTop + kShelfCoverHeight + kCaptionGap +
                                          text.ascent(ui::TextStyle::Callout, sc) +
                                          captionSlide(f);
                const std::string caption = text.truncate(
                    card.title, ui::TextStyle::Callout, sc, kShelfCoverWidth);
                // Focused is primary, everything else is secondary — the same
                // way the reference implementation dims what you are not on.
                text.draw(renderer, caption, baseX, capBaseline, ui::TextStyle::Callout,
                          ui::Color::white(isFocused ? 1.0f : 0.60f), sc);
            }
        }
        }  // end of the shelf branch

        ++frame;
        // Capture before the swap. After a swap the back buffer's contents are
        // undefined, so a readback taken there is whatever the driver left.
        if (shotMode && frame >= shotAfterFrames) {
            renderer.saveFrame(shotPath, dw, dh);
            running = false;
        }
        if (gCaptureRequested) {
            gCaptureRequested = 0;
            renderer.saveFrame("/tmp/cabinetos-frame.bmp", dw, dh);
        }

        if (offscreen) {
            renderer.endOffscreen();
        } else {
            SDL_GL_SwapWindow(window);
        }
    }

    if (playing) {
        cab::Core& core = cab::Core::shared();
        const double realtime = core.audioFramesTotal() / core.avInfo().sampleRate;
        std::fprintf(stderr,
                     "[core] %llu frames, %llu audio frames = %.2fs of emulated time\n",
                     static_cast<unsigned long long>(core.framesRun()),
                     static_cast<unsigned long long>(core.audioFramesTotal()), realtime);
        core.unload();
    }
    if (audioStream) SDL_DestroyAudioStream(audioStream);
    std::fprintf(stderr, "[image] resident %.1f MB, %d still pending\n",
                 images.bytesResident() / (1024.0 * 1024.0), images.pendingCount());
    images.shutdown();
    text.shutdown();
    renderer.shutdown();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
