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

// The same question asked of a game. A ROM payload carries its own platform
// slug and fs_slug, so Home can decide whether the most recently played game is
// one this console can resume without fetching the platform list first.
Coverage coverageFor(const romm::Game& g);

inline bool playable(const romm::Platform& p) {
    return coverageFor(p).support == Support::Playable;
}

inline bool playable(const romm::Game& g) {
    return coverageFor(g).support == Support::Playable;
}

// The tag a save or state is filed under on RomM, for a given manifest core
// name. This is the compatibility marker: Cabinet greys out a state whose tag
// does not match the core about to run, which is what stops someone being
// offered a save that cannot load.
//
// **CabinetOS shares Cabinet's tags, and only because of work already done.**
// A Gambatte state is bit-identical between Cabinet's macOS arm64 build and a
// Linux x86-64 build AT THE SAME COMMIT; core-manifest.json pins that commit,
// build-core.sh asserts it, and CI proved the artifact reproducible across two
// machines. Those three together make a shared tag a fact rather than a hope,
// and they are why a state written on an Apple TV loads on this console.
//
// The rule for adding a core: share Cabinet's tag ONLY where the build is
// provably the same thing — same pinned commit AND the same build arguments.
// Where CabinetOS pulls a different lever, it must use a different tag, or
// Cabinet will offer someone a state that cannot load. Wrong in the safe
// direction costs a greyed-out entry; wrong the other way costs progress.
//
// Returns nullptr for a core whose tag has not been settled, which is a refusal
// to upload rather than a licence to guess.
const char* emulatorTag(const char* manifestCoreName);

}  // namespace catalog
