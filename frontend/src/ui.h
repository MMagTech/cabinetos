// The CabinetOS UI primitives.
//
// Everything the design system describes is a rounded rectangle with some
// combination of fill, border and shadow — a cover card, a settings row, a
// pill, the pause panel. So that is the one primitive this renderer has, drawn
// with a signed-distance field in the fragment shader rather than with geometry,
// which is what makes a 4pt inset rim and a 26px soft shadow cheap and exact at
// any size.
//
// Coordinates are design points in a 1920x1080 space, origin top-left, matching
// docs/PROJECT.md's "The canvas". The projection handles the rest, so a 4K panel
// is a rendering decision and never a layout one.

#pragma once

#include <GLES3/gl3.h>
#include <cstdint>

namespace ui {

// Design-space canvas. Every number in the design system is in these units.
constexpr float kCanvasWidth = 1920.0f;
constexpr float kCanvasHeight = 1080.0f;

struct Color {
    float r, g, b, a;

    // Values in docs/PROJECT.md are written as hex, so accept them that way and
    // keep the source honest against the document.
    static constexpr Color rgb(uint32_t hex, float alpha = 1.0f) {
        return Color{((hex >> 16) & 0xFF) / 255.0f, ((hex >> 8) & 0xFF) / 255.0f,
                     (hex & 0xFF) / 255.0f, alpha};
    }
    static constexpr Color white(float alpha) { return Color{1, 1, 1, alpha}; }
    static constexpr Color black(float alpha) { return Color{0, 0, 0, alpha}; }
};

// The palette, from docs/PROJECT.md "Branding" and "Colour tokens". Named for
// the role rather than the colour, so a retune changes one line here.
namespace palette {
constexpr Color kBackdropTop = Color::rgb(0x3A2268);
constexpr Color kBackdropMid = Color::rgb(0x120C26);  // at 0.55, not the midpoint
constexpr Color kBackdropBottom = Color::rgb(0x090614);
constexpr Color kSurface = Color::rgb(0x241A3D);       // library tile panel
constexpr Color kScreenCyan = Color::rgb(0x58E8F6);    // candidate accent
constexpr Color kScreenBlue = Color::rgb(0x2484D6);
constexpr Color kMarqueePink = Color::rgb(0xFF7AC7);
constexpr Color kMarqueeAmber = Color::rgb(0xFFC457);
constexpr Color kCabinetBody = Color::rgb(0xEEEAE2);
constexpr Color kFocusRim = Color::white(0.85f);
}  // namespace palette

// One rounded rectangle. Anything the design system draws is one of these, or a
// few of them stacked.
struct Rect {
    float x = 0, y = 0, w = 0, h = 0;
    float radius = 0;
    Color fill = Color::white(0);

    // The focus rim: drawn INSIDE the edge, like the reference implementation's
    // inset stroke, so a focused card does not grow by its own border width.
    float border = 0;
    Color borderColor = Color::white(0);

    // A soft drop shadow behind the shape. blur is the falloff distance in
    // points; offsetY pushes it down. The design system's focused-artwork
    // shadow is black @ 55%, blur 26, y +14.
    float shadowBlur = 0;
    float shadowOffsetY = 0;
    Color shadowColor = Color::black(0);
};

// A three-stop vertical gradient, which is exactly what the backdrop is and
// nothing else currently needs.
struct Gradient {
    Color top, mid, bottom;
    float midStop = 0.55f;
};

class Renderer {
public:
    bool init();
    void shutdown();

    // drawableWidth/Height are real pixels; the canvas is scaled to fit and
    // centred, so an overscan-safe layout stays correct on any panel.
    void beginFrame(int drawableWidth, int drawableHeight);
    void drawBackdrop(const Gradient& g);
    void draw(const Rect& r);

    // A textured quad in design points. Glyphs use it; cover art and a running
    // core's frame will use it too, which is the point of it existing rather
    // than a text-only path.
    // lodBias forces sampling from a coarser mip level. That is how the
    // blurred echo under an odd-shaped cover is drawn: a box blur that costs a
    // texture fetch rather than a blur pass.
    void drawTextured(float x, float y, float w, float h, GLuint texture, float u0,
                      float v0, float u1, float v1, const Color& tint,
                      bool singleChannel = true, float lodBias = 0.0f, float clipX = 0,
                      float clipY = 0, float clipW = 0, float clipH = 0,
                      float clipRadius = 0);

    // Device pixels per design point for the frame in progress. Text has to
    // rasterise at device resolution to be crisp on a 4K set, so it needs this.
    float scale() const { return scale_; }

    // --- Frosted glass -------------------------------------------------------
    //
    // The single largest contributor to what the reference implementation
    // actually looks like, and the one thing on its list that Linux supplies
    // nothing for. A panel over cover art without it is not "less pretty", it
    // is unreadable — the art shows through and competes with the text.
    //
    // The mechanism: the scene is drawn into an offscreen texture, mipmapped,
    // and a glass panel samples that texture at a coarse level under itself.
    // A mip chain is a box blur that the GPU already had to build, so this is
    // a texture fetch rather than a blur pass — which matters on integrated
    // graphics that also has a PS2 to emulate.
    //
    //   beginFrame -> draw the world -> presentScene -> drawGlass/draw/text
    //
    // Callers that never use glass can ignore all of it; presentScene is a
    // no-op then.
    void presentScene();
    bool sceneCaptured() const { return scenePresented_; }

    // A rounded panel that blurs what is behind it. `blur` is a mip level:
    // roughly 4 is the "thin material" of a pill, 6 the "regular material" of
    // a panel. `tint` is composited over the blur.
    void drawGlass(const Rect& r, float blur, const Color& tint);

    // Reads the frame back and writes a BMP. The VM has no way to show a
    // screenshot to anyone, and "it looked right on my machine" is not a thing
    // this project can say, so the frontend can always photograph itself.
    bool saveFrame(const char* path, int drawableWidth, int drawableHeight) const;

    // Render into an offscreen target of an arbitrary size instead of the
    // window. This is how a 1080p development VM proves its layout on a 4K
    // television it does not have, and how CI checks a panel size nobody owns.
    // Most people's sets are 4K; some are not; neither may be assumed, so both
    // have to be checkable from the machine that is actually here.
    bool beginOffscreen(int width, int height);
    void endOffscreen();

private:
    GLuint program_ = 0;
    GLuint backdropProgram_ = 0;
    GLuint texturedProgram_ = 0;
    float scale_ = 1.0f;
    GLuint vao_ = 0;
    GLuint vbo_ = 0;
    GLuint offscreenFBO_ = 0;
    GLuint offscreenTex_ = 0;
    int offscreenW_ = 0, offscreenH_ = 0;

    // The scene, captured so panels can blur it.
    GLuint sceneFBO_ = 0, sceneTex_ = 0;
    int sceneW_ = 0, sceneH_ = 0;
    bool scenePresented_ = false;
    int vx_ = 0, vy_ = 0, vw_ = 0, vh_ = 0;
    GLuint blurProgram_ = 0;
    struct {
        GLint canvas, rect, radius, tint, tex, lod;
    } gloc_{};

    struct {
        GLint canvas, rect, radius, fill, border, borderColor, shadow, shadowColor,
            shadowVS;
    } loc_{};
    struct {
        GLint top, mid, bottom, midStop;
    } bloc_{};
    struct {
        GLint canvas, rect, uv, tint, tex, single, lod, clip, clipRadius;
    } tloc_{};
};

}  // namespace ui
