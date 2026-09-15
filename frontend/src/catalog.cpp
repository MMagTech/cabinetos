#include "catalog.h"

#include <sys/stat.h>

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
    {"arcade",               "FBNEO",     Support::Playable, "fbneo_libretro",  nullptr},
    {"arcade",               "MAME2003",  Support::Playable, "mame2003_plus",   nullptr},
    {"atari2600",            nullptr,     Support::Playable, "stella2014",      nullptr},
    {"atari7800",            nullptr,     Support::Playable, "prosystem",       nullptr},
    {"dc",                   nullptr,     Support::Playable, "flycast",         nullptr},
    {"gamegear",             nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"gb",                   nullptr,     Support::Playable, "gambatte",        nullptr},
    {"gba",                  nullptr,     Support::Playable, "mgba",            nullptr},
    {"gbc",                  nullptr,     Support::Playable, "gambatte",        nullptr},
    {"genesis",              nullptr,     Support::Playable, "genesis_plus_gx", nullptr},
    {"n64",                  nullptr,     Support::Playable, "mupen64plus",     nullptr},
    {"nds",                  nullptr,     Support::Playable, "melonds",         nullptr},
    {"neo-geo-pocket-color", nullptr,     Support::Playable, "beetle_ngp",      nullptr},
    {"nes",                  nullptr,     Support::Playable, "fceumm",          nullptr},
    {"ngc",                  nullptr,     Support::Playable, "dolphin",         nullptr},
    {"ps2",                  nullptr,     Support::Playable, "pcsx2",           nullptr},
    {"psp",                  nullptr,     Support::Playable, "ppsspp",          nullptr},
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

Coverage lookup(const std::string& slug, const std::string& fsSlug) {
    const Entry* slugOnly = nullptr;
    for (const Entry& e : kTable) {
        if (!eq(e.slug, slug)) continue;
        if (e.fsSlug) {
            if (eq(e.fsSlug, fsSlug)) return {e.support, e.core, e.reason};
            continue;   // right slug, wrong core — keep looking
        }
        slugOnly = &e;
    }
    if (slugOnly) return {slugOnly->support, slugOnly->core, slugOnly->reason};

    // A slug that matches an entry needing an fsSlug, but whose fsSlug matched
    // none of them, lands here — correctly. An unrecognised arcade set is not
    // playable just because it says "arcade", since we would not know which
    // core to hand it to.
    return {Support::NoCore, nullptr, "no core in the manifest serves this system"};
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
    };
    for (const auto& t : kTags)
        if (std::strcmp(t.core, core) == 0) return t.tag;
    return nullptr;
}

namespace {
std::string gCoreDir = "cores/build";

// Downgrades a Playable answer to NotInstalled when the .so is not on this
// machine. Kept separate from the table because the table is a fact about
// Cabinet's manifest and this is a fact about this console today.
Coverage withInstalled(Coverage c) {
    if (c.support != Support::Playable || !c.core) return c;
    const std::string path = gCoreDir + "/" + c.core + "_libretro.so";
    struct stat st;
    if (::stat(path.c_str(), &st) == 0 && st.st_size > 0) return c;
    c.support = Support::NotInstalled;
    c.reason = "the core for this system is not built on this console yet";
    return c;
}
}  // namespace

void setCoreDirectory(const char* dir) { if (dir) gCoreDir = dir; }

Coverage coverageFor(const romm::Platform& p) {
    return withInstalled(lookup(p.slug, p.fsSlug));
}
Coverage coverageFor(const romm::Game& g) {
    return withInstalled(lookup(g.platformSlug, g.platformFsSlug));
}

}  // namespace catalog
