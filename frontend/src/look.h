// What the menus are drawn in: the signed-in person's colour, and whether the
// console is dark. #75 and #129, built together on 2026-09-27.
//
// COLOUR IS PERSONAL, APPEARANCE IS THE ROOM. MMagTech chose colour per
// account on 2026-09-24 (the console changes colour when somebody else signs
// in, which also says who is playing), and dark mode belongs to the console,
// because it is about the lights being off, not about who is holding the pad.
//
// FIVE HAND-PICKED COLOURS, NOT A PICKER: purple (the brand), blue, teal,
// wine and graphite. Wine rather than red, which reads as an error screen and
// muddies the red and orange box art a retro library is full of; teal rather
// than green, which reads as somebody else's console.
//
// DARK IS ONE RULE OVER ANY COLOUR, not a second hand-tuned set: the
// background goes toward black, the game-art glow and the covers at rest sit
// further back, and text and the focused cover stay as they are. Five looks to
// judge instead of ten. Menus only: a game's picture is the game's.
//
// NOT HERE: the startup screen and first run stay purple. Before anybody has
// signed in there is nobody's colour to use, and the startup screen is the
// brand.

#pragma once

#include <string>

#include "ui.h"

namespace look {

enum class Colour { Purple = 0, Blue, Teal, Wine, Graphite };
constexpr int kColourCount = 5;
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
