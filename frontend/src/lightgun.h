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
// reads them. A gun's node that cannot be opened yet, as one udev has just
// made, is tried again until it opens or goes. Idempotent.
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

// True for a press a gun made in the menus; and which gun, 0 or 1, -1 for
// any other controller's.
bool isGun(uint32_t padId);
int gunOf(uint32_t padId);

// FOR A CAPTURE ON A MACHINE WITH NO GUN (--light-guns-for-capture): count()
// and plugged() say these guns are in, so Settings shows its light gun rows.
// Nothing else changes: no gun is read and no camera touched.
void pretendForCapture(unsigned plugged);

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

// --- WHAT THE GUN'S CAMERA MAKES OF THE SCREEN (#575 rule 12) ---------------
//
// THE SIGNAL. Sinden's software reports the aim at exactly 0 or 32767 on an
// axis when its camera does not see the border, and in between when it does
// (measured on the reference console, 2026-11-28). The gun reports every
// change of the aim and nothing else. Those two classes of reading share no
// value, so going from one to the other is always a change, and always
// reported: at every moment, the gun's latest reading says whether its camera
// last saw the border, a gun held still included. What it cannot say is where
// the gun was pointed: a gun put down, or aimed off the screen for a reload,
// reads at the edge too. guncamera.h decides what the readings mean.
struct Sight {
    bool present = false;
    int64_t onMs = 0;    // time in the window the latest reading was on the screen
    int64_t offMs = 0;   // ... at its edge: off it, or the border not seen
    int flips = 0;       // times it went from on the screen to its edge
    int shots = 0;       // trigger pulls
    int shotsOff = 0;    // ... with the latest reading at the edge
};
// Gun `n` from `fromMs` to now, on clockMs()'s clock. The readings of the
// last kSightKeepMs are kept.
Sight sight(int n, int64_t fromMs);
constexpr int64_t kSightKeepMs = 10000;
// The reader's clock, in milliseconds: steady, not the wall's.
int64_t clockMs();

// SINDEN'S CALIBRATION (#575 rule 13). The D-pad held left three seconds puts
// Sinden's software in calibration: the gun's aim then stays at exactly the
// middle of the screen until the trigger sets the calibration or the pump
// cancels it (measured 2026-11-27). The console sees the hold itself, on the
// gun's keys, so the screen opens whether or not Sinden's software saw it.
constexpr int64_t kCalibrateHoldMs = 3000;
// True once per hold, as the D-pad has been held left kCalibrateHoldMs on a
// gun, in the menus or in a game; `*n` is which.
bool takeCalibrateHold(int* n);
// Whether gun `n`'s latest reading is the middle of the screen to within one
// step on both axes, where Sinden's software holds the aim while it
// calibrates.
bool parked(int n);

// WHERE GUN `n` IS: its USB device in sysfs (its camera hangs on the same hub,
// guncamera.h), its port (the key that tells guns apart) and its USB id
// ("16c0:0f01"). Empty while gun n is not plugged in.
struct Where {
    std::string usb, port, id;
};
Where where(int n);

// THE RECOIL SWITCH (#575 rule 14), in Settings: saved as "lightgun_recoil" in
// settings.json, "off" unless it says "on". The console does not kick the gun:
// Sinden's software does, set at its start, and Sinden's launcher
// (cabinetos-sinden, #575 phase 1) reads this word for each gun's copy and
// starts the copy again when it changes. On is the same in every game: a tap
// kicks once and a hold keeps kicking.
bool recoil();
void setRecoil(bool on);

// --light-gun-test: the rules here with no gun and no screen.
int test();

}  // namespace lightgun
