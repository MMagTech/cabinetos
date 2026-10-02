#include "quality.h"

#include <cstdio>

#include <json-c/json.h>

#include "gpu.h"
#include "prefs.h"
#include "storage.h"

namespace quality {

namespace {

// THE TABLE, one row per level: Performance, Balanced, Quality. Performance is
// each system's own resolution, Balanced about 1080p, Quality about 4K, using
// the emulator's own "~1080p" and "~4K" steps where it labels them. Every
// Quality value was measured on the A9 (2026-10-02) and stepped down where
// 4K was tight there: PS2 and Wii 5x rather than 6x, N64 4x rather than 8x.
// Starting values, judged on the TV before they ship and adjustable in one
// line each.
template <typename T>
const T& pick(Level l, const T (&row)[kLevelCount]) {
    return row[static_cast<int>(l)];
}

std::string gamesPath() { return storage::configDir() + "/picture-quality.json"; }

json_object* loadGames() {
    std::string body;
    if (FILE* f = std::fopen(gamesPath().c_str(), "rb")) {
        char buf[1024];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
        std::fclose(f);
    }
    json_object* o = body.empty() ? nullptr : json_tokener_parse(body.c_str());
    if (o && json_object_get_type(o) == json_type_object) return o;
    if (o) json_object_put(o);
    return json_object_new_object();
}

}  // namespace

const char* levelName(Level l) {
    static const char* const kNames[] = {"Performance", "Balanced", "Quality"};
    return kNames[static_cast<int>(l)];
}

const char* levelWord(Level l) {
    static const char* const kWords[] = {"performance", "balanced", "quality"};
    return kWords[static_cast<int>(l)];
}

bool levelFromWord(const std::string& word, Level* out) {
    for (int i = 0; i < kLevelCount; ++i) {
        if (word == levelWord(static_cast<Level>(i))) {
            *out = static_cast<Level>(i);
            return true;
        }
    }
    return false;
}

Level console() {
    Level l = Level::Performance;
    levelFromWord(prefs::get("picture_quality", ""), &l);
    return l;
}

void setConsole(Level l) { prefs::set("picture_quality", levelWord(l)); }

// A FILE OF ITS OWN, `config/picture-quality.json`, `{"<rom id>": "balanced"}`,
// rather than lines in settings.json: it grows with the library, and
// settings.json is the handful of words a person reads over file access.
std::optional<Level> gameChoice(int romId) {
    json_object* o = loadGames();
    std::optional<Level> out;
    json_object* v = nullptr;
    Level l;
    if (json_object_object_get_ex(o, std::to_string(romId).c_str(), &v) &&
        json_object_get_type(v) == json_type_string && levelFromWord(json_object_get_string(v), &l))
        out = l;
    json_object_put(o);
    return out;
}

void setGameChoice(int romId, std::optional<Level> l) {
    json_object* o = loadGames();
    const std::string key = std::to_string(romId);
    if (l)
        json_object_object_add(o, key.c_str(), json_object_new_string(levelWord(*l)));
    else
        json_object_object_del(o, key.c_str());
    storage::makeDirs(storage::configDir());
    const std::string tmp = gamesPath() + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY), f);
        std::fputc('\n', f);
        const bool ok = std::fflush(f) == 0;
        std::fclose(f);
        if (!ok || std::rename(tmp.c_str(), gamesPath().c_str()) != 0)
            std::fprintf(stderr, "[quality] could not write %s\n", gamesPath().c_str());
    }
    json_object_put(o);
}

Level forGame(int romId) {
    if (const std::optional<Level> own = gameChoice(romId)) return *own;
    return console();
}

std::map<std::string, std::string> coreOptions(const std::string& core,
                                               const std::string& platformSlug, Level l) {
    // GameCube and Wii: internal resolution, and shaders built in the
    // background (2, Async UberShaders) above Performance, when first needed
    // (0, Synchronous) at it. Anisotropic filtering 16x is `4` in Dolphin's
    // numbering. Wii's Quality is 5x: 6x was tight on the A9 in Mario Kart
    // Wii's racing (1.2 to 1.5x realtime), GameCube's 6x comfortable (2.6x).
    if (core == "dolphin") {
        static const char* const kGameCube[] = {"1", "3", "6"};
        static const char* const kWii[] = {"1", "3", "5"};
        static const char* const kShaders[] = {"0", "2", "2"};
        return {
            {"dolphin_efb_scale", pick(l, platformSlug == "wii" ? kWii : kGameCube)},
            {"dolphin_shader_compilation_mode", pick(l, kShaders)},
            {"dolphin_max_anisotropy", "4"},
        };
    }
    // PSP: 480x272, then 4x (1920x1088) and 8x (3840x2176), PPSSPP's own
    // steps. Anisotropic filtering is already 16x by default; said anyway, so
    // a core bump that changed the default is caught by the option check.
    if (core == "ppsspp") {
        static const char* const kRes[] = {"480x272", "1920x1088", "3840x2176"};
        return {
            {"ppsspp_internal_resolution", pick(l, kRes)},
            {"ppsspp_texture_anisotropic_filtering", "16x"},
        };
    }
    // Nintendo 64. ParaLLEl-RDP with Vulkan (catalog::optionOverrides chooses
    // it): 1x, 4x, and 4x again at Quality, because 8x left about 10% to
    // spare at 60 fps on the A9 and there is no step between. Without Vulkan,
    // GLideN64 by the same rule: the N64's own 320x240, then 1280x960.
    if (core == "mupen64plus") {
        if (cab::gpu::vulkan().available) {
            static const char* const kScale[] = {"1x", "4x", "4x"};
            return {{"mupen64plus-parallel-rdp-upscaling", pick(l, kScale)}};
        }
        static const char* const kSize[] = {"320x240", "1280x960", "1280x960"};
        return {{"mupen64plus-43screensize", pick(l, kSize)}};
    }
    // Dreamcast and Naomi: 640x480, 1440x1080, 2880x2160. Anisotropic
    // filtering from Flycast's 4x to 16x.
    if (core == "flycast") {
        static const char* const kRes[] = {"640x480", "1440x1080", "2880x2160"};
        return {
            {"reicast_internal_resolution", pick(l, kRes)},
            {"reicast_anisotropic_filtering", "16"},
        };
    }
    // FIXED AT EVERY LEVEL (MMagTech, 2026-10-02): the processor-side
    // extras that cost nothing a modern PC notices.
    //
    // 3DO at 640x480 rather than 320x240, and its sound processor on its own
    // thread: both as Batocera ships them.
    if (core == "opera") {
        return {
            {"opera_high_resolution", "enabled"},
            {"opera_dsp_threaded", "enabled"},
        };
    }
    // Vector games drawn at their sharpest: thin lines stretched to 4K
    // otherwise look soft and thick.
    if (core == "vecx") return {{"vecx_res_multi", "4"}};
    if (core == "mame2003_plus") return {{"mame2003-plus_vector_resolution", "1707x1280"}};
    return {};
}

Ps2 ps2(Level l) {
    // PS2: 1x, 3x (PCSX2's "~1080p"), and 5x at Quality. 6x was tight on the
    // A9: copying each frame back from the GPU took 12.4 ms of the 16.7 ms
    // frame, against 5.9 ms at 5x. Anisotropic filtering 16x at every level.
    static const float kUpscale[] = {1.0f, 3.0f, 5.0f};
    Ps2 p;
    p.upscale = pick(l, kUpscale);
    p.anisotropy = 16;
    return p;
}

std::vector<Setting> eden(Level l) {
    // Switch, always docked (1920x1080 is its own resolution): 1x, 1x, 2x.
    // Eden numbers its steps from a quarter, so 1x is 3 and 2x is 6
    // (settings_enums.h, ResolutionSetup). Anisotropic 16x is 5 (AnisotropyMode:
    // Automatic, Default, X2, X4, X8, X16). Shaders in the background above
    // Performance. Not yet measured on the A9.
    static const char* const kRes[] = {"3", "3", "6"};
    static const char* const kAsync[] = {"false", "true", "true"};
    return {
        {"Renderer", "resolution_setup", pick(l, kRes)},
        {"Renderer", "max_anisotropy", "5"},
        {"Renderer", "use_asynchronous_shaders", pick(l, kAsync)},
    };
}

std::string rpcs3(Level l) {
    // PS3, whose games are 720p: 100%, 150% (1080p), 300% (4K). The game's own
    // video mode stays 720p; that one changes the game. Not yet measured.
    static const char* const kScale[] = {"100", "150", "300"};
    return std::string("Video:\n  Resolution Scale: ") + pick(l, kScale) +
           "\n  Anisotropic Filter Override: 16\n";
}

std::string xemu(Level l) {
    // Xbox, 640x480: 1x, 2x, 4x. xemu has no anisotropic filtering setting.
    // Not yet measured.
    static const char* const kScale[] = {"1", "2", "4"};
    return std::string("surface_scale = ") + pick(l, kScale) + "\n";
}

std::vector<std::string> xenia(Level l) {
    // Xbox 360, 720p: 1x, 1x, 3x (2160p). 2x would be 1440p, neither target.
    // Edge's anisotropic override counts 1 to 5 for 1x to 16x. Not yet
    // measured; Xenia's upscaling needs sparse binding on Vulkan.
    static const char* const kScale[] = {"1", "1", "3"};
    const std::string s = pick(l, kScale);
    return {
        "--draw_resolution_scale_x=" + s,
        "--draw_resolution_scale_y=" + s,
        "--anisotropic_override=5",
    };
}

void logApplied(const std::string& who, int romId, Level l) {
    const bool own = gameChoice(romId).has_value();
    std::fprintf(stderr, "[quality] %s: %s (%s)\n", who.c_str(), levelName(l),
                 own ? "this game's own choice" : "console setting");
}

}  // namespace quality
