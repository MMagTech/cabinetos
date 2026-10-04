#include "screenfx.h"

#include <GLES3/gl3.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>

#include "image.h"
#include "prefs.h"

namespace screenfx {

namespace {

// THE TEST LIST (#122), every candidate MMagTech is judging on the TV. The
// labels are the shaders' own names so a choice can be named back; the list is
// cut to the keepers once he has seen them.
const std::vector<Look> kConsole = {
    {"plain", "Plain", nullptr},
    {"sharp-bilinear", "Sharp", "interpolation/sharp-bilinear-simple.glslp"},
    {"crt-easymode", "CRT easymode", "crt/crt-easymode.glslp"},
    {"crt-easymode-halation", "CRT easymode glow", "crt/crt-easymode-halation.glslp"},
    {"crt-lottes", "CRT lottes", "crt/crt-lottes.glslp"},
    {"crt-geom", "CRT geom", "crt/crt-geom.glslp"},
    {"zfast-crt", "CRT zfast", "crt/zfast-crt.glslp"},
    {"crt-aperture", "CRT aperture", "crt/crt-aperture.glslp"},
    {"crt-guest", "CRT guest", "crt/crt-guest-dr-venom.glslp"},
    {"composite", "Composite", "ntsc/ntsc-adaptive.glslp"},
};
// N64, Dreamcast and 3DO are drawn above their own size (3DO at 640x480), and
// most CRT shaders draw one scanline per line of what they are given: close to
// one per TV row here, which crawls as the picture moves (zfast on N64 and
// Dreamcast, judged on the TV 2026-10-04), and crt-geom takes anything over 400
// lines for an interlaced signal and flickers. easymode and lottes held steady.
const std::vector<Look> kUpscaled = {
    {"plain", "Plain", nullptr},
    {"sharp-bilinear", "Sharp", "interpolation/sharp-bilinear-simple.glslp"},
    {"crt-easymode", "CRT easymode", "crt/crt-easymode.glslp"},
    {"crt-easymode-halation", "CRT easymode glow", "crt/crt-easymode-halation.glslp"},
    {"crt-lottes", "CRT lottes", "crt/crt-lottes.glslp"},
    {"crt-aperture", "CRT aperture", "crt/crt-aperture.glslp"},
    {"crt-guest", "CRT guest", "crt/crt-guest-dr-venom.glslp"},
    {"composite", "Composite", "ntsc/ntsc-adaptive.glslp"},
};
#define CABINETOS_LCD_LOOKS                                                     \
    {"plain", "Plain", nullptr},                                                \
    {"sharp-bilinear", "Sharp", "interpolation/sharp-bilinear-simple.glslp"},   \
    {"lcd3x", "LCD 3x", "handheld/lcd3x.glslp"},                                \
    {"lcd-grid-v2", "LCD grid", "handheld/lcd-grid-v2.glslp"},                  \
    {"zfast-lcd", "LCD zfast", "handheld/zfast-lcd.glslp"}
const std::vector<Look> kHandheld = {CABINETOS_LCD_LOOKS};
// The original Game Boy's three screens. Each repaints the picture in its
// own shades, so they suit a mono game and override the Colors row.
const std::vector<Look> kGameBoy = {
    CABINETOS_LCD_LOOKS,
    {"gameboy", "Dot matrix", "handheld/gameboy.glslp"},
    {"gameboy-pocket", "Dot matrix Pocket", "handheld/gameboy-pocket.glslp"},
    {"gameboy-light", "Dot matrix Light", "handheld/gameboy-light.glslp"},
};
// Game Boy Color: the dot matrix that keeps the game's colours, since the
// green one turns a colour game green.
const std::vector<Look> kGameBoyColor = {
    CABINETOS_LCD_LOOKS,
    {"gbc-dot-matrix", "Dot matrix", "handheld/gbc-dot-matrix-white.glslp"},
};
#undef CABINETOS_LCD_LOOKS
const std::vector<Look> kNone;

// Up to and including Dreamcast, the systems this console draws itself
// (MMagTech, 2026-10-04). Not PS2 or newer, which are played upscaled.
const char* const kConsoleSlugs[] = {
    "nes", "snes", "genesis", "sms", "segacd", "sega32", "tg16", "turbografx-cd",
    "atari2600", "atari7800", "arcade", "psx", "saturn",
};
const char* const kUpscaledSlugs[] = {"n64", "dc", "3do"};
const char* const kHandheldSlugs[] = {"gba", "gamegear", "neo-geo-pocket-color"};

std::string prefKey(const std::string& slug) { return "look." + slug; }

// presetFor is asked every frame, and prefs reads the file each time.
std::map<std::string, std::string>& presetCache() {
    static std::map<std::string, std::string> m;
    return m;
}

std::string readFile(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

std::string trim(std::string s) {
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t' || s.back() == '\r'))
        s.pop_back();
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t')) ++i;
    s = s.substr(i);
    if (s.size() >= 2 && s.front() == '"' && s.back() == '"') s = s.substr(1, s.size() - 2);
    return s;
}

std::string dirOf(const std::string& path) {
    const size_t slash = path.rfind('/');
    return slash == std::string::npos ? "." : path.substr(0, slash);
}

bool truthy(const std::string& v) { return v == "true" || v == "1"; }

bool hasExtension(const char* name) {
    GLint n = 0;
    glGetIntegerv(GL_NUM_EXTENSIONS, &n);
    for (GLint i = 0; i < n; ++i) {
        const char* e = reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i));
        if (e && std::strcmp(e, name) == 0) return true;
    }
    return false;
}

bool borderClampSupported() {
    static int known = -1;
    if (known < 0) {
        const char* v = reinterpret_cast<const char*>(glGetString(GL_VERSION));
        known = (v && std::strstr(v, "OpenGL ES 3.2")) ||
                hasExtension("GL_EXT_texture_border_clamp") ||
                hasExtension("GL_OES_texture_border_clamp");
    }
    return known == 1;
}

constexpr GLenum kClampToBorder = 0x812D;

GLenum wrapMode(const std::string& v) {
    if (v == "repeat") return GL_REPEAT;
    if (v == "mirrored_repeat") return GL_MIRRORED_REPEAT;
    if (v == "clamp_to_edge") return GL_CLAMP_TO_EDGE;
    // clamp_to_border is RetroArch's default: black outside the picture.
    return borderClampSupported() ? kClampToBorder : GL_CLAMP_TO_EDGE;
}

// RetroArch's shaders are written for desktop GL, where precision qualifiers
// mean nothing, and say "mediump" for GLES phones. On this console's GPU a
// mediump float may be 16 bits, which cannot even count the 3840 columns a
// mask is laid out on, so every qualifier is read as highp: the picture
// RetroArch's own desktop build draws.
std::string highpOnly(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    auto isId = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (size_t i = 0; i < s.size();) {
        if (isId(s[i]) && (i == 0 || !isId(s[i - 1]))) {
            size_t j = i;
            while (j < s.size() && isId(s[j])) ++j;
            const std::string word = s.substr(i, j - i);
            out += (word == "mediump" || word == "lowp") ? "highp" : word;
            i = j;
        } else {
            out += s[i++];
        }
    }
    return out;
}

// The source as RetroArch's GL driver hands it to the compiler: its own
// #version removed, ours first, then which stage and that parameters are
// uniforms. "#pragma parameter" lines are commented out: they are RetroArch's
// to read, and their quoted names are not GLSL.
//
// A DEFAULT PRECISION FIRST, because desktop GL has one and GLSL ES does not:
// a file that declares precision only inside its fragment half, or only under
// GL_ES, otherwise fails on the first vec4 (crt-hyllian, line 95).
//
// `uniforms` false bakes every parameter in at its default instead. GLSL ES
// forbids a global initialised from a uniform, which desktop GLSL allows, and
// crt-hyllian does exactly that with its settings; as constants they compile.
std::string prepare(const std::string& body, const char* stage, bool es3, bool uniforms) {
    std::string out = es3 ? "#version 300 es\n" : "#version 100\n";
    out += std::string("#define ") + stage + "\n";
    if (uniforms) out += "#define PARAMETER_UNIFORM\n";
    out += "precision highp float;\nprecision highp int;\n";
    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.rfind("#version", 0) == 0 || t.rfind("#pragma parameter", 0) == 0)
            out += "// " + line + "\n";
        else
            out += line + "\n";
    }
    return highpOnly(out);
}

GLuint compile(GLenum type, const std::string& src, std::string& log) {
    GLuint s = glCreateShader(type);
    const char* p = src.c_str();
    glShaderSource(s, 1, &p, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char buf[2048] = {};
        glGetShaderInfoLog(s, sizeof buf - 1, nullptr, buf);
        log += buf;
        glDeleteShader(s);
        return 0;
    }
    return s;
}

// Tried in order: GLSL ES 3.00 with live parameters, then with fixed ones,
// then GLSL ES 1.00 (RetroArch's GLES 2 path) the same two ways.
GLuint linkPass(const std::string& body, std::string& log, bool& es3Out, bool& uniformsOut) {
    for (int attempt = 0; attempt < 4; ++attempt) {
        const bool es3 = attempt < 2, uniforms = attempt % 2 == 0;
        std::string l;
        GLuint vs = compile(GL_VERTEX_SHADER, prepare(body, "VERTEX", es3, uniforms), l);
        GLuint fs =
            vs ? compile(GL_FRAGMENT_SHADER, prepare(body, "FRAGMENT", es3, uniforms), l) : 0;
        if (!vs || !fs) {
            if (vs) glDeleteShader(vs);
            log += std::string(es3 ? "[300 es" : "[100") + (uniforms ? "] " : ", fixed] ") + l;
            continue;
        }
        GLuint p = glCreateProgram();
        glAttachShader(p, vs);
        glAttachShader(p, fs);
        glBindAttribLocation(p, 0, "VertexCoord");
        glBindAttribLocation(p, 1, "TexCoord");
        glBindAttribLocation(p, 2, "COLOR");
        glBindAttribLocation(p, 2, "Color");
        glLinkProgram(p);
        glDeleteShader(vs);
        glDeleteShader(fs);
        GLint ok = 0;
        glGetProgramiv(p, GL_LINK_STATUS, &ok);
        if (ok) {
            es3Out = es3;
            uniformsOut = uniforms;
            return p;
        }
        char buf[2048] = {};
        glGetProgramInfoLog(p, sizeof buf - 1, nullptr, buf);
        log += std::string(es3 ? "[300 es link] " : "[100 link] ") + buf;
        glDeleteProgram(p);
    }
    return 0;
}

// The #pragma parameter defaults, as RetroArch reads them.
void readParameters(const std::string& body, std::map<std::string, float>& out) {
    std::istringstream in(body);
    std::string line;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.rfind("#pragma parameter", 0) != 0) continue;
        std::istringstream ls(t.substr(17));
        std::string name;
        ls >> name;
        const size_t q1 = t.find('"'), q2 = q1 == std::string::npos ? q1 : t.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        std::istringstream rest(t.substr(q2 + 1));
        float def = 0;
        if (rest >> def) out.emplace(name, def);  // first file to declare it wins
    }
}

const char* kCopyVS = R"(#version 300 es
precision highp float;
layout(location = 0) in vec4 VertexCoord;
uniform vec4 uUV;
out vec2 vUV;
void main() {
    vUV = mix(uUV.xy, uUV.zw, VertexCoord.xy);
    gl_Position = vec4(VertexCoord.xy * 2.0 - 1.0, 0.0, 1.0);
}
)";
const char* kCopyFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform sampler2D uTex;
out vec4 fragColor;
void main() { fragColor = vec4(texture(uTex, vUV).rgb, 1.0); }
)";

void setFilter(GLuint tex, bool linear, bool mipmap, GLenum wrap) {
    glBindTexture(GL_TEXTURE_2D, tex);
    if (mipmap) glGenerateMipmap(GL_TEXTURE_2D);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                    mipmap ? GL_LINEAR_MIPMAP_LINEAR : (linear ? GL_LINEAR : GL_NEAREST));
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, linear ? GL_LINEAR : GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrap);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrap);
}

}  // namespace

const std::vector<Look>& looksFor(const std::string& slug) {
    if (slug == "gb") return kGameBoy;
    if (slug == "gbc") return kGameBoyColor;
    for (const char* s : kHandheldSlugs)
        if (slug == s) return kHandheld;
    for (const char* s : kConsoleSlugs)
        if (slug == s) return kConsole;
    for (const char* s : kUpscaledSlugs)
        if (slug == s) return kUpscaled;
    return kNone;
}

// ON BY DEFAULT (MMagTech, 2026-10-04): the plain picture is the weakest on a
// 4K set. Every TV system starts on crt-easymode: flat, so nothing in a corner
// is bent or cut, the brightest of the CRT looks, Batocera's own "Scanlines",
// and steady on N64. The handheld default waits for MMagTech's pick of LCD.
const char* defaultFor(const std::string& slug) {
    const auto& looks = looksFor(slug);
    if (&looks == &kConsole || &looks == &kUpscaled) return "crt-easymode";
    return "plain";
}

int chosen(const std::string& slug) {
    const auto& looks = looksFor(slug);
    const std::string v = prefs::get(prefKey(slug), defaultFor(slug));
    for (size_t i = 0; i < looks.size(); ++i)
        if (v == looks[i].id) return static_cast<int>(i);
    // A look this system no longer offers (zfast on N64) falls back to the
    // default rather than being trusted, as in Cabinet.
    for (size_t i = 0; i < looks.size(); ++i)
        if (std::strcmp(defaultFor(slug), looks[i].id) == 0) return static_cast<int>(i);
    return 0;
}

void choose(const std::string& slug, int index) {
    const auto& looks = looksFor(slug);
    if (index < 0 || index >= static_cast<int>(looks.size())) return;
    prefs::set(prefKey(slug), looks[index].id);
    presetCache()[slug] = looks[index].preset ? shaderDir() + "/" + looks[index].preset : "";
}

std::string presetFor(const std::string& slug) {
    auto& cache = presetCache();
    auto it = cache.find(slug);
    if (it != cache.end()) return it->second;
    const auto& looks = looksFor(slug);
    std::string out;
    if (!looks.empty()) {
        const Look& l = looks[chosen(slug)];
        if (l.preset) out = shaderDir() + "/" + l.preset;
    }
    cache[slug] = out;
    return out;
}

std::string shaderDir() {
    const char* env = std::getenv("CABINETOS_SHADERS");
    return env && *env ? env : "/usr/share/cabinetos/shaders";
}

Chain& shared() {
    static Chain c;
    return c;
}

void Chain::release() {
    for (Pass& p : passes_) {
        if (p.program) glDeleteProgram(p.program);
        if (p.out.fbo) glDeleteFramebuffers(1, &p.out.fbo);
        if (p.out.tex) glDeleteTextures(1, &p.out.tex);
    }
    passes_.clear();
    for (Lut& l : luts_)
        if (l.tex) glDeleteTextures(1, &l.tex);
    luts_.clear();
    for (Target& t : history_) {
        if (t.fbo) glDeleteFramebuffers(1, &t.fbo);
        if (t.tex) glDeleteTextures(1, &t.tex);
    }
    history_.clear();
    params_.clear();
    wantsHistory_ = false;
    historyHead_ = 0;
}

void Chain::use(const std::string& path) {
    if (path == path_) return;
    release();
    path_ = path;
    frameCount_ = 0;
    if (path.empty()) return;
    if (!load(path)) release();
}

bool Chain::load(const std::string& path) {
    const std::string preset = readFile(path);
    if (preset.empty()) {
        std::fprintf(stderr, "[look] %s: cannot read\n", path.c_str());
        return false;
    }
    std::map<std::string, std::string> kv;
    {
        std::istringstream in(preset);
        std::string line;
        while (std::getline(in, line)) {
            const std::string t = trim(line);
            if (t.empty() || t[0] == '#') continue;
            const size_t eq = t.find('=');
            if (eq == std::string::npos) continue;
            kv[trim(t.substr(0, eq))] = trim(t.substr(eq + 1));
        }
    }
    auto get = [&](const std::string& k) {
        auto it = kv.find(k);
        return it == kv.end() ? std::string() : it->second;
    };
    const int count = std::atoi(get("shaders").c_str());
    if (count <= 0) {
        std::fprintf(stderr, "[look] %s: no passes\n", path.c_str());
        return false;
    }
    const std::string base = dirOf(path);

    if (!vao_) {
        glGenVertexArrays(1, &vao_);
        glGenBuffers(1, &vbo_);
        GLint prevVao = 0, prevBuf = 0;
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevBuf);
        glBindVertexArray(vao_);
        glBindBuffer(GL_ARRAY_BUFFER, vbo_);
        // VertexCoord and TexCoord are the same unit square: every texture
        // here is exactly the size of the picture it holds.
        const float v[] = {0, 0, 0, 1, 0, 0, 0, 0,  1, 0, 0, 1, 1, 0, 0, 0,
                           0, 1, 0, 1, 0, 1, 0, 0,  1, 1, 0, 1, 1, 1, 0, 0};
        glBufferData(GL_ARRAY_BUFFER, sizeof v, v, GL_STATIC_DRAW);
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), nullptr);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float),
                              reinterpret_cast<void*>(4 * sizeof(float)));
        glDisableVertexAttribArray(2);
        glBindVertexArray(prevVao);
        glBindBuffer(GL_ARRAY_BUFFER, prevBuf);
    }
    if (!copyProgram_) {
        std::string log;
        GLuint vs = compile(GL_VERTEX_SHADER, kCopyVS, log);
        GLuint fs = compile(GL_FRAGMENT_SHADER, kCopyFS, log);
        copyProgram_ = glCreateProgram();
        glAttachShader(copyProgram_, vs);
        glAttachShader(copyProgram_, fs);
        glLinkProgram(copyProgram_);
        glDeleteShader(vs);
        glDeleteShader(fs);
        copyUV_ = glGetUniformLocation(copyProgram_, "uUV");
    }

    std::string versions;
    for (int i = 0; i < count; ++i) {
        const std::string n = std::to_string(i);
        const std::string file = base + "/" + get("shader" + n);
        const std::string body = readFile(file);
        if (body.empty()) {
            std::fprintf(stderr, "[look] %s: cannot read %s\n", path.c_str(), file.c_str());
            return false;
        }
        readParameters(body, params_);
        Pass p;
        std::string log;
        bool es3 = true, uniforms = true;
        p.program = linkPass(body, log, es3, uniforms);
        if (!p.program) {
            std::fprintf(stderr, "[look] %s: pass %d did not compile:\n%s\n", path.c_str(), i,
                         log.c_str());
            passes_.push_back(p);
            return false;
        }
        versions += std::string(es3 ? " 300es" : " 100") + (uniforms ? "" : "-fixed");
        GLint nu = 0;
        glGetProgramiv(p.program, GL_ACTIVE_UNIFORMS, &nu);
        for (GLint u = 0; u < nu; ++u) {
            char name[256] = {};
            GLint size = 0;
            GLenum type = 0;
            glGetActiveUniform(p.program, u, sizeof name - 1, nullptr, &size, &type, name);
            std::string s = name;
            if (s.size() > 3 && s.compare(s.size() - 3, 3, "[0]") == 0) s.resize(s.size() - 3);
            p.loc[s] = glGetUniformLocation(p.program, s.c_str());
            p.type[s] = type;
            if (s.rfind("Prev", 0) == 0 && s.find("Texture") != std::string::npos)
                wantsHistory_ = true;
        }
        const std::string lin = get("filter_linear" + n);
        p.linearSet = !lin.empty();
        p.linear = truthy(lin);
        p.mipmap = truthy(get("mipmap_input" + n));
        p.wrap = wrapMode(get("wrap_mode" + n));
        const std::string st = get("scale_type" + n);
        p.scaleTypeX = get("scale_type_x" + n);
        p.scaleTypeY = get("scale_type_y" + n);
        if (p.scaleTypeX.empty()) p.scaleTypeX = st;
        if (p.scaleTypeY.empty()) p.scaleTypeY = st;
        p.scaleSet = !p.scaleTypeX.empty() || !p.scaleTypeY.empty();
        const std::string sc = get("scale" + n);
        p.scaleX = p.scaleY = sc.empty() ? 1.0f : std::strtof(sc.c_str(), nullptr);
        if (!get("scale_x" + n).empty()) p.scaleX = std::strtof(get("scale_x" + n).c_str(), nullptr);
        if (!get("scale_y" + n).empty()) p.scaleY = std::strtof(get("scale_y" + n).c_str(), nullptr);
        p.floatFb = truthy(get("float_framebuffer" + n));
        p.srgbFb = truthy(get("srgb_framebuffer" + n));
        p.frameCountMod = static_cast<unsigned>(std::atoi(get("frame_count_mod" + n).c_str()));
        p.alias = get("alias" + n);
        passes_.push_back(p);
    }

    // The preset's own parameter values win over the shader's defaults.
    for (auto& [name, value] : params_) {
        const std::string v = get(name);
        if (!v.empty()) value = std::strtof(v.c_str(), nullptr);
    }

    // Lookup textures: "textures = A;B", then each by name.
    std::string names = get("textures");
    std::istringstream ns(names);
    std::string name;
    while (std::getline(ns, name, ';')) {
        name = trim(name);
        if (name.empty()) continue;
        const std::string file = base + "/" + get(name);
        const std::string bytes = readFile(file);
        std::vector<uint8_t> enc(bytes.begin(), bytes.end()), rgba;
        Lut l;
        l.name = name;
        if (!ui::decodeImage(enc, rgba, l.w, l.h)) {
            std::fprintf(stderr, "[look] %s: cannot decode %s\n", path.c_str(), file.c_str());
            return false;
        }
        glGenTextures(1, &l.tex);
        glBindTexture(GL_TEXTURE_2D, l.tex);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, l.w, l.h, 0, GL_RGBA, GL_UNSIGNED_BYTE,
                     rgba.data());
        const std::string wm = get(name + "_wrap_mode");
        setFilter(l.tex, truthy(get(name + "_linear")), truthy(get(name + "_mipmap")),
                  wrapMode(wm.empty() ? "clamp_to_border" : wm));
        luts_.push_back(l);
    }

    history_.resize(wantsHistory_ ? 8 : 1);
    std::fprintf(stderr, "[look] loaded %s: %d pass%s (%s ), %zu parameters, %zu textures%s\n",
                 path.c_str(), count, count == 1 ? "" : "es", versions.c_str(), params_.size(),
                 luts_.size(), wantsHistory_ ? ", previous frames" : "");
    return true;
}

void Chain::allocate(Target& t, int w, int h, GLenum format) {
    if (t.tex && t.w == w && t.h == h && t.format == format) return;
    if (!t.tex) glGenTextures(1, &t.tex);
    if (!t.fbo) glGenFramebuffers(1, &t.fbo);
    glBindTexture(GL_TEXTURE_2D, t.tex);
    const GLenum type = format == GL_RGBA16F ? GL_HALF_FLOAT : GL_UNSIGNED_BYTE;
    glTexImage2D(GL_TEXTURE_2D, 0, format, w, h, 0, GL_RGBA, type, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, t.tex, 0);
    t.w = w;
    t.h = h;
    t.format = format;
}

void Chain::setUniforms(Pass& p, int index, int inW, int inH, int outW, int outH, int& unit) {
    auto has = [&](const char* n) {
        auto it = p.loc.find(n);
        return it != p.loc.end() && it->second >= 0 ? it->second : -1;
    };
    auto vec2 = [&](const std::string& n, float x, float y) {
        auto it = p.loc.find(n);
        if (it != p.loc.end() && it->second >= 0) glUniform2f(it->second, x, y);
    };
    auto tex = [&](const std::string& n, GLuint t) {
        auto it = p.loc.find(n);
        if (it == p.loc.end() || it->second < 0) return;
        glActiveTexture(GL_TEXTURE0 + unit);
        glBindTexture(GL_TEXTURE_2D, t);
        glUniform1i(it->second, unit);
        ++unit;
    };
    auto number = [&](const char* n, double v) {
        auto it = p.loc.find(n);
        if (it == p.loc.end() || it->second < 0) return;
        if (p.type[n] == GL_INT) glUniform1i(it->second, static_cast<int>(v));
        else glUniform1f(it->second, static_cast<float>(v));
    };

    if (GLint l = has("MVPMatrix"); l >= 0) {
        const float m[16] = {2, 0, 0, 0, 0, 2, 0, 0, 0, 0, 1, 0, -1, -1, 0, 1};
        glUniformMatrix4fv(l, 1, GL_FALSE, m);
    }
    number("FrameCount", p.frameCountMod ? frameCount_ % p.frameCountMod : frameCount_);
    number("FrameDirection", 1);
    vec2("OutputSize", outW, outH);
    vec2("TextureSize", inW, inH);
    vec2("InputSize", inW, inH);
    const Target& orig = history_[historyHead_];
    vec2("OrigTextureSize", orig.w, orig.h);
    vec2("OrigInputSize", orig.w, orig.h);
    tex("OrigTexture", orig.tex);
    if (wantsHistory_) {
        for (int k = 0; k < 7; ++k) {
            const Target& t = history_[(historyHead_ + history_.size() - 1 - k) % history_.size()];
            const std::string n = k == 0 ? "Prev" : "Prev" + std::to_string(k);
            tex(n + "Texture", t.tex);
            vec2(n + "TextureSize", t.w, t.h);
            vec2(n + "InputSize", t.w, t.h);
        }
    }
    for (int j = 0; j < index; ++j) {
        const Target& t = passes_[j].out;
        const std::string pn = "Pass" + std::to_string(j + 1);
        tex(pn + "Texture", t.tex);
        vec2(pn + "TextureSize", t.w, t.h);
        vec2(pn + "InputSize", t.w, t.h);
        const std::string pp = "PassPrev" + std::to_string(index - j);
        tex(pp + "Texture", t.tex);
        vec2(pp + "TextureSize", t.w, t.h);
        vec2(pp + "InputSize", t.w, t.h);
        if (!passes_[j].alias.empty()) {
            tex(passes_[j].alias + "Texture", t.tex);
            vec2(passes_[j].alias + "TextureSize", t.w, t.h);
            vec2(passes_[j].alias + "InputSize", t.w, t.h);
        }
    }
    for (const Lut& l : luts_) tex(l.name, l.tex);
    for (const auto& [name, value] : params_) {
        auto it = p.loc.find(name);
        if (it != p.loc.end() && it->second >= 0) glUniform1f(it->second, value);
    }
}

GLuint Chain::run(GLuint src, float u0, float v0, float u1, float v1, int srcW, int srcH,
                  int outW, int outH) {
    if (passes_.empty() || srcW <= 0 || srcH <= 0 || outW <= 0 || outH <= 0) return 0;

    GLint prevFbo = 0, prevProgram = 0, prevVao = 0, prevActive = 0, prevTex = 0;
    GLint prevViewport[4] = {};
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFbo);
    glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
    glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActive);
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    glActiveTexture(GL_TEXTURE0);
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
    const bool blend = glIsEnabled(GL_BLEND), scissor = glIsEnabled(GL_SCISSOR_TEST);
    glDisable(GL_BLEND);
    glDisable(GL_SCISSOR_TEST);
    glBindVertexArray(vao_);

    // The picture, upright and exactly its own size: the "original".
    historyHead_ = (historyHead_ + 1) % history_.size();
    Target& orig = history_[historyHead_];
    allocate(orig, srcW, srcH, GL_RGBA8);
    glBindFramebuffer(GL_FRAMEBUFFER, orig.fbo);
    glViewport(0, 0, srcW, srcH);
    glUseProgram(copyProgram_);
    glUniform4f(copyUV_, u0, v0, u1, v1);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, src);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    if (wantsHistory_ && frameCount_ == 0) {
        // Nothing older yet: start every slot as this frame, not as black.
        for (Target& t : history_) {
            if (&t == &orig) continue;
            allocate(t, srcW, srcH, GL_RGBA8);
            glBindFramebuffer(GL_FRAMEBUFFER, t.fbo);
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, src);  // allocate bound the slot's own
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
        }
    }

    static const bool floatOk = hasExtension("GL_EXT_color_buffer_float");
    GLuint input = orig.tex;
    int inW = srcW, inH = srcH;
    const int last = static_cast<int>(passes_.size()) - 1;
    for (int i = 0; i <= last; ++i) {
        Pass& p = passes_[i];
        auto size = [&](const std::string& type, float scale, int in, int out) {
            const std::string t = type.empty() ? (i == last && !p.scaleSet ? "viewport" : "source")
                                               : type;
            float v = t == "absolute" ? scale : t == "viewport" ? out * scale : in * scale;
            return std::max(1, static_cast<int>(std::lround(v)));
        };
        const int w = size(p.scaleTypeX, p.scaleX, inW, outW);
        const int h = size(p.scaleTypeY, p.scaleY, inH, outH);
        // The last pass is what reaches the screen, which RetroArch draws to
        // the plain backbuffer whatever the preset says about its format.
        GLenum format = GL_RGBA8;
        if (i != last && p.srgbFb) format = GL_SRGB8_ALPHA8;
        else if (i != last && p.floatFb && floatOk) format = GL_RGBA16F;
        allocate(p.out, w, h, format);

        // How this pass reads its input: unset is nearest, as RetroArch's own
        // default with smoothing off.
        setFilter(input, p.linearSet && p.linear, p.mipmap, p.wrap);

        glBindFramebuffer(GL_FRAMEBUFFER, p.out.fbo);
        glViewport(0, 0, w, h);
        glUseProgram(p.program);
        glVertexAttrib4f(2, 1, 1, 1, 1);
        auto it = p.loc.find("Texture");
        if (it != p.loc.end() && it->second >= 0) {
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, input);
            glUniform1i(it->second, 0);
        }
        int unit = 1;
        setUniforms(p, i, inW, inH, w, h, unit);
        glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

        input = p.out.tex;
        inW = w;
        inH = h;
    }
    ++frameCount_;

    // Drawn by the caller at about one texel per screen pixel when the last
    // pass is viewport-sized, so nearest keeps the mask exact; stretched when
    // it is not.
    const bool exact = inW == outW && inH == outH;
    setFilter(input, !exact, false, GL_CLAMP_TO_EDGE);

    glBindFramebuffer(GL_FRAMEBUFFER, prevFbo);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glUseProgram(prevProgram);
    glBindVertexArray(prevVao);
    for (int u = 15; u >= 0; --u) {
        glActiveTexture(GL_TEXTURE0 + u);
        glBindTexture(GL_TEXTURE_2D, 0);
    }
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, prevTex);
    glActiveTexture(prevActive);
    if (blend) glEnable(GL_BLEND);
    if (scissor) glEnable(GL_SCISSOR_TEST);
    return input;
}

}  // namespace screenfx
