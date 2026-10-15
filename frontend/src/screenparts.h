// The parts more than one screen draws. Moved out of main.cpp in the
// restructure (#398); batch (h1) brings the rest. choice.cpp keeps its own
// near-copy of the prompt (another button letter, no fade), as it was.

#pragma once

namespace ui {
class Renderer;
class TextRenderer;
}

void drawPressPrompt(ui::Renderer& r, ui::TextRenderer& text, float sc, const char* before,
                     const char* after, float base, float alpha);
