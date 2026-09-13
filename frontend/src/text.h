// Text for the CabinetOS frontend.
//
// Glyphs are rasterised by FreeType on first sight and packed into one
// greyscale atlas, then drawn as textured quads. That is the whole design; it
// is what almost every game UI does, and it keeps text on the same code path
// as everything else the renderer draws.
//
// Two things here are not obvious and both matter on a television.
//
// RASTERISE AT DEVICE PIXELS, LAY OUT IN DESIGN POINTS. The canvas is
// 1920x1080 design points scaled to whatever panel is attached, so a 31pt
// caption is 31 pixels tall on a 1080p set and 62 on a 4K one. Rasterising at
// the design size and letting the GPU scale the result would make 4K text a
// blurry upscale of 1080p text, which is exactly the thing a console must not
// look like. So the atlas is keyed by device pixel size, and the same label at
// two scales is two entries.
//
// FALL BACK FOR GLYPHS THE FACE DOES NOT HAVE. A ROM library is full of
// Japanese titles. Noto Sans has no CJK coverage; Noto Sans CJK does, and both
// are already in the image. A missing glyph walks the fallback list rather than
// drawing a box.

#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ui {

class Renderer;
struct Color;

// The type ramp from docs/PROJECT.md, "Type". Sizes are design points and are
// Apple's published tvOS ramp, which is what the reference implementation was
// laid out against. They are the intended proportions; retune on a television,
// not on a monitor.
enum class TextStyle {
    LargeTitle,  // 76 bold  — game detail title, settings page titles
    Title1,      // 57
    Title2,      // 48 bold  — shelf headers
    Title3,      // 38 semibold — settings rows, tile titles, pills
    Headline,    // 38 semibold — hero card title
    Callout,     // 31       — cover captions
    Body,        // 29
    Footnote,    // 29       — game counts
    Caption1,    // 25       — hero platform label
    Caption2,    // 23
    ScreenTitle, // 40 bold  — the one hardcoded size in the reference app
};

enum class Weight { Regular, Medium, SemiBold, Bold };

float styleSize(TextStyle s);
Weight styleWeight(TextStyle s);

class TextRenderer {
public:
    ~TextRenderer();

    // Faces are tried in order for every glyph, so put the broadest coverage
    // last. Returns false only if the primary face cannot be opened at all.
    bool init(const std::vector<std::string>& facePathsByWeight);
    void shutdown();

    // Width of a string in design points, without drawing it. Needed before
    // anything can be centred or right-aligned, and by the ellipsis logic.
    float measure(std::string_view utf8, TextStyle style, float scale);

    // Draws with (x, y) as the LEFT BASELINE, in design points. Baseline rather
    // than top-left because that is what makes two different sizes on one line
    // sit correctly against each other.
    void draw(Renderer& r, std::string_view utf8, float x, float baselineY,
              TextStyle style, const Color& color, float scale);

    // Ascent and descent in design points, for laying a line out from a box.
    float ascent(TextStyle style, float scale);
    float lineHeight(TextStyle style, float scale);

    // Truncates to fit, appending a real ellipsis. Game titles are long and a
    // shelf caption has one line; the reference implementation gives a grid
    // caption two and reserves the space either way.
    std::string truncate(std::string_view utf8, TextStyle style, float scale,
                         float maxWidth);

private:
    struct Glyph {
        float u0 = 0, v0 = 0, u1 = 0, v1 = 0;
        int w = 0, h = 0;      // device pixels
        int bearingX = 0, bearingY = 0;
        float advance = 0;     // device pixels
        bool valid = false;
    };

    // Keyed by weight, device pixel size and codepoint together: the same
    // character at two scales is genuinely two different bitmaps.
    uint64_t key(Weight w, int pixelSize, uint32_t codepoint) const;
    const Glyph& glyph(Weight w, int pixelSize, uint32_t codepoint);
    bool packInto(int w, int h, int& outX, int& outY);

    void* library_ = nullptr;                 // FT_Library
    std::vector<void*> faces_;                // FT_Face, one per weight
    std::vector<void*> fallbackFaces_;        // FT_Face, tried in order
    std::unordered_map<uint64_t, Glyph> glyphs_;

    GLuint atlas_ = 0;
    int atlasSize_ = 0;
    int penX_ = 0, penY_ = 0, rowHeight_ = 0;
};

// Decodes one UTF-8 code point, advancing i. Invalid bytes yield U+FFFD and
// advance by one, so a malformed name from a server cannot hang the loop.
uint32_t nextCodepoint(std::string_view s, size_t& i);

}  // namespace ui
