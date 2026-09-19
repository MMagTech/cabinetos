#include "catalog.h"

#include <sys/stat.h>

#include <map>
#include <vector>

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

// The manifest's name for a core, given either that name or the file it was
// built into. Resolved against the table, so the answer is a name the table
// actually uses rather than one produced by trimming a string and hoping.
//
// Unrecognised input comes back unchanged. That is the honest answer: a caller
// holding a name this table has never heard of has a problem no normalisation
// can fix.
std::string manifestName(const std::string& core) {
    std::string stem = core;
    // A path is one of the things a caller may hold — --core takes one — and a
    // directory prefix would defeat every comparison below.
    if (const size_t slash = stem.find_last_of('/'); slash != std::string::npos)
        stem.erase(0, slash + 1);
    const std::string dotSo = ".so";
    if (stem.size() > dotSo.size() &&
        stem.compare(stem.size() - dotSo.size(), dotSo.size(), dotSo) == 0)
        stem.erase(stem.size() - dotSo.size());

    auto known = [](const std::string& name) {
        for (const Entry& e : kTable)
            if (e.core && name == e.core) return true;
        return false;
    };
    if (known(stem)) return stem;

    const std::string suffix = "_libretro";
    if (stem.size() > suffix.size() &&
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0) {
        const std::string shorter = stem.substr(0, stem.size() - suffix.size());
        if (known(shorter)) return shorter;
    }
    return stem;
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
        // Pinned at c989c255, and the only core in the set where there is no
        // configuration difference left to justify. The commit is identical on
        // every platform Cabinet ships it to (the manifest's own
        // diverges_across_platforms is false), both of the patches Cabinet's
        // builder applies travel and are asserted, the two CMake levers that
        // the unix build would otherwise decide differently are matched
        // (USING_GLES2, MOBILE_DEVICE), and the CPU engine — which is an
        // OPTION in this core rather than a build flag — is answered with
        // Cabinet's own "IR JIT" in optionOverrides below.
        //
        // What is not proved, stated because it is true of the other five
        // here as well: no state written by this build has been loaded by
        // Cabinet's. The cross-platform load was proved once, on gambatte, and
        // every tag since rests on configuration parity rather than on its own
        // experiment.
        {"ppsspp", "ppsspp-native"},
    };
    for (const auto& t : kTags)
        if (std::strcmp(t.core, core) == 0) return t.tag;
    return nullptr;
}

const char* saveTag(const char* core) {
    if (!core) return nullptr;
    // A state tag is always good enough for a save: it is the stricter test of
    // the two, and every core that passes it writes both.
    if (const char* t = emulatorTag(core)) return t;

    // The five the state rule refuses and a save has no reason to. Each one is
    // the reference implementation's own string, so a card written here lands
    // in the row an Apple TV already reads — which is the whole point, and is
    // how the thirteen Dreamcast cards already on the server become testable.
    //
    // The bytes are the emulated machine's, not the emulator's. See the header
    // for why that is the line, and docs/PROJECT.md, *The save audit*, for the
    // measurement behind it.
    struct { const char* core; const char* tag; } kSaveTags[] = {
        // The VMU. Same pinned commit as the reference implementation
        // (a172e000) but that build carries unscripted working-tree edits, so
        // there is no revision for a STATE to match and emulatorTag correctly
        // says nothing. A 128 KB VMU image in the VMU's own format is not
        // something an unscripted edit can change the shape of.
        {"flycast", "flycast-native"},
        // 3DO NVRAM. Same commit as every platform the reference ships
        // (a501a278), no patches and no build arguments on either side — this
        // one would pass the state rule too and simply has not been through
        // it. The save is the 3DO's own NVRAM either way.
        {"opera", "opera-native"},
        // Arcade NVRAM, FinalBurn Neo's half. Same commit (2444fbe3), one
        // source tree serves every platform the reference builds, no patches
        // and no build arguments.
        {"fbneo_libretro", "fbneo-native"},
        // Arcade NVRAM, MAME 2003-Plus's half. This is the one where the
        // commits genuinely differ — CabinetOS pins 21256d24, which is the
        // reference's MAC revision, while its iOS and tvOS builds are at
        // 93159c0c. A board's NVRAM is its own chip's contents and travels
        // across that; a state would not, and does not.
        {"mame2003_plus", "mame2003plus-native"},
        // Neo Geo Pocket flash. Same commit as the reference's iOS build
        // (a50d5ac2), one source tree, no patches, no build arguments.
        {"beetle_ngp", "ngp-native"},
    };
    const std::string name = manifestName(core);
    for (const auto& t : kSaveTags)
        if (name == t.core) return t.tag;
    return nullptr;
}

namespace {
std::string gCoreDir = "cores/build";
}  // namespace

std::string coreFileName(const std::string& manifestCoreName) {
    // See the header. cores/build-core.sh has the same three lines and the
    // same comment; the two must agree.
    std::string stem = manifestCoreName;
    const std::string suffix = "_libretro";
    if (stem.size() > suffix.size() &&
        stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) == 0)
        stem.erase(stem.size() - suffix.size());
    return stem + "_libretro.so";
}

namespace {

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

    const std::string path = gCoreDir + "/" + coreFileName(c.core);
    struct stat st;
    if (::stat(path.c_str(), &st) != 0 || st.st_size == 0) {
        c.support = Support::NotInstalled;
        c.reason = "the core for this system is not built on this console yet";
        return c;
    }

    // A hardware-rendered core used to stop here: built, on the disk, and
    // still unrunnable, because the host refused RETRO_ENVIRONMENT_SET_HW_RENDER
    // and these three cores draw with GL rather than handing back pixels.
    //
    // It no longer does. The host owns a GLES context and hands the core a
    // framebuffer inside it, and this was measured rather than assumed:
    // Mario Kart 64 reaches its title screen on Mupen64Plus and Ikaruga
    // reaches its own on Flycast, both from the real library, both with
    // sound. So `hwRender` no longer changes the answer — it records which
    // rows take that path, which is the fact PPSSPP will be checked against
    // when it is built. It is kept in place rather than removed because this
    // table is positional and every row below a removed field silently
    // re-assigns; see the struct.
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

std::map<std::string, std::string> optionOverrides(const std::string& core) {
    // Callers hold the core by two different names — the manifest's, which is
    // what the launch path has, and the FILE's, which is what the options
    // audit has when it is walking a directory. So this takes either, and
    // resolves it against the table rather than by stripping suffixes blindly:
    // fbneo_libretro's manifest name ends in _libretro already, so a blind
    // strip would quietly invent a third name for it.
    //
    // Getting this wrong is silent. An override that matches nothing leaves
    // every option at its default and looks exactly like having no override —
    // which is the shape of bug this file already warns about twice.
    const std::string coreName = manifestName(core);

    // Nearly empty, deliberately. Every option is answered with the core's own
    // default, which is the correct baseline and is what was missing. A choice
    // belongs here only when there is a reason for it, and the reason belongs
    // beside it — an override with no justification is the thing that goes
    // stale and that nobody can later tell apart from a mistake.
    if (coreName == "ppsspp") {
        // PPSSPP is the only core in the set whose CPU BACKEND is a runtime
        // option rather than a build flag, so the lever five other cores pull
        // in cores/build-core.sh is pulled here instead. Its declared default
        // is "JIT", the native recompiler; Cabinet ships "IR JIT", which is
        // upstream's own string for the IR interpreter, on all three of its
        // platforms.
        //
        // Matching it. Two reasons, and the first is the standing rule: until
        // a backend difference has been MEASURED not to move a core's state
        // format, match Cabinet's configuration exactly — that is how
        // pcsx_rearmed, melonDS and picodrive were each settled, and PSP has
        // not been through it. The second is that nothing is being given up
        // today: PSP is four games in the reference library, and Cabinet's own
        // bench found the IR interpreter FASTER than the recompiler on an M4
        // (Lumines 1.93 ms against 3.05 ms mean) because compilation stalls
        // land inside frames.
        //
        // What it costs, said plainly: on an x86-64 console the native
        // recompiler is the engine PPSSPP is usually run with, and this leaves
        // it switched off. The experiment that would change this answer is the
        // one cores/backend-diff.sh exists for, run on the two option values
        // rather than two builds — and it needs a PSP game and a machine,
        // which is the SER5.
        //
        // ppsspp_internal_resolution deliberately does NOT appear here. Its
        // declared default is "480x272", the PSP's own screen, which is what
        // RetroArch would answer and what Cabinet ships on a television. It is
        // also the option that used to be this core's defaults trap: the
        // internal default is 0, "Auto (native)", which sizes the render to a
        // display a libretro frontend never reports, so every frame comes out
        // 0x0 and is dropped. Answering declared defaults already closes that,
        // which is this console's first free ride from the core-options work.
        // Raising it is a look-and-performance decision and it waits for real
        // hardware.
        return {{"ppsspp_cpu_core", "IR JIT"}};
    }

    // Genesis Plus GX, and this one decides WHERE THE SAVE IS, not how it
    // plays. The Sega CD's internal backup RAM is the console's own 8 KB, so
    // the core names its file after the BIOS region — `scd_U.brm` — and every
    // Sega CD game in the library shares it. That is real hardware's own
    // behaviour and it is wrong here for two reasons: RomM files a save
    // against ONE rom, so a shared file would have to be filed under whichever
    // game happened to be played last; and 8 KB shared across a library fills
    // up. The reference implementation forced this for the same reasons on
    // 2026-08-16 and its save path assumes it.
    //
    // MEASURED, not read across: the first run of the save work restored
    // Lunar's real 8 KB card to `<stem>.brm` and Genesis Plus GX ignored it
    // and made a fresh `scd_U.brm` beside it. The option is the difference.
    // Each game already has its own save directory here, so "per game" costs
    // nothing beyond the name.
    //
    // cart_size stays at the core's declared "4meg", which already matches —
    // it is recorded here only because the reference forces it explicitly and
    // the reason is worth keeping: unanswered, the core's cart_size global
    // stays 0 and Sonic CD refuses to boot past "RAM cartridge not
    // initialized". Answering declared defaults is what closes that, which is
    // this console's second free ride from the core-options work.
    if (coreName == "genesis_plus_gx") {
        return {{"genesis_plus_gx_system_bram", "per game"}};
    }

    // Opera, and both of these are the difference between 3DO working and
    // 3DO not starting at all.
    //
    // opera_bios is the defaults trap at its purest. Its declared default is
    // "disabled", its value has to be a BIOS FILENAME, and answered with the
    // default the core has no BIOS ROM and there is no boot. RomM holds the
    // 3DO firmware under exactly this name on the reference server and the
    // launch path fetches every file a platform lists into `bios/`, so the
    // two halves meet. THE WEAK JOINT, said out loud: this is a filename
    // written down here and a filename on somebody's server, and nothing
    // checks that they agree. The reference stages whatever 1 MB firmware the
    // platform has UNDER this name, which is the stronger answer and is worth
    // building the day a server is found that calls it something else.
    //
    // opera_nvram_storage is a declared-default-versus-code-fallback mismatch:
    // the option table says "per game" and the core's own unanswered fallback
    // is "shared". Forced to shared deliberately, because it buys a fixed
    // filename — `opera/shared/nvram.0.srm` — that the save sync can rely on,
    // and each game already has its own save directory here so shared IS per
    // game. catalog::saveFiles depends on this being set.
    if (coreName == "opera") {
        return {
            {"opera_bios", "panafz10.bin"},
            {"opera_nvram_storage", "shared"},
        };
    }
    return {};
}

const char* directorySaveRoot(const char* core) {
    if (!core) return nullptr;
    if (manifestName(core) == "ppsspp") return "PSP/SAVEDATA";
    return nullptr;
}

std::vector<SaveFile> saveFiles(const std::string& slug, const std::string& fsSlug,
                                const std::string& stem) {
    // Straight out of the audit's per-platform table, and each row was read
    // off the core that writes it rather than guessed. docs/PROJECT.md, *The
    // save audit*, has the evidence for every line.
    //
    // Nothing here is keyed on the core, because a core is not a platform:
    // genesis_plus_gx appears once, for Sega CD, and the same core running a
    // Master System cartridge has a real battery this console already syncs.

    // Dreamcast. The one row that is not under the save directory: Flycast
    // never answers RETRO_MEMORY_SAVE_RAM — confirmed against its own
    // retro_get_memory_data, which only ever answers RETRO_MEMORY_SYSTEM_RAM —
    // and writes the card into the system directory instead, beside the BIOS.
    //
    // `per_content_vmus` defaults to 0, so the file is the bare
    // `vmu_save_A1.bin`; the capture scans for the suffix anyway, because with
    // that option on the core prefixes the disc's own game id and a capture
    // that insisted on the exact name would silently find nothing.
    if (slug == "dc") {
        SaveFile vmu;
        vmu.path = "dc/vmu_save_A1.bin";
        vmu.captureSuffix = "vmu_save_A1.bin";
        vmu.untouched = Untouched::VmuDirectory;
        vmu.inSystemDir = true;
        return {vmu};
    }

    // Sega CD, and it is TWO regions rather than one. Games prefer the
    // external RAM cartridge when it is present, so a console that treated
    // "the .brm" as one thing would have one of them overwrite the other —
    // which is why the scan excludes `cart.brm` and the cartridge gets its own
    // row. `4Mbit_cart.brm` is fixed by the core's own defaults for cart_bram
    // and cart_size; per-game separation already comes from the save directory.
    if (slug == "segacd") {
        SaveFile internal;
        internal.path = stem + ".brm";
        internal.captureSuffix = ".brm";
        internal.captureExclude = "cart.brm";
        internal.untouched = Untouched::SegaCDBackup;
        SaveFile cart;
        cart.path = "4Mbit_cart.brm";
        cart.captureSuffix = "cart.brm";
        cart.region = "cart";
        cart.untouched = Untouched::SegaCDBackup;
        return {internal, cart};
    }

    if (slug == "neo-geo-pocket-color") {
        SaveFile f;
        f.path = stem + ".flash";
        f.captureSuffix = ".flash";
        return {f};
    }

    // melonDS writes `<content basename>.sav` through its own SRAM manager,
    // debounce-flushed during play and flushed again at unload.
    if (slug == "nds") {
        SaveFile f;
        f.path = stem + ".sav";
        f.captureSuffix = ".sav";
        return {f};
    }

    // Opera writes to a fixed nested path rather than a flat suffix-named
    // file, so this one is exact in both directions.
    if (slug == "3do") {
        SaveFile f;
        f.path = "opera/shared/nvram.0.srm";
        f.untouched = Untouched::ThreeDONvram;
        return {f};
    }

    // Arcade. The two emulators disagree about both the folder and the
    // extension and their contents are not interchangeable, which is exactly
    // why this keeps them apart instead of treating "the arcade save" as one
    // thing. The stem is the set name — `lethalen`, `smashtv` — because that
    // is what the core was handed; see the note in main.cpp about why an
    // arcade entry keeps the server's own file name.
    if (slug == "arcade") {
        SaveFile f;
        f.untouched = Untouched::Uniform;
        if (fsSlug == "MAME2003") {
            f.coreRowName = "mame2003Plus";
            // ONE DIRECTORY DEEPER THAN THE REFERENCE IMPLEMENTATION SAYS, and
            // this was measured rather than carried across. Cabinet writes
            // `nvram/<stem>.nv` straight under the save directory; MAME
            // 2003-Plus as this console builds and drives it puts its whole
            // working tree under a `mame2003-plus/` folder first, so the file
            // is `mame2003-plus/nvram/lethalen.nv`. Confirmed twice on the
            // test VM, 2026-09-19: by running Lethal Enforcers and reading the
            // directory afterwards, and by the orphaned NVRAM the old flat
            // save pile left behind, which sits at exactly that path.
            //
            // Placing it at the reference's path is not a harmless miss. The
            // core does not find it, bootstraps a fresh image instead, and
            // writes that — so the restore silently does nothing and a
            // capture aimed at the same wrong path finds nothing either. That
            // is what the first run of this did.
            f.path = "mame2003-plus/nvram/" + stem + ".nv";
            return {f};
        }
        if (fsSlug == "FBNEO") {
            f.coreRowName = "fbneo";
            f.path = "fbneo/" + stem + ".fs";
            return {f};
        }
        return {};
    }

    // PSP is in this class too and is already built, as a tree rather than a
    // file. directorySaveRoot above is its entry.
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
        // Nothing produces this today. Kept because it is the honest answer
        // for a core that asks for something this context cannot serve —
        // desktop GL or Vulkan rather than GLES — which the host refuses by
        // name. Both cores tested asked for GLES 3.0 and got it.
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
