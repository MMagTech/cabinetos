#include "sound.h"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

namespace sound {
namespace {

// 48 kHz because it is what the A9's HDMI audio runs at and what every other
// device this is likely to meet runs at. Mono, because a navigation click has
// no business having a position: it is the interface speaking, not something in
// the scene. SDL widens it to the device's channel count.
constexpr int kRate = 48000;

bool gReady = false;
bool gEnabled = true;
float gVolume = 0.22f;
SDL_AudioStream* gStream = nullptr;

// One cue, as samples.
struct Voice {
    std::vector<int16_t> pcm;
};
Voice gVoices[4];

// A cue is two sine partials under one exponential envelope, and that is the
// whole synthesiser.
//
// WHY TWO PARTIALS AND NOT ONE. A single sine reads as a test tone — it is too
// pure to sound like an object. A quiet partial a fifth or an octave above the
// first gives it an edge that a television's speakers can actually reproduce,
// which matters more here than it would on headphones.
//
// WHY AN EXPONENTIAL DECAY AND A 3 MS ATTACK. Everything physical that makes a
// short sound decays exponentially, and starting a waveform at full amplitude
// puts a step in the signal that comes out as a click on top of the click. The
// 3 ms ramp costs nothing and removes it.
//
// `bend` slides the pitch across the sound: up for a choice, down for leaving.
// It is what makes Activate and Back read as opposites rather than as two
// unrelated noises.
void build(Voice& v, float hz, float partialHz, float partialGain,
           float seconds, float bend) {
    const int n = static_cast<int>(seconds * kRate);
    v.pcm.resize(static_cast<size_t>(n));
    const int attack = static_cast<int>(0.003f * kRate);
    // Phase is accumulated rather than computed from t, because the pitch bend
    // means the frequency is different at every sample and sin(2*pi*f*t) with a
    // moving f is not a continuous waveform — it jumps, and the jumps are
    // audible as a buzz.
    double phase = 0.0, phase2 = 0.0;
    for (int i = 0; i < n; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(n);
        const float f = hz * (1.0f + bend * t);
        const float f2 = partialHz * (1.0f + bend * t);
        phase += 2.0 * M_PI * f / kRate;
        phase2 += 2.0 * M_PI * f2 / kRate;
        // Decays to about 1% of full by the end, whatever the length is.
        float env = std::exp(-4.6f * t);
        if (i < attack) env *= static_cast<float>(i) / static_cast<float>(attack);
        const float s = env * (static_cast<float>(std::sin(phase)) +
                               partialGain * static_cast<float>(std::sin(phase2)));
        // Headroom for the two partials summing, before the runtime volume.
        v.pcm[static_cast<size_t>(i)] =
            static_cast<int16_t>(SDL_clamp(s * 0.55f, -1.0f, 1.0f) * 32767.0f);
    }
}

}  // namespace

bool init() {
    SDL_AudioSpec spec{};
    spec.format = SDL_AUDIO_S16;
    spec.channels = 1;
    spec.freq = kRate;
    gStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec,
                                        nullptr, nullptr);
    if (!gStream) {
        // Not an error. Say it once and let every play() be a no-op.
        std::fprintf(stderr, "[sound] no audio device, the interface is silent: %s\n",
                     SDL_GetError());
        return false;
    }
    SDL_ResumeAudioStreamDevice(gStream);

    // THE SET, AND WHAT EACH ONE IS TRYING TO BE.
    //
    // MOVE is the one that plays hundreds of times an evening, so it is the
    // shortest and the highest: 45 ms, and gone before the focus animation it
    // accompanies has finished. Anything longer turns a run along a shelf into
    // a drum roll.
    build(gVoices[0], 880.0f, 1320.0f, 0.30f, 0.045f, 0.0f);
    // ACTIVATE is lower, longer and rises. It is the sound of something opening
    // and it is allowed to be heard.
    build(gVoices[1], 520.0f, 780.0f, 0.35f, 0.110f, 0.18f);
    // BACK is the same shape falling instead of rising, which is the whole
    // reason it reads as the opposite of Activate rather than as another noise.
    build(gVoices[2], 620.0f, 930.0f, 0.30f, 0.090f, -0.22f);
    // EDGE is focus hitting a wall — a duller, quieter thud with no partial to
    // speak of, so it reads as "nothing there" rather than as a move that
    // worked. Without it, a controller pressed against the end of a row is
    // silent, which feels like the console stopped listening.
    build(gVoices[3], 320.0f, 480.0f, 0.12f, 0.055f, -0.10f);

    gReady = true;
    std::fprintf(stderr, "[sound] interface audio at %d Hz\n", kRate);
    return true;
}

void shutdown() {
    if (gStream) SDL_DestroyAudioStream(gStream);
    gStream = nullptr;
    gReady = false;
}

// THE LEVELS, as volumes. Medium is the 0.22 the console always had. The steps
// are about 10 dB apart (0.07, 0.22, 0.65), because loudness is heard as a
// ratio: equal steps in volume would make Quiet to Medium sound like a much
// bigger move than Medium to Loud. The first try was 5 dB (0.12, 0.40), and
// MMagTech on the TV, 2026-09-24: "the steps are too close in volume".
// The cues keep their own headroom, so anything up to 1.0 does not clip.
float gLevelVolume[kLevelCount] = {0.0f, 0.07f, 0.22f, 0.65f};
constexpr const char* kLevelName[kLevelCount] = {"Off", "Quiet", "Medium", "Loud"};
constexpr const char* kLevelWord[kLevelCount] = {"off", "quiet", "medium", "loud"};
Level gLevel = Level::Medium;

void setLevel(Level l) {
    gLevel = l;
    gEnabled = (l != Level::Off);
    if (gEnabled) gVolume = gLevelVolume[static_cast<int>(l)];
}
Level level() { return gLevel; }
const char* levelName(Level l) { return kLevelName[static_cast<int>(l)]; }
const char* levelWord(Level l) { return kLevelWord[static_cast<int>(l)]; }
bool levelFromWord(const std::string& word, Level* out) {
    for (int i = 0; i < kLevelCount; ++i)
        if (word == kLevelWord[i]) { *out = static_cast<Level>(i); return true; }
    return false;
}

void setLevelVolumes(float quiet, float medium, float loud) {
    gLevelVolume[1] = quiet;
    gLevelVolume[2] = medium;
    gLevelVolume[3] = loud;
    setLevel(gLevel);
}

void setEnabled(bool on) { gEnabled = on; }
bool enabled() { return gEnabled; }
void setVolume(float v) { gVolume = v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

void play(Cue c) {
    if (!gReady || !gEnabled || gVolume <= 0.0f) return;
    const Voice& v = gVoices[static_cast<int>(c)];
    if (v.pcm.empty()) return;

    // DO NOT LET CLICKS QUEUE UP. SDL's stream is a queue, so pushing a cue
    // while one is still playing appends it rather than mixing it — and a
    // controller held on the d-pad would build a backlog that keeps ticking
    // after the person has stopped moving, which is the same failure the 180 ms
    // focus tempo exists to prevent. If there is already more than one cue's
    // worth of audio waiting, clear it and start this one now.
    const int queued = SDL_GetAudioStreamQueued(gStream);
    if (queued > static_cast<int>(v.pcm.size() * sizeof(int16_t)))
        SDL_ClearAudioStream(gStream);

    // Scaled here rather than at build time so the volume can move without
    // rebuilding the cues — which is what a Settings slider would want.
    std::vector<int16_t> out(v.pcm.size());
    for (size_t i = 0; i < v.pcm.size(); ++i)
        out[i] = static_cast<int16_t>(static_cast<float>(v.pcm[i]) * gVolume);
    SDL_PutAudioStreamData(gStream, out.data(),
                           static_cast<int>(out.size() * sizeof(int16_t)));
}

}  // namespace sound
