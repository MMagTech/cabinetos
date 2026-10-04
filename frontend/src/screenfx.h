// Screen looks (#122): RetroArch's own shader files, run over the game picture.
//
// NOT A PORT. Cabinet had to rewrite every look in Metal; this console draws
// the game with OpenGL ES, which is what RetroArch's GLSL shaders are written
// for, so the files are used exactly as libretro ships them
// (github.com/libretro/glsl-shaders, pinned in frontend/data/shaders/README.md).
// What lives here is the part of RetroArch that runs them: a .glslp preset's
// passes, each pass's size and filtering, and the uniforms RetroArch hands a
// GLSL shader (Texture, TextureSize, InputSize, OutputSize, FrameCount,
// MVPMatrix, OrigTexture, PrevTexture to Prev6Texture, pass aliases, lookup
// textures and #pragma parameters).
//
// THE TEXTURE CONVENTION, once: every texture here holds the picture with its
// top row at v = 0, the way a software core's frame is uploaded. A hardware
// core's frame (bottom row first, in a corner of a larger target) is copied
// into that shape first, so no shader ever sees a flipped or padded picture,
// and TextureSize always equals InputSize.
//
// Rotation is NOT applied here. RetroArch runs the shaders in the game's own
// space and turns the result, so a vertical board's scanlines run the way its
// sideways monitor's did; the caller turns the finished texture.

#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace screenfx {

// One choice in the pause menu's Look row.
struct Look {
    const char* id;      // what is stored
    const char* label;   // on screen
    // The preset, relative to the shader directory; nullptr is the console's
    // own picture with no shader at all.
    const char* preset;
};

// The looks a system is offered, by RomM platform slug; empty for a system
// that has none (PS2 and newer, DS, PSP, Virtual Boy, Vectrex). Consoles never
// see the handheld looks, and handhelds never see the TV looks.
const std::vector<Look>& looksFor(const std::string& platformSlug);

// The stored choice's index into looksFor, or 0.
int chosen(const std::string& platformSlug);
void choose(const std::string& platformSlug, int index);

// The preset path for the stored choice, or "" for none.
std::string presetFor(const std::string& platformSlug);

// Where the shader files are: $CABINETOS_SHADERS, else the image's copy.
std::string shaderDir();

class Chain {
public:
    // Loads the preset at `path` if it is not the one loaded; "" unloads.
    // A preset that fails to load is logged once and leaves no chain, so the
    // game is drawn plainly rather than not at all.
    void use(const std::string& path);
    bool active() const { return !passes_.empty(); }

    // Runs every pass over the picture at `src` (its uv rectangle, which may
    // be flipped, and its size in pixels) for an output of outW x outH real
    // screen pixels, in the picture's own unrotated orientation. Returns the
    // texture to draw, top row at v = 0. Restores the GL state it touches.
    GLuint run(GLuint src, float u0, float v0, float u1, float v1, int srcW, int srcH,
               int outW, int outH);

    void release();

private:
    struct Target {
        GLuint tex = 0, fbo = 0;
        int w = 0, h = 0;
        GLenum format = GL_RGBA8;
    };
    struct Pass {
        GLuint program = 0;
        std::map<std::string, GLint> loc;     // uniform name -> location
        std::map<std::string, GLenum> type;   // uniform name -> type
        bool linear = false;
        bool linearSet = false;
        bool mipmap = false;
        GLenum wrap = GL_CLAMP_TO_EDGE;
        std::string scaleTypeX = "", scaleTypeY = "";
        float scaleX = 1.0f, scaleY = 1.0f;
        bool scaleSet = false;
        bool floatFb = false, srgbFb = false;
        unsigned frameCountMod = 0;
        std::string alias;
        Target out;
    };
    struct Lut {
        std::string name;
        GLuint tex = 0;
        int w = 0, h = 0;
    };

    bool load(const std::string& path);
    void allocate(Target& t, int w, int h, GLenum format);
    void setUniforms(Pass& p, int index, int inW, int inH, int outW, int outH,
                     int& unit);

    std::string path_;
    std::string failed_;
    std::vector<Pass> passes_;
    std::vector<Lut> luts_;
    std::map<std::string, float> params_;
    GLuint copyProgram_ = 0;
    GLint copyUV_ = -1;
    GLuint vao_ = 0, vbo_ = 0;
    Target orig_;
    // The last seven originals, newest first, only when a pass asks.
    bool wantsHistory_ = false;
    std::vector<Target> history_;
    size_t historyHead_ = 0;
    uint32_t frameCount_ = 0;
};

Chain& shared();

}  // namespace screenfx
