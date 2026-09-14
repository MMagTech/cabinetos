// Which of a RomM library's platforms this console can actually play.
//
// A RomM server holds whatever its owner put in it. CabinetOS ships a fixed set
// of cores, so the two do not match and the gap is not small: the reference
// library has 35 platforms and six of them — Jaguar, ColecoVision, Switch, PS3,
// Vita, Wii — have no core in Cabinet's manifest at all.
//
// A console must not offer a game it cannot run. That is a promise it breaks in
// the worst possible place, after the person has chosen something and is waiting
// for it. But a library that silently drops 240 games is its own bug: someone
// who owns Switch games and sees none will reasonably conclude the scan failed.
//
// So the answer is neither "show everything" nor "hide quietly" — it is to KNOW,
// per platform, and to say so. This is the knowing part.
//
// Keyed on `slug`, with `fsSlug` breaking the tie, because those travel between
// servers whereas `id` does not — the opposite of identity WITHIN one server,
// where `id` is the only unique field. "Arcade" is the case that needs both:
// two platforms, one slug, different cores.

#pragma once

#include "romm.h"

namespace catalog {

enum class Support {
    // A core exists and ships. The game can be launched.
    Playable,
    // Nothing in the manifest serves this system. Switch, PS3, Vita and the
    // rest. Not a defect, just outside what this console is.
    NoCore,
    // A core exists but CabinetOS deliberately does not ship it. There is
    // always a reason, and `reason()` gives it, because a decision nobody can
    // recover is indistinguishable from a bug.
    Excluded,
};

struct Coverage {
    Support support = Support::NoCore;
    const char* core = nullptr;     // manifest core name, when there is one
    const char* reason = nullptr;   // why, when it is not simply playable
};

Coverage coverageFor(const romm::Platform& p);

inline bool playable(const romm::Platform& p) {
    return coverageFor(p).support == Support::Playable;
}

}  // namespace catalog
