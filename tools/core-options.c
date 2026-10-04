// What can this core be configured with, exactly?
//
// THE VERSION-BUMP CHECK (#63, MMagTech 2026-10-02). The console tells each
// core a handful of option values (catalog::optionOverrides, quality.h,
// sysopts.h). A core upgrade that renames an option, drops a value or moves a
// default would leave those answers going nowhere, silently: a renamed
// `dolphin_efb_scale` plays every game at native resolution and nothing says
// so. Every core is pinned, so this can only happen when a commit moves a pin,
// and this tool is how that commit finds out.
//
// It loads the core, answers the environment the way the frontend does
// (core.cpp: every generation of the options API, US table, version 2), and
// prints one line per option, sorted:
//
//     <key>\t<default>\t<value>|<value>|...
//
// cores/check-options.sh compares that with cores/options/<core>.txt and fails on any
// difference; the frontend's --check-option-tables then checks every value
// the console sets against the same files.
//
// SOME CORES DECLARE ONLY WITH A GAME LOADED. FCEUmm and MAME 2003-Plus do it
// inside retro_load_game and declare before they look at the file, so a stub
// is enough (`--game`). Dolphin and FBNeo need a real game; their lists come
// from the A9 (tools/check-applied.sh), not from CI.
//
//   core-options <core.so> [--game <file>]
//
// Build:
//   cc -O2 -o core-options tools/core-options.c -ldl

#include <dlfcn.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../frontend/src/libretro.h"

#define MAX_OPTS 1024

struct opt {
    char *key;
    char *def;
    char *values;   // joined with '|'
};

static struct opt g_opts[MAX_OPTS];
static int g_count;

static char *dup_or_empty(const char *s) { return strdup(s ? s : ""); }

// A repeat replaces, as core.cpp's declareOption does.
static void declare(const char *key, const char *def, const char *values) {
    if (!key || !*key) return;
    for (int i = 0; i < g_count; ++i) {
        if (strcmp(g_opts[i].key, key) == 0) {
            free(g_opts[i].def);
            free(g_opts[i].values);
            g_opts[i].def = dup_or_empty(def);
            g_opts[i].values = dup_or_empty(values);
            return;
        }
    }
    if (g_count >= MAX_OPTS) return;
    g_opts[g_count].key = strdup(key);
    g_opts[g_count].def = dup_or_empty(def);
    g_opts[g_count].values = dup_or_empty(values);
    ++g_count;
}

static void join_values(const struct retro_core_option_value *v, char *out, size_t n) {
    out[0] = '\0';
    for (; v && v->value; ++v) {
        if (out[0]) strncat(out, "|", n - strlen(out) - 1);
        strncat(out, v->value, n - strlen(out) - 1);
    }
}

static void capture_v1(const struct retro_core_option_definition *d) {
    char buf[8192];
    for (; d && d->key; ++d) {
        join_values(d->values, buf, sizeof buf);
        // No stated default: the first value, as the API documents.
        const char *def = d->default_value;
        char first[256] = "";
        if (!def || !*def) {
            const char *bar = strchr(buf, '|');
            size_t len = bar ? (size_t)(bar - buf) : strlen(buf);
            if (len >= sizeof first) len = sizeof first - 1;
            memcpy(first, buf, len);
            first[len] = '\0';
            def = first;
        }
        declare(d->key, def, buf);
    }
}

static void capture_v2(const struct retro_core_options_v2 *o) {
    if (!o) return;
    char buf[8192];
    for (const struct retro_core_option_v2_definition *d = o->definitions; d && d->key; ++d) {
        join_values(d->values, buf, sizeof buf);
        const char *def = d->default_value;
        char first[256] = "";
        if (!def || !*def) {
            const char *bar = strchr(buf, '|');
            size_t len = bar ? (size_t)(bar - buf) : strlen(buf);
            if (len >= sizeof first) len = sizeof first - 1;
            memcpy(first, buf, len);
            first[len] = '\0';
            def = first;
        }
        declare(d->key, def, buf);
    }
}

// "Description; first|second|third", the original format.
static void capture_variables(const struct retro_variable *v) {
    for (; v && v->key; ++v) {
        const char *val = v->value ? v->value : "";
        const char *semi = strchr(val, ';');
        const char *list = semi ? semi + 1 : val;
        while (*list == ' ' || *list == '\t') ++list;
        char first[256];
        const char *bar = strchr(list, '|');
        size_t len = bar ? (size_t)(bar - list) : strlen(list);
        if (len >= sizeof first) len = sizeof first - 1;
        memcpy(first, list, len);
        first[len] = '\0';
        declare(v->key, first, list);
    }
}

static void log_cb(enum retro_log_level level, const char *fmt, ...) {
    (void)level;
    (void)fmt;
}

static bool environment(unsigned cmd, void *data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_VARIABLES:
            capture_variables((const struct retro_variable *)data);
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS:
            capture_v1((const struct retro_core_option_definition *)data);
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_INTL:
            if (data) capture_v1(((const struct retro_core_options_intl *)data)->us);
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2:
            capture_v2((const struct retro_core_options_v2 *)data);
            return true;
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_V2_INTL:
            if (data) capture_v2(((const struct retro_core_options_v2_intl *)data)->us);
            return true;
        case RETRO_ENVIRONMENT_GET_CORE_OPTIONS_VERSION:
            *(unsigned *)data = 2;
            return true;
        case RETRO_ENVIRONMENT_GET_LOG_INTERFACE:
            ((struct retro_log_callback *)data)->log = log_cb;
            return true;
        case RETRO_ENVIRONMENT_GET_SYSTEM_DIRECTORY:
        case RETRO_ENVIRONMENT_GET_SAVE_DIRECTORY:
            *(const char **)data = "/tmp";
            return true;
        case RETRO_ENVIRONMENT_SET_PIXEL_FORMAT:
        case RETRO_ENVIRONMENT_SET_CORE_OPTIONS_DISPLAY:
        case RETRO_ENVIRONMENT_SET_SUPPORT_NO_GAME:
            return true;
        case RETRO_ENVIRONMENT_GET_CAN_DUPE:
            *(bool *)data = true;
            return true;
        default:
            // GET_VARIABLE unanswered: the core keeps its own default, which
            // is all a listing needs. SET_HW_RENDER refused: nothing draws.
            return false;
    }
}

static void video_refresh(const void *d, unsigned w, unsigned h, size_t p) {
    (void)d; (void)w; (void)h; (void)p;
}
static void audio_sample(int16_t l, int16_t r) { (void)l; (void)r; }
static size_t audio_batch(const int16_t *d, size_t f) { (void)d; return f; }
static void input_poll(void) {}
static int16_t input_state(unsigned port, unsigned dev, unsigned idx, unsigned id) {
    (void)port; (void)dev; (void)idx; (void)id;
    return 0;
}

static int by_key(const void *a, const void *b) {
    return strcmp(((const struct opt *)a)->key, ((const struct opt *)b)->key);
}

#define SYM(name) \
    do { \
        *(void **)(&name) = dlsym(core, #name); \
        if (!name) { fprintf(stderr, "core-options: missing %s\n", #name); return 1; } \
    } while (0)

int main(int argc, char **argv) {
    if (argc != 2 && !(argc == 4 && strcmp(argv[2], "--game") == 0)) {
        fprintf(stderr, "usage: core-options <core.so> [--game <file>]\n");
        return 2;
    }
    void *core = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!core) {
        const char *why = dlerror();
        fprintf(stderr, "core-options: %s: %s\n", argv[1], why ? why : "dlopen failed");
        return 1;
    }
    void (*retro_set_environment)(retro_environment_t);
    void (*retro_set_video_refresh)(retro_video_refresh_t);
    void (*retro_set_audio_sample)(retro_audio_sample_t);
    void (*retro_set_audio_sample_batch)(retro_audio_sample_batch_t);
    void (*retro_set_input_poll)(retro_input_poll_t);
    void (*retro_set_input_state)(retro_input_state_t);
    void (*retro_init)(void);
    bool (*retro_load_game)(const struct retro_game_info *);
    SYM(retro_set_environment);
    SYM(retro_set_video_refresh);
    SYM(retro_set_audio_sample);
    SYM(retro_set_audio_sample_batch);
    SYM(retro_set_input_poll);
    SYM(retro_set_input_state);
    SYM(retro_init);
    SYM(retro_load_game);

    retro_set_environment(environment);
    retro_set_video_refresh(video_refresh);
    retro_set_audio_sample(audio_sample);
    retro_set_audio_sample_batch(audio_batch);
    retro_set_input_poll(input_poll);
    retro_set_input_state(input_state);
    retro_init();

    if (argc == 4) {
        // The stub's contents do not matter: these cores declare their
        // options before they look at the file. Whether the load then
        // succeeds is not this tool's question.
        static char buf[1 << 16];
        size_t n = 0;
        FILE *f = fopen(argv[3], "rb");
        if (f) {
            n = fread(buf, 1, sizeof buf, f);
            fclose(f);
        }
        struct retro_game_info info = {argv[3], buf, n, NULL};
        (void)retro_load_game(&info);
    }

    qsort(g_opts, (size_t)g_count, sizeof g_opts[0], by_key);
    for (int i = 0; i < g_count; ++i)
        printf("%s\t%s\t%s\n", g_opts[i].key, g_opts[i].def, g_opts[i].values);
    fflush(stdout);
    // No retro_deinit: several cores tear down threads and atexit handlers in
    // ways a listing does not need to survive.
    _exit(g_count > 0 ? 0 : 3);
}
