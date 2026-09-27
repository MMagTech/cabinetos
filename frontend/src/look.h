// What the menus are drawn in: the signed-in person's colour, and whether the
// console is dark. #75 and #129, built together on 2026-09-27.
//
// COLOUR IS PERSONAL, APPEARANCE IS THE ROOM. MMagTech chose colour per
// account on 2026-09-24 (the console changes colour when somebody else signs
// in, which also says who is playing), and dark mode belongs to the console,
// because it is about the lights being off, not about who is holding the pad.
//
// EIGHT HAND-PICKED COLOURS, NOT A PICKER, in order round the colour wheel:
// purple (the brand), blue, teal, green, amber, red, wine and graphite. The
// first five were purple, blue, teal, wine and graphite; MMagTech asked for a
// few more the same evening, and the three added fill the wheel's gaps rather
// than sitting beside one already there. Red is a deep crimson, kept dark so
// it does not read as an error screen.
//
// THEN FIVE BRIGHT ONES, for children: MMagTech, the same evening, *"the
// colors just seem to lack some fun ones like ones my kids might like"*.
// Pink, Sky, Lime, Orange, and Sunset, the one with three hues down the
// screen. Brighter only at the top, where the bar's white text sits, so
// Lime and Orange are kept deeper than a crayon; the middle and bottom stay
// dark for the covers and the words.
//
// SUNSET WAS THE FUN ONE (*"oh the gradient is fun"*), so three more with a
// hue for each stop: Ocean, Aurora and Fire. To be cut down to a final set
// on the TV; sixteen is too many to walk with Left and Right.
//
// DARK IS ONE RULE OVER ANY COLOUR, not a second hand-tuned set: the
// background goes toward black, the game-art glow and the covers at rest sit
// further back, and text and the focused cover stay as they are. Each look is
// judged once, not once per appearance. Menus only: a game's picture is the game's.
//
// NOT HERE: the startup screen and first run stay purple. Before anybody has
// signed in there is nobody's colour to use, and the startup screen is the
// brand.

#pragma once

#include <string>

#include "ui.h"

namespace look {

enum class Colour {
    Purple = 0, Blue, Teal, Green, Amber, Red, Wine, Graphite,
    Pink, Sky, Lime, Orange, Sunset, Ocean, Aurora, Fire
};
constexpr int kColourCount = 16;
const char* colourName(Colour c);                 // "Purple", for the screen
const char* colourWord(Colour c);                 // "purple", for the file
Colour colourFromWord(const std::string& word);   // anything unknown is Purple

enum class Appearance { Standard = 0, Dark, Scheduled };
constexpr int kAppearanceCount = 3;
const char* appearanceName(Appearance a);
const char* appearanceWord(Appearance a);
Appearance appearanceFromWord(const std::string& word);

// Whether Scheduled is dark at `hour` (0-23, local time), dark from `from`
// until `until`. A span that crosses midnight is the normal case.
bool scheduledDark(int hour, int from, int until);

// "8 PM", for the From and Until rows.
std::string hourName(int hour);

// What to draw in. A change fades over a second and a half unless `instant`,
// which is for startup and for behind the account-switch curtain.
void setColour(Colour c, bool instant = false);
void setDark(bool dark, bool instant = false);
Colour colour();
bool dark();
void tick(float dt);

// The menus' background and panel colour, as they are this frame.
ui::Gradient backdrop();
ui::Color surface();

// 0 standard to 1 dark, eased, for the things dark turns down.
float darkness();
float artFill(float standard);    // the game-art glow behind a browsing screen
float artScrim(float standard);   // the black laid over that glow
float restDim(float standard);    // the black over a cover nobody is on

}  // namespace look
