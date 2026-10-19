// The wiki's QR codes as the console draws them: How to's Wiki, and What's
// new from About (#329). Each code is made from wikiqr.h's rows, and the
// patch in its middle carries the cabinet. wikiqr.h is written whole by
// tools/make-wiki-qr.py each time it runs, so what is written by hand is
// here. Moved out of settings.cpp in the restructure (#398).

#pragma once

#include "qr.h"

namespace ui {
class Renderer;
}

namespace wikiqr {

// Which of the tool's codes.
enum class Which { Wiki, WhatsNew };

// The address the code reads as.
const char* url(Which w);

// The code, module for module as the tool made it.
qr::Code qrCode(Which w);

// The patch over the code drawn at qx, qy, `side` wide with four quiet modules
// round it: white, the start-up screen's purple, then the cabinet. Drawn
// after the code, at `alpha`.
void drawPatch(Which w, ui::Renderer& r, float qx, float qy, float side, float alpha);

}  // namespace wikiqr
