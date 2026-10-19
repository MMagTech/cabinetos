// The wiki's QR codes as the console draws them (wikiqrview.h). Moved out of
// settings.cpp in the restructure (#398).

#include "wikiqrview.h"

#include "startscreen.h"
#include "ui.h"
#include "wikiqr.h"

namespace wikiqr {

namespace {

// One code's numbers from wikiqr.h, which names each code's apart.
struct Baked {
    const char* url;
    int size;
    int patchX, patchY, patchW, patchH;
    const char* const* rows;
};

Baked baked(Which w) {
    if (w == Which::WhatsNew)
        return {kWhatsNewUrl, kWhatsNewSize, kWhatsNewPatchX, kWhatsNewPatchY,
                kWhatsNewPatchW, kWhatsNewPatchH, kWhatsNewRows};
    return {kUrl, kSize, kPatchX, kPatchY, kPatchW, kPatchH, kRows};
}

}  // namespace

const char* url(Which w) { return baked(w).url; }

qr::Code qrCode(Which w) {
    const Baked b = baked(w);
    qr::Code code;
    code.size = b.size;
    for (int y = 0; y < b.size; ++y)
        for (int x = 0; x < b.size; ++x)
            code.modules.push_back(b.rows[y][x] == '#' ? 1 : 0);
    return code;
}

void drawPatch(Which w, ui::Renderer& r, float qx, float qy, float side, float alpha) {
    // The patch, as the tool drew it in the preview it checked: white,
    // the startup screen's purple, then the cabinet.
    const Baked b = baked(w);
    const float m = side / static_cast<float>(b.size + 8);   // 4 quiet each side
    const float X = qx + (4 + b.patchX) * m, Y = qy + (4 + b.patchY) * m;
    const float W = b.patchW * m, H = b.patchH * m, t = kTileInset * m;
    r.draw(ui::Rect{X, Y, W, H, kWhiteRadius * m, ui::Color::white(1.0f)});
    r.draw(ui::Rect{X + t, Y + t, W - 2 * t, H - 2 * t, kTileRadius * m,
                    ui::palette::kBackdropTop});
    const float ch = (b.patchH - kCabinetShort) * m;
    setup::drawCabinetIcon(r, X + (W - ch * 527.0f / 720.0f) * 0.5f,
                           Y + kCabinetTop * m, ch, alpha);
}

}  // namespace wikiqr
