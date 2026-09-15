#include "core.h"

#include <dlfcn.h>

#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <unordered_map>

#include "libretro.h"

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

unsigned gPixelFormat = RETRO_PIXEL_FORMAT_0RGB1555;
std::string gSystemDir, gSaveDir;

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

// Core options. Answered from here, falling back to the core's own default,
// which is what an absent key means.
std::unordered_map<std::string, std::string> gOptions;

void videoRefresh(const void* data, unsigned width, unsigned height, size_t pitch) {
    if (!data) return;  // "same picture as last time"
    const size_t bpp = (gPixelFormat == RETRO_PIXEL_FORMAT_XRGB8888) ? 4 : 2;
    gFrame.resize(pitch * height);
    std::memcpy(gFrame.data(), data, pitch * height);
    gFrameW = width;
    gFrameH = height;
    gFramePitch = pitch;
    gFrameDirty = true;
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
        const float v = (index == RETRO_DEVICE_INDEX_ANALOG_LEFT)
                            ? (id == RETRO_DEVICE_ID_ANALOG_X ? pad.leftX : pad.leftY)
                            : (id == RETRO_DEVICE_ID_ANALOG_X ? pad.rightX : pad.rightY);
        return static_cast<int16_t>(std::clamp(v, -1.0f, 1.0f) * 32767.0f);
    }
    return 0;
}

void logCallback(enum retro_log_level level, const char* fmt, ...) {
    if (level < RETRO_LOG_WARN) return;  // cores are chatty at INFO
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
            auto it = gOptions.find(var->key ? var->key : "");
            var->value = (it == gOptions.end()) ? nullptr : it->second.c_str();
            return var->value != nullptr;
        }

        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *static_cast<bool*>(data) = false;
            return true;

        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
            // Accepted and ignored for now. The option TABLE is per platform,
            // not per core (docs/PROJECT.md: "Cores and platforms are not the
            // same list"), so it is owned above this layer, not discovered
            // here.
            return true;

        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_CONTROLLER_INFO:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_SUPPORT_ACHIEVEMENTS:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return true;

        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *static_cast<unsigned*>(data) = 0;  // the plain SET_VARIABLES API
            return true;

        case RETRO_ENVIRONMENT_GET_LANGUAGE:
            *static_cast<unsigned*>(data) = RETRO_LANGUAGE_ENGLISH;
            return true;

        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;

        case RETRO_ENVIRONMENT_SET_HW_RENDER:
            // Three cores in the whole set ask for this — Flycast,
            // Mupen64Plus and PPSSPP — and none of them is the one being
            // brought up first. Refusing is honest; pretending would hand the
            // core a context that does not exist.
            std::fprintf(stderr, "[core] asked for hardware rendering; not yet\n");
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

bool Core::load(const std::string& soPath) {
    unload();
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

void Core::unload() {
    if (!handle_) return;
    if (gameLoaded_) unloadGame();
    if (g.deinit) g.deinit();
    dlclose(handle_);
    handle_ = nullptr;
    g = {};
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
}

bool Core::loadGame(const std::string& romPath, const std::string& systemDir,
                    const std::string& saveDir) {
    if (!handle_) {
        error_ = "no core loaded";
        return false;
    }
    gSystemDir = systemDir;
    gSaveDir = saveDir;

    std::vector<uint8_t> rom;
    if (FILE* f = std::fopen(romPath.c_str(), "rb")) {
        std::fseek(f, 0, SEEK_END);
        long n = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        if (n > 0) {
            rom.resize(static_cast<size_t>(n));
            if (std::fread(rom.data(), 1, rom.size(), f) != rom.size()) rom.clear();
        }
        std::fclose(f);
    }
    if (rom.empty()) {
        error_ = "cannot read " + romPath;
        return false;
    }

    retro_game_info info{};
    info.path = romPath.c_str();
    info.data = rom.data();
    info.size = rom.size();

    if (!g.load_game(&info)) {
        // Deliberately does not claim the core is wrong, because nothing here
        // knows that: a refusal looks identical for a missing BIOS, an
        // incomplete set, or a genuinely bad file.
        error_ = "the core refused " + romPath;
        return false;
    }
    gameLoaded_ = true;

    retro_system_av_info av{};
    g.get_system_av_info(&av);
    av_.fps = av.timing.fps > 0 ? av.timing.fps : 60.0;
    av_.sampleRate = av.timing.sample_rate > 0 ? av.timing.sample_rate : 44100.0;
    av_.baseWidth = av.geometry.base_width;
    av_.baseHeight = av.geometry.base_height;
    av_.maxWidth = av.geometry.max_width;
    av_.maxHeight = av.geometry.max_height;
    av_.aspectRatio = av.geometry.aspect_ratio;

    accumulator_ = 0.0;
    gFramesRun = 0;
    gAudioFrames = 0;
    gAudio.clear();

    std::fprintf(stderr, "[core] loaded %s\n", romPath.c_str());
    std::fprintf(stderr, "[core] %ux%u, %.4f fps, %.0f Hz, aspect %.4f\n", av_.baseWidth,
                 av_.baseHeight, av_.fps, av_.sampleRate, av_.aspectRatio);
    return true;
}

void Core::unloadGame() {
    if (!gameLoaded_) return;
    // This is the one moment a file-writing core flushes its save. RetroArch
    // does exactly this, in this order, and the reference implementation lost
    // saves for months by unloading lazily at the NEXT launch instead.
    if (g.unload_game) g.unload_game();
    gameLoaded_ = false;
}

int Core::runFor(double dt) {
    if (!gameLoaded_) return 0;

    const double interval = 1.0 / std::max(av_.fps, 1.0);
    accumulator_ += dt;
    // Anything beyond a couple of frames behind is time that is simply gone.
    // Letting it accumulate would make the core sprint to catch up, which
    // stutters the picture and floods the audio buffer.
    if (accumulator_ > interval * 4) accumulator_ = interval;

    int ran = 0;
    while (accumulator_ >= interval && ran < 2) {
        g.run();
        ++gFramesRun;
        ++ran;
        accumulator_ -= interval;
    }
    return ran;
}

bool Core::uploadFrame() {
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
    gAudioDrain.swap(gAudio);
    gAudio.clear();
    return gAudioDrain;
}

void Core::setPad(int port, const PadState& pad) {
    if (port < 0 || port >= kMaxPorts) return;
    gPads[port] = pad;
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

uint64_t Core::frameDigest() const {
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

uint64_t Core::framesRun() const { return gFramesRun; }
uint64_t Core::audioFramesTotal() const { return gAudioFrames; }

}  // namespace cab
