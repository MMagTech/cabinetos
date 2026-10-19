// ui::QrTexture, a QR code drawn as one texture. Its three owners include it:
// the link screen (qrlink.h), Settings (settings.h) and setup's pairing step
// (setup.cpp). Moved out of ui.h in the restructure (#398).

#pragma once

#include <GLES3/gl3.h>

#include <cstdint>
#include <vector>

#include "qr.h"
#include "ui.h"

namespace ui {

// --- The QR code, as one texture -------------------------------------------
//
// A version-4 code is 33x33 modules, and drawing each as its own rounded
// rectangle is eleven hundred draw calls a frame for a picture that never
// changes. One texture, one quad, uploaded when the code changes and not again.
//
// SINGLE CHANNEL, AND NEAREST FILTERING. The renderer's single-channel path
// multiplies the texel by the tint's alpha, which is exactly what is wanted: a
// texel of 1 where a module is DARK, tinted near-black, over a white card. And
// nearest, because a QR code is the one thing on this console that must not be
// smoothed — a blurred module boundary is a module a camera cannot call.
class QrTexture {
public:
    // Never copied: a copy would free the same texture twice (#488, HOME-11).
    QrTexture() = default;
    QrTexture(const QrTexture&) = delete;
    QrTexture& operator=(const QrTexture&) = delete;
    ~QrTexture() { release(); }

    void set(const qr::Code& c) {
        release();
        if (!c.valid()) return;
        size_ = c.size;
        std::vector<uint8_t> px(static_cast<size_t>(size_) * size_);
        for (int y = 0; y < size_; ++y)
            for (int x = 0; x < size_; ++x)
                px[static_cast<size_t>(y) * size_ + x] = c.at(x, y) ? 255 : 0;
        glGenTextures(1, &tex_);
        glBindTexture(GL_TEXTURE_2D, tex_);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size_, size_, 0, GL_RED,
                     GL_UNSIGNED_BYTE, px.data());
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    bool valid() const { return tex_ != 0; }

    // Draws it centred in the given square, WITH THE QUIET ZONE, because the
    // quiet zone is the renderer's job and a code drawn flush to the edge of
    // its card does not scan at all. Measured; see qr.h.
    void draw(Renderer& r, float x, float y, float side) const {
        if (!tex_) return;
        constexpr int kQuiet = 4;
        const float modules = static_cast<float>(size_ + kQuiet * 2);
        const float m = side / modules;                 // one module, in points
        // The card the code sits on: white, and large enough to carry the quiet
        // zone as real light modules rather than as a promise.
        Rect card;
        card.x = x; card.y = y; card.w = side; card.h = side;
        card.radius = 12.0f;
        card.fill = Color::white(1.0f);
        r.draw(card);
        r.drawTextured(x + kQuiet * m, y + kQuiet * m, m * size_, m * size_, tex_,
                       0, 0, 1, 1, Color::rgb(0x0B0616, 1.0f), /*singleChannel=*/true);
    }

private:
    void release() {
        if (tex_) glDeleteTextures(1, &tex_);
        tex_ = 0;
        size_ = 0;
    }
    GLuint tex_ = 0;
    int size_ = 0;
};

}  // namespace ui
