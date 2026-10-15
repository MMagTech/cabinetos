#include "launch.h"

#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cctype>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "cache.h"
#include "catalog.h"
#include "core.h"
#include "disc.h"
#include "fileio.h"
#include "library.h"
#include "ps2.h"
#include "quality.h"
#include "romfile.h"
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

// How many things are in a directory. Used once, to decide whether a download
// left anything behind it when the core turned out to read its own archive.
static int countEntries(const std::string& dir) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return -1;
    int n = 0;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        ++n;
    }
    ::closedir(d);
    return n;
}

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
        // FIRMWARE FIRST, and EVERY file the platform lists rather than
        // whichever one this game looks like it needs. A core looks BIOS up by
        // name in the system directory and ignores what it does not want, so
        // extra files cost a little disk and a missing one costs the launch.
        // Cabinet's rule — see docs/CABINET.md.
        //
        // Shared by every game on the platform and by every person on the
        // console, so it lives at the root in `bios/` rather than inside one
        // game's directory, and a file already present at the right size is not
        // fetched again.
        {
            job.stage = LaunchJob::Stage::Firmware;
            const std::string biosDir = storage::biosDir();
            storage::makeDirs(biosDir);
            std::vector<romm::Firmware> firmware;
            std::string ferr;
            // THIS PLATFORM'S OWN FILES, and only these, are candidates for
            // the aliasing below. `bios/` is shared by the whole console, so a
            // scan of it by size would reach across platforms — and it very
            // nearly did: the PlayStation BIOS is 524288 bytes, exactly the
            // size of Saturn's, and the first version of this picked between
            // them on alphabetical order. It happened to choose correctly and
            // that is not a property anyone should rely on.
            std::vector<std::string> platformFirmware;
            // NOT ASKED WHEN THE SERVER IS AWAY (#88): what an earlier
            // launch fetched is in bios/ and the core finds it there; asking
            // would only hold the launch for a timeout.
            const bool away = romm::serverAway();
            if (away) ferr = "the server is away; using what is in bios/";
            const bool listed = !away && client.fetchFirmware(platformId, &firmware, &ferr);
            if (listed) {
                for (const auto& f : firmware) {
                    if (job.cancel.load()) break;
                    const std::string fdest = biosDir + "/" + storage::safeSegment(f.fileName);
                    platformFirmware.push_back(fdest);
                    struct stat fst;
                    if (f.sizeBytes > 0 && ::stat(fdest.c_str(), &fst) == 0 &&
                        fst.st_size == f.sizeBytes) {
                        std::fprintf(stderr, "[firmware] %s already here\n",
                                     f.fileName.c_str());
                        continue;
                    }
                    const std::string fpath = "/api/firmware/" + std::to_string(f.id) +
                                              "/content/" + f.fileName;
                    std::string derr;
                    if (client.fetchToFile(fpath, fdest,
                            [&job](int64_t got, int64_t total) {
                                job.got = got;
                                job.total = total;
                                return !job.cancel.load();
                            }, &derr)) {
                        std::fprintf(stderr, "[firmware] %s (%lld bytes)\n",
                                     f.fileName.c_str(),
                                     static_cast<long long>(f.sizeBytes));
                    } else {
                        // Not fatal here. WHICH BIOS a core needs is the core's
                        // business and most platforms need none, so a refusal
                        // to launch is the honest place to find out.
                        std::fprintf(stderr, "[firmware] %s: %s\n",
                                     f.fileName.c_str(), derr.c_str());
                    }
                }
            } else {
                std::fprintf(stderr, "[firmware] none listed: %s\n", ferr.c_str());
            }

            // AND THEN UNDER THE NAME THE CORE WILL ACTUALLY OPEN.
            //
            // Downloading the file is only half of it. A core looks its BIOS
            // up by a fixed name and RomM serves it under whatever name
            // somebody chose, so the file can be sitting right there and the
            // core still say it has no BIOS — which is what stopped every
            // Saturn game on this console from starting. See
            // catalog::firmwareAliases for why the match is on SIZE and why
            // the copy goes under every name rather than the likeliest one.
            // Only from a list the server gave: offline, the copies an earlier
            // launch placed are already there.
            if (const catalog::FirmwareAliases fa = catalog::firmwareAliases(slug, fsSlug);
                fa.sizeBytes > 0 && listed) {
                // The source is whichever of THIS PLATFORM's firmware files
                // is exactly the right length — never just whatever in `bios/`
                // happens to match, which would reach into another platform's
                // BIOS of the same size. Checked on the disk rather than
                // against what RomM said, so a file already present from an
                // earlier launch counts and a failed download does not.
                std::string source;
                for (const std::string& cand : platformFirmware) {
                    struct stat st;
                    if (::lstat(cand.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
                    if (st.st_size != fa.sizeBytes) continue;
                    if (source.empty() || cand < source) source = cand;
                }
                if (source.empty()) {
                    // Not fatal, and not silent either: the core is about to
                    // say the same thing less clearly.
                    std::fprintf(stderr,
                                 "[firmware] this platform lists no file of %lld bytes — "
                                 "its core will not find a BIOS\n",
                                 static_cast<long long>(fa.sizeBytes));
                } else {
                    std::vector<std::string> targets;
                    for (const char* n : fa.names) targets.push_back(biosDir + "/" + n);
                    if (fa.subDir) {
                        const std::string sub = biosDir + "/" + fa.subDir;
                        storage::makeDirs(sub);
                        for (const char* n : fa.names) targets.push_back(sub + "/" + n);
                    }
                    const std::vector<uint8_t> bytes = cab::readBytes(source);
                    for (const std::string& t : targets) {
                        if (t == source) continue;
                        struct stat st;
                        if (::stat(t.c_str(), &st) == 0 && st.st_size == fa.sizeBytes) continue;
                        if (bytes.empty() || !cab::writeBytes(t, bytes)) {
                            std::fprintf(stderr, "[firmware] could not place %s\n", t.c_str());
                            continue;
                        }
                        std::fprintf(stderr, "[firmware] %s also placed as %s\n",
                                     source.c_str(), t.c_str());
                    }
                }
            }
            // A SYSTEM THAT CANNOT START WITHOUT ITS BIOS, AND NONE CAME (#90).
            // Only when the server ANSWERED: offline, the list is unknown and
            // whatever is already on the console may be enough. "None" means
            // no file this platform lists is on the disk after the fetch, or,
            // where the core wants a known size, none of that size.
            if (listed && catalog::needsBios(slug)) {
                const catalog::FirmwareAliases fa = catalog::firmwareAliases(slug, fsSlug);
                bool have = false;
                for (const std::string& cand : platformFirmware) {
                    struct stat st;
                    if (::stat(cand.c_str(), &st) != 0 || st.st_size <= 0) continue;
                    if (fa.sizeBytes > 0 && st.st_size != fa.sizeBytes) continue;
                    have = true;
                }
                job.biosMissing = !have;
                if (!have)
                    std::fprintf(stderr, "[firmware] no BIOS for %s on the server\n",
                                 slug.c_str());
            }
            job.got = 0;
            job.total = 0;
        }

        // A GAME ON SEVERAL DISCS (#162): every disc fetched by its file id,
        // not as the zip RomM makes of the folder (which carries manuals,
        // soundtracks and DLC, and a list that may name them), into the
        // game's one folder, with one list the console writes. Only on the
        // systems disc.h takes, and only when the folder holds two discs or
        // more: one disc and its tracks is the ordinary path below.
        if (!emu && disc::handles(slug)) {
            std::vector<romm::RomFile> files;
            std::string lerr;
            if (romm::serverAway()) lerr = "the server is away";
            const bool listed =
                !romm::serverAway() && client.fetchRomFiles(id, &files, &lerr);
            std::vector<disc::Disc> discs;
            std::vector<std::pair<romm::RomFile, std::string>> fetch;
            int64_t fetchBytes = 0;
            if (listed) {
                const disc::Plan plan = disc::plan(files);
                if (plan.discNames.size() >= 2) {
                    std::vector<std::string> names, listedNames;
                    for (const std::string& n : plan.discNames)
                        names.push_back(storage::safeSegment(n));
                    // THE LIBRARY'S OWN LIST, read for the order and nothing
                    // else: it is never handed to a core.
                    if (plan.listFileId) {
                        romm::RomFile lf;
                        lf.id = plan.listFileId;
                        lf.fileName = plan.listFileName;
                        const std::vector<uint8_t> b =
                            client.fetchBytes(romm::Client::romFilePath(id, lf));
                        for (std::string l : disc::readList(std::string(b.begin(), b.end()))) {
                            if (const size_t sl = l.find_last_of("/\\"); sl != std::string::npos)
                                l.erase(0, sl + 1);
                            listedNames.push_back(storage::safeSegment(l));
                        }
                    }
                    discs = disc::fromNames(names, listedNames);
                    // EVERY DISC THERE AT ITS SIZE, each checked on its own,
                    // so one missing or cut short is the only one fetched.
                    for (const romm::RomFile& f : plan.fetch) {
                        const std::string d = entryPath + "/" + storage::safeSegment(f.fileName);
                        struct stat st;
                        if (::stat(d.c_str(), &st) == 0 && st.st_size == f.sizeBytes) continue;
                        fetch.emplace_back(f, d);
                        fetchBytes += f.sizeBytes;
                    }
                }
            } else {
                // NO SERVER: the discs as this console last wrote them, and
                // every one of them must be here.
                discs = disc::readRecord(entryPath);
                if (discs.size() < 2) discs.clear();
                for (const disc::Disc& d : discs) {
                    if (storage::exists(entryPath + "/" + d.file)) continue;
                    std::fprintf(stderr, "[disc] %s is not here and the server cannot be asked: %s\n",
                                 d.file.c_str(), lerr.c_str());
                    job.message = "Couldn't download this game";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
                if (!discs.empty())
                    std::fprintf(stderr, "[disc] no server (%s): the %zu discs as last written\n",
                                 lerr.c_str(), discs.size());
            }
            if (!discs.empty()) {
                // ONE FOLDER. A one-file entry left by a download from before
                // this (the folder as RomM's zip) goes; the folder replaces it.
                struct stat est;
                if (::lstat(entryPath.c_str(), &est) == 0 && !S_ISDIR(est.st_mode))
                    storage::removeEntry(entryPath);
                storage::makeDirs(entryPath);
                const int64_t need =
                    fetchBytes +
                    static_cast<int64_t>(static_cast<double>(fetchBytes) * cache::kOverheadFraction);
                if (need > 0 && cache::freeBytes(location) < need) {
                    cache::evictUntilFree(location, need, id);
                    if (cache::freeBytes(location) < need) {
                        job.message = "Not enough space";
                        job.stage = LaunchJob::Stage::Failed;
                        return;
                    }
                }
                job.stage = LaunchJob::Stage::Downloading;
                int64_t done = 0;
                for (const auto& [f, d] : fetch) {
                    std::string derr;
                    const int64_t before = done;
                    if (!client.fetchToFile(romm::Client::romFilePath(id, f), d,
                            [&job, before, fetchBytes](int64_t got, int64_t) {
                                job.got = before + got;
                                job.total = fetchBytes;
                                return !job.cancel.load();
                            }, &derr)) {
                        std::fprintf(stderr, "[launch] download %s: %s\n", f.fileName.c_str(),
                                     derr.c_str());
                        job.message = "Couldn't download this game";
                        job.stage = LaunchJob::Stage::Failed;
                        return;
                    }
                    done += f.sizeBytes;
                    std::fprintf(stderr, "[disc] fetched %s (%lld bytes)\n", f.fileName.c_str(),
                                 static_cast<long long>(f.sizeBytes));
                }
                if (fetch.empty())
                    std::fprintf(stderr, "[launch] already downloaded, %zu discs\n", discs.size());
                const std::string list = entryPath + "/" + disc::listFileName(fsName);
                const std::string text = disc::listText(discs);
                if (!disc::writeRecord(entryPath, discs) ||
                    !cab::writeBytes(list, std::vector<uint8_t>(text.begin(), text.end()))) {
                    std::fprintf(stderr, "[disc] could not write the list in %s\n",
                                 entryPath.c_str());
                    job.message = "Couldn't download this game";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
                for (const disc::Disc& d : discs)
                    std::fprintf(stderr, "[disc] %s: %s\n", d.label.c_str(), d.file.c_str());
                job.discs = discs;
                job.romPath = list;
                job.got = 0;
                job.total = 0;
                job.stage = LaunchJob::Stage::Ready;
                return;
            }
        }

        // A GAME THAT IS INSTALLED BEFORE IT RUNS (a PS3 PKG; the row's
        // `installs`). Its folder is fetched FILE BY FILE rather than as the
        // zip RomM makes of a folder, because the zip route would hold the
        // zip, the PKG out of it and the installed game at once: three times
        // a 20 GB game. This way it is twice, and only while the install
        // runs; the PKG is deleted once the game it became is in place
        // (measured: the installed game is the PKG's size, open question
        // 19). The folder is never collapsed to one file, because the
        // emulator keeps its compile cache in it, so removing the game
        // removes both.
        if (emu && emu->installs) {
            storage::makeDirs(entryPath);
            std::vector<romm::RomFile> files;
            std::string lerr;
            if (romm::serverAway()) lerr = "the server is away";
            const bool listed =
                !romm::serverAway() && client.fetchRomFiles(id, &files, &lerr);
            if (!listed) {
                // OFFLINE, OR THE SERVER SAID NOTHING: a game already installed
                // here still plays, which is all the list was for.
                std::fprintf(stderr, "[launch] files of %d: %s\n", id, lerr.c_str());
                if (cab::standalone::bootPath(entryPath).empty()) {
                    job.message = "Couldn't download this game";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
            }
            // What is missing, and what the install will need on top: each
            // file not yet installed may cost its size again while it goes
            // in. Counted for every large file rather than by a `.pkg` name,
            // which a PKG does not always have (standalone.cpp, isPkg).
            std::vector<std::pair<const romm::RomFile*, std::string>> fetch;
            int64_t fetchBytes = 0, installBytes = 0;
            for (const romm::RomFile& f : files) {
                const std::string name = storage::safeSegment(f.fileName);
                if (cab::standalone::installedFile(entryPath, name, f.sizeBytes)) continue;
                const bool iso = name.size() > 4 &&
                                 strcasecmp(name.c_str() + name.size() - 4, ".iso") == 0;
                if (!iso) installBytes += f.sizeBytes;
                struct stat st;
                const std::string d = entryPath + "/" + name;
                if (::stat(d.c_str(), &st) == 0 && st.st_size == f.sizeBytes) continue;
                fetch.emplace_back(&f, d);
                fetchBytes += f.sizeBytes;
            }
            const int64_t need =
                fetchBytes + installBytes +
                static_cast<int64_t>(static_cast<double>(fetchBytes + installBytes) *
                                     cache::kOverheadFraction);
            if (need > 0 && cache::freeBytes(location) < need) {
                cache::evictUntilFree(location, need, id);
                if (cache::freeBytes(location) < need) {
                    job.message = "Not enough space";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
            }
            job.stage = LaunchJob::Stage::Downloading;
            int64_t done = 0;
            for (const auto& [f, d] : fetch) {
                std::string derr;
                const int64_t before = done;
                if (!client.fetchToFile(romm::Client::romFilePath(id, *f), d,
                        [&job, before, fetchBytes](int64_t got, int64_t) {
                            job.got = before + got;
                            job.total = fetchBytes;
                            return !job.cancel.load();
                        }, &derr)) {
                    std::fprintf(stderr, "[launch] download %s: %s\n", f->fileName.c_str(),
                                 derr.c_str());
                    job.message = "Couldn't download this game";
                    job.stage = LaunchJob::Stage::Failed;
                    return;
                }
                done += f->sizeBytes;
            }
            job.got = 0;
            job.total = 0;
            job.stage = LaunchJob::Stage::Installing;
            std::string rom, why;
            if (!cab::standalone::installGame(*emu, entryPath, job.cancel, &rom, &why)) {
                job.message = why;
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
            job.romPath = rom;
            job.stage = LaunchJob::Stage::Ready;
            return;
        }

        // A GAME KEPT AS A FOLDER OF SEVERAL FILES, FETCHED FILE BY FILE.
        // RomM serves such a folder as a zip it makes, a little larger than
        // the game's `fs_size_bytes` (Star Ocean: 7,832,230,048 against
        // 7,832,229,235), so the size check below never matched and the whole
        // game was fetched again at every launch: a player's Switch game with
        // `update/` and `dlc/` folders, 2026-10-12. The zip was also kept
        // beside what came out of it, and its subfolders flattened, so an
        // update could be what booted. Now each of the game's own files is
        // fetched by its id and checked by its own size, as games on several
        // discs and PS3 games already are. Only the game's files, the top of
        // the folder (disc::plan): `update/`, `dlc/`, `manual/` and the rest
        // stay on the server (updates and DLC are #181).
        if (multiFile) {
            std::vector<romm::RomFile> files;
            std::string lerr;
            if (romm::serverAway()) lerr = "the server is away";
            const bool listed =
                !romm::serverAway() && client.fetchRomFiles(id, &files, &lerr);
            // A GAME THAT WAS ONE FILE AND IS NOW A FOLDER: somebody added an
            // `update/` beside it on the server after it was downloaded. Its
            // entry here is still that one file. Offline it still plays as it
            // is; online the file moves into the game's new folder under the
            // name of the game file its size matches, rather than coming
            // down again, and the folder takes the entry's name without the
            // file's extension.
            std::string entry = entryPath;
            struct stat est;
            if (::lstat(entry.c_str(), &est) == 0 && S_ISREG(est.st_mode)) {
                if (!listed) {
                    std::fprintf(stderr, "[launch] files of %d: %s; playing the one file "
                                         "here\n", id, lerr.c_str());
                    job.romPath = entry;
                    job.stage = LaunchJob::Stage::Ready;
                    return;
                }
                const size_t slash = entry.find_last_of('/');
                const size_t dot = entry.find_last_of('.');
                const std::string folder =
                    dot != std::string::npos && (slash == std::string::npos || dot > slash)
                        ? entry.substr(0, dot) : entry + ".folder";
                std::string as;
                for (const romm::RomFile& f : disc::plan(files).fetch)
                    if (f.sizeBytes == est.st_size) { as = storage::safeSegment(f.fileName); break; }
                storage::makeDirs(folder);
                if (!as.empty() && ::rename(entry.c_str(), (folder + "/" + as).c_str()) == 0)
                    std::fprintf(stderr, "[launch] %s is a folder on the server now; moved "
                                         "into %s\n", entry.c_str(), folder.c_str());
                else if (::unlink(entry.c_str()) == 0)
                    std::fprintf(stderr, "[launch] %s is a folder on the server now and "
                                         "matches none of its files; removed\n", entry.c_str());
                entry = folder;
                job.entryPath = folder;
            }
            storage::makeDirs(entry);
            std::vector<std::pair<std::string, int64_t>> mine;   // on the disk, size
            if (listed) {
                const disc::Plan plan = disc::plan(files);
                std::vector<std::pair<const romm::RomFile*, std::string>> fetch;
                int64_t fetchBytes = 0;
                for (const romm::RomFile& f : plan.fetch) {
                    const std::string name = storage::safeSegment(f.fileName);
                    mine.emplace_back(name, f.sizeBytes);
                    const std::string d = entry + "/" + name;
                    struct stat st;
                    if (::stat(d.c_str(), &st) == 0 && st.st_size == f.sizeBytes) continue;
                    fetch.emplace_back(&f, d);
                    fetchBytes += f.sizeBytes;
                }
                const int64_t need =
                    fetchBytes +
                    static_cast<int64_t>(static_cast<double>(fetchBytes) * cache::kOverheadFraction);
                if (need > 0 && cache::freeBytes(location) < need) {
                    cache::evictUntilFree(location, need, id);
                    if (cache::freeBytes(location) < need) {
                        job.message = "Not enough space";
                        job.stage = LaunchJob::Stage::Failed;
                        return;
                    }
                }
                int64_t done = 0;
                for (const auto& [f, d] : fetch) {
                    std::string derr;
                    const int64_t before = done;
                    if (!client.fetchToFile(romm::Client::romFilePath(id, *f), d,
                            [&job, before, fetchBytes](int64_t got, int64_t) {
                                job.got = before + got;
                                job.total = fetchBytes;
                                return !job.cancel.load();
                            }, &derr)) {
                        std::fprintf(stderr, "[launch] download %s: %s\n",
                                     f->fileName.c_str(), derr.c_str());
                        job.message = "Couldn't download this game";
                        job.stage = LaunchJob::Stage::Failed;
                        return;
                    }
                    done += f->sizeBytes;
                }
                job.got = 0;
                job.total = 0;
                if (fetch.empty())
                    std::fprintf(stderr, "[launch] already downloaded, %zu files\n", mine.size());
                else
                    std::fprintf(stderr, "[launch] fetched %zu of the game's %zu files\n",
                                 fetch.size(), mine.size());
                // THE ZIP THE OLD ROUTE KEPT, named after the folder, once
                // every file it held is here on its own. Only a zip.
                const std::string old = entry + "/" + storage::safeSegment(fsName);
                bool ours = false;
                for (const auto& m : mine) ours = ours || m.first == storage::safeSegment(fsName);
                if (!ours && romfile::sniffFile(old) == romfile::Kind::Zip &&
                    ::unlink(old.c_str()) == 0)
                    std::fprintf(stderr, "[launch] removed %s, the folder's zip from before\n",
                                 old.c_str());
                // AND WHAT CAME OUT OF IT that is not the game: the zip route
                // unpacked `update/`, `dlc/` and the rest flat into the
                // folder. Each is removed by its name in RomM's own list,
                // and only when no game file shares the name.
                for (const romm::RomFile& f : files) {
                    const std::string n = storage::safeSegment(f.fileName);
                    bool game = false;
                    for (const auto& m : mine) game = game || m.first == n;
                    struct stat st;
                    const std::string d = entry + "/" + n;
                    if (!game && ::lstat(d.c_str(), &st) == 0 && S_ISREG(st.st_mode) &&
                        ::unlink(d.c_str()) == 0)
                        std::fprintf(stderr, "[launch] removed %s, unpacked from the folder's "
                                             "zip before (%s)\n", d.c_str(),
                                     f.category.empty() ? "not the game" : f.category.c_str());
                }
            } else {
                // OFFLINE, OR THE SERVER SAID NOTHING: the files already here.
                std::fprintf(stderr, "[launch] files of %d: %s\n", id, lerr.c_str());
                if (DIR* dir = ::opendir(entry.c_str())) {
                    while (struct dirent* e = ::readdir(dir)) {
                        const std::string n = e->d_name;
                        if (n[0] == '.' || (n.size() > 5 && n.compare(n.size() - 5, 5, ".part") == 0))
                            continue;
                        struct stat st;
                        const std::string d = entry + "/" + n;
                        if (::stat(d.c_str(), &st) == 0 && S_ISREG(st.st_mode))
                            mine.emplace_back(n, st.st_size);
                    }
                    ::closedir(dir);
                }
            }
            // WHAT TO START: a disc's sheet (.cue, .gdi) over its tracks, then
            // a file the emulator lists, then the largest.
            std::string boot;
            int rank = -1;
            int64_t bootSize = -1;
            for (const auto& [n, size] : mine) {
                std::string ext = n.substr(n.find_last_of('.') == std::string::npos
                                               ? n.size() : n.find_last_of('.') + 1);
                for (char& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                const bool sheet = ext == "cue" || ext == "gdi" || ext == "ccd" ||
                                   ext == "toc" || ext == "mds";
                const int r = sheet && romfile::accepts(validExts, n) ? 2
                              : romfile::accepts(validExts, n)        ? 1
                                                                       : 0;
                if (r > rank || (r == rank && size > bootSize)) {
                    boot = entry + "/" + n;
                    rank = r;
                    bootSize = size;
                }
            }
            if (boot.empty()) {
                job.message = "Couldn't download this game";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
            std::string primary, err;
            romfile::Kind kind = romfile::Kind::Plain;
            job.stage = LaunchJob::Stage::Unpacking;
            if (!romfile::prepareFile(boot, entry, validExts, blockExtract, &primary, &kind,
                                      &err)) {
                std::fprintf(stderr, "[launch] file: %s\n", err.c_str());
                job.message = "Couldn't open this game's file";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
            job.romPath = primary;
            job.stage = LaunchJob::Stage::Ready;
            return;
        }

        // A GAME IS ONE ENTRY, AND THE ENTRY IS A FILE WHEN THE GAME IS ONE
        // FILE. `cache/Sony Playstation/321 - Crash Bandicoot.chd` is what 18
        // asked for and what somebody browsing over SFTP wants to find. An
        // archive that unpacks into a .cue and its .bin cannot be one file, so
        // it becomes a directory of the same name — and because the two are
        // renamed identically, keeping and releasing do not care which it is.
        //
        // The download always builds the directory form first, because whether
        // it collapses is not knowable until the core has been asked whether it
        // opens its own archives.
        struct stat est;
        const bool entryIsFile =
            ::lstat(entryPath.c_str(), &est) == 0 && !S_ISDIR(est.st_mode);
        std::string dest = entryPath;
        if (!entryIsFile) {
            storage::makeDirs(entryPath);
            dest = entryPath + "/" + storage::safeSegment(fsName);
        }

        // Already here and the right size? Then it is the same ROM: RomM told
        // us how big it is, and a partial download was never renamed into
        // place. Re-fetching 112 MB to play the same game twice is not a
        // caching subtlety, it is just wrong.
        bool haveIt = false;
        if (struct stat st; expectedSize > 0 && ::stat(dest.c_str(), &st) == 0)
            haveIt = st.st_size == expectedSize;

        std::string err;
        if (!haveIt) {
            // ROOM FOR IT FIRST, which until the cache existed nothing checked:
            // the disk filled and stayed full. See cache.h for the policy.
            //
            // expectedSize is RomM's `fs_size_bytes`, so for an archived game
            // it is the COMPRESSED size and this is only the first of two
            // checks. What it unpacks to cannot be known until the archive is
            // here and its own index has been read — see the second check
            // below, after the download.
            if (expectedSize > 0) {
                const int64_t want =
                    expectedSize +
                    static_cast<int64_t>(static_cast<double>(expectedSize) *
                                         cache::kOverheadFraction);
                if (cache::freeBytes(location) < want) {
                    cache::evictUntilFree(location, want, /*protectRomId=*/0);
                    if (cache::freeBytes(location) < want) {
                        // THE CONSTRAINT, NOT WHAT TO DO ABOUT IT (#110,
                        // MMagTech, 2026-10-06). It said "Remove downloads or
                        // add a drive" from 2026-09-26.
                        job.message = "Not enough space";
                        job.stage = LaunchJob::Stage::Failed;
                        return;
                    }
                }
            }
            const std::string path =
                "/api/roms/" + std::to_string(id) + "/content/" + fsName;
            const bool ok = client.fetchToFile(path, dest,
                [&job](int64_t got, int64_t total) {
                    job.got = got;
                    job.total = total;
                    return !job.cancel.load();
                }, &err);
            if (!ok) {
                std::fprintf(stderr, "[launch] download: %s\n", err.c_str());
                job.message = "Couldn't download this game";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
            // SAID, so a game downloaded again at every launch shows in the
            // log and the diagnostic report as a line, not as a missing one.
            struct stat dst;
            std::fprintf(stderr, "[launch] downloaded %lld bytes\n",
                         ::stat(dest.c_str(), &dst) == 0 ? static_cast<long long>(dst.st_size)
                                                         : -1LL);
        } else {
            std::fprintf(stderr, "[launch] already downloaded, %lld bytes\n",
                         static_cast<long long>(expectedSize));
        }

        // IS IT A GAME AT ALL? Asked of the bytes, before a core is ever
        // handed them, because the alternative is what this console did until
        // now: pass a web page to an emulator, watch it accept it, report
        // correct geometry, run, and draw black for as long as anyone cares to
        // wait. See romfile.h, Kind::NotAGame, for the three real files that
        // do this on the reference library.
        //
        // AND THE BAD FILE IS DELETED. Without that the size check above
        // matches it on every later launch — "already downloaded" — and the
        // game is permanently broken with no way back from inside the product.
        // A download that is not a game is not a download.
        {
            std::string serr;
            if (romfile::sniffFile(dest, &serr) == romfile::Kind::NotAGame) {
                ::unlink(dest.c_str());
                if (!entryIsFile) ::rmdir(entryPath.c_str());
                job.message =
                    "this game's file is a web page, not a game. It needs "
                    "replacing on the server";
                std::fprintf(stderr,
                             "[launch] %s is a web page, not a game; deleted rather than "
                             "kept, because a cached one would fail the same way for "
                             "ever\n", dest.c_str());
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
        }

        job.stage = LaunchJob::Stage::Unpacking;
        std::string primary;
        romfile::Kind kind = romfile::Kind::Plain;

        if (entryIsFile) {
            // A file entry is one the console already collapsed, which only
            // happens for a payload no core wanted unpacked. Nothing to do.
            job.romPath = dest;
            job.stage = LaunchJob::Stage::Ready;
            return;
        }

        // THE SECOND CHECK, and the compression ratio is why it exists: 868 KB
        // of Space Harrier becomes 2 MB, and a DS ROM padded with empty space
        // compresses far harder than that. No multiplier is safe, so the
        // archive is asked what it holds rather than guessed at.
        //
        // Both files exist at once during extraction, so the room needed is
        // what comes out, on top of the archive already on disk. Nothing to do
        // for a .chd or an arcade set handed over unextracted, which is most of
        // the large files in a library.
        if (const int64_t unpacked = romfile::unpackedSize(dest, validExts, blockExtract);
            unpacked > 0 && cache::freeBytes(location) < unpacked) {
            cache::evictUntilFree(location, unpacked, id);
            if (cache::freeBytes(location) < unpacked) {
                job.message = "Not enough space";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
        }

        if (!romfile::prepareFile(dest, entryPath, validExts, blockExtract, &primary,
                                  &kind, &err)) {
            std::fprintf(stderr, "[launch] file: %s\n", err.c_str());
            job.message = "Couldn't open this game's file";
            job.stage = LaunchJob::Stage::Failed;
            return;
        }

        // Nothing came out of it, so the directory holds one file and the
        // layout says it should not be a directory at all. Two renames inside
        // one filesystem, whatever the game weighs.
        //
        // EXCEPT WHEN THE CORE OPENS THE ARCHIVE ITSELF, and this cost every
        // arcade game on the console. A core that answers `block_extract` is
        // handed the file whole, and for MAME 2003-Plus and FBNeo the file's
        // own NAME is how the machine is chosen — `lethalen.zip` is the Lethal
        // Enforcers driver. Collapsing renames it to `3022 - Lethal
        // Enforcers.zip`, the core looks that up in its driver table, finds
        // nothing, and the launch ends at "Game driver not found". That has
        // been true of all 223 arcade games since the folder layout landed on
        // 2026-09-18 and nobody ran one; found by running one, 2026-09-19.
        //
        // So the entry stays a directory and the file inside keeps the name
        // the server gave it. The layout already allows either shape and
        // renames both identically, so keeping and releasing do not care —
        // and the save filed under `nvram/lethalen.nv` now matches what every
        // other MAME frontend, and the reference implementation, already
        // writes.
        if (primary == dest && countEntries(entryPath) == 1 && !blockExtract) {
            const size_t dot = fsName.find_last_of('.');
            const std::string ext = dot == std::string::npos ? "" : fsName.substr(dot);
            const std::string tmp = entryPath + ".collapsing";
            const std::string flat = entryPath + ext;
            if (::rename(dest.c_str(), tmp.c_str()) == 0 &&
                ::rmdir(entryPath.c_str()) == 0 &&
                ::rename(tmp.c_str(), flat.c_str()) == 0) {
                primary = flat;
                job.entryPath = flat;
            } else {
                // Not worth failing a launch over: the game is playable exactly
                // where it is, it is simply a directory holding one file. Said
                // out loud because a layout that quietly does not hold is worse
                // than one that does not hold.
                std::fprintf(stderr, "[storage] could not flatten %s; leaving it a "
                                     "directory\n", entryPath.c_str());
                ::rename(tmp.c_str(), dest.c_str());
            }
        }

        job.romPath = primary;
        job.stage = LaunchJob::Stage::Ready;
    });
    return true;
}
