#include "ui.h"

#include <cstdio>
#include <cstring>
#include <vector>

namespace ui {
namespace {

// A fullscreen triangle rather than a quad: two triangles share an edge that
// the rasteriser evaluates twice, and one oversized triangle has no seam at all.
// The vertex shader derives its own positions, so there is nothing in the buffer
// but a vertex index.
const char* kQuadVS = R"(#version 300 es
precision highp float;
uniform vec2 uCanvas;
uniform vec4 uRect;     // x, y, w, h in design points
uniform float uShadow;  // blur radius, so the geometry can be grown to fit it
out vec2 vPoint;        // fragment position in design points
void main() {
    // gl_VertexID 0,1,2,3 -> the four corners of a unit quad.
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    // Grow outward so the shadow has room to fall off outside the shape.
    float pad = uShadow * 2.0 + 4.0;
    vec2 p = uRect.xy - pad + corner * (uRect.zw + pad * 2.0);
    vPoint = p;
    // Design points -> clip space. Y is flipped: the canvas is top-left origin.
    vec2 ndc = vec2(p.x / uCanvas.x * 2.0 - 1.0, 1.0 - p.y / uCanvas.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

// The rounded-rectangle signed distance function. Negative inside, positive
// outside, and the value IS the distance in points — which is what lets the rim
// be exactly 4pt and the shadow fall off over exactly 26.
const char* kQuadFS = R"(#version 300 es
precision highp float;
in vec2 vPoint;
uniform vec4 uRect;
uniform float uRadius;
uniform vec4 uFill;
uniform float uBorder;
uniform vec4 uBorderColor;
uniform vec2 uShadowParams;  // blur, offsetY
uniform vec4 uShadowColor;
out vec4 fragColor;

float roundedBoxSDF(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec2 center = uRect.xy + uRect.zw * 0.5;
    // Not `half`: that is a reserved word in GLSL ES and the compiler rejects
    // it outright.
    vec2 halfSize = uRect.zw * 0.5;
    float r = min(uRadius, min(halfSize.x, halfSize.y));

    float d = roundedBoxSDF(vPoint - center, halfSize, r);

    // One point of feathering. The canvas is scaled to the panel, so this is
    // deliberately in design points and not in pixels: the softness of an edge
    // should look the same on 1080p and 4K, not be twice as sharp on one.
    float aa = 1.0;
    float shapeAlpha = 1.0 - smoothstep(-aa, aa, d);

    vec4 color = vec4(0.0);

    // Shadow first, behind everything, offset downward and blurred by feeding
    // the SDF through a wider smoothstep.
    if (uShadowParams.x > 0.0) {
        float sd = roundedBoxSDF(vPoint - center - vec2(0.0, uShadowParams.y), halfSize, r);
        float sa = 1.0 - smoothstep(-uShadowParams.x, uShadowParams.x, sd);
        // Square it: a linear falloff reads as a grey halo with a visible edge,
        // where a real shadow is dense near the object and fades fast.
        sa = sa * sa;
        color = vec4(uShadowColor.rgb, uShadowColor.a * sa);
    }

    // Fill, composited over the shadow.
    vec4 fill = vec4(uFill.rgb, uFill.a * shapeAlpha);
    color.rgb = mix(color.rgb, fill.rgb, fill.a);
    color.a = color.a + fill.a * (1.0 - color.a);

    // The rim, drawn INSIDE the edge so a focused card does not grow by its own
    // border width. Band between -border and 0 in SDF space.
    if (uBorder > 0.0) {
        float inner = 1.0 - smoothstep(-uBorder - aa, -uBorder + aa, d);
        float band = shapeAlpha - inner;
        vec4 b = vec4(uBorderColor.rgb, uBorderColor.a * band);
        color.rgb = mix(color.rgb, b.rgb, b.a);
        color.a = color.a + b.a * (1.0 - color.a);
    }

    fragColor = color;
}
)";

const char* kBackdropVS = R"(#version 300 es
precision highp float;
out vec2 vUV;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vUV = corner;
    gl_Position = vec4(corner * 2.0 - 1.0, 0.0, 1.0);
}
)";

// Three stops with the middle one at 0.55, which is the icon generator's own
// value and the detail this document previously omitted.
const char* kBackdropFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
uniform vec3 uTop;
uniform vec3 uMid;
uniform vec3 uBottom;
uniform float uMidStop;
out vec4 fragColor;
void main() {
    float t = 1.0 - vUV.y;  // vUV.y is 0 at the bottom in clip space
    vec3 c = t < uMidStop
        ? mix(uTop, uMid, t / uMidStop)
        : mix(uMid, uBottom, (t - uMidStop) / (1.0 - uMidStop));
    fragColor = vec4(c, 1.0);
}
)";

// One textured quad. The same shader serves glyphs and pictures: a glyph atlas
// is a single-channel coverage mask that tints, a cover or a core's frame is
// full colour that does not. One flag decides which, rather than two shaders
// that would drift apart.
const char* kTexturedVS = R"(#version 300 es
precision highp float;
uniform vec2 uCanvas;
uniform vec4 uRect;
uniform vec4 uUV;
out vec2 vUV;
out vec2 vPoint;
void main() {
    vec2 corner = vec2(float(gl_VertexID & 1), float((gl_VertexID >> 1) & 1));
    vec2 p = uRect.xy + corner * uRect.zw;
    vUV = mix(uUV.xy, uUV.zw, corner);
    vPoint = p;
    vec2 ndc = vec2(p.x / uCanvas.x * 2.0 - 1.0, 1.0 - p.y / uCanvas.y * 2.0);
    gl_Position = vec4(ndc, 0.0, 1.0);
}
)";

const char* kTexturedFS = R"(#version 300 es
precision highp float;
in vec2 vUV;
in vec2 vPoint;
uniform sampler2D uTex;
uniform vec4 uTint;
uniform int uSingleChannel;
uniform float uLod;
// The rounded box this draw is clipped to, which is NOT the same rectangle as
// the one being drawn: a cover is fitted or overflowed inside a card, and both
// have to stop at the card's rounded edge. Zero size means no clip.
uniform vec4 uClip;
uniform float uClipRadius;
out vec4 fragColor;

float clipSDF(vec2 p, vec2 halfSize, float r) {
    vec2 q = abs(p) - halfSize + r;
    return min(max(q.x, q.y), 0.0) + length(max(q, 0.0)) - r;
}

void main() {
    vec4 t = uLod > 0.0 ? textureLod(uTex, vUV, uLod) : texture(uTex, vUV);
    vec4 c = uSingleChannel == 1
        // Coverage in red, colour from the tint. Straight alpha, composited by
        // the same blend function everything else uses.
        ? vec4(uTint.rgb, uTint.a * t.r)
        : vec4(t.rgb * uTint.rgb, t.a * uTint.a);

    if (uClip.z > 0.0) {
        vec2 center = uClip.xy + uClip.zw * 0.5;
        vec2 halfSize = uClip.zw * 0.5;
        float r = min(uClipRadius, min(halfSize.x, halfSize.y));
        float d = clipSDF(vPoint - center, halfSize, r);
        c.a *= 1.0 - smoothstep(-1.0, 1.0, d);
    }
    fragColor = c;
}
)";

GLuint compile(GLenum type, const char* src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetShaderInfoLog(s, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[ui] shader compile failed: %s\n", log);
        glDeleteShader(s);
        return 0;
    }
    return s;
}

GLuint link(const char* vs, const char* fs) {
    GLuint v = compile(GL_VERTEX_SHADER, vs);
    GLuint f = compile(GL_FRAGMENT_SHADER, fs);
    if (!v || !f) return 0;
    GLuint p = glCreateProgram();
    glAttachShader(p, v);
    glAttachShader(p, f);
    glLinkProgram(p);
    GLint ok = 0;
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[2048];
        glGetProgramInfoLog(p, sizeof(log), nullptr, log);
        std::fprintf(stderr, "[ui] program link failed: %s\n", log);
        glDeleteProgram(p);
        p = 0;
    }
    glDeleteShader(v);
    glDeleteShader(f);
    return p;
}

}  // namespace

bool Renderer::init() {
    program_ = link(kQuadVS, kQuadFS);
    backdropProgram_ = link(kBackdropVS, kBackdropFS);
    texturedProgram_ = link(kTexturedVS, kTexturedFS);
    if (!program_ || !backdropProgram_ || !texturedProgram_) return false;

    loc_.canvas = glGetUniformLocation(program_, "uCanvas");
    loc_.rect = glGetUniformLocation(program_, "uRect");
    loc_.radius = glGetUniformLocation(program_, "uRadius");
    loc_.fill = glGetUniformLocation(program_, "uFill");
    loc_.border = glGetUniformLocation(program_, "uBorder");
    loc_.borderColor = glGetUniformLocation(program_, "uBorderColor");
    loc_.shadow = glGetUniformLocation(program_, "uShadowParams");
    loc_.shadowColor = glGetUniformLocation(program_, "uShadowColor");
    loc_.shadowVS = glGetUniformLocation(program_, "uShadow");

    bloc_.top = glGetUniformLocation(backdropProgram_, "uTop");
    bloc_.mid = glGetUniformLocation(backdropProgram_, "uMid");
    bloc_.bottom = glGetUniformLocation(backdropProgram_, "uBottom");
    bloc_.midStop = glGetUniformLocation(backdropProgram_, "uMidStop");

    tloc_.canvas = glGetUniformLocation(texturedProgram_, "uCanvas");
    tloc_.rect = glGetUniformLocation(texturedProgram_, "uRect");
    tloc_.uv = glGetUniformLocation(texturedProgram_, "uUV");
    tloc_.tint = glGetUniformLocation(texturedProgram_, "uTint");
    tloc_.tex = glGetUniformLocation(texturedProgram_, "uTex");
    tloc_.single = glGetUniformLocation(texturedProgram_, "uSingleChannel");
    tloc_.lod = glGetUniformLocation(texturedProgram_, "uLod");
    tloc_.clip = glGetUniformLocation(texturedProgram_, "uClip");
    tloc_.clipRadius = glGetUniformLocation(texturedProgram_, "uClipRadius");

    // GLES 3 still requires a bound vertex array even when every attribute is
    // derived from gl_VertexID and nothing is read from a buffer.
    glGenVertexArrays(1, &vao_);
    glBindVertexArray(vao_);
    return true;
}

void Renderer::drawTextured(float x, float y, float w, float h, GLuint texture,
                            float u0, float v0, float u1, float v1, const Color& tint,
                            bool singleChannel, float lodBias, float clipX, float clipY,
                            float clipW, float clipH, float clipRadius) {
    glUseProgram(texturedProgram_);
    glUniform2f(tloc_.canvas, kCanvasWidth, kCanvasHeight);
    glUniform4f(tloc_.rect, x, y, w, h);
    glUniform4f(tloc_.uv, u0, v0, u1, v1);
    glUniform4f(tloc_.tint, tint.r, tint.g, tint.b, tint.a);
    glUniform1i(tloc_.single, singleChannel ? 1 : 0);
    glUniform1f(tloc_.lod, lodBias);
    glUniform4f(tloc_.clip, clipX, clipY, clipW, clipH);
    glUniform1f(tloc_.clipRadius, clipRadius);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(tloc_.tex, 0);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

bool Renderer::beginOffscreen(int width, int height) {
    if (offscreenFBO_ && (offscreenW_ != width || offscreenH_ != height)) endOffscreen();
    if (!offscreenFBO_) {
        glGenTextures(1, &offscreenTex_);
        glBindTexture(GL_TEXTURE_2D, offscreenTex_);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA,
                     GL_UNSIGNED_BYTE, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glGenFramebuffers(1, &offscreenFBO_);
        glBindFramebuffer(GL_FRAMEBUFFER, offscreenFBO_);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                               offscreenTex_, 0);
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::fprintf(stderr, "[ui] offscreen %dx%d incomplete\n", width, height);
            endOffscreen();
            return false;
        }
        offscreenW_ = width;
        offscreenH_ = height;
    }
    glBindFramebuffer(GL_FRAMEBUFFER, offscreenFBO_);
    return true;
}

void Renderer::endOffscreen() {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    if (offscreenFBO_) glDeleteFramebuffers(1, &offscreenFBO_);
    if (offscreenTex_) glDeleteTextures(1, &offscreenTex_);
    offscreenFBO_ = offscreenTex_ = 0;
    offscreenW_ = offscreenH_ = 0;
}

void Renderer::shutdown() {
    endOffscreen();
    if (vbo_) glDeleteBuffers(1, &vbo_);
    if (vao_) glDeleteVertexArrays(1, &vao_);
    if (program_) glDeleteProgram(program_);
    if (backdropProgram_) glDeleteProgram(backdropProgram_);
    if (texturedProgram_) glDeleteProgram(texturedProgram_);
    vbo_ = vao_ = program_ = backdropProgram_ = texturedProgram_ = 0;
}

void Renderer::beginFrame(int drawableWidth, int drawableHeight) {
    // Letterbox rather than stretch. A 16:9 panel is the normal case and this is
    // a no-op there; anything else keeps the design's proportions and puts the
    // slack in bars, which is what a console does.
    float scale = std::min(static_cast<float>(drawableWidth) / kCanvasWidth,
                           static_cast<float>(drawableHeight) / kCanvasHeight);
    scale_ = scale;
    int vw = static_cast<int>(kCanvasWidth * scale);
    int vh = static_cast<int>(kCanvasHeight * scale);
    glViewport((drawableWidth - vw) / 2, (drawableHeight - vh) / 2, vw, vh);

    glDisable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE,
                        GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindVertexArray(vao_);
}

void Renderer::drawBackdrop(const Gradient& g) {
    glUseProgram(backdropProgram_);
    glUniform3f(bloc_.top, g.top.r, g.top.g, g.top.b);
    glUniform3f(bloc_.mid, g.mid.r, g.mid.g, g.mid.b);
    glUniform3f(bloc_.bottom, g.bottom.r, g.bottom.g, g.bottom.b);
    glUniform1f(bloc_.midStop, g.midStop);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

void Renderer::draw(const Rect& r) {
    glUseProgram(program_);
    glUniform2f(loc_.canvas, kCanvasWidth, kCanvasHeight);
    glUniform4f(loc_.rect, r.x, r.y, r.w, r.h);
    glUniform1f(loc_.radius, r.radius);
    glUniform4f(loc_.fill, r.fill.r, r.fill.g, r.fill.b, r.fill.a);
    glUniform1f(loc_.border, r.border);
    glUniform4f(loc_.borderColor, r.borderColor.r, r.borderColor.g, r.borderColor.b,
                r.borderColor.a);
    glUniform2f(loc_.shadow, r.shadowBlur, r.shadowOffsetY);
    glUniform4f(loc_.shadowColor, r.shadowColor.r, r.shadowColor.g, r.shadowColor.b,
                r.shadowColor.a);
    // The vertex shader needs the blur too, to grow its own geometry enough
    // for the falloff to have somewhere to land.
    if (loc_.shadowVS >= 0) glUniform1f(loc_.shadowVS, r.shadowBlur);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
}

bool Renderer::saveFrame(const char* path, int w, int h) const {
    std::vector<uint8_t> pixels(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());

    // A 24-bit BMP: no encoder, no dependency, and every tool reads it.
    const uint32_t rowBytes = static_cast<uint32_t>(w) * 3;
    const uint32_t padding = (4 - (rowBytes % 4)) % 4;
    const uint32_t imageBytes = (rowBytes + padding) * h;
    const uint32_t fileBytes = 54 + imageBytes;

    FILE* f = std::fopen(path, "wb");
    if (!f) {
        std::fprintf(stderr, "[ui] cannot write %s\n", path);
        return false;
    }
    uint8_t header[54] = {};
    header[0] = 'B';
    header[1] = 'M';
    std::memcpy(header + 2, &fileBytes, 4);
    uint32_t offset = 54;
    std::memcpy(header + 10, &offset, 4);
    uint32_t dibSize = 40;
    std::memcpy(header + 14, &dibSize, 4);
    int32_t sw = w, sh = h;  // positive height: BMP rows run bottom-up, like GL
    std::memcpy(header + 18, &sw, 4);
    std::memcpy(header + 22, &sh, 4);
    uint16_t planes = 1, bpp = 24;
    std::memcpy(header + 26, &planes, 2);
    std::memcpy(header + 28, &bpp, 2);
    std::memcpy(header + 34, &imageBytes, 4);
    std::fwrite(header, 1, sizeof(header), f);

    std::vector<uint8_t> row(rowBytes + padding, 0);
    for (int y = 0; y < h; ++y) {
        const uint8_t* src = pixels.data() + static_cast<size_t>(y) * w * 4;
        for (int x = 0; x < w; ++x) {
            row[x * 3 + 0] = src[x * 4 + 2];  // BMP is BGR
            row[x * 3 + 1] = src[x * 4 + 1];
            row[x * 3 + 2] = src[x * 4 + 0];
        }
        std::fwrite(row.data(), 1, row.size(), f);
    }
    std::fclose(f);
    std::fprintf(stderr, "[ui] wrote %s (%dx%d)\n", path, w, h);
    return true;
}

}  // namespace ui
