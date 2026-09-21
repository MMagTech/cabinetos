// See ps2.h. This is the frontend's side of the wire; the emulator's side is
// frontend/ps2/CabinetPS2Bridge.cpp and it is built separately, against PCSX2.

#include "ps2.h"

#include <cstdio>
#include <cstring>
#include <dlfcn.h>

namespace {

void* gHandle = nullptr;
std::string gError;

// Every entry point the shared object publishes. Resolved once, at load, and
// asserted — because a symbol that resolves to null and is called later is a
// crash on the television with no message, while a missing one found here is
// one line saying which.
struct Api {
    int      (*start)(const char*, const char*, const char*, const char*, const char*, const char*, float, int) = nullptr;
    const char* (*error)() = nullptr;
    int      (*running)() = nullptr;
    void     (*stop)() = nullptr;
    void     (*setPaused)(int) = nullptr;
    void     (*setPad)(unsigned, uint32_t, float, float, float, float, float, float) = nullptr;
    int      (*takeFrame)(const uint32_t**, unsigned*, unsigned*, uint64_t*) = nullptr;
    unsigned (*drainAudio)(int16_t*, unsigned) = nullptr;
    unsigned (*sampleRate)() = nullptr;
    void     (*metrics)(float*, float*, double*) = nullptr;
    const char* (*version)() = nullptr;
} gApi;

uint64_t gFrameSerial = 0;

template <typename T>
bool resolve(T& fn, const char* name) {
    fn = reinterpret_cast<T>(dlsym(gHandle, name));
    if (!fn) {
        gError = std::string("cabinetos-ps2.so has no ") + name;
        return false;
    }
    return true;
}

} // namespace

bool ps2::load(const std::string& soPath) {
    if (gHandle) return true;

    // RTLD_LOCAL for the same reason every libretro core gets it: PCSX2 brings
    // its own copies of fmt, imgui, zlib and a dozen more, and letting those
    // into the global namespace would let them answer a call the frontend
    // meant for its own.
    gHandle = dlopen(soPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!gHandle) {
        const char* why = dlerror();
        gError = "could not open " + soPath + ": " + (why ? why : "no reason given");
        return false;
    }

    const bool ok =
        resolve(gApi.start, "cps2_start") &&
        resolve(gApi.error, "cps2_error") &&
        resolve(gApi.running, "cps2_running") &&
        resolve(gApi.stop, "cps2_stop") &&
        resolve(gApi.setPaused, "cps2_set_paused") &&
        resolve(gApi.setPad, "cps2_set_pad") &&
        resolve(gApi.takeFrame, "cps2_take_frame") &&
        resolve(gApi.drainAudio, "cps2_drain_audio") &&
        resolve(gApi.sampleRate, "cps2_audio_sample_rate") &&
        resolve(gApi.metrics, "cps2_metrics") &&
        resolve(gApi.version, "cps2_version");

    if (!ok) {
        dlclose(gHandle);
        gHandle = nullptr;
        return false;
    }

    std::fprintf(stderr, "[ps2] %s (PCSX2 %s)\n", soPath.c_str(), gApi.version());
    return true;
}

void ps2::unload() {
    if (!gHandle) return;
    // NOT dlclose'd, deliberately, and this is the same decision the libretro
    // path makes. PCSX2 starts threads and registers atexit handlers; closing
    // the library out from under them has no defined behaviour and the failure
    // would arrive at process exit, a long way from the cause. A console runs
    // one emulator at a time and re-loading the same object is free.
    gHandle = gHandle;
}

bool ps2::loaded() { return gHandle != nullptr; }
const std::string& ps2::error() { return gError; }

std::string ps2::version() {
    return gApi.version ? gApi.version() : std::string();
}

bool ps2::startGame(const std::string& discPath, const std::string& biosDir,
                    const std::string& memcardsDir, const std::string& memoryCard,
                    const std::string& scratchDir, const std::string& resourcesDir,
                    float upscale) {
    if (!gHandle) { gError = "no PlayStation 2 emulator is loaded"; return false; }

    gFrameSerial = 0;
    if (!gApi.start(discPath.c_str(), biosDir.c_str(), memcardsDir.c_str(), memoryCard.c_str(),
                    scratchDir.c_str(), resourcesDir.c_str(), upscale, 1)) {
        gError = gApi.error();
        if (gError.empty()) gError = "PCSX2 would not start";
        return false;
    }
    return true;
}

void ps2::stopGame() {
    if (!gHandle) return;
    gApi.stop();
    gFrameSerial = 0;
}

bool ps2::running() { return gHandle && gApi.running() != 0; }

void ps2::setPaused(bool paused) {
    if (gHandle) gApi.setPaused(paused ? 1 : 0);
}

void ps2::setPad(int port, uint32_t buttons, float leftX, float leftY, float rightX, float rightY,
                 float leftTrigger, float rightTrigger) {
    if (!gHandle || port < 0) return;
    gApi.setPad(static_cast<unsigned>(port), buttons, leftX, leftY, rightX, rightY,
                leftTrigger, rightTrigger);
}

bool ps2::takeFrame(const uint32_t** pixels, unsigned& width, unsigned& height) {
    if (!gHandle) return false;
    unsigned w = 0, h = 0;
    if (!gApi.takeFrame(pixels, &w, &h, &gFrameSerial)) return false;
    width = w;
    height = h;
    return true;
}

void ps2::drainAudio(std::vector<int16_t>& out) {
    if (!gHandle) return;
    // A generous slice: at 48 kHz a 60 Hz frame is 800 stereo frames, and a
    // frontend that stalls briefly must not lose the sound made while it did.
    // Asking for more than exists costs nothing — the stream pads with the
    // silence it would have played anyway.
    static constexpr unsigned kMaxFrames = 8192;
    const size_t at = out.size();
    out.resize(at + kMaxFrames * 2);
    const unsigned got = gApi.drainAudio(out.data() + at, kMaxFrames);
    out.resize(at + static_cast<size_t>(got) * 2);
}

unsigned ps2::sampleRate() { return gHandle ? gApi.sampleRate() : 0; }

void ps2::metrics(float& fps, float& speed, double& readbackUs) {
    fps = 0.0f; speed = 0.0f; readbackUs = 0.0;
    if (gHandle) gApi.metrics(&fps, &speed, &readbackUs);
}
