#include "catalog.h"

#include <sys/stat.h>

#include <map>

#include <cstring>
#include <string>

namespace catalog {
namespace {

struct Entry {
    const char* slug;
    const char* fsSlug;   // nullptr when the slug alone is enough
    Support support;
    const char* core;
    const char* reason;
    // True for the three cores that render through RETRO_ENVIRONMENT_SET_HW_RENDER.
    // A fact about the CORE rather than about this console, which is why it
    // lives in the table beside the core name — but it is answered here rather
    // than by asking the core, because the answer has to be available before
    // anything is loaded, while a shelf is being drawn.
    bool hwRender = false;
    // Set only where the slug alone is ambiguous: the name of the SYSTEM this
    // row actually serves, used to qualify a tile so two platforms with the
    // same name can be told apart. Cabinet's manifest carries the same thing in
    // its `systems` field.
    //
    // LAST IN THE STRUCT ON PURPOSE. Every row below is positional, so a field
    // inserted in the middle would silently re-assign the ones that follow it —
    // the three rows ending in `true` would have handed that `true` to the
    // wrong member.
    const char* system = nullptr;
};

// Derived from Cabinet's core-manifest.json, 2026-09-14. The manifest is the
// source of truth and this follows it; if a core is added there, add it here.
//
// It is not generated from the manifest because the manifest is not in this
// repository and is not fetchable — see docs/PROJECT.md, open question 13. When
// it lands, this table is a candidate for generation.
const Entry kTable[] = {
    {"3do",                  nullptr,     Support::Playable, "opera",           nullptr},
    // One slug, two platforms, two different cores. This is the case the
    // "never key on slug alone" rule exists for.
    {"arcade",               "FBNEO",     Support::Playable, "fbneo_libretro",  nullptr,
     false, "FinalBurn Neo"},
    {"arcade",               "MAME2003",  Support::Playable, "mame2003_plus",   nullptr,
     false, "MAME 2003-Plus"},
    {"atari2600",            nullptr,     Support::Playable, "stella2014",      nullptr},
    {"atari7800",            nullptr,     Support::Playable, "prosystem",       nullptr},
    {"dc",                   nullptr,     Support::Playable, "flycast",         nullptr, true},
    {"gamegear",             nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"gb",                   nullptr,     Support::Playable, "gambatte",        nullptr},
    {"gba",                  nullptr,     Support::Playable, "mgba",            nullptr},
    {"gbc",                  nullptr,     Support::Playable, "gambatte",        nullptr},
    {"genesis",              nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"n64",                  nullptr,     Support::Playable, "mupen64plus",     nullptr, true},
    {"nds",                  nullptr,     Support::Playable, "melonds",         nullptr},
    {"neo-geo-pocket-color", nullptr,     Support::Playable, "beetle_ngp",      nullptr},
    {"nes",                  nullptr,     Support::Playable, "fceumm",          nullptr},
    {"ngc",                  nullptr,     Support::Playable, "dolphin",         nullptr},
    {"ps2",                  nullptr,     Support::Playable, "pcsx2",           nullptr},
    {"psp",                  nullptr,     Support::Playable, "ppsspp",          nullptr, true},
    {"psx",                  nullptr,     Support::Playable, "pcsx_rearmed",    nullptr},
    {"saturn",               nullptr,     Support::Playable, "beetle_saturn",   nullptr},
    {"segacd",               nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"sega32",               nullptr,     Support::Playable, "picodrive",       nullptr},
    {"sms",                  nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"snes",                 nullptr,     Support::Playable, "snes9x",          nullptr},
    {"tg16",                 nullptr,     Support::Playable, "beetle_pce_fast", nullptr},
    {"turbografx-cd",        nullptr,     Support::Playable, "beetle_pce_fast", nullptr},
    {"vectrex",              nullptr,     Support::Playable, "vecx",            nullptr},
    {"virtualboy",           nullptr,     Support::Playable, "beetle_vb",       nullptr},

    // A core exists and Cabinet does not ship it. The manifest says so in as
    // many words — "iOS-only by decision" — and there is no tvOS build, which
    // is the platform CabinetOS most resembles. Not carried over without a
    // deliberate decision, because whatever made it wrong for a television
    // has not changed.
    {"Game & Watch",         nullptr,     Support::Excluded, "gw",
     "iOS-only in Cabinet by decision; never built for tvOS"},
};

bool eq(const char* a, const std::string& b) { return b == a; }

}  // namespace

namespace {

// Returns the table row, not a Coverage, because the row carries one thing the
// caller needs that the answer does not: whether the core is hardware-rendered.
const Entry* lookup(const std::string& slug, const std::string& fsSlug) {
    const Entry* slugOnly = nullptr;
    for (const Entry& e : kTable) {
        if (!eq(e.slug, slug)) continue;
        if (e.fsSlug) {
            if (eq(e.fsSlug, fsSlug)) return &e;
            continue;   // right slug, wrong core — keep looking
        }
        slugOnly = &e;
    }
    // A slug that matches an entry needing an fsSlug, but whose fsSlug matched
    // none of them, falls through to nullptr — correctly. An unrecognised
    // arcade set is not playable just because it says "arcade", since we would
    // not know which core to hand it to.
    return slugOnly;
}

}  // namespace

const char* emulatorTag(const char* core) {
    if (!core) return nullptr;
    // Cabinet's own strings, from RommApp/RommApp/Native/NativeCore.swift.
    // Only cores whose CabinetOS build matches Cabinet's configuration appear
    // here; see the header.
    struct { const char* core; const char* tag; } kTags[] = {
        // Pinned at d9d6cd06, no build arguments on any platform, and the
        // save-state portability result was proved with this core.
        {"gambatte", "gambatte-native"},
        // Pinned at a7985a9c. CabinetOS builds HAVE_CDROM=0, which is what
        // Cabinet's Apple builds get by default, and the object-file
        // comparison showed the flag changes nothing under core/.
        {"genesis_plus_gx", "gpgx-native"},
        // Pinned at ba61a4fd. CabinetOS builds DYNAREC=lightrec where Cabinet
        // runs the interpreter on iOS/tvOS and ari64 on the Mac — and sharing
        // the tag across that difference is what Cabinet ALREADY does between
        // its own two. Justified rather than assumed: the state format
        // tolerates either backend, and the dynarec section is block addresses
        // rather than machine state. docs/PROJECT.md, open question 13.
        {"pcsx_rearmed", "pcsx-rearmed-native"},
        // Pinned at 66b5d263. CabinetOS builds JIT_ARCH=x64 where Cabinet runs
        // the interpreter on iOS/tvOS and aarch64 on the Mac — and this one was
        // MEASURED, not argued: both builds run Tetris DS to an identical video
        // and audio digest over 600 frames, write states of identical size, and
        // every cross-load lands on the same digest as the matching control.
        // Upstream intends it — the only JIT-conditional lines in any
        // DoSavestate are guarded `if (!file->Saving)`.
        {"melonds", "melonds-native"},
        // Pinned at 733c711a, and built with use_sh2drc=0, which is exactly
        // what Cabinet's Apple builds get. Same commit, same argument, so the
        // tag needs no argument at all. The states were checked anyway and came
        // out byte-identical to the recompiler build's.
        {"picodrive", "picodrive-native"},
        // Pinned at e31759b2, no CPU backend anywhere in this core, and the
        // feature set was read off Cabinet's own shipping archives with `nm -u`
        // rather than guessed: zlib and nothing else.
        //
        // One caveat, and it is Cabinet's rather than ours: libmgba_mac.a
        // reports its revision as `e31759b24-dirty`, so Cabinet's Mac build
        // carries a modification no script applies — the same class of problem
        // as Flycast's unscripted edits. The iOS and tvOS archives are clean at
        // this commit, and tvOS is the platform states travel to and from most.
        {"mgba", "mgba-native"},
    };
    for (const auto& t : kTags)
        if (std::strcmp(t.core, core) == 0) return t.tag;
    return nullptr;
}

namespace {
std::string gCoreDir = "cores/build";

// Turns a table row into the answer for THIS console. The table is a fact about
// Cabinet's manifest; everything here is a fact about the machine it is running
// on, which is why the two are kept apart.
//
// Two ways a Playable row stops being playable, and they are different work:
// the core has not been built, or it has been built and cannot be driven.
Coverage answer(const Entry* e) {
    if (!e) return {Support::NoCore, nullptr,
                    "no core in the manifest serves this system"};

    Coverage c{e->support, e->core, e->reason};
    if (c.support != Support::Playable || !c.core) return c;

    // A manifest name that already ends in _libretro does not get a second one:
    // fbneo_libretro would otherwise be looked up as fbneo_libretro_libretro.so.
    // cores/build-core.sh applies the same rule when it files the artifact —
    // the two must agree, and this comment is on both.
    std::string stem = c.core;
    const std::string suffix = "_libretro";
    if (stem.size() > suffix.size() &&
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        stem.erase(stem.size() - suffix.size());
    const std::string path = gCoreDir + "/" + stem + "_libretro.so";
    struct stat st;
    if (::stat(path.c_str(), &st) != 0 || st.st_size == 0) {
        c.support = Support::NotInstalled;
        c.reason = "the core for this system is not built on this console yet";
        return c;
    }

    // Built, and still not runnable. Asked AFTER the file check so that the
    // reason names the nearer of the two obstacles.
    if (e->hwRender) {
        c.support = Support::NeedsHardwareRender;
        c.reason = "this system needs a hardware-rendered core, which this "
                   "console cannot host yet";
    }
    return c;
}
}  // namespace

void setCoreDirectory(const char* dir) { if (dir) gCoreDir = dir; }

Coverage coverageFor(const romm::Platform& p) {
    return answer(lookup(p.slug, p.fsSlug));
}
Coverage coverageFor(const romm::Game& g) {
    return answer(lookup(g.platformSlug, g.platformFsSlug));
}

std::map<std::string, std::string> optionOverrides(const std::string& coreName) {
    // Nothing yet, deliberately. Every option is answered with the core's own
    // default, which is the correct baseline and is what was missing. A choice
    // belongs here only when there is a reason for it, and the reason belongs
    // beside it — an override with no justification is the thing that goes
    // stale and that nobody can later tell apart from a mistake.
    (void)coreName;
    return {};
}

const char* shortReason(Support s) {
    switch (s) {
        case Support::Playable: return "";
        // Measured against the tile that shows them, not guessed: a library
        // tile's second line holds about twenty-three characters at Footnote,
        // and anything longer comes back as an ellipsis where the explanation
        // was meant to be.
        case Support::NoCore: return "No core for this system";
        case Support::Excluded: return "Not shipped here";
        case Support::NotInstalled: return "Core not built yet";
        case Support::NeedsHardwareRender: return "Needs a 3D core";
    }
    return "Not playable here";
}

std::string displayName(const romm::Platform& p) {
    const std::string base = p.name.empty() ? p.slug : p.name;
    const Entry* e = lookup(p.slug, p.fsSlug);
    // Only the ambiguous rows carry a system name, so everything else comes
    // back exactly as the server named it. Qualifying a platform nobody can
    // confuse would be noise.
    if (e && e->system) return base + " (" + e->system + ")";
    // An arcade set this table does not recognise is still ambiguous to a
    // person — two tiles saying "Arcade" — so fall back to the one field that
    // actually distinguishes them on the server.
    if (!p.fsSlug.empty() && p.slug == "arcade") return base + " (" + p.fsSlug + ")";
    return base;
}

}  // namespace catalog
