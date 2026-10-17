// The Wiki's QR code as Settings draws it on How to: the code, made from
// wikiqr.h's rows, and the patch in its middle with the cabinet on it.
// wikiqr.h is written whole by tools/make-wiki-qr.py each time it runs, so
// what is written by hand is here. Moved out of settings.cpp in the
// restructure (#398).

#pragma once

#include "qr.h"

namespace ui {
class Renderer;
}

namespace wikiqr {

// The code, module for module as the tool made it.
qr::Code qrCode();

// The patch over the code drawn at qx, qy, `side` wide with four quiet modules
// round it: white, the start-up screen's purple, then the cabinet. Drawn
// after the code, at `alpha`.
void drawPatch(ui::Renderer& r, float qx, float qy, float side, float alpha);

}  // namespace wikiqr
