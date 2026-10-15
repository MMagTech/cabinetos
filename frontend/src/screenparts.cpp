#include "screenparts.h"

#include "text.h"
#include "ui.h"

// "PRESS (A) TO ...", centred, with the A drawn as a button badge the way
// consoles prompt: a white disc with a dark A, on the text's x-height. Home's
// empty state says "Press (A) to open the Library" this way, and a game held
// after a state loads says "Press (A) to continue".
void drawPressPrompt(ui::Renderer& r, ui::TextRenderer& text, float sc,
                     const char* before, const char* after, float base,
                     float alpha) {
    const ui::TextStyle st = ui::TextStyle::Title3;
    const float gap = 16.0f;
    const float badge = text.lineHeight(st, sc) * 0.95f;
    const float w1 = text.measure(before, st, sc);
    const float w2 = text.measure(after, st, sc);
    float x = (ui::kCanvasWidth - (w1 + gap + badge + gap + w2)) * 0.5f;
    text.draw(r, before, x, base, st, ui::Color::white(0.92f * alpha), sc);
    x += w1 + gap;
    const float capMid = base - text.ascent(st, sc) * 0.36f;
    r.draw(ui::Rect{x, capMid - badge * 0.5f, badge, badge, badge * 0.5f,
                    ui::Color::white(0.95f * alpha)});
    const float aw = text.measure("A", st, sc);
    text.draw(r, "A", x + (badge - aw) * 0.5f, capMid + text.ascent(st, sc) * 0.36f, st,
              ui::Color{0.07f, 0.05f, 0.12f, alpha}, sc);
    x += badge + gap;
    text.draw(r, after, x, base, st, ui::Color::white(0.92f * alpha), sc);
}
