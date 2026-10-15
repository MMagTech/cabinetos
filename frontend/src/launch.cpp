#include "launch.h"

#include <cstdio>
#include <map>
#include <string>
#include <thread>

#include "cache.h"
#include "catalog.h"
#include "core.h"
#include "library.h"
#include "ps2.h"
#include "quality.h"
#include "romm.h"
#include "standalone.h"
#include "storage.h"
#include "sysopts.h"
#include "wii.h"

// PlayStation 2 picture quality.
//
// **THESE TWO FLAGS ARE A TEST INSTRUMENT AND NOT THE PRODUCT.** MMagTech,
// 2026-09-21: they exist so a number can be tried in front of the television,
// and how this is really exposed is open question 23 — ONE quality setting for
// the whole console, not a per-emulator menu and not a command line. Nobody
// should build a settings screen on top of these, and nobody should read a
// value chosen here as a decision: the point of them is to find out what the
// decision should be.
//
// Both default to what PCSX2 itself ships — native resolution, no anisotropic
// filtering — so a console that is given neither flag behaves exactly as it
// did. See --ps2-upscale in the argument parser for what the multiplier means
// on a 4K panel.
//
// SINCE #63 NEITHER IS SET UNLESS A FLAG SAYS SO, and the picture quality level
// decides (quality.h). -1 means "not given".
static float gPs2Upscale = -1.0f;
static int gPs2Anisotropy = -1;

// Set by main()'s parser. ONE DEFINITION OF EACH, HERE: a second copy of the
// two statics in main.cpp would compile, the parser would write that copy, and
// --ps2-upscale and --ps2-aniso would stop working without a word.
void setPs2Upscale(float upscale) { gPs2Upscale = upscale; }
void setPs2Anisotropy(int anisotropy) { gPs2Anisotropy = anisotropy; }

// Starts one. Returns false if the game cannot be played here at all, which is
// worth saying immediately rather than after a download.
//
// WHERE THE BYTES GO is decided here and nowhere else. A game somebody keeps is
// written straight into `roms/` rather than downloaded into `cache/` and then
// moved, because the move would be a second pass over gigabytes for no reason.
// A game already on the disk is left exactly where it is — including when
// somebody else keeps it, which is the case that makes "one kept game, two
// people" true on disk rather than only in a comment.
bool beginLaunch(LaunchJob& job, romm::Client& client, const romm::Game& game,
                 const std::string& coreDir, std::string* err,
                 bool playWhenReady, bool keepWhenReady) {
    const catalog::Coverage cov = catalog::coverageFor(game);
    if (cov.support != catalog::Support::Playable || !cov.core) {
        *err = game.platformName + ": " + (cov.reason ? cov.reason : "not playable here");
        return false;
    }

    // REMEMBERED, so it can be shown and played with no server (#88): the
    // game is on its way onto the drive, or already there.
    cache::rememberGame(game.id, gameRecordJson(game));

    job.stop();
    job.cancel = false;
    job.got = 0;
    job.total = 0;
    job.title = game.name.empty() ? game.fsName : game.name;
    job.romId = game.id;
    job.platformFsSlug = game.platformFsSlug;
    job.systemName = game.platformName;
    job.biosMissing = false;
    job.platformSlug = game.platformSlug;
    job.titleId = game.titleId;
    job.platformId = game.platformId;
    job.raHash = game.raHash;
    job.fsStem = game.fsName;
    if (const size_t dot = job.fsStem.find_last_of('.'); dot != std::string::npos)
        job.fsStem.erase(dot);
    job.coreName = cov.core;
    // ONE RULE, ONE PLACE. This used to append `_libretro.so` to whatever the
    // manifest called the core, which is right for twenty of the twenty-one
    // and wrong for `fbneo_libretro`, whose name already ends in it — so the
    // launch looked for `fbneo_libretro_libretro.so`, a file nothing builds,
    // while coverageFor looked for the right one and reported the game
    // playable. Every FBNeo game on this console failed at the moment somebody
    // pressed Play. See catalog::coreFileName.
    job.corePath = coreDir + "/" + catalog::coreFileName(cov.core);
    job.romPath.clear();
    job.discs.clear();
    job.message.clear();
    job.playWhenReady = playWhenReady;
    job.keepWhenReady = keepWhenReady;
    job.loweredCurtain = false;
    job.stage = LaunchJob::Stage::Downloading;

    const storage::User& user = storage::currentUser();

    // AN EMULATOR THAT IS ITS OWN APPLICATION LOADS NOTHING HERE. There is no
    // core to open and nothing in this process to hand directories to; the
    // game file is fetched exactly as for any other system, and pumpLaunch
    // starts the program once it is on the disk. docs/PROJECT.md, open
    // question 32.
    const cab::standalone::Emulator* emu = cab::standalone::find(cov.core);
    std::string validExts;
    bool blockExtract = false;
    if (emu) {
        // The emulator opens the file as it came, and a Switch NSP is not an
        // archive the console would open, so nothing is unpacked. NOT
        // `blockExtract`: that also stops a one-file game collapsing into one
        // file (`852 - Aqua Kitty UDX.nsp`), which is how every other system's
        // one-file game is kept, and it left each Switch game a folder.
        validExts = emu->extensions;
    } else {
        // The core is loaded here, on the frame thread, before the worker starts:
        // it is cheap, it is the thing that decides whether the archive gets
        // opened, and a core that will not load should fail now rather than after
        // three gigabytes have been fetched.
        cab::Core& core = cab::Core::shared();
        // Before load(), because retro_init is inside it and a core may read the
        // directories there and never ask again. See Core::setDirectories.
        //
        // THE SAVE DIRECTORY IS THIS PERSON'S AND THIS GAME'S. Every core on the
        // machine used to share one, which is why attributing a PSP save folder to
        // the game that wrote it needed a timestamp comparison rather than a path.
        const std::string saveDir =
            storage::savesDir(user, game.platformFsSlug, game.id, cov.core);
        storage::makeDirs(saveDir);
        core.setDirectories(storage::biosDir(), saveDir);
        // Also before load(), and for the same reason. This was reaching only the
        // --core-options audit until PPSSPP needed the first real override, which
        // meant the override table was being PRINTED rather than applied: every
        // core played on its declared defaults and the audit agreed with itself.
        // Invisible while the table was empty, wrong the moment it was not.
        //
        // PICTURE QUALITY ON TOP (#63): the level this game runs at decides
        // its resolution and shader building; quality.h is the one place that
        // knows what each level means.
        {
            const quality::Level level = quality::forGame(game.id);
            std::map<std::string, std::string> opts = catalog::optionOverrides(job.coreName);
            for (const auto& kv : quality::coreOptions(job.coreName, game.platformSlug, level))
                opts[kv.first] = kv.second;
            // And the system's own pause menu rows (#73).
            for (const auto& kv : sysopts::overrides(game.platformSlug))
                opts[kv.first] = kv.second;
            // The Wii's rumble switch as the last Wii game's HOME Menu left
            // it (#274, wii.h).
            if (game.platformSlug == "wii" && job.coreName == "dolphin")
                opts["dolphin_enable_rumble"] = wii::rumbleOption();
            core.setOptionOverrides(opts);
            quality::logApplied(job.coreName, game.id, level);
        }

        // PLAYSTATION 2 NEEDS THREE THINGS NO LIBRETRO CORE DOES, and the second
        // is the one that matters most.
        //
        // PCSX2 refuses to start without its own resources folder — its game
        // database, fonts and GS shaders — rather than degrading, so it ships with
        // the emulator the way PPSSPP's system files already do.
        //
        // The memory card is NOT named here. Core::loadGame names it from the rom
        // by the same rule catalog::saveFiles uses, so the existing save machinery
        // — restore before launch, capture after, refuse an unformatted card, file
        // it on the server under the name Cabinet's Mac uses — works on it
        // unchanged.
        if (cov.core && std::string(cov.core) == "pcsx2") {
            // The level's values, unless a --ps2-upscale or --ps2-anisotropy
            // given for a measurement says otherwise.
            const quality::Ps2 q = quality::ps2(quality::forGame(game.id));
            core.setPs2(storage::imageAssetsDir() + "/pcsx2/resources",
                        gPs2Upscale > 0.0f ? gPs2Upscale : q.upscale,
                        gPs2Anisotropy >= 0 ? gPs2Anisotropy : q.anisotropy,
                        ps2::widescreenOn(game.id));
        }

        if (!core.load(job.corePath)) {
            std::fprintf(stderr, "[launch] core %s: %s\n", job.coreName.c_str(),
                         core.error().c_str());
            *err = "Couldn't start this game";
            job.stage = LaunchJob::Stage::Idle;
            return false;
        }
        validExts = core.validExtensions();
        blockExtract = core.blockExtract();
    }

    // THE KEEP IS RECORDED BEFORE THE FIRST BYTE MOVES, for two reasons that
    // both matter. It makes the in-flight download safe from the eviction its
    // own size may trigger — a game in `roms/` is not a candidate, because
    // eviction only walks `cache/` — and it means the console has already
    // decided it can afford the promise rather than discovering it cannot after
    // fetching two gigabytes. A job that fails clears the record again, in
    // pumpLaunch.
    if (keepWhenReady) {
        std::string kerr;
        if (!cache::keep(user, game.id, gameRecordJson(game), &kerr)) {
            std::fprintf(stderr, "[keep] %s\n", kerr.c_str());
            *err = "Couldn't download this game";
            job.stage = LaunchJob::Stage::Idle;
            return false;
        }
    }

    // ONE GAME, ONE COPY, and this is where that is enforced. A game kept while
    // the drive was plugged in, then played while it was not, is on the machine
    // twice — see cache::dedupe. Checked at the moment somebody presses Play
    // rather than when a drive appears, so there is no plug-in event to miss.
    const cache::Placement placed = cache::dedupe(game.id, game.sizeBytes);

    // Already here? Then that is where it stays.
    //
    // OTHERWISE, AND THIS IS THE RULE THAT CAUGHT ME OUT: a fetch only writes
    // into `roms/` when the person is keeping the game with THIS press. Pressing
    // Play is not a storage act — that is the product's own rule, the one that
    // makes Download the single deliberate one — so a game fetched by playing it
    // goes into the cache even when somebody keeps it.
    //
    // The case that proves it: keep a game on the drive, unplug the drive, press
    // Play. The game is still kept and the keep record is still on the internal
    // disk, so asking "is this kept" put the fetched copy in `roms/` on the
    // INTERNAL disk, where nothing is allowed to evict it. Do that twenty times
    // with the drive in a drawer and the console has filled its own disk with
    // games it may not delete — which is the exact failure the system reserve
    // exists to prevent, arriving by a door nothing was watching.
    //
    // As a cached copy it is a stand-in: evictable, costing a download at worst,
    // and deleted outright the moment the drive comes back and dedupe sees the
    // real one. Which is what a copy of a file you already own should be.
    const bool kept = placed.present ? placed.kept : keepWhenReady;
    const std::string location = placed.present
                                     ? placed.location
                                     : (keepWhenReady ? cache::keepLocation(game.sizeBytes)
                                                      : storage::primaryLocation());
    job.entryPath = placed.present
                        ? placed.entryPath
                        : cache::entryPathFor(location, game.platformFsSlug, game.id,
                                              job.title, kept);

    const int id = game.id;
    const int platformId = game.platformId;
    const std::string slug = game.platformSlug;
    const std::string fsSlug = game.platformFsSlug;
    const std::string fsName = game.fsName;
    const int64_t expectedSize = game.sizeBytes;
    const bool multiFile = game.multiFile;
    const std::string entryPath = job.entryPath;
    job.worker = std::thread([&job, &client, id, platformId, slug, fsSlug, fsName, emu,
                              entryPath, location, validExts, blockExtract, expectedSize,
                              multiFile]() {
        launchWorker(job, client, id, platformId, slug, fsSlug, fsName, emu, entryPath,
                     location, validExts, blockExtract, expectedSize, multiFile);
    });
    return true;
}
