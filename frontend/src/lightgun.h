// Sinden light guns (#575): finding them, reading them, and what each gun's
// buttons do in the menus and in a game. The console reads the guns itself,
// as wiiremote.cpp reads Wii Remotes; what a core is told of them is
// core_input.cpp's (Core::setGun), from the state here.
//
// WHAT A GUN IS HERE: a USB device whose product string is "SindenLightgun",
// read on the reference console on 2026-11-27. Never its id: our two report
// 16c0:0f01 and 16c0:0f02, another owner's two may share one, and both of
// ours carry the serial HIDLG. Each gun is a small hub (0424:2512) holding its
// camera on port 1 and the gun on port 2. The gun makes two input devices on
// one HID interface: "SindenLightgun Mouse", absolute, ABS_X and ABS_Y 0 to
// 32767 across the screen Sinden's border goes around, with BTN_LEFT, BTN_RIGHT
// and BTN_MIDDLE; and "SindenLightgun Keyboard", with KEY_1, KEY_5 and the
// arrow keys (KEY_2 and KEY_6 in place of KEY_1 and KEY_5 on a gun Sinden's
// software numbered 2). The two are paired by the USB device they hang from, and guns
// are told apart by the USB port they are in, never by device numbers or the
// order they came in.
//
// THE AIM IS SINDEN'S SOFTWARE'S, written to the gun over its serial port;
// this file only reads what the gun then reports, and it reports only on a
// change: a gun held still sends almost nothing, so no rule here judges
// "aiming" by how often it speaks.
//
// KEPT FROM EVERYTHING ELSE: each node is grabbed (EVIOCGRAB) as it is opened,
// so gamescope's libinput hears nothing of it while the console runs, and
// 72-cabinetos-light-gun.rules keeps libinput and SDL from it from the start.
// A gun's arrow keys never reach player 1's D-pad (play.cpp) and its trigger
// never clicks anything.
//
// THE BUTTONS (#575 rule 10), as Sinden's own defaults set them, with or
// without Sinden's software running:
//
//   Trigger                 BTN_LEFT    menus: Select   game: shoot
//   Pump, or front left     BTN_RIGHT   menus: Back     game: second button
//   Front right             BTN_MIDDLE  menus: nothing  game: third button
//   Rear left               KEY_1 or 2  menus: Start    game: Start
//   Rear right              KEY_5 or 6  menus: nothing  game: coin or Select;
//                                       held a second, the pause menu
//   D-pad                   arrows      menus: moves    game: the D-pad
//
// THE REAR RIGHT BUTTON GOES TO A GAME AS IT IS LET GO: a tap is a coin, and a
// hold of a second opens the pause menu without spending one. In every game,
// a gun game or not, as a Wii Remote's HOME held is (rule 3).
//
// TWO GUNS (#575 rule 11): the first one on is gun 1, which is player 1's,
// and a gun that drops off USB and comes back gets its own number back, while
// the console runs (rule 6). When the console starts with both in, they are
// numbered as they were last time, remembered by their USB ports in
// config/settings.json ("lightgun_order").

#pragma once

#include <cstdint>
#include <string>

namespace lightgun {

// At most two guns, as Sinden's software drives at most two.
constexpr int kMaxGuns = 2;

// A gun's buttons, one bit each.
enum Button : uint16_t {
    kTrigger = 1u << 0,
    kSecond = 1u << 1,   // the pump, and the front left button
    kThird = 1u << 2,    // the front right button
    kStart = 1u << 3,    // the rear left button
    kCoin = 1u << 4,     // the rear right button, as a game is given it
    kUp = 1u << 5,
    kDown = 1u << 6,
    kLeft = 1u << 7,
    kRight = 1u << 8,
};

// One gun as a game sees it this frame.
struct Gun {
    bool present = false;
    // Where it aims, as a share of the screen the border goes around (the
    // console's whole canvas): 0 to 1 across and down. Measured on the first
    // day within 1% of the screen's height everywhere, uncalibrated, with
    // 0..32767 laid over the whole screen. Meaningful only while present.
    float x = 0.5f, y = 0.5f;
    uint16_t buttons = 0;
};

// Starts the thread that finds guns, keeps up with them coming and going, and
// reads them. Idempotent.
void start();

// How many guns are plugged in now, and which: bit n for gun n + 1. Cheap.
int count();
unsigned plugged();

// Gun `n` (0 or 1) now. Cheap: a copy taken under the reader's lock, read
// once a frame by the game's input (App::readGameInput).
Gun gun(int n);

// WHERE THE GUN'S PRESSES GO. In the menus (nothing playing, or the pause
// menu over a game) its buttons are the console's own controller presses, as
// a Wii Remote's are: SDL gamepad events from a pad id no real pad has
// (isGun). In a game they are the game's alone, read by state through gun(),
// and nothing is pushed.
void setInMenus(bool inMenus);
// While any game runs, the console's or an emulator of its own: only then
// does the rear right button's hold open the pause menu (and, under it, close
// it, as a Wii Remote's HOME held does).
void setInGame(bool inGame);

// True once per hold: the rear right button held a second while a game runs.
bool takePauseHold();

// True when a gun button went down in a game since the last call: a press,
// for the screen's dim and time played (App::followGunsInGame). In the menus
// the pushed presses count by themselves (App::handleEvent).
bool takePressInGame();

// True for a press a gun made in the menus.
bool isGun(uint32_t padId);

// WHERE THE AIM LANDS ON THE GAME'S PICTURE. `x`, `y` are the gun's aim as a
// share of the screen (Gun::x, Gun::y); the picture is the rectangle it is
// drawn in, as a share of the same screen. The answer is
// libretro's, -32767 at the picture's left or top edge to 32767 at its right
// or bottom; false when the aim is off the picture, which is where a game's
// reload by shooting off the screen is (the bars beside a 4:3 picture are
// still inside the border, so the gun sees them).
bool onPicture(float x, float y, float picX, float picY, float picW, float picH, int16_t* outX,
               int16_t* outY);

// THE DEVICE A CORE IS GIVEN FOR A GUN, on the port of the player holding it,
// in a light-gun game (catalog's lists say which games those are); 0 for a
// core the console does not hand a gun yet. FinalBurn Neo first; one core per
// change after it (#575).
unsigned deviceFor(const std::string& core);

// The share of the screen's height the border takes on each side (#575 rule
// 9): white, 2%.
constexpr float kBorderShare = 0.02f;

// --light-gun-test: the rules here with no gun and no screen.
int test();

}  // namespace lightgun
