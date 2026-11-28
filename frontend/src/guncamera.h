// The camera of each Sinden light gun sets itself (#575 rule 12): the
// exposure at which the gun sees the border, found by calibration and kept
// while a game plays, remembered per gun and per TV.
//
// WHY THE CONSOLE, NOT SINDEN'S SOFTWARE. Sinden's software sets its camera's
// exposure once, as it starts (78, "Exposure AutoSet to 78" in its log), and
// a fixed exposure fails as a room's light changes: on the reference console
// on 2026-11-28 the gun saw the border at every exposure from 10 to 160 at
// 08:35, and at 09:50, the room brighter, lost it about three readings in four
// at 78 while seeing it 510 readings of 510 at 40. The camera's settings may
// be changed through v4l2 while Sinden's software streams from it (measured),
// and a change holds until its next start; the launcher starts a copy again by
// itself (cabinetos-sinden, phase 1), so the console sets it again whenever it
// finds the camera not at the exposure it keeps.
//
// THE LADDER, 1.5 ms to 16 ms. Above 16 ms the camera slows (66 frames a
// second at 16 ms, 52 at 20, 10 at 120; measured 2026-11-27) and below 1.5 ms
// it saw nothing at night.
//
// WHAT "SEES THE BORDER" RESTS ON: the gun's latest reading, on the screen or
// at its edge (lightgun.h, Sight), which says whether its camera last saw the
// border even while it is held still. A look at one exposure is the share of
// its time the reading was on the screen. A gun put down, or aimed off the
// screen, reads at the edge too: so a look counts only while the player aims
// at the screen (calibration's "Keep aiming at the middle"), and in a game
// only trouble starts one (trouble(), below).
//
// THE CAMERAS ARE TOUCHED ONLY WHILE IT MATTERS: while a light-gun game shows
// its border and is being played, or while the calibration screen is open
// (setWanted). Never in the menus, where no border is drawn and the gun sees
// nothing to aim by. One thread does it all, never the frame's: a camera's
// control is a USB request, and one that drops off USB can take seconds to
// answer.

#pragma once

#include <cstdint>
#include <string>

#include "lightgun.h"

namespace guncamera {

// THE EXPOSURES TRIED, in the camera's unit (v4l2 `exposure_time_absolute`,
// 100 microseconds): eleven, each about a quarter more than the last, so each
// step changes the picture as much at the bottom as at the top. 19, 39 and
// 79 are Sinden's own "very low", "low" and "default".
constexpr int kRungs = 11;
constexpr int kLadder[kRungs] = {15, 19, 24, 30, 39, 49, 62, 79, 100, 126, 160};

// A LOOK SEES THE BORDER when the gun's reading was on the screen for at
// least this share of it.
constexpr float kSees = 0.9f;

// THE MIDDLE (rule 12: "finds the exposures at which the gun sees the border
// and takes the middle of them"): the middle rung of the longest run of rungs
// that see it; of two runs as long, and of two middles, the lower, since a
// room growing brighter, the change measured, needs less. -1 when no rung
// sees it.
int middleOf(const float (&seen)[kRungs]);

// ONE SWEEP UP THE LADDER for one gun: at each rung the camera is given the
// exposure, left kSettleMs for the camera and Sinden's software to take it,
// then looked at for kLookMs. Eleven rungs, about four seconds.
class Sweep {
public:
    static constexpr int64_t kSettleMs = 120;
    static constexpr int64_t kLookMs = 250;
    void begin(int64_t t);
    bool running() const { return rung_ >= 0 && rung_ < kRungs; }
    // The exposure the camera should have now, while it runs.
    int exposure() const { return kLadder[running() ? rung_ : kRungs - 1]; }
    // Where this rung's look begins: the sight the gun had since then is what
    // tick() is given.
    int64_t lookFrom() const { return at_ + kSettleMs; }
    // At `t`, with the gun's sight since lookFrom(): once this rung has been
    // looked at, its share is kept and the next rung begins. True as the last
    // is done.
    bool tick(int64_t t, const lightgun::Sight& s);
    // How far along, 0 to 1.
    float progress() const;
    // The share each rung saw the border; the middle of them once it is done.
    float seen[kRungs] = {};
    int result() const { return middleOf(seen); }

private:
    int rung_ = -1;
    int64_t at_ = 0;
};

// TROUBLE IN A GAME, over the gun's last kTroubleMs (rule 12: "the console
// keeps adjusting while a game plays"):
//   Flicker: the reading went from the screen to its edge at least six times,
//     so the camera catches the border on some frames and not on others, as
//     at 78 in the bright room (about three readings in four lost). A reload
//     aimed off the screen is one; nobody reloads three times a second.
//   Blind: the trigger pulled three times or more and the reading never on
//     the screen: shooting at a screen the camera cannot see.
// A gun put down, or held away from the screen, flickers not and shoots not,
// and is no trouble.
enum class Trouble { None, Flicker, Blind };
constexpr int64_t kTroubleMs = 2000;
Trouble trouble(const lightgun::Sight& lastTwoSeconds);
// After a sweep in a game, the next waits this long, whatever it found.
constexpr int64_t kSweepAgainMs = 30000;
// And none starts before the border has been up this long: time to pick the
// gun up.
constexpr int64_t kFollowAfterMs = 2000;

// REMEMBERED PER GUN AND PER TV, in config/lightgun-cameras.json: for each TV,
// the exposure each gun was last given by a sweep. A TV by its EDID's maker,
// product code and serial number, which gamescope's own copy of the EDID
// keeps as well; a gun by its USB id and port, as lightgun.h tells guns apart.
// A gun with nothing remembered on this TV starts at what another gun was
// last given on it, since the TV and the room set the exposure more than the
// gun does (both guns saw the border at the same exposures, 2026-11-28); with
// nothing for the TV at all, at Sinden's own, until a sweep.
std::string tvKey(const std::string& edid);   // "GSM-c0a4-01010101"; empty if no EDID
std::string tvNow();                          // the TV plugged in now, or "unknown"
std::string gunKey(const lightgun::Where& w); // "16c0:0f01@0000:c8:00.0-1.2.2"
int remembered(const std::string& tv, const std::string& gun);   // -1 for none
void remember(const std::string& tv, const std::string& gun, int exposure);

// THE GUN'S CAMERA: the SindenCam on the gun's own hub (each gun is a small
// hub holding its camera and the gun, read on the reference console on
// 2026-11-27), its capture node, the one whose index is 0, as Sinden's
// launcher pairs them; never by device numbers or order. Empty when there is
// none, or more than one.
std::string cameraOf(const std::string& gunUsb);

// The worker that does all of the above.
void start();
// Told once a frame (App::followGunsInGame): `inPlay`, a light-gun game shows
// its border and is being played, not paused; `calibrating`, the calibration
// screen is open. With neither, no camera is touched.
void setWanted(bool inPlay, bool calibrating);
// THE CALIBRATION SCREEN'S SWEEP for gun `n`: begun, or stopped with the
// camera put back as it was.
void calibrate(int n);
void cancel(int n);
struct Progress {
    bool running = false;
    bool done = false;       // a sweep asked by calibrate() has finished
    float along = 0.0f;      // 0 to 1
    int exposure = -1;       // what it found, -1 for nothing
    bool noCamera = false;   // the gun has no camera the console can reach
};
Progress progress(int n);

// --gun-camera-test: the rules here, against the exposures measured on the
// reference console, with no gun, no camera and no screen.
int test();

}  // namespace guncamera
