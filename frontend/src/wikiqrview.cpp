// The Wiki's QR code as Settings draws it on How to (wikiqrview.h). Moved out
// of settings.cpp in the restructure (#398).

#include "wikiqrview.h"

#include "startscreen.h"
#include "ui.h"
#include "wikiqr.h"

namespace wikiqr {

qr::Code qrCode() {
    qr::Code code;
    code.size = wikiqr::kSize;
    for (int y = 0; y < wikiqr::kSize; ++y)
        for (int x = 0; x < wikiqr::kSize; ++x)
            code.modules.push_back(wikiqr::kRows[y][x] == '#' ? 1 : 0);
    return code;
}

void drawPatch(ui::Renderer& r, float qx, float qy, float side, float alpha) {
    // The patch, as the tool drew it in the preview it checked: white,
    // the startup screen's purple, then the cabinet.
    const float m = side / static_cast<float>(wikiqr::kSize + 8);   // 4 quiet each side
    const float X = qx + (4 + wikiqr::kPatchX) * m, Y = qy + (4 + wikiqr::kPatchY) * m;
    const float W = wikiqr::kPatchW * m, H = wikiqr::kPatchH * m, t = wikiqr::kTileInset * m;
    r.draw(ui::Rect{X, Y, W, H, wikiqr::kWhiteRadius * m, ui::Color::white(1.0f)});
    r.draw(ui::Rect{X + t, Y + t, W - 2 * t, H - 2 * t, wikiqr::kTileRadius * m,
                    ui::palette::kBackdropTop});
    const float ch = (wikiqr::kPatchH - wikiqr::kCabinetShort) * m;
    setup::drawCabinetIcon(r, X + (W - ch * 527.0f / 720.0f) * 0.5f,
                           Y + wikiqr::kCabinetTop * m, ch, alpha);
}

}  // namespace wikiqr
