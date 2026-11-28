// The light guns' calibration screen (#575 rules 12 and 13): over everything,
// a game paused under it, opened by Settings' Light gun calibration row or by
// a gun's D-pad held left three seconds, in the menus or in any game.
//
// WHAT IT DOES, in order:
//   Shoot  "Shoot the middle": the border around a black screen and a target
//          at its middle. The shot says which gun, and that it is aimed at the
//          middle. With Sinden's software calibrating (the D-pad held left: the
//          gun's aim held at exactly the middle, lightgun::parked), the same
//          shot is what sets Sinden's calibration, kept in the gun; the screen
//          waits until the aim moves again, which is Sinden done.
//   Sweep  "Keep aiming at the middle": the camera's sweep (guncamera.h), about
//          four seconds, the exposure kept the middle of those that saw the
//          border, remembered for this gun and TV.
//   Done   the gun's aim as a dot, so the player sees where it lands, and
//          Press (A) to continue (the trigger is a gun's A in the menus).
//   Missed "Couldn't see the screen": the camera saw the border at no
//          exposure; its exposure stays as it was. Press (A) to try again.
// Back (the pump, a pad's B) leaves at any step and changes nothing more,
// except while Sinden's software holds a gun's aim: then only that gun can end
// it (its pump cancels Sinden's calibration too), or Sinden would set its
// calibration at the gun's next shot wherever it pointed.
//
// WHY THE SHOT COMES FIRST AND THE SWEEP AFTER: while Sinden's software
// calibrates, it holds the aim at the middle, so the readings say nothing of
// the camera until it lets go (lightgun.h, Sight).
//
// EITHER ANSWER WORKS on whether Sinden's software sees the D-pad held left
// while the console holds the gun's input devices (unknown until a gun was in
// hand): the console opens the screen from the hold it reads itself, and the
// shot is Sinden's set only when the aim was held at the middle. The log says
// which: "aim at the middle" or "aim not at the middle" on the hold's line.

#include "app.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "guncamera.h"
#include "lightgun.h"
#include "screenparts.h"
#include "sound.h"

using namespace design;   // NOLINT: the design system's names (design.h, motion.h, card.h)

namespace {

// How long a shot taken with the aim held at the middle waits for Sinden's
// software to let go before the screen takes the shot as not Sinden's.
constexpr int64_t kSindenSetsMs = 3000;

const char* stepName(App::GunCalibration::Step s) {
    switch (s) {
        case App::GunCalibration::Step::Shoot: return "shoot";
        case App::GunCalibration::Step::Sweep: return "sweep";
        case App::GunCalibration::Step::Done: return "done";
        case App::GunCalibration::Step::Missed: return "missed";
        default: return "closed";
    }
}

}  // namespace

void App::openGunCalibration(int gun) {
    using Step = GunCalibration::Step;
    if (gunCal.open() && gunCal.gun >= 0 && gunCal.step == Step::Sweep) guncamera::cancel(gunCal.gun);
    const bool was = gunCal.open();
    gunCal.step = Step::Shoot;
    gunCal.gun = gun;
    gunCal.waitForSinden = false;
    if (!was) {
        // A direction held in the menus as the D-pad was held left stops
        // repeating behind the screen; every press is the screen's now.
        letGoOfHeld();
        gunCal.shown.retarget(1.0f, kFocusDuration);
        sound::play(sound::Cue::Activate);
    }
    std::fprintf(stderr, "[guncal] open%s for %s%s\n", was ? " again" : "",
                 gun < 0 ? "whichever gun shoots" : (gun == 0 ? "gun 1" : "gun 2"),
                 playing ? ", the game paused" : "");
}

void App::closeGunCalibration(const char* why) {
    if (!gunCal.open()) return;
    if (gunCal.step == GunCalibration::Step::Sweep && gunCal.gun >= 0)
        guncamera::cancel(gunCal.gun);
    std::fprintf(stderr, "[guncal] closed from %s: %s\n", stepName(gunCal.step), why);
    gunCal.step = GunCalibration::Step::Closed;
    gunCal.gun = -1;
    gunCal.waitForSinden = false;
    gunCal.shown.settle(0.0f);
    gunCal.capture = false;
}

void App::followGunCalibration(float dt) {
    using Step = GunCalibration::Step;
    gunCal.shown.tick(dt);
    if (!gunCal.open() || gunCal.capture) return;
    const int n = gunCal.gun;
    if (n >= 0 ? !(lightgun::plugged() & (1u << n)) : lightgun::plugged() == 0) {
        closeGunCalibration(n >= 0 ? "the gun left" : "no gun is plugged in");
        return;
    }
    if (gunCal.step == Step::Shoot && n >= 0 && gunCal.waitForSinden) {
        if (!lightgun::parked(n)) {
            std::fprintf(stderr, "[guncal] gun %d's aim moves again: Sinden set its calibration\n",
                         n + 1);
            gunCal.waitForSinden = false;
            gunCal.step = Step::Sweep;
            guncamera::calibrate(n);
        } else if (lightgun::clockMs() - gunCal.shotAt >= kSindenSetsMs) {
            // Still held: Sinden did not take the shot. Another one may.
            std::fprintf(stderr, "[guncal] gun %d's aim still held at the middle %lld ms after "
                                 "the shot\n",
                         n + 1, static_cast<long long>(kSindenSetsMs));
            gunCal.waitForSinden = false;
        }
    }
    if (gunCal.step == Step::Sweep && n >= 0) {
        const guncamera::Progress p = guncamera::progress(n);
        gunCal.along = p.along;
        if (p.done) {
            gunCal.step = p.exposure >= 0 || p.noCamera ? Step::Done : Step::Missed;
            std::fprintf(stderr, "[guncal] gun %d: %s\n", n + 1,
                         p.noCamera          ? "no camera to set; done"
                         : p.exposure >= 0   ? "exposure found; done"
                                             : "the border seen at no exposure");
        }
    }
    if (gunCal.step == Step::Done && n >= 0) {
        const lightgun::Gun g = lightgun::gun(n);
        gunCal.aimShown = g.present;
        gunCal.aim[0] = g.x;
        gunCal.aim[1] = g.y;
    }
}

bool App::gunCalibrationTakes(const SDL_Event& e) {
    using Step = GunCalibration::Step;
    if (!gunCal.open()) return false;
    bool a = false, b = false;
    int from = -1;   // the gun that pressed, -1 for a pad or the keyboard
    switch (e.type) {
        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
            from = lightgun::gunOf(e.gbutton.which);
            a = e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH;
            b = e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST;
            break;
        case SDL_EVENT_KEY_DOWN:
            if (e.key.key == SDLK_POWER || e.key.key == SDLK_SLEEP) return false;
            if (e.key.repeat) return true;
            a = e.key.key == SDLK_RETURN;
            b = e.key.key == SDLK_ESCAPE || e.key.key == SDLK_BACKSPACE;
            break;
        case SDL_EVENT_GAMEPAD_BUTTON_UP:
        case SDL_EVENT_KEY_UP:
        case SDL_EVENT_GAMEPAD_AXIS_MOTION:
            return true;
        default:
            return false;
    }
    if (!a && !b) return true;
    const int n = gunCal.gun;
    // A GUN SINDEN'S SOFTWARE IS CALIBRATING ends only from that gun: the
    // one this screen calibrates, or before a shot, any whose aim is held.
    int heldBy = -1;
    for (int g = 0; g < lightgun::kMaxGuns && heldBy < 0; ++g)
        if ((n < 0 || g == n) && lightgun::parked(g)) heldBy = g;
    if (b) {
        if (heldBy >= 0 && from != heldBy) return true;
        sound::play(sound::Cue::Back);
        closeGunCalibration(from >= 0 ? "the gun's pump" : "Back");
        return true;
    }
    switch (gunCal.step) {
        case Step::Shoot:
            // Only a gun shoots, and once one has, only that gun; while
            // Sinden's software holds a gun's aim, only that one.
            if (from < 0 || (n >= 0 && from != n) || (heldBy >= 0 && from != heldBy) ||
                gunCal.waitForSinden)
                return true;
            gunCal.gun = from;
            sound::play(sound::Cue::Activate);
            if (lightgun::parked(from)) {
                gunCal.waitForSinden = true;
                gunCal.shotAt = lightgun::clockMs();
                std::fprintf(stderr, "[guncal] gun %d shot the middle, its aim held there by "
                                     "Sinden: waiting for Sinden to set it\n",
                             from + 1);
            } else {
                std::fprintf(stderr, "[guncal] gun %d shot the middle; Sinden is not "
                                     "calibrating\n",
                             from + 1);
                gunCal.step = Step::Sweep;
                gunCal.along = 0.0f;
                guncamera::calibrate(from);
            }
            break;
        case Step::Sweep:
            break;
        case Step::Done:
            sound::play(sound::Cue::Activate);
            closeGunCalibration("continue");
            break;
        case Step::Missed:
            sound::play(sound::Cue::Activate);
            gunCal.step = Step::Shoot;
            gunCal.waitForSinden = false;
            std::fprintf(stderr, "[guncal] trying again\n");
            break;
        default:
            break;
    }
    return true;
}

void App::drawGunCalibration(float sc) {
    using Step = GunCalibration::Step;
    const float alpha = gunCal.shown.value();
    if (!gunCal.open() || alpha <= 0.001f) return;
    const float w = ui::kCanvasWidth, h = ui::kCanvasHeight;
    // Black, so the border is the one bright frame the camera finds.
    renderer.draw(ui::Rect{0, 0, w, h, 0, ui::Color::black(alpha)});
    renderer.setContentAlpha(alpha);
    drawGunBorder(gunBorderEdge(static_cast<int>(std::lround(sc * h))));
    // THE TARGET: two rings and a dot at the middle, grey enough that the
    // border stays the brightest thing the camera sees.
    const float cx = w * 0.5f, cy = h * 0.5f;
    const ui::Color ring = ui::Color::white(0.7f);
    for (const float d : {220.0f, 120.0f}) {
        ui::Rect r{cx - d * 0.5f, cy - d * 0.5f, d, d, d * 0.5f, ui::Color::white(0)};
        r.border = 6.0f;
        r.borderColor = ring;
        renderer.draw(r);
    }
    renderer.draw(ui::Rect{cx - 9.0f, cy - 9.0f, 18.0f, 18.0f, 9.0f, ring});

    const ui::TextStyle st = ui::TextStyle::Title2;
    const float base = h * 0.75f;
    auto centred = [&](const char* s) {
        text.draw(renderer, s, (w - text.measure(s, st, sc)) * 0.5f, base, st,
                  ui::Color::white(0.92f), sc);
    };
    switch (gunCal.step) {
        case Step::Shoot:
            centred("Shoot the middle");
            break;
        case Step::Sweep: {
            centred("Keep aiming at the middle");
            // How far along, under the words.
            const float bw = 420.0f, bh = 8.0f, by = base + 56.0f;
            renderer.draw(ui::Rect{(w - bw) * 0.5f, by, bw, bh, bh * 0.5f, ui::Color::white(0.2f)});
            const float f = std::clamp(gunCal.along, 0.0f, 1.0f);
            if (f > 0.0f)
                renderer.draw(ui::Rect{(w - bw) * 0.5f, by, std::max(bw * f, bh), bh, bh * 0.5f,
                                       ui::Color::white(0.7f)});
            break;
        }
        case Step::Done:
            if (gunCal.aimShown) {
                // WHERE THE GUN LANDS NOW, in the console's colour, ringed in
                // black so it shows on the white border too.
                const float d = 34.0f;
                ui::Rect dot{gunCal.aim[0] * w - d * 0.5f, gunCal.aim[1] * h - d * 0.5f, d, d,
                             d * 0.5f, ui::palette::kScreenCyan};
                dot.border = 4.0f;
                dot.borderColor = ui::Color::black(0.9f);
                renderer.draw(dot);
            }
            drawPressPrompt(renderer, text, sc, "Press", "to continue", base, 1.0f);
            break;
        case Step::Missed:
            centred("Couldn't see the screen");
            drawPressPrompt(renderer, text, sc, "Press", "to try again", base + 90.0f, 1.0f);
            break;
        default:
            break;
    }
    renderer.setContentAlpha(1.0f);
}
