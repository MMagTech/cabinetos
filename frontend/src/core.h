// The libretro host.
//
// This is the part that makes CabinetOS an emulator host rather than a
// launcher: it loads a core as a shared library, feeds it a ROM and controller
// input, and owns the loop that decides when it runs.
//
// Shape, and why.
//
// ONE CORE AT A TIME, IN PROCESS-GLOBAL STATE. Libretro's callbacks are plain C
// function pointers with no context argument, so the process holds exactly one
// core's worth of state no matter how this class is shaped. The singleton is
// the honest interface to that, and it matches the reference implementation,
// which reached the same conclusion for the same reason.
//
// ONE .so PER CORE, dlopen'd with RTLD_LOCAL. On Apple, where everything is
// statically linked into one binary, only one core can carry the standard
// retro_* names, so Cabinet renames every other core's symbols with a prefix
// and merges each into a single relocatable object. On Linux that entire
// apparatus is unnecessary: RTLD_LOCAL gives each core its own namespace for
// free. See docs/PROJECT.md, open question 13.
//
// THE FRONTEND DECIDES WHEN THE CORE RUNS. Not the display, not the core. See
// `runFor`.

#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <vector>

namespace cab {

// What the core asked to be run at, and how big its picture is.
struct AVInfo {
    double fps = 60.0;
    double sampleRate = 44100.0;
    unsigned baseWidth = 0, baseHeight = 0;
    unsigned maxWidth = 0, maxHeight = 0;
    float aspectRatio = 0.0f;  // 0 means "derive it from the pixel dimensions"
};

// libretro's RetroPad, which every core speaks regardless of what the real
// hardware had. Repeated here rather than pulling libretro.h into the UI: the
// frontend above this layer has no business knowing what libretro is, and these
// values are a stable part of its ABI rather than an implementation detail.
// Verified against libretro.h.
enum Button : uint32_t {
    B = 0,
    Y = 1,
    Select = 2,
    Start = 3,
    Up = 4,
    Down = 5,
    Left = 6,
    Right = 7,
    A = 8,
    X = 9,
    L = 10,
    R = 11,
    L2 = 12,
    R2 = 13,
    L3 = 14,
    R3 = 15,
};

inline constexpr uint32_t bit(Button b) { return 1u << static_cast<uint32_t>(b); }

// Bit N of the mask is RetroPad button N.
struct PadState {
    uint32_t buttons = 0;
    float leftX = 0, leftY = 0;
    float rightX = 0, rightY = 0;
};

class Core {
public:
    static Core& shared();

    // dlopen, resolve the retro_* entry points, retro_init. Returns false with
    // a reason on `error()`.
    bool load(const std::string& soPath);
    void unload();
    bool loaded() const { return handle_ != nullptr; }

    // systemDir is where a core looks BIOS files up by name. saveDir is where a
    // core that writes its own save files puts them, and it MUST outlive the
    // session: pointing it at a per-launch temp directory is how the reference
    // implementation used to lose saves.
    bool loadGame(const std::string& romPath, const std::string& systemDir,
                  const std::string& saveDir);
    void unloadGame();
    bool gameLoaded() const { return gameLoaded_; }

    // Advances emulation by however many frames are due, given the wall clock.
    //
    // Deliberately not "run one frame per draw". The panel's refresh rate and
    // the core's frame rate are different numbers — an NTSC core wants 59.94
    // and a television may be at 60, 120 or anything else — and running once
    // per draw makes the emulated machine run fast or slow by exactly that
    // ratio. The reference implementation measured the symptom on Dreamcast:
    // 65,000-85,000 audio frames a second against 44,100 of realtime, which is
    // what made the music play back sped up.
    //
    // Returns how many emulated frames actually ran.
    int runFor(double dtSeconds);

    // Uploads the most recent frame into `texture()`. Call on the GL thread.
    // Returns false if the core has not produced a picture yet.
    bool uploadFrame();
    GLuint texture() const { return texture_; }
    unsigned frameWidth() const { return frameWidth_; }
    unsigned frameHeight() const { return frameHeight_; }

    // Drains audio produced since the last call, as interleaved 16-bit stereo
    // at the core's own rate.
    const std::vector<int16_t>& drainAudio();

    void setPad(int port, const PadState& pad);

    // --- Save states ---------------------------------------------------------
    //
    // The whole product rests on these. A state written on an Apple TV has to
    // load on this machine, which is what core parity is for (docs/PROJECT.md,
    // "Core parity is a hard constraint").
    //
    // THREADING: both must run on the thread that drives runFor. A snapshot
    // taken part-way through a retro_run is corrupt by definition.
    size_t stateSize() const;
    bool saveState(std::vector<uint8_t>& out);
    // Returns false when the core rejects the bytes, which is the expected
    // failure for a state written by a different build of the same core — the
    // exact thing the manifest exists to prevent.
    bool loadState(const std::vector<uint8_t>& data);

    // --- In-game saves -------------------------------------------------------
    //
    // A different mechanism from save states, and the one people assume is
    // safe once the game says it saved. Most cores expose the cartridge
    // battery here; some write their own files into the save directory
    // instead and this returns nothing for them.
    bool saveRAM(std::vector<uint8_t>& out) const;
    bool loadSaveRAM(const std::vector<uint8_t>& data);
    // Game Boy keeps its real-time clock in a region of its own. Saving only
    // the save RAM loses the clock that Pokemon Gold and Silver depend on.
    bool memoryRegion(unsigned id, std::vector<uint8_t>& out) const;
    bool loadMemoryRegion(unsigned id, const std::vector<uint8_t>& data);

    const AVInfo& avInfo() const { return av_; }
    const std::string& coreName() const { return coreName_; }
    // The core's own answers about what it will open. Both come from
    // retro_get_system_info and neither is knowable from the platform: a
    // platform does not have an opinion about archives, and two cores serving
    // the same platform can differ. See romfile.h.
    const std::string& validExtensions() const { return validExtensions_; }
    bool blockExtract() const { return blockExtract_; }
    bool needFullpath() const { return needFullpath_; }
    const std::string& coreVersion() const { return coreVersion_; }
    const std::string& error() const { return error_; }

    // A hash of the current picture, for determinism checks. Cheap, and it
    // catches a divergence that audio alone would not.
    uint64_t frameDigest() const;

    // Emulated frames run since the game loaded, and audio frames produced.
    // Audio against the core's own sample rate is the only direct read on
    // whether emulated time is advancing at realtime.
    uint64_t framesRun() const;
    uint64_t audioFramesTotal() const;

private:
    Core() = default;
    void* symbol(const char* name);

    void* handle_ = nullptr;
    bool gameLoaded_ = false;
    std::string error_;
    std::string coreName_, coreVersion_, validExtensions_;
    bool blockExtract_ = false;
    bool needFullpath_ = false;
    AVInfo av_;

    GLuint texture_ = 0;
    unsigned frameWidth_ = 0, frameHeight_ = 0;

    // Wall-clock pacing. Capped so a stall cannot bank a debt the core then
    // tries to repay all at once, which stutters and floods the audio buffer.
    double accumulator_ = 0.0;
};

}  // namespace cab
