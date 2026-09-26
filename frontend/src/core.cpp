#include "core.h"
#include "ps2.h"

#include <dlfcn.h>
#include <unistd.h>

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <string>
#include <mutex>
#include <unordered_map>

#include "libretro.h"
#include "libretro_vulkan.h"
#include "vkhost.h"

// The vendored libretro.h stops at environment call 72 and this one is 73.
//
// Carried across by value rather than by updating the header: libretro.h is
// 3,937 lines that every part of this host reads, and replacing it wholesale
// to gain one constant is a change with a blast radius out of all proportion
// to the gain. The guard means a later wholesale update simply wins, rather
// than colliding with this.
//
// A core asks this to find out which version of the negotiation interface the
// frontend understands, BEFORE it declares one. Answering matters: unanswered,
// a core is entitled to assume the newest it knows.
#ifndef RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT
#define RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT \
    (73 | RETRO_ENVIRONMENT_EXPERIMENTAL)
#endif

namespace cab {
namespace {

// File-static rather than instance state, and deliberately. Libretro's
// callbacks are plain C function pointers with no context argument, so the
// process holds exactly one core's worth of state however this is shaped.
struct {
    unsigned (*api_version)(void) = nullptr;
    void (*get_system_info)(retro_system_info*) = nullptr;
    void (*get_system_av_info)(retro_system_av_info*) = nullptr;
    void (*set_environment)(retro_environment_t) = nullptr;
    void (*set_video_refresh)(retro_video_refresh_t) = nullptr;
    void (*set_audio_sample)(retro_audio_sample_t) = nullptr;
    void (*set_audio_sample_batch)(retro_audio_sample_batch_t) = nullptr;
    void (*set_input_poll)(retro_input_poll_t) = nullptr;
    void (*set_input_state)(retro_input_state_t) = nullptr;
    void (*set_controller_port_device)(unsigned, unsigned) = nullptr;
    void (*init)(void) = nullptr;
    void (*deinit)(void) = nullptr;
    bool (*load_game)(const retro_game_info*) = nullptr;
    void (*unload_game)(void) = nullptr;
    void (*run)(void) = nullptr;
    void (*reset)(void) = nullptr;
    size_t (*serialize_size)(void) = nullptr;
    bool (*serialize)(void*, size_t) = nullptr;
    bool (*unserialize)(const void*, size_t) = nullptr;
    void* (*get_memory_data)(unsigned) = nullptr;
    size_t (*get_memory_size)(unsigned) = nullptr;
} g;

constexpr int kMaxPorts = 2;
PadState gPads[kMaxPorts];

// How many controller ports the CORE says it has, from
// RETRO_ENVIRONMENT_SET_CONTROLLER_INFO. Four for Flycast, and it matters that
// the answer comes from the core rather than from a number chosen here — see
// the note in loadGame about telling a core what is plugged in.
unsigned gCorePorts = 0;

unsigned gPixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
std::string gSystemDir, gSaveDir;

// How the core wants its picture turned, in 90-degree counter-clockwise steps.
//
// A vertical arcade board — DoDonPachi, Ikaruga, most shmups — has its monitor
// bolted in sideways in the cabinet, so the board renders a picture that is
// sideways in memory and asks the frontend to turn it. A frontend that ignores
// the ask draws a rotated game, which is what this console did for every TATE
// board in the library until 2026-09-19.
//
// THIS IS THE CORE'S ANSWER, NOT THE PLATFORM'S. It cannot be looked up from
// the system, because the same arcade emulator serves upright and vertical
// boards from one platform row and only knows which after the game is loaded.
unsigned gRotation = 0;
bool gRotationAnnounced = false;

// Once per game, like the rotation line. Cleared in loadGame.
bool gInputDescriptorsLogged = false;

// RetroPad ids as a person says them, for the input descriptor log. The order
// is libretro's own and matches cab::Button.
const char* padIdName(unsigned id) {
    switch (id) {
        case RETRO_DEVICE_ID_JOYPAD_B: return "B";
        case RETRO_DEVICE_ID_JOYPAD_Y: return "Y";
        case RETRO_DEVICE_ID_JOYPAD_SELECT: return "Select";
        case RETRO_DEVICE_ID_JOYPAD_START: return "Start";
        case RETRO_DEVICE_ID_JOYPAD_UP: return "Up";
        case RETRO_DEVICE_ID_JOYPAD_DOWN: return "Down";
        case RETRO_DEVICE_ID_JOYPAD_LEFT: return "Left";
        case RETRO_DEVICE_ID_JOYPAD_RIGHT: return "Right";
        case RETRO_DEVICE_ID_JOYPAD_A: return "A";
        case RETRO_DEVICE_ID_JOYPAD_X: return "X";
        case RETRO_DEVICE_ID_JOYPAD_L: return "L (shoulder)";
        case RETRO_DEVICE_ID_JOYPAD_R: return "R (shoulder)";
        case RETRO_DEVICE_ID_JOYPAD_L2: return "L2 (trigger)";
        case RETRO_DEVICE_ID_JOYPAD_R2: return "R2 (trigger)";
        case RETRO_DEVICE_ID_JOYPAD_L3: return "L3 (stick click)";
        case RETRO_DEVICE_ID_JOYPAD_R3: return "R3 (stick click)";
        default: return nullptr;
    }
}

// The core writes into its own buffer and reuses it between calls, so a frame
// is copied out rather than referenced.
std::vector<uint8_t> gFrame;
unsigned gFrameW = 0, gFrameH = 0;
size_t gFramePitch = 0;
bool gFrameDirty = false;

std::vector<int16_t> gAudio;
std::vector<int16_t> gAudioDrain;
uint64_t gAudioFrames = 0;
uint64_t gFramesRun = 0;

// --- Core options -----------------------------------------------------------
//
// AN UNANSWERED OPTION IS NOT THE DEFAULT. It is the worst value in the list,
// and it fails silently.
//
// A core reads its settings by asking the frontend for each variable in turn.
// When the frontend does not answer, the core does NOT fall back to the default
// printed in its own option table — the whole case is skipped and the C global
// keeps whatever it was initialised to, which is zero. Zero means silence for a
// sample rate, black for brightness, and off for every toggle whose useful
// state is on. This cost the reference implementation eight separate evenings,
// one option at a time.
//
// This file used to say the opposite, in as many words, above a map that
// nothing ever wrote to. So every option of every core went unanswered, and
// twenty cores have been running on zeroes.
//
// THE FIX IS TO CAPTURE THE TABLE THE CORE DECLARES and answer every key in it.
// A core announces its options through one of three generations of the same
// idea, and a frontend has to take whichever it is offered:
//
//   SET_VARIABLES          the original. "Description; first|second|third",
//                          and the FIRST value is the default by convention.
//   SET_CORE_OPTIONS       adds an explicit `default_value` rather than relying
//                          on ordering, plus per-value labels.
//   SET_CORE_OPTIONS_V2    adds categories. Same defaults.
//
// Each has an _INTL variant that wraps a US table and a localised one; the US
// table is the one with the keys in it, so that is the one read.
//
// We report version 2, because the explicit `default_value` is a fact the core
// states rather than a convention we infer from ordering.
struct Option {
    std::string key;
    std::string desc;
    std::vector<std::string> values;
    std::string defaultValue;   // what the core says
    std::string chosen;         // what we answer, which may be an override
    bool overridden = false;
    bool asked = false;         // did the core actually come back for it
};

std::vector<Option> gDeclared;                              // declaration order
std::unordered_map<std::string, size_t> gByKey;             // key -> gDeclared
std::unordered_map<std::string, std::string> gOverrides;    // our deliberate choices
// Keys a core asked for that it never declared. Not answerable, and worth
// counting: it means either the core has a bug or we missed a table.
std::vector<std::string> gUndeclaredAsks;

// THE CONTROL. With this on, GET_VARIABLE answers nothing, which is exactly
// what this host did before the table was captured — so the difference the fix
// makes can be measured rather than asserted.
//
// docs/PROJECT.md: "Run the control before believing a comparison." The
// backend-diff tool exists for the same reason, and it earned its keep by
// showing that a difference everyone believed in was the build id.
bool gAnswerOptions = true;

// The other control, and it is the same idea as gAnswerOptions: turn one thing
// off so the difference can be measured rather than argued about.
//
// With this set, SET_HW_RENDER is refused no matter what the core asked for.
// A core that draws with GL then falls back to whatever it does with no
// picture — Dolphin selects its Null video backend and keeps emulating — which
// separates "this core cannot get a picture out of this host" from "this core
// cannot run in this host at all". Those two look identical from the outside
// and they are a different amount of work.
bool gRefuseHWRender = false;

void resetOptions() {
    gDeclared.clear();
    gByKey.clear();
    gUndeclaredAsks.clear();
}

void declareOption(const char* key, const char* desc,
                   std::vector<std::string> values, const char* defaultValue) {
    if (!key || !*key) return;
    // A core may declare its table more than once — the API explicitly allows
    // re-declaration to update descriptions — so a repeat replaces rather than
    // duplicates.
    auto it = gByKey.find(key);
    const size_t idx = (it == gByKey.end()) ? gDeclared.size() : it->second;
    if (it == gByKey.end()) {
        gDeclared.emplace_back();
        gByKey[key] = idx;
    }
    Option& o = gDeclared[idx];
    o.key = key;
    o.desc = desc ? desc : "";
    o.values = std::move(values);
    // An explicit default wins; otherwise the first listed value, which is what
    // the original API documents: "First entry should be treated as a default."
    if (defaultValue && *defaultValue) o.defaultValue = defaultValue;
    else if (!o.values.empty()) o.defaultValue = o.values.front();
    else o.defaultValue.clear();

    auto ov = gOverrides.find(o.key);
    o.overridden = (ov != gOverrides.end());
    o.chosen = o.overridden ? ov->second : o.defaultValue;
}

// "Description; first|second|third" — the original format. Everything before
// the first ';' is prose; the rest is the value list.
void captureVariables(const retro_variable* vars) {
    if (!vars) return;
    for (; vars->key; ++vars) {
        std::string desc, list;
        if (vars->value) {
            const std::string v = vars->value;
            const size_t semi = v.find(';');
            if (semi == std::string::npos) {
                list = v;
            } else {
                desc = v.substr(0, semi);
                list = v.substr(semi + 1);
                // The spec says the ';' is followed by a space. Trim whatever
                // whitespace is actually there rather than assuming exactly one.
                size_t b = list.find_first_not_of(" \t");
                list = (b == std::string::npos) ? std::string() : list.substr(b);
            }
        }
        std::vector<std::string> values;
        size_t start = 0;
        while (start <= list.size() && !list.empty()) {
            const size_t bar = list.find('|', start);
            values.push_back(list.substr(start, bar == std::string::npos
                                                    ? std::string::npos
                                                    : bar - start));
            if (bar == std::string::npos) break;
            start = bar + 1;
        }
        declareOption(vars->key, desc.c_str(), std::move(values), nullptr);
    }
}

void captureDefinitions(const retro_core_option_definition* defs) {
    if (!defs) return;
    for (; defs->key; ++defs) {
        std::vector<std::string> values;
        for (const retro_core_option_value* v = defs->values; v && v->value; ++v)
            values.emplace_back(v->value);
        declareOption(defs->key, defs->desc, std::move(values), defs->default_value);
    }
}

void captureV2(const retro_core_options_v2* opts) {
    if (!opts || !opts->definitions) return;
    for (const retro_core_option_v2_definition* d = opts->definitions; d->key; ++d) {
        std::vector<std::string> values;
        for (const retro_core_option_value* v = d->values; v && v->value; ++v)
            values.emplace_back(v->value);
        declareOption(d->key, d->desc, std::move(values), d->default_value);
    }
}

// --- Hardware-rendered cores -------------------------------------------------
//
// See core.h for what this is and why the core gets a framebuffer rather than a
// context. What is here is the mechanism.
//
// ONE CONTEXT, SHARED WITH THE UI. The core draws into a texture this file
// owns, in the frontend's own GLES 3.0 context, and the player draws that
// texture. There is no second context, no second thread, and no readback in
// the steady path.
//
// WHICH CONTEXTS ARE ACCEPTED, AND WHY REFUSING IS STILL THE RIGHT ANSWER
// SOMETIMES. Only GLES, because that is what this context is; desktop GL and
// Vulkan are refused with the version named, rather than accepted and then
// failed inside the core, which reads as a broken game rather than as a
// frontend that cannot do something.
//
// THE VERSION IS READ, NOT ASSUMED. SDL is asked for GLES 3.0 and the driver
// is free to hand back more — on the test VM it hands back 3.2. Refusing a
// core that wants 3.1 because of what was ASKED for would be turning down
// something this machine can actually do, so the version comes from
// glGetIntegerv on the context that exists.
retro_hw_render_callback gHW{};
bool gHWWanted = false;        // the core asked, and it was accepted
// WHICH API, because from here on almost nothing is shared. A GLES core draws
// into an FBO this file owns, on this thread; a Vulkan core draws wherever it
// likes on whatever thread it likes and hands over a finished image. See
// vkhost.h for why that difference is the whole point.
bool gHWVulkan = false;
bool gHWContextLive = false;   // context_reset has run and context_destroy has not
GLuint gHWFBO = 0, gHWColor = 0, gHWDepth = 0;
unsigned gHWTargetW = 0, gHWTargetH = 0;   // what the target is sized to
bool gHWFrame = false;         // the last frame came from the core's GL
std::string gHWContextName;

uintptr_t currentFramebuffer(void) { return gHWFBO; }

retro_proc_address_t procAddress(const char* sym) {
    // SDL's, not EGL's: SDL created this context, and on a driver where the
    // two disagree the one that made the context is the one to ask.
    return reinterpret_cast<retro_proc_address_t>(SDL_GL_GetProcAddress(sym));
}

std::string contextName(const retro_hw_render_callback& cb) {
    char buf[64];
    switch (cb.context_type) {
        case RETRO_HW_CONTEXT_OPENGLES2: return "OpenGL ES 2.0";
        case RETRO_HW_CONTEXT_OPENGLES3: return "OpenGL ES 3.0";
        case RETRO_HW_CONTEXT_OPENGLES_VERSION:
            std::snprintf(buf, sizeof(buf), "OpenGL ES %u.%u", cb.version_major,
                          cb.version_minor);
            return buf;
        case RETRO_HW_CONTEXT_OPENGL:
            std::snprintf(buf, sizeof(buf), "OpenGL %u.%u compatibility",
                          cb.version_major, cb.version_minor);
            return buf;
        case RETRO_HW_CONTEXT_OPENGL_CORE:
            std::snprintf(buf, sizeof(buf), "OpenGL %u.%u core", cb.version_major,
                          cb.version_minor);
            return buf;
        case RETRO_HW_CONTEXT_VULKAN: return "Vulkan";
        case RETRO_HW_CONTEXT_DIRECT3D: return "Direct3D";
        case RETRO_HW_CONTEXT_NONE: return "none";
        default: return "an API this frontend has never heard of";
    }
}

// What the context actually is, asked once. Falls back to the 3.0 that was
// requested when there is no context to ask — which is the core-options audit,
// where cores are loaded with no window and nothing will be rendered anyway.
void contextVersion(unsigned& major, unsigned& minor) {
    static unsigned cachedMajor = 0, cachedMinor = 0;
    if (cachedMajor == 0) {
        if (SDL_GL_GetCurrentContext()) {
            GLint a = 0, b = 0;
            glGetIntegerv(GL_MAJOR_VERSION, &a);
            glGetIntegerv(GL_MINOR_VERSION, &b);
            cachedMajor = a > 0 ? static_cast<unsigned>(a) : 3;
            cachedMinor = a > 0 ? static_cast<unsigned>(b) : 0;
        } else {
            cachedMajor = 3;
            cachedMinor = 0;
        }
    }
    major = cachedMajor;
    minor = cachedMinor;
}

bool canServe(const retro_hw_render_callback& cb) {
    unsigned haveMajor = 3, haveMinor = 0;
    contextVersion(haveMajor, haveMinor);
    auto atLeast = [&](unsigned major, unsigned minor) {
        return haveMajor > major || (haveMajor == major && haveMinor >= minor);
    };
    switch (cb.context_type) {
        case RETRO_HW_CONTEXT_OPENGLES2:
            return true;  // every GLES 3 context runs GLES 2 content
        case RETRO_HW_CONTEXT_OPENGLES3:
            return atLeast(3, 0);
        case RETRO_HW_CONTEXT_OPENGLES_VERSION:
            return atLeast(cb.version_major, cb.version_minor);
        case RETRO_HW_CONTEXT_VULKAN:
            // Served where the machine has a usable Vulkan device AND can
            // export what it draws, which is one question with one answer —
            // see gpu.h. On the test VM this is false and the core is refused
            // by name, exactly as before.
            return vk::available();
        default:
            // Desktop GL and Direct3D. Not this context, and not something an
            // environment callback should quietly paper over.
            return false;
    }
}

void destroyHWTarget() {
    if (gHWFBO) glDeleteFramebuffers(1, &gHWFBO);
    if (gHWColor) glDeleteTextures(1, &gHWColor);
    if (gHWDepth) glDeleteRenderbuffers(1, &gHWDepth);
    gHWFBO = gHWColor = gHWDepth = 0;
    gHWTargetW = gHWTargetH = 0;
}

// Allocates the target the core renders into, at the size it declared as its
// maximum. Returns false with the reason on stderr, which is a hard failure:
// a hardware core with no target draws nothing at all.
bool ensureHWTarget(unsigned w, unsigned h) {
    if (w == 0 || h == 0) return false;
    if (gHWFBO && w == gHWTargetW && h == gHWTargetH) return true;
    destroyHWTarget();

    glGenTextures(1, &gHWColor);
    glBindTexture(GL_TEXTURE_2D, gHWColor);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, static_cast<GLsizei>(w),
                 static_cast<GLsizei>(h), 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    // Linear, where the software path uses nearest — and the difference is not
    // an inconsistency. A Game Boy's 160x144 pixels were each chosen by
    // somebody; a Dreamcast's output is already a rendering of a 3D scene at
    // whatever internal resolution the core was asked for, so smoothing it
    // between that size and the panel is what it wants.
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenFramebuffers(1, &gHWFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, gHWFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           gHWColor, 0);

    if (gHW.depth) {
        // Packed 24/8 when stencil is wanted as well, which is what the ABI
        // says to do. Stencil alone is defined as invalid and ignored.
        glGenRenderbuffers(1, &gHWDepth);
        glBindRenderbuffer(GL_RENDERBUFFER, gHWDepth);
        glRenderbufferStorage(GL_RENDERBUFFER,
                              gHW.stencil ? GL_DEPTH24_STENCIL8 : GL_DEPTH_COMPONENT24,
                              static_cast<GLsizei>(w), static_cast<GLsizei>(h));
        glFramebufferRenderbuffer(
            GL_FRAMEBUFFER,
            gHW.stencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
            GL_RENDERBUFFER, gHWDepth);
    }

    const GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::fprintf(stderr,
                     "[core] the %ux%u render target this core asked for is "
                     "incomplete (0x%x)\n", w, h, status);
        destroyHWTarget();
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        return false;
    }

    // Cleared once, so the first presented frame is black rather than whatever
    // the driver left in the allocation.
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT | (gHW.depth ? GL_DEPTH_BUFFER_BIT : 0u) |
            (gHW.stencil ? GL_STENCIL_BUFFER_BIT : 0u));
    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    gHWTargetW = w;
    gHWTargetH = h;
    std::fprintf(stderr, "[core] render target %ux%u, depth %s, stencil %s\n", w, h,
                 gHW.depth ? "yes" : "no", gHW.stencil ? "yes" : "no");
    return true;
}

// Puts the context back the way the UI expects to find it.
//
// A core rendering a 3D machine sets depth testing, culling, scissoring, a
// stencil mask and a colour mask, binds its own program, buffers and textures,
// and leaves all of it set. The UI's beginFrame establishes only what it uses,
// which was correct while nothing else touched the context and is not correct
// now. Fixing it here rather than in the renderer keeps the invariant next to
// the thing that breaks it: the rule is that retro_run leaves the context as
// it found it, and this is that rule.
void restoreGLState() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glBindRenderbuffer(GL_RENDERBUFFER, 0);
    glUseProgram(0);
    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_STENCIL_TEST);
    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDepthMask(GL_TRUE);
    glStencilMask(0xFF);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    // The software upload path sets these and a core may too. Left set, a
    // stride from one core's frame corrupts the next texture anything uploads,
    // which shows up as skewed cover art rather than as anything to do with
    // emulation.
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
}

// Says once, for a turned picture, what the core hands over and what ends up
// on the screen.
//
// IT EXISTS BECAUSE THE TWO SIZES THE CORE REPORTS DISAGREE AND NEITHER IS
// WRONG. MAME 2003-Plus declares 224x256 in its av_info for Arkanoid — that is
// the picture as SHOWN, already turned — and then hands back a 256x224 buffer
// every frame, which is the board's own sideways output. The layout has to use
// the second one and gets the first one for free in the launch log, so an
// honest line that prints both is the difference between reading this in a
// minute and inferring it off a photograph.
void announceRotation(unsigned width, unsigned height) {
    // Not a function-local static: this process plays one game after another,
    // and a static would say it once in the life of the console rather than
    // once per game. Cleared beside gRotation in loadGame.
    if (gRotationAnnounced || gRotation == 0) return;
    gRotationAnnounced = true;
    const bool quarter = (gRotation & 1u) != 0u;
    std::fprintf(stderr,
                 "[core] the core hands back %ux%u and asks for %u degrees "
                 "counter-clockwise; shown as %ux%u\n",
                 width, height, gRotation * 90, quarter ? height : width,
                 quarter ? width : height);
}

void videoRefresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (data == RETRO_HW_FRAME_BUFFER_VALID) {
        // Not a buffer at all: the core has already drawn this frame into our
        // texture, and this call is only telling us how much of it is picture.
        // There is nothing to copy and nothing to upload.
        //
        // Said out loud once, because "the core is running but the screen is
        // black" has two completely different causes — the core has not
        // presented anything, or it has and the picture is not reaching the
        // screen — and they lead to opposite places. Without this line the
        // two are indistinguishable from outside.
        if (!gHWFrame) {
            std::fprintf(stderr, "[core] first hardware frame: %ux%u into the "
                                 "%ux%u target\n", width, height, gHWTargetW,
                         gHWTargetH);
        }
        gHWFrame = true;
        gFrameW = width;
        gFrameH = height;
        gFrameDirty = true;
        // retro_vulkan_image carries no size, so this is the only place the
        // frontend learns how much of the core's image is picture. See
        // vkhost.h, setFrameSize.
        if (gHWVulkan) vk::setFrameSize(width, height);
        announceRotation(width, height);
        return;
    }
    if (!data) return;  // "same picture as last time"
    gHWFrame = false;
    const size_t bpp = (gPixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) ? 4 : 2;
    gFrame.resize(pitch * height);
    std::memcpy(gFrame.data(), data, pitch * height);
    gFrameW = width;
    gFrameH = height;
    gFramePitch = pitch;
    gFrameDirty = true;
    announceRotation(width, height);
    (void)bpp;
}

void audioSample(int16_t left, int16_t right) {
    gAudio.push_back(left);
    gAudio.push_back(right);
    ++gAudioFrames;
}

size_t audioSampleBatch(const int16_t* data, size_t frames) {
    gAudio.insert(gAudio.end(), data, data + frames * 2);
    gAudioFrames += frames;
    return frames;
}

void inputPoll(void) {}

int16_t inputState(unsigned port, unsigned device, unsigned index, unsigned id) {
    if (port >= kMaxPorts) return 0;
    const PadState& pad = gPads[port];
    if (device == RETRO_DEVICE_JOYPAD) {
        if (id == RETRO_DEVICE_ID_JOYPAD_MASK) return static_cast<int16_t>(pad.buttons);
        return (pad.buttons >> id) & 1;
    }
    if (device == RETRO_DEVICE_ANALOG) {
        // HOW FAR A TRIGGER IS PRESSED. A third index, and it is NOT a stick:
        // its `id` is a joypad button id rather than an axis, so L2 is 12 and
        // R2 is 13. Flycast asks for exactly these two, because the
        // Dreamcast's triggers are analogue.
        //
        // THIS USED TO RETURN THE RIGHT STICK'S Y AXIS. The old line tested
        // only for the LEFT index and treated everything else as the right
        // stick, so "how far is the left trigger pressed" was answered with
        // where the right stick was sitting. Two faults in one: the triggers
        // could not be pressed, and the right stick drove them.
        //
        // AND IT WAS WORSE THAN RETURNING NOTHING, which is the part worth
        // remembering. Flycast reads the analogue value FIRST and only falls
        // back to the digital L2/R2 bit when that value is zero — so the
        // reference implementation, which answers this index with a plain 0,
        // works by taking the fallback. A real pad's right stick rests a few
        // hundred counts off centre, that is not zero, and a non-zero answer
        // means "the trigger is very slightly pressed" and suppresses the
        // fallback entirely. Found 2026-09-19 by MMagTech, who said the
        // shoulder buttons did not work and was right.
        if (index == RETRO_DEVICE_INDEX_ANALOG_BUTTON) {
            const float t = id == RETRO_DEVICE_ID_JOYPAD_L2   ? pad.leftTrigger
                            : id == RETRO_DEVICE_ID_JOYPAD_R2 ? pad.rightTrigger
                                                              : 0.0f;
            // Anything else the core asks about here is an ordinary button,
            // which is pressed or it is not.
            if (id != RETRO_DEVICE_ID_JOYPAD_L2 && id != RETRO_DEVICE_ID_JOYPAD_R2)
                return ((pad.buttons >> id) & 1u) ? 32767 : 0;
            return static_cast<int16_t>(std::clamp(t, 0.0f, 1.0f) * 32767.0f);
        }
        // Sticks. Only the two indices that ARE sticks, and only the two ids
        // that are axes — the old fall-through is what caused the above.
        if (index != RETRO_DEVICE_INDEX_ANALOG_LEFT &&
            index != RETRO_DEVICE_INDEX_ANALOG_RIGHT)
            return 0;
        if (id != RETRO_DEVICE_ID_ANALOG_X && id != RETRO_DEVICE_ID_ANALOG_Y) return 0;
        const float v = (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
                            ? (id == RETRO_DEVICE_ID_ANALOG_X ? pad.leftX : pad.leftY)
                            : (id == RETRO_DEVICE_ID_ANALOG_X ? pad.rightX : pad.rightY);
        return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    }
    return 0;
}

// Cores are chatty at INFO, so warnings and above are what reaches the log.
//
// RAISING THIS WAS TRIED AND IT BREAKS PPSSPP. A flag that let everything
// through was built to read one INFO line the core prints — and with it on, the
// process aborts at exit with "terminate called without an active exception",
// reproducibly, twice, while the identical run without it exits 0. The cause is
// not established; the flag is gone, and the one line worth having says itself
// at WARN instead (cores/build-core.sh's cpu-engine probe).
void logCallback(enum retro_log_level level, const char* fmt, ...) {
    if (level < RETRO_LOG_WARN) return;
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    std::fprintf(stderr, "[core] %s", buf);
}

bool environment(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            // Yes: a core may hand back a null frame meaning "unchanged", and
            // the draw loop keeps presenting the last one.
            *static_cast<bool*>(data) = true;
            return true;

        case RETRO_ENVIRONMENT_SET_ROTATION: {
            // 0, 1, 2, 3 — 90 degrees counter-clockwise each. Answered rather
            // than refused: a core that is told no is entitled to carry on
            // drawing sideways, and several do.
            const unsigned r = *static_cast<const unsigned*>(data);
            if (r > 3) return false;
            gRotation = r;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT: {
            const unsigned fmt = *static_cast<const enum retro_pixel_format*>(data);
            if (fmt > RETRO_PIXEL_FORMAT_RGB565) return false;
            gPixelFormat = fmt;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
            *static_cast<const char**>(data) = gSystemDir.c_str();
            return true;

        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            // Must outlive the session. See the header.
            *static_cast<const char**>(data) = gSaveDir.c_str();
            return true;

        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            static_cast<retro_log_callback*>(data)->log = logCallback;
            return true;

        case RETRO_ENVIRONMENT_GET_VARIABLE: {
            auto* var = static_cast<retro_variable*>(data);
            const std::string key = var->key ? var->key : "";
            if (!gAnswerOptions) {
                // The control: the old behaviour, where every option went
                // unanswered and every core silently ran on zeroes.
                var->value = nullptr;
                return false;
            }
            auto it = gByKey.find(key);
            if (it == gByKey.end()) {
                // A key the core never declared. There is nothing honest to
                // answer with — we do not know its values, let alone its
                // default — so it is recorded and reported rather than guessed
                // at. This is the one case that stays unanswered, and it is a
                // bug in the core or a table we failed to read.
                var->value = nullptr;
                if (!key.empty() &&
                    std::find(gUndeclaredAsks.begin(), gUndeclaredAsks.end(), key) ==
                        gUndeclaredAsks.end()) {
                    gUndeclaredAsks.push_back(key);
                    std::fprintf(stderr, "[core] asked for an option it never "
                                         "declared: %s\n", key.c_str());
                }
                return false;
            }
            Option& o = gDeclared[it->second];
            o.asked = true;
            var->value = o.chosen.c_str();
            return true;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = false;
            return true;

        // The core telling us what it can be configured with. Whichever
        // generation of the API it uses, the table is captured and every key in
        // it gets an answer — see the note above `struct Option`.
        case RETRO_ENVIRONMENT_SET_VARIABLES:
            captureVariables(static_cast<const retro_variable*>(data));
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            captureDefinitions(
                static_cast<const retro_core_option_definition*>(data));
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
            // The US table is the one carrying the keys; `local` is the same
            // table translated, and may be absent.
            if (auto* intl = static_cast<const retro_core_options_intl*>(data))
                captureDefinitions(intl->us);
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
            captureV2(static_cast<const retro_core_options_v2*>(data));
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
            if (auto* intl = static_cast<const retro_core_options_v2_intl*>(data))
                captureV2(intl->us);
            return true;

        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO: {
            // Acknowledged before, and the data thrown away. It is the only
            // place a core says how many ports it has, and loadGame has to
            // know: a core that is told about some of its ports and not the
            // rest can sit waiting for the rest forever. The array is
            // terminated by an entry with no types.
            gCorePorts = 0;
            if (const auto* info = static_cast<const retro_controller_info*>(data))
                while (info[gCorePorts].types || info[gCorePorts].num_types) ++gCorePorts;
            return true;
        }

        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS: {
            // WHAT EACH BUTTON ACTUALLY DOES IN THIS GAME, IN THE CORE'S OWN
            // WORDS. Accepted and thrown away until 2026-09-19, and throwing
            // it away is what made "the shoulder buttons do not work" an
            // unanswerable question: a RetroPad has sixteen ids, a real
            // machine has fewer, and the ones a core does not use are silent
            // by design rather than broken. The Dreamcast pad, for one, has no
            // shoulder BUTTONS at all — its L and R are analogue triggers —
            // so a press that does nothing is the correct behaviour and looks
            // identical to a bug.
            //
            // Printed once per game, port 0 only, and only for the joypad:
            // Flycast declares four ports and a mouse and a lightgun besides,
            // and a wall of those is not a diagnostic.
            if (gInputDescriptorsLogged) return true;
            const auto* d = static_cast<const retro_input_descriptor*>(data);
            if (!d) return true;
            std::string line;
            for (; d->description; ++d) {
                if (d->port != 0 || d->device != RETRO_DEVICE_JOYPAD) continue;
                if (d->index != 0) continue;
                const char* name = padIdName(d->id);
                if (!name) continue;
                if (!line.empty()) line += ", ";
                line += name;
                line += "=";
                line += d->description;
            }
            // The flag is set only once something was actually printed. A core
            // is free to call this more than once and to describe other ports
            // first, and latching on a call that said nothing about port 0
            // would silently throw away the one that does.
            if (line.empty()) return true;
            gInputDescriptorsLogged = true;
            std::fprintf(stderr, "[input] port 0: %s\n", line.c_str());
            // Say the silent ones too, because that is the half that answers
            // the question somebody actually asked.
            std::string unused;
            for (unsigned id = 0; id <= RETRO_DEVICE_ID_JOYPAD_R3; ++id) {
                const char* name = padIdName(id);
                if (!name) continue;
                bool found = false;
                for (const auto* e = static_cast<const retro_input_descriptor*>(data);
                     e->description; ++e) {
                    if (e->port == 0 && e->device == RETRO_DEVICE_JOYPAD &&
                        e->index == 0 && e->id == id) {
                        found = true;
                        break;
                    }
                }
                if (found) continue;
                if (!unused.empty()) unused += ", ";
                unused += name;
            }
            if (!unused.empty())
                std::fprintf(stderr, "[input] port 0 does nothing in this game: %s\n",
                             unused.c_str());
            return true;
        }

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return true;

        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            // Version 2, not 0. A core told version 0 falls back to the
            // original API, where the default is "whichever value is listed
            // first" — a convention we would be inferring. At version 2 the
            // core states its default outright, which is a fact rather than an
            // inference, and every core that only speaks the old API still
            // works because it calls SET_VARIABLES regardless.
            *static_cast<unsigned*>(data) = 2;
            return true;

        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
            return true;

        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;

        case RETRO_ENVIRONMENT_SET_HW_RENDER: {
            // Flycast, Mupen64Plus and PPSSPP. See the block above inputPoll
            // for what is handed over and why it is a framebuffer rather than
            // a context.
            auto* cb = static_cast<retro_hw_render_callback*>(data);
            if (!cb) return false;
            gHWContextName = contextName(*cb);
            if (gRefuseHWRender) {
                std::fprintf(stderr,
                             "[core] refusing %s because --core-no-hw-render was "
                             "asked for; this is the control, not a fault\n",
                             gHWContextName.c_str());
                gHWContextName += " (refused: the control)";
                return false;
            }
            if (!canServe(*cb)) {
                // Named, not just refused. "Hardware rendering unavailable" is
                // the kind of message that sends somebody looking at the core;
                // the version is what tells them the frontend's context is the
                // thing to change.
                unsigned haveMajor = 3, haveMinor = 0;
                contextVersion(haveMajor, haveMinor);
                std::fprintf(stderr,
                             "[core] asked for %s; this frontend holds an OpenGL "
                             "ES %u.%u context and will not pretend otherwise\n",
                             gHWContextName.c_str(), haveMajor, haveMinor);
                gHWContextName += " (refused)";
                return false;
            }
            gHWVulkan = cb->context_type == RETRO_HW_CONTEXT_VULKAN;
            if (!gHWVulkan) {
                // The core keeps this struct and calls through it, so the two
                // frontend-owned fields are written into the CORE's copy. Ours
                // is for context_reset, context_destroy and the depth/stencil
                // flags, which are read after the core has stopped looking at
                // it.
                //
                // NEITHER FIELD MEANS ANYTHING IN VULKAN. There is no
                // framebuffer object to name and no context to load symbols
                // out of; the core gets the device and the queue through
                // GET_HW_RENDER_INTERFACE instead. Filling them in anyway
                // would hand a Vulkan core two GL function pointers.
                cb->get_current_framebuffer = currentFramebuffer;
                cb->get_proc_address = procAddress;
            }
            gHW = *cb;
            gHWWanted = true;
            std::fprintf(stderr, "[core] hardware rendering: %s, %s origin\n",
                         gHWContextName.c_str(),
                         cb->bottom_left_origin ? "bottom-left" : "top-left");
            return true;
        }

        case RETRO_ENVIRONMENT_GET_PREFERRED_HW_RENDER:
            // Worth answering rather than refusing: the cores that can render
            // through more than one API ask this before choosing, and an
            // unanswered ask means the core picks whatever its build defaults
            // to — which on Linux is usually desktop GL, which this context
            // cannot serve. So this is the difference between Flycast asking
            // for something serveable and Flycast asking for something that
            // has to be turned down.
            //
            // THIS ONE LINE IS WHAT DECIDES WHICH API A CORE USES, and it is
            // the "advertise whichever it got" half of docs/PROJECT.md open
            // question 20. It was hard-wired to GLES, and that is why Dolphin
            // spent fifty seconds of emulated Mario Kart drawing into a
            // context its own render thread could not reach: the core asked
            // what we preferred, was told OpenGL ES, and dutifully took it —
            // and never asked for the Vulkan it also has compiled in.
            //
            // A core that cannot do Vulkan is unaffected: it ignores an answer
            // it has no path for and asks for GLES anyway, which canServe
            // still accepts. Nothing about the twenty-one cores changes.
            *static_cast<unsigned*>(data) =
                vk::available() ? RETRO_HW_CONTEXT_VULKAN : RETRO_HW_CONTEXT_OPENGLES3;
            return true;

        case RETRO_ENVIRONMENT_SET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE: {
            // How a Vulkan core says which physical device it needs and which
            // extensions must be on it. It can arrive either side of
            // SET_HW_RENDER, so it is stored and acted on when the device is
            // built. See vkhost.h.
            auto* n = static_cast<const retro_hw_render_context_negotiation_interface*>(data);
            if (!n || n->interface_type !=
                          RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
                return false;
            vk::setNegotiation(
                reinterpret_cast<const retro_hw_render_context_negotiation_interface_vulkan*>(n));
            return true;
        }

        case RETRO_ENVIRONMENT_GET_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_SUPPORT: {
            auto* s = static_cast<retro_hw_render_context_negotiation_interface*>(data);
            if (!s || s->interface_type !=
                          RETRO_HW_RENDER_CONTEXT_NEGOTIATION_INTERFACE_VULKAN)
                return false;
            // Version 1. The v2 entry points hand the frontend a wrapper the
            // core calls to create the instance and device, so that the
            // frontend can add its own extensions to what the core asked for.
            // This host does not need that yet — it passes its three required
            // extensions straight to create_device — and claiming a version
            // whose contract is not implemented is how a core ends up calling
            // a null wrapper.
            s->interface_version = 1;
            return true;
        }

        case RETRO_ENVIRONMENT_GET_HW_RENDER_INTERFACE: {
            // Logged because a core that cannot get this has no way to present
            // and no way to say so — it simply runs and draws nothing, which
            // is the single hardest failure in this area to tell apart from a
            // broken game.
            std::fprintf(stderr, "[vulkan] the core asked for the render interface: %s\n",
                         !gHWVulkan            ? "refused, this core is not on Vulkan"
                         : !vk::renderInterface() ? "refused, there is no device yet"
                                                  : "handed over");
            if (!gHWVulkan) return false;
            const retro_hw_render_interface_vulkan* iface = vk::renderInterface();
            if (!iface) return false;
            *static_cast<const retro_hw_render_interface**>(data) =
                reinterpret_cast<const retro_hw_render_interface*>(iface);
            return true;
        }

        case RETRO_ENVIRONMENT_SET_HW_SHARED_CONTEXT:
            // No. A shared context is for a core that renders from its own
            // thread, and this frontend drives retro_run from the thread that
            // owns the context. Flycast's threaded renderer asks; refused, it
            // submits from the run thread, which is the arrangement the frame
            // pacing in runFor is built on.
            return false;

        default:
            return false;
    }
}

}  // namespace

Core& Core::shared() {
    static Core instance;
    return instance;
}

void* Core::symbol(const char* name) {
    dlerror();  // clear, so the next read belongs to this lookup
    void* s = dlsym(handle_, name);
    if (!s) {
        const char* why = dlerror();
        error_ = std::string("missing symbol ") + name + (why ? std::string(": ") + why : "");
    }
    return s;
}

namespace {
// Cores get ABSOLUTE directories, always, and they are created before the core
// is told about them.
//
// The frontend names both relative to where it runs, which is convenient here
// and wrong to hand over. A core is entitled to do more with these than fopen
// them: PPSSPP wraps them in a path type of its own that asks whether a path is
// absolute and behaves differently when it is not, and handed "romcache/saves"
// it mounted the memory stick somewhere it could not write — the game ran, the
// save failed, and the only sign was a line in the core's own log saying
// "Error writing file ms0:/PSP/SAVEDATA/...". That is the melonDS lesson
// arriving a second time by a different route, so it is fixed once, here,
// rather than per core.
//
// The mkdir matters as much as the absolute path: a core that is handed a
// directory which does not exist yet has nowhere to put a save, and finds that
// out silently.
std::string absoluteDir(const std::string& dir) {
    if (dir.empty()) return dir;
    std::string abs = dir;
    if (abs.front() != '/') {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) abs = std::string(cwd) + "/" + abs;
    }
    SDL_CreateDirectory(abs.c_str());
    return abs;
}
}  // namespace

void Core::setDirectories(const std::string& systemDir, const std::string& saveDir) {
    gSystemDir = absoluteDir(systemDir);
    gSaveDir = absoluteDir(saveDir);
}

// PLAYSTATION 2 IS NOT A LIBRETRO CORE, AND THESE FEW BRANCHES ARE THE WHOLE
// PRICE OF THAT.
//
// PCSX2 is a complete emulator that runs its own machine on its own thread and
// does not return until the game stops. The frame loop therefore cannot step
// it, which is the one thing every other emulator here has in common. What it
// CAN do is take the finished picture, take the finished sound, hand over the
// pad and stop it — and those are the same four things this class already does
// for the other twenty-one.
//
// So the branch lives here, at the bottom, rather than as a second Core the
// screens have to know about. Everything above this file — the launch screen,
// the overlay, the save sync, the frame loop in main.cpp — is unchanged and
// does not learn a new concept. See ps2.h.
static bool gIsPs2 = false;
static std::string gPs2ResourcesDir;
static float gPs2Upscale = 1.0f;
static int gPs2Anisotropy = 0;

bool Core::load(const std::string& soPath) {
    unload();

    // The emulator is recognised by its filename, the same way every core is.
    // catalog::coreFileName produces this for the `ps2` platform.
    if (soPath.size() >= 16 &&
        soPath.compare(soPath.size() - 16, 16, "cabinetos-ps2.so") == 0) {
        if (!ps2::load(soPath)) {
            error_ = ps2::error();
            return false;
        }
        gIsPs2 = true;
        handle_ = reinterpret_cast<void*>(1); // loaded() is asked about by name everywhere
        return true;
    }
    gIsPs2 = false;
    // Whatever the last core asked for is not this one's business either.
    gHW = {};
    gHWWanted = false;
    gHWVulkan = false;
    gHWFrame = false;
    gHWContextName.clear();
    // Whatever the last core declared is not this one's business. Cleared here
    // rather than in unload() so a caller that inspects the table after a core
    // is unloaded still sees what that core actually asked for.
    resetOptions();
    // RTLD_LOCAL is the whole reason this is simple on Linux: each core's
    // retro_* symbols stay private to it, so many cores can be loaded over a
    // session without colliding, and none of Cabinet's prefix-and-merge
    // machinery is needed.
    handle_ = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle_) {
        // Read dlerror() ONCE. It clears itself on read, so calling it twice
        // returns null the second time — and assigning that null to a
        // std::string is a segfault, which is how a perfectly clear "file not
        // found" turned into a crash with no message at all.
        const char* why = dlerror();
        error_ = why ? why : "dlopen failed and gave no reason";
        return false;
    }

    g.api_version = reinterpret_cast<unsigned (*)(void)>(symbol("retro_api_version"));
    g.get_system_info =
        reinterpret_cast<void (*)(retro_system_info*)>(symbol("retro_get_system_info"));
    g.get_system_av_info = reinterpret_cast<void (*)(retro_system_av_info*)>(
        symbol("retro_get_system_av_info"));
    g.set_environment =
        reinterpret_cast<void (*)(retro_environment_t)>(symbol("retro_set_environment"));
    g.set_video_refresh = reinterpret_cast<void (*)(retro_video_refresh_t)>(
        symbol("retro_set_video_refresh"));
    g.set_audio_sample =
        reinterpret_cast<void (*)(retro_audio_sample_t)>(symbol("retro_set_audio_sample"));
    g.set_audio_sample_batch = reinterpret_cast<void (*)(retro_audio_sample_batch_t)>(
        symbol("retro_set_audio_sample_batch"));
    g.set_input_poll =
        reinterpret_cast<void (*)(retro_input_poll_t)>(symbol("retro_set_input_poll"));
    g.set_input_state =
        reinterpret_cast<void (*)(retro_input_state_t)>(symbol("retro_set_input_state"));
    g.set_controller_port_device = reinterpret_cast<void (*)(unsigned, unsigned)>(
        symbol("retro_set_controller_port_device"));
    g.init = reinterpret_cast<void (*)(void)>(symbol("retro_init"));
    g.deinit = reinterpret_cast<void (*)(void)>(symbol("retro_deinit"));
    g.load_game = reinterpret_cast<bool (*)(const retro_game_info*)>(symbol("retro_load_game"));
    g.unload_game = reinterpret_cast<void (*)(void)>(symbol("retro_unload_game"));
    g.run = reinterpret_cast<void (*)(void)>(symbol("retro_run"));
    g.reset = reinterpret_cast<void (*)(void)>(symbol("retro_reset"));
    g.serialize_size = reinterpret_cast<size_t (*)(void)>(symbol("retro_serialize_size"));
    g.serialize = reinterpret_cast<bool (*)(void*, size_t)>(symbol("retro_serialize"));
    g.unserialize =
        reinterpret_cast<bool (*)(const void*, size_t)>(symbol("retro_unserialize"));
    g.get_memory_data =
        reinterpret_cast<void* (*)(unsigned)>(symbol("retro_get_memory_data"));
    g.get_memory_size =
        reinterpret_cast<size_t (*)(unsigned)>(symbol("retro_get_memory_size"));

    if (!g.api_version || !g.init || !g.run || !g.load_game) {
        unload();
        return false;
    }

    const unsigned api = g.api_version();
    if (api != RETRO_API_VERSION) {
        char buf[128];
        snprintf(buf, sizeof(buf), "core speaks libretro %u, this frontend speaks %u", api,
                 RETRO_API_VERSION);
        error_ = buf;
        unload();
        return false;
    }

    retro_system_info info{};
    g.get_system_info(&info);
    coreName_ = info.library_name ? info.library_name : "?";
    validExtensions_ = info.valid_extensions ? info.valid_extensions : "";
    blockExtract_ = info.block_extract;
    needFullpath_ = info.need_fullpath;
    coreVersion_ = info.library_version ? info.library_version : "?";

    // One retro_run is one emulated frame for every core here but this one.
    //
    // PPSSPP with a GL context runs its emulation on a thread of its own and
    // uses retro_run to consume ONE SWAP from it — and a swap is one GAME
    // frame, not one vblank. A 30fps PSP game flips every other vblank, so
    // pacing it at the 59.94 the core reports runs it at twice speed, with the
    // audio to match. Cabinet measured exactly 2.0x on an Apple TV, on
    // Lumines: 120 emulated vblanks a second against 60 swaps.
    //
    // Frame pacing alone cannot see this, because the accumulator counts the
    // runs it asked for rather than the emulated time they produced. What can
    // see it is the audio, which is the one thing the core emits at a rate the
    // emulated machine decides — so the brake is audioAhead() below.
    //
    // Named rather than taken from the hardware-render flag, because the other
    // two hardware-rendered cores do not need it and one of them is measurably
    // worse for having it: applying Cabinet's governor to every core slowed N64
    // down, reported from a real device within hours.
    governed_ = coreName_ == "PPSSPP";

    // Order matters: the environment callback must be installed before
    // retro_init, because a core may call it from there.
    g.set_environment(environment);
    g.init();
    g.set_video_refresh(videoRefresh);
    g.set_audio_sample(audioSample);
    g.set_audio_sample_batch(audioSampleBatch);
    g.set_input_poll(inputPoll);
    g.set_input_state(inputState);

    std::fprintf(stderr, "[core] %s %s (libretro %u)\n", coreName_.c_str(),
                 coreVersion_.c_str(), api);
    return true;
}

void Core::setOptionOverrides(const std::map<std::string, std::string>& overrides) {
    gOverrides.clear();
    for (const auto& [k, v] : overrides) gOverrides[k] = v;
    // Re-apply to anything already declared, so setting these after a load is
    // not silently a no-op. Before load() is still the correct time to call it,
    // because a core may read its options inside retro_init.
    for (Option& o : gDeclared) {
        auto it = gOverrides.find(o.key);
        o.overridden = (it != gOverrides.end());
        o.chosen = o.overridden ? it->second : o.defaultValue;
    }
}

std::vector<Core::OptionReport> Core::options() const {
    std::vector<OptionReport> out;
    out.reserve(gDeclared.size());
    for (const Option& o : gDeclared)
        out.push_back({o.key, o.desc, o.values, o.defaultValue, o.chosen,
                       o.overridden, o.asked});
    return out;
}

std::vector<std::string> Core::undeclaredOptionAsks() const {
    return gUndeclaredAsks;
}

void Core::setAnswerOptions(bool on) { gAnswerOptions = on; }
void Core::setRefuseHWRender(bool on) { gRefuseHWRender = on; }


void Core::unload() {
    if (!handle_) return;
    if (gameLoaded_) unloadGame();
    if (gIsPs2) {
        ps2::unload();
        gIsPs2 = false;
        handle_ = nullptr;
        if (texture_) { glDeleteTextures(1, &texture_); texture_ = 0; }
        return;
    }
    if (g.deinit) g.deinit();
    dlclose(handle_);
    handle_ = nullptr;
    g = {};
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    destroyHWTarget();
    if (gHWVulkan) vk::destroyContext();
    gHWWanted = false;
    gHWVulkan = false;
    gHWFrame = false;
}

bool Core::loadGame(const std::string& romPath, const std::string& systemDir,
                    const std::string& saveDir) {
    if (!handle_) {
        error_ = "no core loaded";
        return false;
    }
    if (gIsPs2) {
        // THE CARD IS NAMED FROM THE ROM, HERE, BY THE SAME RULE main.cpp USES
        // FOR EVERY OTHER SAVE. `catalog::saveFiles` says a PlayStation 2 card
        // is `<stem>.ps2` in the save directory, where the stem is the rom's
        // filename without its extension — and `filesave.cpp` already restores
        // that file before launch, captures it after, refuses to upload an
        // unformatted one, and files it on the server under the name Cabinet's
        // Mac uses.
        //
        // Deriving it here rather than being told means the two cannot drift.
        // A card PCSX2 wrote under a name the save layer does not look for is
        // a save that exists on disk and never reaches the server, and nothing
        // would say so.
        std::string stem = romPath;
        if (const size_t slash = stem.find_last_of('/'); slash != std::string::npos)
            stem.erase(0, slash + 1);
        if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos)
            stem.erase(dot);

        // Firmware is shared and comes from RomM; the card is this person's and
        // this game's; the scratch is throwaway. Three places, deliberately.
        // `bios/pcsx2/bios`, and the repetition is not a mistake. The outer
        // one is this console's firmware directory for every system; the
        // `pcsx2` folder is where the PlayStation 2's own BIOS files sit,
        // which is where the save work already put them; and PCSX2 is handed
        // that directory DIRECTLY rather than a root it appends `bios` to.
        // Getting it one segment short produced a VM that started and was
        // destroyed in the same twelve milliseconds, saying only "Searching
        // for a BIOS image in ..." with no error after it.
        const std::string biosDir = systemDir + "/pcsx2/bios";
        const std::string scratchDir = saveDir + "/pcsx2-scratch";

        if (!ps2::startGame(romPath, biosDir, saveDir, stem + ".ps2", scratchDir,
                            gPs2ResourcesDir, gPs2Upscale, gPs2Anisotropy)) {
            error_ = ps2::error();
            return false;
        }
        gameLoaded_ = true;
        gFramesRun = 0;
        gAudioFrames = 0;
        frameWidth_ = 0;
        frameHeight_ = 0;
        // av_ is what the rest of the frontend reads for display. PCSX2 paces
        // ITSELF, so nothing here drives the emulator; the 60 is the NTSC
        // figure and a PAL disc simply runs at its own rate underneath.
        av_.fps = 60.0;
        av_.sampleRate = 48000.0;
        return true;
    }
    gSystemDir = absoluteDir(systemDir);
    gSaveDir = absoluteDir(saveDir);

    // Cleared HERE, not in load(), because rotation is a fact about the GAME
    // and not about the core. One arcade emulator serves upright and vertical
    // boards out of the same .so, and it calls SET_ROTATION from inside
    // retro_load_game — so a vertical game followed by an upright one that
    // never calls it at all would otherwise leave the upright one sideways.
    gRotation = 0;
    gRotationAnnounced = false;
    gInputDescriptorsLogged = false;

    // need_fullpath means the core opens the file ITSELF, and libretro is
    // explicit that the frontend must then not load it. This used to read it
    // anyway, which was invisible while the cores that ask for it were handed
    // small files — and stops being invisible at PSP, where The Warriors is
    // 1.8 GB on a machine with 4 GB of memory. The bytes were allocated, read,
    // and then ignored by the core.
    //
    // The file is still checked, because "the core refused it" and "there is
    // no file there" are different failures and only one of them is the core's.
    std::vector<uint8_t> rom;
    size_t romSize = 0;
    if (FILE* f = std::fopen(romPath.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) {
            romSize = static_cast<size_t>(n);
            if (!needFullpath_) {
                rom.resize(romSize);
                if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) rom.clear();
            }
        }
        std::fclose(f);
    }
    if (romSize == 0 || (!needFullpath_ && rom.empty())) {
        error_ = "cannot read " + romPath;
        return false;
    }

    retro_game_info info{};
    info.path = romPath.c_str();
    info.data = needFullpath_ ? nullptr : rom.data();
    info.size = needFullpath_ ? 0 : rom.size();

    if (!g.load_game(&info)) {
        // Deliberately does not claim the core is wrong, because nothing here
        // knows that: a refusal looks identical for a missing BIOS, an
        // incomplete set, or a genuinely bad file.
        error_ = "the core refused " + romPath;
        return false;
    }
    gameLoaded_ = true;

    // TELL THE CORE WHAT IS PLUGGED IN, and this was never being said.
    //
    // libretro's own documentation says a joypad is assumed on every port, and
    // it is a poor assumption to lean on: a core is free to build its idea of
    // the machine out of these calls and Flycast does exactly that. It
    // constructs the Dreamcast's Maple bus from `retro_set_controller_port_device`,
    // so a port nothing was ever said about carries no controller — and a VMU
    // lives in a CONTROLLER'S expansion socket, not in the console. With no
    // call, Ikaruga opens on "memory card not connected" however good the card
    // sitting on the disk is.
    //
    // That symptom has been in this project's notes for days, filed as a
    // curiosity of the file-writing save class. It is not: the save work put
    // a real 128 KB card in exactly the right place and the game still said
    // the slot was empty, which is what made it worth looking at the call
    // nobody was making. Found 2026-09-19 by photographing the screen rather
    // than by reading the log, which said nothing at all.
    //
    // EVERY PORT THE CORE HAS, not just the ones this console drives, and that
    // is the half that took the longest to find. Flycast will not act on any
    // of these calls until it has heard about ALL FOUR of its ports — it
    // returns early while any port is still unset, so the code that reads
    // `device_port1_slot1` and puts a VMU in the controller's expansion socket
    // never runs. Telling it about one port is the same as telling it about
    // none.
    //
    // ONE PAD, HONESTLY. This frontend drives port 0 and nothing else — see
    // setPad — so port 0 is a joypad and the rest are empty. Saying every port
    // has a controller would have Flycast create four memory cards for a
    // console with one player, three of which nothing would ever sync and all
    // of which would sit in the system directory.
    if (g.set_controller_port_device) {
        const unsigned ports = std::max(gCorePorts, 1u);
        g.set_controller_port_device(0, RETRO_DEVICE_JOYPAD);
        for (unsigned p = 1; p < ports; ++p)
            g.set_controller_port_device(p, RETRO_DEVICE_NONE);
    }

    retro_system_av_info av{};
    g.get_system_av_info(&av);
    av_.fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
    av_.sampleRate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100.0;
    av_.baseWidth = av.geometry.base_width;
    av_.baseHeight = av.geometry.base_height;
    av_.maxWidth = av.geometry.max_width;
    av_.maxHeight = av.geometry.max_height;
    av_.aspectRatio = av.geometry.aspect_ratio;

    if (gHWWanted) {
        // The target can only be built now: its size is the maximum geometry,
        // and that is not known until the game is loaded. Cores ask for the
        // context during retro_load_game, so this is the first moment both
        // facts exist.
        const unsigned w = std::max(av_.maxWidth, av_.baseWidth);
        const unsigned h = std::max(av_.maxHeight, av_.baseHeight);
        if (gHWVulkan) {
            // NOTHING IS ALLOCATED HERE FOR THE PICTURE, and that is the
            // difference. A Vulkan core renders into images it owns and hands
            // one over per frame, so what this builds is the device, the queue
            // and the one exported image the picture crosses on — sized later,
            // from the frame the core actually produces, rather than from its
            // declared maximum.
            std::string verr;
            if (!vk::createContext(&verr)) {
                error_ = "this core wants Vulkan and " + verr;
                g.unload_game();
                gameLoaded_ = false;
                return false;
            }
        } else if (!ensureHWTarget(w, h)) {
            error_ = "the core needs a render target this context cannot build";
            g.unload_game();
            gameLoaded_ = false;
            return false;
        }
        // The core's GL resources do not exist until it is told the context is
        // ready, and it must be told before the first retro_run.
        if (gHW.context_reset) gHW.context_reset();
        gHWContextLive = true;
        // context_reset builds shaders and buffers, and leaves them bound.
        restoreGLState();
    }

    accumulator_ = 0.0;
    paceClock_ = 0.0;
    governorSkips_ = 0;
    gFramesRun = 0;
    gAudioFrames = 0;
    gAudio.clear();

    std::fprintf(stderr, "[core] loaded %s\n", romPath.c_str());
    std::fprintf(stderr, "[core] %ux%u, %.4f fps, %.0f Hz, aspect %.4f\n", av_.baseWidth,
                 av_.baseHeight, av_.fps, av_.sampleRate, av_.aspectRatio);
    return true;
}

void Core::unloadGame() {
    if (gIsPs2) {
        if (!gameLoaded_) return;
        // BLOCKS until PCSX2 has actually stopped, which is what makes the
        // memory card capture that follows meaningful: the card is flushed
        // during shutdown, so anything read before this returns is the card as
        // it was when the game started.
        ps2::stopGame();
        gameLoaded_ = false;
        return;
    }
    if (!gameLoaded_) return;
    // The core frees its GL resources here, and it happens BEFORE the game is
    // unloaded — which is the order RetroArch uses, and it is not arbitrary: a
    // core's context_destroy walks structures that retro_unload_game is about
    // to tear down.
    if (gHWContextLive) {
        if (gHW.context_destroy) gHW.context_destroy();
        gHWContextLive = false;
        restoreGLState();
    }
    // This is the one moment a file-writing core flushes its save. RetroArch
    // does exactly this, in this order, and the reference implementation lost
    // saves for months by unloading lazily at the NEXT launch instead.
    if (g.unload_game) g.unload_game();
    gameLoaded_ = false;
    gHWFrame = false;
    destroyHWTarget();
    // AFTER unload_game, for the same reason context_destroy runs before it:
    // the core's own Vulkan objects live on this device, and pulling the
    // device out from under a core that has not finished tearing down is a
    // crash inside the driver with no useful backtrace.
    if (gHWVulkan) vk::destroyContext();
}

double Core::audioAhead() const {
    if (av_.sampleRate <= 0) return 0.0;
    return static_cast<double>(gAudioFrames) / av_.sampleRate - paceClock_;
}

int Core::runFor(double dt) {
    if (!gameLoaded_) return 0;
    if (gIsPs2) {
        // NOTHING TO STEP, AND THAT IS NOT A GAP. PCSX2 runs its own machine
        // on its own thread at its own pace, with its own frame limiter — so
        // there is no "advance one frame" to call and no governor to run. The
        // frame counter follows what actually arrived, which is what the rest
        // of the frontend reads.
        //
        // The one thing that IS still this function's job on every other core
        // — not stepping while the overlay is open — is handled by the caller
        // simply not calling it, and for PlayStation 2 that is not enough: a
        // thread does not stop because nobody asked it to. Pausing is wired
        // separately, through setPaused.
        (void)dt;
        return 1;
    }

    const double interval = 1.0 / std::max(av_.fps, 1.0);
    accumulator_ += dt;
    // Anything beyond a couple of frames behind is time that is simply gone.
    // Letting it accumulate would make the core sprint to catch up, which
    // stutters the picture and floods the audio buffer.
    if (accumulator_ > interval * 4) accumulator_ = interval;
    // The clock the governor measures against. It is whatever clock runFor is
    // driven by, which is the wall clock in the product and a synthetic one
    // frame per draw under --screenshot — so the RELATIONSHIP the governor
    // enforces holds in both, and a capture of a 30fps PSP game advances it at
    // 30 game frames per 60 drawn rather than sprinting.
    paceClock_ += dt;

    int ran = 0;
    while (accumulator_ >= interval && ran < 2) {
        // The second brake, and only one core has it. See load(): the
        // accumulator counts the runs that were asked for, and for PPSSPP a
        // run is a game frame rather than a vblank, so the accumulator can be
        // satisfied while the emulated machine is running at twice speed.
        //
        // The core's own audio output is the check, because its rate is
        // decided by the emulated machine rather than by us. Ahead of the
        // clock by more than the cushion and the core is simply not due,
        // whatever the accumulator says.
        //
        // THE CUSHION IS FELT LATENCY. The lead it permits is input lag, at
        // 10 ms per hundredth of a second. Cabinet shipped 50 ms, had it
        // reported as bad input lag on Dreamcast, and settled on 20 ms. This
        // is that number, inherited rather than measured — the display path
        // here is different and it is a thing to tune on the SER5 with a pad
        // in hand, not on a software-rendered VM.
        if (governed_ && audioAhead() > 0.020) {
            ++governorSkips_;
            break;
        }
        if (gHWWanted && gHWFBO) {
            // Bound before the core runs as well as answered on request. Cores
            // differ about when they ask for the framebuffer, and one that
            // draws before asking would otherwise land on the window.
            glBindFramebuffer(GL_FRAMEBUFFER, gHWFBO);
            glViewport(0, 0, static_cast<GLsizei>(gHWTargetW),
                       static_cast<GLsizei>(gHWTargetH));
        }
        g.run();
        // Only the GLES path disturbs our GL state. A Vulkan core has not
        // touched the context at all, and calling this for it would be a
        // handful of redundant GL calls per frame for nothing.
        if (gHWWanted && !gHWVulkan) restoreGLState();
        ++gFramesRun;
        ++ran;
        accumulator_ -= interval;
    }
    return ran;
}

bool Core::uploadFrame() {
    if (gIsPs2) {
        const uint32_t* pixels = nullptr;
        unsigned w = 0, h = 0;
        if (!ps2::takeFrame(&pixels, w, h)) {
            // Nothing new. The last frame stays uploaded, which is what makes
            // a paused game sit under the overlay rather than going black.
            return texture_ != 0;
        }
        if (!texture_) {
            glGenTextures(1, &texture_);
            glBindTexture(GL_TEXTURE_2D, texture_);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        } else {
            glBindTexture(GL_TEXTURE_2D, texture_);
        }
        // Reallocated only when the shape changes. It does change mid-game:
        // Homura goes from 640x448 to 640x512 the moment it leaves its own
        // video-mode menu, and a PAL game switching modes is ordinary.
        if (w != frameWidth_ || h != frameHeight_) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                         0, GL_RGBA, GL_UNSIGNED_BYTE, pixels);
            frameWidth_ = w;
            frameHeight_ = h;
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h),
                            GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        }
        gFramesRun++;
        return true;
    }
    if (gHWVulkan) {
        // The one place the two APIs meet. present() copies whatever the core
        // last handed over into the exported image, which IS the GL texture —
        // same memory, no readback. It runs here rather than in the frame loop
        // because this is already the call every screen makes on the GL thread
        // before it draws a picture.
        frameWidth_ = gFrameW;
        frameHeight_ = gFrameH;
        gFrameDirty = false;
        vk::present();
        return vk::texture() != 0;
    }
    if (gHWFrame) {
        // Nothing to do, and that is the whole point of sharing one context:
        // the core has already drawn into the texture the player is about to
        // sample. No copy, no upload, no readback.
        frameWidth_ = gFrameW;
        frameHeight_ = gFrameH;
        gFrameDirty = false;
        return gHWColor != 0;
    }
    if (!gFrameDirty || gFrameW == 0 || gFrameH == 0) return texture_ != 0;
    gFrameDirty = false;

    if (!texture_) {
        glGenTextures(1, &texture_);
        glBindTexture(GL_TEXTURE_2D, texture_);
        // Nearest, deliberately. A Game Boy picture is 160x144 on a 4K panel
        // and every pixel is a deliberate choice by someone in 1989; smoothing
        // it is a decision the shader layer should make on purpose, not one
        // the upload makes by accident.
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        // A BRAND NEW TEXTURE HAS NO STORAGE, WHATEVER THE LAST GAME'S SIZE WAS.
        //
        // THIS IS THE SECOND-GAME BLACK SCREEN — found 2026-09-21, reported as
        // "black with audio" against 3DO, TurboGrafx and Dreamcast over several
        // days and recorded in docs/NEXT-SESSION.md as not reproducible.
        //
        // It is entirely reproducible once you know the rule: PLAY ONE GAME,
        // LEAVE IT, AND PLAY ANOTHER OF THE SAME PIXEL DIMENSIONS.
        //
        //   unload() deletes this texture and sets texture_ to 0. It does NOT
        //   reset frameWidth_/frameHeight_ — the only place those are cleared
        //   is the PlayStation 2 branch of loadGame, which no libretro core
        //   takes. So the next game arrives with the PREVIOUS game's size still
        //   recorded, `sizeChanged` comes out false, and the upload below calls
        //   glTexSubImage2D on a texture that has just been generated and never
        //   allocated. That is GL_INVALID_OPERATION: nothing is uploaded, the
        //   texture samples as black, and the core carries on producing perfect
        //   frames and perfectly good audio into a picture nobody can see.
        //
        // Which is why it looked like a per-game fault and was not: Air Zonk
        // then Devil's Crush are both 256x240 and the second is black; Air Zonk
        // then a SNES game is 256x240 then 256x224, `sizeChanged` is true, and
        // it works. The first game after a restart always works, because these
        // start at zero.
        //
        // Fixed here rather than in unload(), because this is the only place
        // that knows the texture has no storage — and it stays correct however
        // the texture came to be missing.
        frameWidth_ = 0;
        frameHeight_ = 0;
    }
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    const bool sizeChanged = (gFrameW != frameWidth_ || gFrameH != frameHeight_);
    frameWidth_ = gFrameW;
    frameHeight_ = gFrameH;

    if (gPixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
        // Uploaded as-is: libretro's RGB565 and GL_UNSIGNED_SHORT_5_6_5 pack
        // the channels the same way, so there is no conversion to do. The
        // pitch may exceed the width, which is what UNPACK_ROW_LENGTH is for.
        glPixelStorei(GL_UNPACK_ROW_LENGTH, static_cast<GLint>(gFramePitch / 2));
        if (sizeChanged) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB565, gFrameW, gFrameH, 0, GL_RGB,
                         GL_UNSIGNED_SHORT_5_6_5, gFrame.data());
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gFrameW, gFrameH, GL_RGB,
                            GL_UNSIGNED_SHORT_5_6_5, gFrame.data());
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    } else {
        // 0RGB1555 and XRGB8888 have no direct GLES equivalent, so they are
        // widened on the CPU. Both are rare in this core set and both are
        // small pictures; if a big one ever lands here, this becomes a shader.
        static std::vector<uint8_t> rgba;
        rgba.resize(static_cast<size_t>(gFrameW) * gFrameH * 4);
        for (unsigned y = 0; y < gFrameH; ++y) {
            const uint8_t* src = gFrame.data() + static_cast<size_t>(y) * gFramePitch;
            uint8_t* dst = rgba.data() + static_cast<size_t>(y) * gFrameW * 4;
            for (unsigned x = 0; x < gFrameW; ++x) {
                if (gPixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
                    const uint32_t p = reinterpret_cast<const uint32_t*>(src)[x];
                    dst[x * 4 + 0] = (p >> 16) & 0xFF;
                    dst[x * 4 + 1] = (p >> 8) & 0xFF;
                    dst[x * 4 + 2] = p & 0xFF;
                } else {
                    const uint16_t p = reinterpret_cast<const uint16_t*>(src)[x];
                    dst[x * 4 + 0] = ((p >> 10) & 0x1F) << 3;
                    dst[x * 4 + 1] = ((p >> 5) & 0x1F) << 3;
                    dst[x * 4 + 2] = (p & 0x1F) << 3;
                }
                dst[x * 4 + 3] = 255;
            }
        }
        if (sizeChanged) {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, gFrameW, gFrameH, 0, GL_RGBA,
                         GL_UNSIGNED_BYTE, rgba.data());
        } else {
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, gFrameW, gFrameH, GL_RGBA,
                            GL_UNSIGNED_BYTE, rgba.data());
        }
    }
    return true;
}

const std::vector<int16_t>& Core::drainAudio() {
    if (gIsPs2) {
        gAudioDrain.clear();
        ps2::drainAudio(gAudioDrain);
        // Counted here rather than in the emulator, because this is the number
        // `running()` is built on — audio is the one output that does not
        // appear until the machine actually runs.
        gAudioFrames += gAudioDrain.size() / 2;
        return gAudioDrain;
    }
    gAudioDrain.swap(gAudio);
    gAudio.clear();
    return gAudioDrain;
}

void Core::setPs2(const std::string& resourcesDir, float upscale, int anisotropy) {
    gPs2ResourcesDir = resourcesDir;
    gPs2Upscale = upscale > 0.0f ? upscale : 1.0f;
    gPs2Anisotropy = anisotropy;
}

void Core::setPaused(bool paused) {
    // Every libretro core stops because main.cpp stops calling runFor. Only
    // PCSX2 has a thread that needs telling.
    if (gIsPs2) ps2::setPaused(paused);
}

bool Core::isPs2() const { return gIsPs2; }

void Core::setPad(int port, const PadState& pad) {
    if (port < 0 || port >= kMaxPorts) return;
    gPads[port] = pad;
    if (gIsPs2) {
        ps2::setPad(port, pad.buttons, pad.leftX, pad.leftY, pad.rightX, pad.rightY,
                    pad.leftTrigger, pad.rightTrigger);
    }
}

size_t Core::stateSize() const {
    return (gameLoaded_ && g.serialize_size) ? g.serialize_size() : 0;
}

bool Core::saveState(std::vector<uint8_t>& out) {
    if (!gameLoaded_ || !g.serialize_size || !g.serialize) return false;
    const size_t n = g.serialize_size();
    if (n == 0) return false;  // some cores genuinely cannot; that is not an error
    out.resize(n);
    return g.serialize(out.data(), n);
}

size_t Core::saveRamSize() const {
    if (!gameLoaded_ || !g.get_memory_size) return 0;
    return g.get_memory_size(RETRO_MEMORY_SAVE_RAM);
}

bool Core::readSaveRam(std::vector<uint8_t>& out) const {
    out.clear();
    const size_t n = saveRamSize();
    if (n == 0 || !g.get_memory_data) return false;   // no battery in this cart
    const void* p = g.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!p) return false;
    out.resize(n);
    std::memcpy(out.data(), p, n);
    return true;
}

bool Core::writeSaveRam(const std::vector<uint8_t>& data) {
    const size_t n = saveRamSize();
    if (n == 0 || data.empty() || !g.get_memory_data) return false;
    void* p = g.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!p) return false;
    // Sizes can differ between a save written elsewhere and what this core
    // exposes. Copy what fits rather than refusing: a short save is a smaller
    // cart image and the remainder is already zeroed, and a long one is not
    // ours to truncate silently — but dropping the tail is still better than
    // losing the save entirely.
    std::memcpy(p, data.data(), std::min(n, data.size()));
    return true;
}

bool Core::loadState(const std::vector<uint8_t>& data) {
    if (!gameLoaded_ || !g.unserialize || data.empty()) return false;
    return g.unserialize(data.data(), data.size());
}

bool Core::saveRAM(std::vector<uint8_t>& out) const {
    return memoryRegion(RETRO_MEMORY_SAVE_RAM, out);
}

bool Core::loadSaveRAM(const std::vector<uint8_t>& data) {
    return loadMemoryRegion(RETRO_MEMORY_SAVE_RAM, data);
}

bool Core::memoryRegion(unsigned id, std::vector<uint8_t>& out) const {
    if (!gameLoaded_ || !g.get_memory_data || !g.get_memory_size) return false;
    const size_t n = g.get_memory_size(id);
    const void* p = g.get_memory_data(id);
    if (!p || n == 0) return false;
    out.assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
    return true;
}

bool Core::loadMemoryRegion(unsigned id, const std::vector<uint8_t>& data) {
    if (!gameLoaded_ || !g.get_memory_data || !g.get_memory_size) return false;
    void* p = g.get_memory_data(id);
    const size_t n = g.get_memory_size(id);
    if (!p || n == 0) return false;
    // Copy the SMALLER of the two rather than demanding they match. Two cores
    // in this set genuinely report a different size at restore time than at
    // capture time: Genesis Plus GX trims to the bytes actually written once
    // the game is running, and mGBA reports the 128KB flash maximum until it
    // has autodetected the real save type. RetroArch does the same thing when
    // it reads an .srm back in.
    std::memcpy(p, data.data(), std::min(n, data.size()));
    return true;
}

GLuint Core::texture() const {
    if (gHWVulkan) return vk::texture();
    return gHWFrame ? gHWColor : texture_;
}

bool Core::hardwareRendered() const {
    // PLAYSTATION 2 COUNTS, AND SAYING SO FIXES A REAL FAULT ON THE TELEVISION.
    //
    // This answer is read in exactly one place: whether to INTEGER scale the
    // picture. That is right for a Game Boy, whose pixels are a deliberate
    // grid, and wrong for a machine rendering a 3D scene — the comment at that
    // call site says so already, for Dreamcast.
    //
    // PCSX2 renders with Vulkan on the GPU, so it is hardware-rendered in every
    // sense that matters here; it simply does not arrive through libretro's
    // hardware-render path, which is what gHWWanted tracks. Left false, a
    // 640x448 PlayStation 2 picture floored to 2x and drew 896 rows of 1080 —
    // MMagTech, 2026-09-21: "the game didnt touch the top or bottum of the
    // screen almost like it was windowed".
    return gIsPs2 || gHWWanted;
}

unsigned Core::rotation() const { return gRotation; }

const std::string& Core::hardwareContext() const { return gHWContextName; }

void Core::frameUV(float& u0, float& v0, float& u1, float& v1) const {
    u0 = 0;
    v0 = 0;
    u1 = 1;
    v1 = 1;
    if (gHWVulkan) {
        vk::frameUV(u0, v0, u1, v1);
        return;
    }
    if (!gHWFrame || gHWTargetW == 0 || gHWTargetH == 0) return;
    // The picture is a corner of a target sized to the core's declared
    // maximum, so sampling the whole texture would draw a small picture inside
    // a large black field.
    u1 = static_cast<float>(gFrameW) / static_cast<float>(gHWTargetW);
    const float top = static_cast<float>(gFrameH) / static_cast<float>(gHWTargetH);
    if (gHW.bottom_left_origin) {
        // Row zero is the BOTTOM of the picture. Swapping the two V
        // coordinates is the flip; it costs nothing because it is the same
        // quad drawn with its texture coordinates the other way up.
        v0 = top;
        v1 = 0;
    } else {
        v1 = top;
    }
}

uint64_t Core::frameDigest() const {
    if (gHWFrame) {
        // A readback, and the only one anywhere in this file. It exists for
        // the determinism checks, which run under --state-test and never in
        // the player: there is no CPU-side copy of a hardware frame to hash,
        // so the choice is to read it back here or to have no video half of
        // the comparison at all for exactly the cores whose parity is least
        // certain.
        if (!gHWFBO || gFrameW == 0 || gFrameH == 0) return 0;
        std::vector<uint8_t> px(static_cast<size_t>(gFrameW) * gFrameH * 4);
        GLint bound = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        glBindFramebuffer(GL_FRAMEBUFFER, gHWFBO);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, static_cast<GLsizei>(gFrameW), static_cast<GLsizei>(gFrameH),
                     GL_RGBA, GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
        uint64_t hh = 1469598103934665603ull;
        for (uint8_t b : px) {
            hh ^= b;
            hh *= 1099511628211ull;
        }
        return hh;
    }
    uint64_t h = 1469598103934665603ull;  // FNV-1a
    for (unsigned y = 0; y < gFrameH; ++y) {
        const uint8_t* row = gFrame.data() + static_cast<size_t>(y) * gFramePitch;
        const size_t bytes = static_cast<size_t>(gFrameW) *
                             (gPixelFormat == RETRO_PIXEL_FORMAT_XRGB8888 ? 4 : 2);
        // The visible pixels only. Padding between rows is whatever the core
        // left there and is not part of the picture.
        for (size_t i = 0; i < bytes; ++i) {
            h ^= row[i];
            h *= 1099511628211ull;
        }
    }
    return h;
}

bool Core::snapshot(std::vector<uint8_t>& rgba, unsigned& width, unsigned& height) const {
    const unsigned w = gFrameW, h = gFrameH;
    if (w == 0 || h == 0 || isPs2()) return false;
    std::vector<uint8_t> px(static_cast<size_t>(w) * h * 4);

    // Read a GL framebuffer's bottom-left w x h into px, top row first when
    // `bottomUp` says row zero is the picture's bottom.
    auto readFBO = [&](GLuint fbo, bool bottomUp) {
        GLint bound = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glPixelStorei(GL_PACK_ALIGNMENT, 1);
        glReadPixels(0, 0, static_cast<GLsizei>(w), static_cast<GLsizei>(h), GL_RGBA,
                     GL_UNSIGNED_BYTE, px.data());
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
        if (!bottomUp) return;
        const size_t row = static_cast<size_t>(w) * 4;
        std::vector<uint8_t> tmp(row);
        for (unsigned y = 0; y < h / 2; ++y) {
            uint8_t* a = px.data() + y * row;
            uint8_t* b = px.data() + (h - 1 - y) * row;
            std::memcpy(tmp.data(), a, row);
            std::memcpy(a, b, row);
            std::memcpy(b, tmp.data(), row);
        }
    };

    if (gHWVulkan) {
        // The exported image is an ordinary GL texture (vkhost), top row
        // first, the picture in its top-left corner. present() waits on its
        // fence, so what is there is complete.
        const GLuint tex = vk::texture();
        if (!tex) return false;
        GLuint fbo = 0;
        glGenFramebuffers(1, &fbo);
        GLint bound = 0;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &bound);
        glBindFramebuffer(GL_FRAMEBUFFER, fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
        const bool ok = glCheckFramebufferStatus(GL_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE;
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(bound));
        if (ok) readFBO(fbo, /*bottomUp=*/false);
        glDeleteFramebuffers(1, &fbo);
        if (!ok) return false;
    } else if (gHWFrame) {
        // glReadPixels hands back row zero = the framebuffer's y = 0, which
        // is the picture's bottom exactly when the core says so.
        if (!gHWFBO) return false;
        readFBO(gHWFBO, gHW.bottom_left_origin);
    } else {
        if (gFrame.size() < gFramePitch * h) return false;
        for (unsigned y = 0; y < h; ++y) {
            const uint8_t* src = gFrame.data() + static_cast<size_t>(y) * gFramePitch;
            uint8_t* dst = px.data() + static_cast<size_t>(y) * w * 4;
            for (unsigned x = 0; x < w; ++x, dst += 4) {
                uint8_t r, g, b;
                if (gPixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) {
                    uint32_t p;
                    std::memcpy(&p, src + x * 4, 4);
                    r = (p >> 16) & 0xFF; g = (p >> 8) & 0xFF; b = p & 0xFF;
                } else {
                    uint16_t p;
                    std::memcpy(&p, src + x * 2, 2);
                    if (gPixelFormat == RETRO_PIXEL_FORMAT_RGB565) {
                        r = ((p >> 11) & 0x1F) << 3; g = ((p >> 5) & 0x3F) << 2; b = (p & 0x1F) << 3;
                    } else {
                        r = ((p >> 10) & 0x1F) << 3; g = ((p >> 5) & 0x1F) << 3; b = (p & 0x1F) << 3;
                    }
                }
                dst[0] = r; dst[1] = g; dst[2] = b; dst[3] = 0xFF;
            }
        }
    }
    // Opaque whatever the core left in alpha: a PNG with holes in it shows
    // the page behind.
    for (size_t i = 3; i < px.size(); i += 4) px[i] = 0xFF;

    // THE QUARTER TURN, as the player's vertex shader turns it (ui.cpp, uRot:
    // the destination's corner reads the source at (1 - y, x) for one step
    // anticlockwise), so a vertical arcade game's picture stands upright.
    const unsigned r = gRotation & 3u;
    if (r == 0) {
        rgba = std::move(px);
        width = w;
        height = h;
        return true;
    }
    const unsigned ow = (r & 1u) ? h : w, oh = (r & 1u) ? w : h;
    rgba.assign(static_cast<size_t>(ow) * oh * 4, 0);
    for (unsigned dy = 0; dy < oh; ++dy) {
        for (unsigned dx = 0; dx < ow; ++dx) {
            unsigned sx, sy;
            if (r == 1) { sx = w - 1 - dy; sy = dx; }
            else if (r == 2) { sx = w - 1 - dx; sy = h - 1 - dy; }
            else { sx = dy; sy = h - 1 - dx; }
            std::memcpy(rgba.data() + (static_cast<size_t>(dy) * ow + dx) * 4,
                        px.data() + (static_cast<size_t>(sy) * w + sx) * 4, 4);
        }
    }
    width = ow;
    height = oh;
    return true;
}

uint64_t Core::framesRun() const { return gFramesRun; }
uint64_t Core::audioFramesTotal() const { return gAudioFrames; }

}  // namespace cab
