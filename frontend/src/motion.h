// The animation primitive: easeOut, easeInOut and design::Animated, which
// every focus, fade and slide on the console runs on. Its default duration is
// design.h's kFocusDuration. Moved out of design.h in the restructure (#398).

#pragma once

#include <algorithm>
#include <cmath>

#include "design.h"

namespace design {

// --- Motion -----------------------------------------------------------------

// Ease-out is the default: things arrive quickly and settle. Ease-in-out is for
// a change of state the person asked for. Ease-in is used nowhere.
inline float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

inline float easeInOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t < 0.5f ? 4.0f * t * t * t
                    : 1.0f - std::pow(-2.0f * t + 2.0f, 3.0f) * 0.5f;
}

// One animated scalar that behaves the way the reference implementation's
// animations do: a change re-targets from wherever the value currently is, so
// an interruption mid-flight is smooth rather than a jump.
struct Animated {
    float from = 0, to = 0, elapsed = 0, duration = kFocusDuration;
    // The overlay sets this for the design system's ease-IN-out: a panel that
    // covers the game should leave as deliberately as it arrives, and an
    // ease-out exit snaps away at the end.
    bool smooth = false;

    void retarget(float target, float seconds) {
        if (target == to) return;
        from = value();
        to = target;
        duration = seconds;
        elapsed = 0;
    }
    void tick(float dt) { elapsed = std::min(elapsed + dt, duration); }
    // Jumps straight to the target. A screenshot should show the resting
    // focused state, not a frame part-way through the transition into it.
    void settle(float target) {
        retarget(target, kFocusDuration);
        elapsed = duration;
    }
    float value() const {
        if (duration <= 0) return to;
        const float t = elapsed / duration;
        return from + (to - from) * (smooth ? easeInOut(t) : easeOut(t));
    }
};

}  // namespace design
