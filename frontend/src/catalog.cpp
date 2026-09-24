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
    // NOT A GAP, A DECISION — MMagTech, 2026-09-20: this console will not build
    // it, "as the games are too small on a tv". The reason a person reads has
    // to be about THIS console, not about what Cabinet chose on a phone: 171
    // games is the largest excluded row in the reference library, so it is the
    // tile most likely to be asked about.
    {"Game & Watch",         nullptr,     Support::Excluded, "gw",
     "not built here. These games are too small to play on a television"},
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

        // EIGHT MORE LIBRETRO CORES, 2026-09-23. Their states stayed on the
        // console only because nobody had run the check above, and the check
        // took a minute: every one is pinned at Cabinet's own commit, and all
        // but mupen64plus are built with exactly Cabinet's arguments (none at
        // all, or vecx's HAS_GPU=0 on both sides). The ninth of that group,
        // beetle_saturn, is below with the ones that stay local. MMagTech, rightly:
        // states belong on every libretro core, and open question 25 already
        // said so. Their saves ride the same tags, through saveTag.
        {"fceumm", "fceumm-native"},        // 236ccdfc
        {"snes9x", "snes9x-native"},        // 890b5d44
        {"beetle_pce_fast", "pcefast-native"},  // 2f623abd
        {"beetle_vb", "beetle-vb-native"},  // 83ed4260
        {"prosystem", "prosystem-native"},  // 8a880142
        {"stella2014", "stella2014-native"},  // 4a7da825
        {"vecx", "vecx-native"},            // 8f671cc9, HAS_GPU=0 both sides
        // N64, THE ONE WITH A DIFFERENCE, said plainly. Same commit, f275caf4,
        // on every platform Cabinet ships. But Cabinet's build flags do not
        // link on Linux (see docs/PROJECT.md, *mupen64plus: it builds*), so
        // this build is -O3 -ffast-math with the unix branch's RSP/RDP
        // choices where Cabinet's is -Ofast with its own. A state holds the
        // machine's memory and registers, which neither changes; if a state
        // from here ever refuses to load on an Apple TV, this line is why.
        //
        // Its SAVE is safer than its state: mupen64plus packs EEPROM, four
        // controller paks, SRAM and flash into one 296960-byte struct at this
        // commit, and the seven N64 saves on the reference server each hold
        // data exactly where their game's chip is.
        {"mupen64plus", "mupen64plus-native"},

        // AND THREE OF THE FIVE THAT HAD SAVES ONLY, the same day, for the
        // same reason: same commit as Cabinet, no patches, no build arguments
        // on either side. Their saves travelled already through saveTag.
        {"opera", "opera-native"},            // a501a278
        {"fbneo_libretro", "fbneo-native"},   // 2444fbe3
        {"beetle_ngp", "ngp-native"},         // a50d5ac2, Cabinet's iOS commit
        //
        // THE OTHER TWO STAY SAVES ONLY, and each has a reason that is a fact
        // rather than a missing check (see saveTag for both):
        //   mame2003_plus  pinned at Cabinet's MAC commit, 21256d24; its iOS
        //                  and tvOS builds are at 93159c0c. A state from here
        //                  would be offered on an Apple TV that cannot load it.
        //   flycast        Cabinet's build carries unscripted working-tree
        //                  edits, so there is no revision to match at all.
        //   beetle_saturn  MEASURED: an Apple TV state (Daytona USA) was
        //                  REFUSED by this build. CabinetOS is at Cabinet's
        //                  Mac commit, ed549bda; Cabinet's Apple TV build is
        //                  at a commit its manifest could not recover. Its
        //                  SAVE travels (saveTag), and was proved on Sega Rally.
        //
        // Cabinet's Apple TV builds of NES and SNES are older commits too, and
        // their states load here anyway: Aladdin and R.C. Pro-Am from an Apple
        // TV, both mid-game, 2026-09-23. The fix for all of it is on Cabinet's
        // side: build its Apple TV cores at the commit its manifest pins.
        // Both cores still make and load states on this console.
    };
    for (const auto& t : kTags)
        if (std::strcmp(t.core, core) == 0) return t.tag;
    return nullptr;
}

bool snapshotsAllowed(const char* core) {
    if (!core) return true;
    // True to the machines: memory-card consoles, and everything after them.
    static const char* const kNoSnapshots[] = {"pcsx2", "dolphin"};
    for (const char* c : kNoSnapshots)
        if (std::strcmp(c, core) == 0) return false;
    return true;
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
        // PlayStation 2's memory card and GameCube's, and these two tags were
        // READ OFF THE ROWS ALREADY ON THE SERVER rather than derived from the
        // pattern above — they are plainly `pcsx2` and `dolphin`, with no
        // `-native` suffix, because Cabinet for Mac tags them that way and its
        // comment says why: "PCSX2 rather than Cabinet, because the format is
        // PCSX2's and another PCSX2 could read it."
        //
        // Matching them is the whole point. A card written here has to land in
        // the row a Mac already reads, and RomM matches a row by filename with
        // the tag NOT included — so the tag is what stops two emulators'
        // uploads being confused for one another, and the name is what decides
        // which row is overwritten.
        //
        // NOTE these are not state tags and must never become them. The
        // libretro cores here are hard forks — LRPS2 and libretro/dolphin —
        // and a state written by either will not load in the Mac's build.
        {"pcsx2", "pcsx2"},
        {"dolphin", "dolphin"},
        // Arcade NVRAM, MAME 2003-Plus's half. This is the one where the
        // commits genuinely differ — CabinetOS pins 21256d24, which is the
        // reference's MAC revision, while its iOS and tvOS builds are at
        // 93159c0c. A board's NVRAM is its own chip's contents and travels
        // across that; a state would not, and does not.
        {"mame2003_plus", "mame2003plus-native"},
        // The Saturn's own backup RAM, 32 KB, `BackUpRam Format`. Its STATE
        // stays local (emulatorTag says why); the save is the machine's and
        // an Apple TV's Sega Rally save restored here intact. It only reaches
        // SAVE_RAM while beetle_saturn_save_method is "libretro", which
        // optionOverrides forces.
        {"beetle_saturn", "saturn-native"},

        // 3DO, FBNeo and Neo Geo Pocket were here too, and moved up into
        // emulatorTag on 2026-09-23 when their states went.
        //
        // NES, SNES, N64, TurboGrafx CD and Saturn were added here on
        // 2026-09-23 as saves only, and moved up into emulatorTag the same
        // day, when their states went too. A state tag is always good enough
        // for a save, which is the first line of this function.
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
    // PLAYSTATION 2 IS NOT A LIBRETRO CORE AND ITS FILE IS NOT NAMED LIKE ONE.
    //
    // PCSX2 is a whole emulator, embedded from upstream and built by
    // cores/build-pcsx2.sh rather than cores/build-core.sh. The name says so
    // deliberately: a file called `pcsx2_libretro.so` would be the libretro
    // core, which this console DELIBERATELY DOES NOT SHIP — its source
    // repository does not exist, so it can never be pinned, built in CI or
    // audited. Seeing the two names side by side is how somebody tells at a
    // glance which one a console is running. docs/PROJECT.md, open question 12b.
    if (manifestCoreName == "pcsx2") return "cabinetos-ps2.so";

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

    // Beetle Saturn, and this one also decides where the save is. "libretro"
    // hands the Saturn's backup RAM to the frontend through SAVE_RAM, which
    // is what saveTag's `saturn-native` rides; "mednafen" makes the core keep
    // its own .bkr file instead, which nothing here reads, so every Saturn
    // save would stay on this console and never reach the server.
    //
    // It is already the declared default at the pinned ed549bda. Forced
    // anyway, because the reference forces it for exactly this reason
    // (NativeCoreOptions.swift): its Saturn saves had nowhere to go once
    // before, and a core bump that changed the default would strand them
    // again without a word.
    if (coreName == "beetle_saturn") {
        return {{"beetle_saturn_save_method", "libretro"}};
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
    // PlayStation 2, and both of these are faults rather than preferences.
    //
    // `shared_memory_cards` defaults to ON, which puts every game's save in
    // one `Mcd001.ps2` that belongs to no rom — RomM stores a save against a
    // rom, so a shared card cannot be synced at all. Cabinet for Mac makes the
    // same choice and its comment says why.
    //
    // `analog_mode1` defaults to OFF, which is the DualShock's analogue mode
    // disabled: the sticks do nothing. A PlayStation 2 game with dead sticks
    // reads as a broken emulator, and it is one unanswered option — exactly
    // the class of silent fault the note at the top of this function is about.
    if (manifestName(coreName) == "pcsx2") {
        return {
            {"pcsx2_shared_memory_cards", "disabled"},
            {"pcsx2_analog_mode1", "enabled"},
            {"pcsx2_analog_mode2", "enabled"},
        };
    }
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

FirmwareAliases firmwareAliases(const std::string& slug, const std::string& fsSlug) {
    // Sizes and names taken from the reference implementation's own table,
    // which was built against real hardware, and checked against what the
    // reference server actually serves.

    // Saturn: Japan and NA/EU, 512 KB. THE ONE THAT WAS BROKEN — the server
    // calls it `saturn_bios.bin` and the core opens `sega_101.bin`. Both names
    // go down because Beetle Saturn chooses between them from the disc's own
    // region code at load time.
    if (slug == "saturn")
        return {524288, {"sega_101.bin", "mpr-17933.bin"}, nullptr};

    // Sega CD: NTSC-U, PAL and NTSC-J, a fixed 128 KB boot ROM. The reference
    // server already uses these names, so this row changes nothing there and
    // exists for the server that does not.
    if (slug == "segacd")
        return {131072, {"bios_CD_U.bin", "bios_CD_E.bin", "bios_CD_J.bin"}, nullptr};

    // TurboGrafx-CD: Beetle PCE Fast defaults to System Card 3, 256 KB.
    if (slug == "turbografx-cd")
        return {262144, {"syscard3.pce"}, nullptr};

    // 3DO: every retail BIOS Opera knows is exactly 1 MB, and one name is
    // enough because Opera does not scan by region — it opens exactly the file
    // `opera_bios` names, and optionOverrides always answers `panafz10.bin`.
    // So whatever 1 MB firmware the platform carries is staged under the one
    // name the core will be told to load, and the weak joint that comment
    // warns about is closed.
    if (slug == "3do")
        return {1048576, {"panafz10.bin"}, nullptr};

    // Dreamcast: the name already matches, and the DIRECTORY does not.
    // Flycast reads the boot ROM from `dc/` inside the system directory, and
    // when it is not there it falls back to its own HLE BIOS silently — so a
    // console can look entirely healthy while running an approximation of the
    // machine. Copied rather than moved: a flat `bios/dc_boot.bin` is what
    // RomM sent and what anyone looking would expect to find.
    if (slug == "dc")
        return {2097152, {"dc_boot.bin"}, "dc"};

    return {};
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

    // PlayStation 2. LRPS2 keeps the card in the SAVE directory once shared
    // cards are turned off — measured, not assumed: with
    // `pcsx2_shared_memory_cards` at its default it writes `Mcd001.ps2` into
    // the system directory, which is the arrangement Cabinet for Mac rejects
    // in as many words ("a shared card belongs to no rom in particular").
    // With it off the file is `<stem>.ps2` in the save directory, which is
    // already per game, so per-game separation costs nothing extra.
    //
    // NAMED THE MAC'S WAY on the server, because the whole point is that the
    // card travels — see SaveFile::macRowName.
    if (slug == "ps2") {
        SaveFile f;
        f.path = stem + ".ps2";
        f.captureSuffix = ".ps2";
        f.region = "ps2";
        f.untouched = Untouched::Ps2Format;
        f.macRowName = true;
        return {f};
    }

    // GameCube. Dolphin writes whatever its slot A device says, and the
    // libretro core sets neither the device nor the path — so left alone it
    // produces a GCI FOLDER of loose files while Cabinet for Mac produces a
    // whole-card `.raw`. The console writes Dolphin's own config before launch
    // to make the two agree; see writeDolphinConfig in main.cpp.
    //
    // THE NAME IS NOT PREDICTABLE FROM HERE and that is Dolphin's doing: ask
    // for `card.raw` and it writes `card.USA.raw`, stamping the region in, and
    // it stamps the SIZE in too when the card is not the default — one of the
    // three cards on the reference server was `cabinet-934.USA.251.raw`. So
    // the capture scans for the suffix rather than insisting on a name, the
    // same as Dreamcast.
    if (slug == "ngc") {
        SaveFile f;
        f.path = "card.raw";
        f.captureSuffix = ".raw";
        // The console's own clock and settings, which Dolphin keeps beside the
        // card and which are not anybody's save.
        f.captureExclude = "SRAM.raw";
        f.region = "raw";
        f.untouched = Untouched::GameCubeDirectory;
        // THIS CONSOLE'S OWN NAMING, unlike PlayStation 2 above, and the
        // reason is that there is nothing to be compatible with.
        //
        // All three GameCube rows on the reference server were EMPTY cards —
        // measured 2026-09-20, 33 to 37 non-blank bytes in a 16 MB file, no
        // directory entry in either copy — so there is no real save anywhere
        // to match. And the Mac's spelling is not predictable from here:
        // Dolphin stamps region and card size into the filename, so matching
        // it means prefix-scanning the server's rows, which is logic with no
        // test behind it.
        //
        // A guess with no test is how a save silently goes to the wrong row.
        // When a real GameCube save exists on the Mac again, that is the
        // moment to make the two agree — with something to check against.
        f.macRowName = false;
        return {f};
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

std::string displayQualifier(const romm::Platform& p) {
    const Entry* e = lookup(p.slug, p.fsSlug);
    // Only the ambiguous rows carry a system name, so everything else comes
    // back empty. Qualifying a platform nobody can confuse would be noise.
    if (e && e->system) return e->system;
    // An arcade set this table does not recognise is still ambiguous to a
    // person — two tiles saying "Arcade" — so fall back to the one field that
    // actually distinguishes them on the server.
    if (!p.fsSlug.empty() && p.slug == "arcade") return p.fsSlug;
    return {};
}

std::string displayName(const romm::Platform& p) {
    // The joined form, for the places that want one string: a grid's heading,
    // a launch screen, a line of output. A tile wants the two halves and calls
    // displayQualifier for the second.
    const std::string base = p.name.empty() ? p.slug : p.name;
    const std::string q = displayQualifier(p);
    return q.empty() ? base : base + " (" + q + ")";
}

}  // namespace catalog
