// Does a save state written on one platform load on another?
//
// That is the question the whole product rests on (docs/PROJECT.md, "Core
// parity is a hard constraint"), and this is the instrument for answering it.
// Deliberately a single C file with no dependencies beyond dlfcn and
// libretro.h, so that the SAME SOURCE compiles on macOS arm64 and Linux
// x86-64. If the harness differed between the two, a difference in the output
// would tell you nothing.
//
//   state-probe dump   <core> <rom> <frames> <out.state>
//   state-probe verify <core> <rom> <frames> <in.state>
//
// dump   runs `frames` frames from boot with no input, then serializes.
// verify runs the same, loads the given state over the top, runs 300 more
//        frames and prints a digest of the video — so "the core accepted the
//        bytes" is not mistaken for "the machine is in the right state".
//
// Build:
//   cc -O2 -o state-probe tools/state-probe.c -ldl        (Linux)
//   cc -O2 -o state-probe tools/state-probe.c             (macOS)

#include <dlfcn.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../frontend/src/libretro.h"

static unsigned g_pixel_format = RETRO_PIXEL_FORMAT_0RGB1555;
static uint64_t g_video_digest = 1469598103934665603ULL;
static uint64_t g_audio_digest = 1469598103934665603ULL;
static unsigned g_w, g_h;

static void mix(uint64_t *h, const void *p, size_t n) {
    const uint8_t *b = (const uint8_t *)p;
    for (size_t i = 0; i < n; i++) {
        *h ^= b[i];
        *h *= 1099511628211ULL;
    }
}

static void video_refresh(const void *data, unsigned w, unsigned h, size_t pitch) {
    if (!data) return;
    g_w = w;
    g_h = h;
    const size_t bpp = (g_pixel_format == RETRO_PIXEL_FORMAT_XRGB8888) ? 4 : 2;
    // Visible pixels only. Row padding is whatever the core left there and is
    // not part of the picture — including it would make the digest depend on
    // an allocation detail rather than on the emulated machine.
    for (unsigned y = 0; y < h; y++)
        mix(&g_video_digest, (const uint8_t *)data + (size_t)y * pitch, (size_t)w * bpp);
}

static void audio_sample(int16_t l, int16_t r) {
    mix(&g_audio_digest, &l, sizeof l);
    mix(&g_audio_digest, &r, sizeof r);
}

static size_t audio_batch(const int16_t *data, size_t frames) {
    mix(&g_audio_digest, data, frames * 2 * sizeof(int16_t));
    return frames;
}

static void input_poll(void) {}
static int16_t input_state(unsigned p, unsigned d, unsigned i, unsigned id) {
    (void)p; (void)d; (void)i; (void)id;
    return 0;  // no input, so the run is reproducible
}

static bool environment(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data = true;
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
            g_pixel_format = *(const enum retro_pixel_format *)data;
            return g_pixel_format <= RETRO_PIXEL_FORMAT_RGB565;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char **)data = ".";
            return true;
        case RETRO_ENVIRONMENT_GET_VARIABLE:
            // Every option left at the core's own default, identically on both
            // platforms. Answering differently here would be the same class of
            // mistake as building with different flags.
            ((struct retro_variable *)data)->value = NULL;
            return false;
        case RETRO_ENVIRONMENT_GET_VARIABLE_UPDATE:
            *(bool *)data = false;
            return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *(unsigned *)data = 0;
            return true;
        case RETRO_ENVIRONMENT_SET_VARIABLES:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
        case RETRO_ENVIRONMENT_SET_INPUT_DESCRIPTORS:
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
        case RETRO_ENVIRONMENT_SET_PERFORMANCE_LEVEL:
        case RETRO_ENVIRONMENT_SET_SERIALIZATION_QUIRKS:
            return true;
        default:
            return false;
    }
}

#define SYM(name) \
    do { \
        *(void **)(&name) = dlsym(core, #name); \
        if (!name) { fprintf(stderr, "missing %s\n", #name); return 2; } \
    } while (0)

int main(int argc, char **argv) {
    if (argc != 6) {
        fprintf(stderr,
                "usage: %s dump|verify <core.so> <rom> <frames> <state-file>\n", argv[0]);
        return 1;
    }
    const char *mode = argv[1], *corePath = argv[2], *romPath = argv[3];
    const long frames = strtol(argv[4], NULL, 10);
    const char *statePath = argv[5];

    void *core = dlopen(corePath, RTLD_NOW | RTLD_LOCAL);
    if (!core) {
        // Read dlerror ONCE: it clears itself, and the second read is NULL.
        const char *why = dlerror();
        fprintf(stderr, "dlopen: %s\n", why ? why : "failed");
        return 2;
    }

    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_init)(void);
    bool (*retro_load_game)(const struct retro_game_info *);
    void (*retro_run)(void);
    size_t (*retro_serialize_size)(void);
    bool (*retro_serialize)(void *, size_t);
    bool (*retro_unserialize)(const void *, size_t);
    void (*retro_get_system_info)(struct retro_system_info *);

    SYM(retro_set_environment);
    SYM(retro_set_video_refresh);
    SYM(retro_set_audio_sample);
    SYM(retro_set_audio_sample_batch);
    SYM(retro_set_input_poll);
    SYM(retro_set_input_state);
    SYM(retro_init);
    SYM(retro_load_game);
    SYM(retro_run);
    SYM(retro_serialize_size);
    SYM(retro_serialize);
    SYM(retro_unserialize);
    SYM(retro_get_system_info);

    struct retro_system_info info;
    memset(&info, 0, sizeof info);
    retro_get_system_info(&info);

    retro_set_environment(environment);
    retro_init();
    retro_set_video_refresh(video_refresh);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);

    FILE *rf = fopen(romPath, "rb");
    if (!rf) { fprintf(stderr, "cannot read %s\n", romPath); return 2; }
    fseek(rf, 0, SEEK_END);
    long romSize = ftell(rf);
    fseek(rf, 0, SEEK_SET);
    void *rom = malloc((size_t)romSize);
    if (fread(rom, 1, (size_t)romSize, rf) != (size_t)romSize) { return 2; }
    fclose(rf);

    struct retro_game_info game;
    memset(&game, 0, sizeof game);
    game.path = romPath;
    game.data = rom;
    game.size = (size_t)romSize;
    if (!retro_load_game(&game)) { fprintf(stderr, "core refused the rom\n"); return 2; }

    printf("core        %s %s\n", info.library_name ? info.library_name : "?",
           info.library_version ? info.library_version : "?");

    for (long i = 0; i < frames; i++) retro_run();
    printf("after %ld frames  video %016llx  audio %016llx  (%ux%u)\n", frames,
           (unsigned long long)g_video_digest, (unsigned long long)g_audio_digest, g_w, g_h);

    const size_t n = retro_serialize_size();
    printf("state size  %zu\n", n);

    if (strcmp(mode, "dump") == 0) {
        void *buf = malloc(n);
        if (!retro_serialize(buf, n)) { fprintf(stderr, "serialize failed\n"); return 3; }
        FILE *o = fopen(statePath, "wb");
        if (!o) { fprintf(stderr, "cannot write %s\n", statePath); return 3; }
        fwrite(buf, 1, n, o);
        fclose(o);
        printf("wrote       %s (%zu bytes)\n", statePath, n);
        return 0;
    }

    // verify: load a state written elsewhere over the top of this machine.
    FILE *sf = fopen(statePath, "rb");
    if (!sf) { fprintf(stderr, "cannot read %s\n", statePath); return 3; }
    fseek(sf, 0, SEEK_END);
    long sn = ftell(sf);
    fseek(sf, 0, SEEK_SET);
    void *sbuf = malloc((size_t)sn);
    if (fread(sbuf, 1, (size_t)sn, sf) != (size_t)sn) { return 3; }
    fclose(sf);

    printf("loading     %s (%ld bytes, core wants %zu)\n", statePath, sn, n);
    if ((size_t)sn != n) {
        // A size mismatch is decisive on its own: the two builds do not agree
        // on what a state even is.
        printf("VERDICT     INCOMPATIBLE - state sizes differ\n");
        return 4;
    }
    if (!retro_unserialize(sbuf, (size_t)sn)) {
        printf("VERDICT     REJECTED - the core refused the state\n");
        return 4;
    }

    // Accepted is not the same as correct. Run on and print a digest the other
    // platform can be compared against.
    g_video_digest = 1469598103934665603ULL;
    g_audio_digest = 1469598103934665603ULL;
    for (int i = 0; i < 300; i++) retro_run();
    printf("ACCEPTED    300 frames after load: video %016llx  audio %016llx\n",
           (unsigned long long)g_video_digest, (unsigned long long)g_audio_digest);
    printf("VERDICT     compare that video digest against the other platform's\n");
    return 0;
}
