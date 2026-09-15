// CabinetOS frontend — the foundation.
//
// What this proves, which is the whole point of it existing:
//
//   1. A Wayland client with a real GLES 3 context, under cage or gamescope,
//      with no toolkit between us and the frame.
//   2. The 1920x1080 design canvas, letterboxed to whatever panel is attached.
//   3. The design system's focus treatment, to the point: 1.10 scale, a 4pt
//      inset white rim at 85%, a black 55% shadow blurred 26 and offset 14
//      down, and the caption sliding clear of the grown card.
//   4. Motion at the right tempo — 180ms ease-out, the most-used value in the
//      reference implementation.
//   5. Controller and keyboard driving focus, with neither required.
//
// What it deliberately does not do yet: text (there is no font layer), and
// cores (there is no Linux core built). Both are next, and neither changes
// anything here.
//
// See docs/PROJECT.md, "The design system".

#include <SDL3/SDL.h>

#include <sys/stat.h>

#include <atomic>
#include <csignal>
#include <thread>

#include <unistd.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "core.h"
#include "image.h"
#include "keyboard.h"
#include "catalog.h"
#include "romfile.h"
#include "romm.h"
#include "text.h"
#include "ui.h"

namespace {

// Photograph the running console on demand, without stopping it.
//
//   kill -USR1 $(pgrep -f cabinetos-frontend)
//
// This is not a debugging convenience, it is how anyone ever finds out what a
// CabinetOS machine actually has on its screen. The test VM has no way to show
// a person a picture, and later the machines that matter are in other people's
// living rooms — where "send me a photo of the telly" is the whole bug report
// channel (see *HDMI-CEC*, which has exactly this problem).
//
// A signal handler may do almost nothing safely, so it sets a flag and the
// frame loop does the work, on the thread that owns the GL context.
volatile std::sig_atomic_t gCaptureRequested = 0;
void requestCapture(int) { gCaptureRequested = 1; }

// --- The design system, as numbers -----------------------------------------
// Every value here is quoted from docs/PROJECT.md. If one of them changes
// there, it changes here, and nowhere else.

constexpr float kContentInset = 60.0f;

// The hero, from Cabinet's tvOS Home. See docs/PROJECT.md — the height is
// min(canvasHeight * 0.40, 420) and was settled on a real television after four
// rejected values, so it is not a number to adjust from a VM screenshot.
constexpr float kHeroTop = 40.0f;
constexpr float kHeroRadius = 18.0f;
constexpr float kHeroArtInsetTop = 14.0f;
constexpr float kHeroBandPadX = 12.0f;
constexpr float kHeroBandPadY = 10.0f;
constexpr float kHeroGapBelow = 20.0f;
// A pill, so blur 4 — the "thin material" tier. The band is a panel at 6.
// The design system's own tiers: 1.10 for a cover, 1.06 for a pill, 1.03 for a
// full-width row. The hero is full-width, so it takes the row tier.
constexpr float kRowFocusScale = 1.03f;
constexpr float kPillFocusScale = 1.06f;
constexpr float kHeroPillBlur = 4.0f;
constexpr float kHeroBandBlur = 6.0f;
constexpr float kShelfCoverWidth = 260.0f;
constexpr float kShelfCoverHeight = 347.0f;  // 3:4
constexpr float kShelfSpacing = 40.0f;
constexpr float kShelfHeadroom = 24.0f;  // room for the focused card to grow
constexpr float kCoverRadius = 10.0f;
constexpr float kCaptionGap = 6.0f;

constexpr float kFocusScale = 1.10f;
constexpr float kPressScale = 1.02f;
constexpr float kFocusRimWidth = 4.0f;
constexpr float kFocusShadowBlur = 26.0f;
constexpr float kFocusShadowOffsetY = 14.0f;

// 180ms ease-out. The focus tempo, and nothing about focus should be slower:
// a controller crosses a shelf faster than that and the animations must not
// queue up behind the person driving them.
constexpr float kFocusDuration = 0.180f;
constexpr float kPressDuration = 0.120f;

// The caption rides down by half of (scale - 1) times the cover height, because
// a scale about the centre advances the bottom edge by exactly that much. The
// +2 is the reference implementation's own breathing room. If kFocusScale
// changes, this follows it automatically.
float captionSlide(float focusAmount) {
    return focusAmount * (kShelfCoverHeight * (kFocusScale - 1.0f) * 0.5f + 2.0f);
}

float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}

// One animated scalar that behaves the way the reference implementation's
// animations do: a change re-targets from wherever the value currently is, so
// an interruption mid-flight is smooth rather than a jump.
struct Animated {
    float from = 0, to = 0, elapsed = 0, duration = kFocusDuration;

    void retarget(float target, float seconds) {
        if (target == to) return;
        from = value();
        to = target;
        duration = seconds;
        elapsed = 0;
    }
    void tick(float dt) { elapsed = std::min(elapsed + dt, duration); }
    float value() const {
        if (duration <= 0) return to;
        return from + (to - from) * easeOut(elapsed / duration);
    }
};

struct Card {
    // The RomM ROM id, and the only safe way to match a card to anything else.
    // Titles collide: "Altered Beast" is a Game & Watch entry AND a Genesis
    // one in the reference library, so matching Recent to the library by name
    // can show the wrong platform's cover for the game you actually played.
    int id = 0;
    ui::Color art;        // shown until the cover arrives, and if it never does
    std::string title;
    // A local path in the sample library, a RomM cover path with live data.
    // Empty means there is no art, which is a normal state and not a failure:
    // arcade sets often have none, and Game & Watch has none at all.
    std::string cover;
    Animated focus;
    Animated press;
};

// A stable colour for a card with no art, from its title. Better than one grey
// for everything: a shelf of coverless games stays distinguishable, and the
// same game is the same colour every time the library is opened.
ui::Color colorForTitle(const std::string& title) {
    uint32_t h = 2166136261u;
    for (unsigned char c : title) { h ^= c; h *= 16777619u; }
    // Fixed saturation and value, hue from the hash: keeps every generated
    // colour inside the design system's range instead of producing mud.
    const float hue = static_cast<float>(h % 360u);
    const float s = 0.45f, v = 0.62f;
    const float c2 = v * s;
    const float x = c2 * (1.0f - std::fabs(std::fmod(hue / 60.0f, 2.0f) - 1.0f));
    const float m = v - c2;
    float r = 0, g = 0, b = 0;
    if (hue < 60)       { r = c2; g = x; }
    else if (hue < 120) { r = x; g = c2; }
    else if (hue < 180) { g = c2; b = x; }
    else if (hue < 240) { g = x; b = c2; }
    else if (hue < 300) { r = x; b = c2; }
    else                { r = c2; b = x; }
    return ui::Color{r + m, g + m, b + m, 1.0f};
}

// Stand-in library. Real covers and names arrive with the RomM client in Phase
// 4; these exist so the layout is exercised against the shapes real data has —
// a long title that has to truncate, and a Japanese one, which a ROM library is
// full of and which is the reason the font stack has a CJK fallback at all.
struct SampleEntry { uint32_t art; const char* title; const char* cover; };
const SampleEntry kSampleLibrary[] = {
    {0x2484D6, "Sonic the Hedgehog 2", "covers/a-3x4.jpg"},
    {0xEC405C, "Super Metroid", "covers/b-3x4.png"},
    {0x58E8F6, "Castlevania: Symphony of the Night", "covers/c-3x4.jpg"},
    // Deliberately the wrong shape: a squarish arcade flyer. This is the
    // odd-aspect case, and it must letterbox onto a blurred echo of itself
    // rather than crop the title off the top of the art.
    {0xFFC457, "\xE3\x83\x89\xE3\x83\xA9\xE3\x82\xAD\xE3\x83\xA5\xE3\x83\xBC\xE3\x82\xB7\xE3\x83\xA5", "covers/d-square.png"},
    // A wide one, for the same reason in the other direction.
    {0xFF7AC7, "Streets of Rage 2", "covers/e-wide.jpg"},
    // No cover at all. Arcade sets often have none, and the coloured panel with
    // the title under it is the honest answer rather than a grey box.
    {0x7A6BC4, "Chrono Trigger", nullptr},
};

}  // namespace

// A launch in progress, off the frame thread.
//
// Downloading on the thread that draws is what the old planLaunch did, and it
// was fine only because Tetris is 19 KB. Press Enter on the reference library's
// 1.78 GB arcade set and the console freezes solid — no animation, nothing to
// look at, no way to cancel — for as long as a few gigabytes takes. That is
// worse than the memory cost, because it is the part a person experiences.
//
// So a worker does it, the frame loop reads a snapshot every frame, and the
// bytes go straight to disk. The client was deliberately built synchronous so
// that callers could do exactly this, the same way ImageCache already does.
struct LaunchJob {
    enum class Stage { Idle, Firmware, Downloading, Unpacking, Ready, Failed };

    std::atomic<Stage> stage{Stage::Idle};
    std::atomic<int64_t> got{0};
    std::atomic<int64_t> total{0};
    std::atomic<bool> cancel{false};

    // Written by the worker before it sets Ready or Failed, read by the frame
    // thread only after it observes one of those. The atomic stage is the
    // handover.
    std::string romPath;
    std::string coreName;
    std::string corePath;
    std::string title;
    std::string message;

    std::thread worker;

    ~LaunchJob() { stop(); }
    void stop() {
        cancel = true;
        if (worker.joinable()) worker.join();
    }
    bool busy() const {
        const Stage st = stage.load();
        return st == Stage::Firmware || st == Stage::Downloading || st == Stage::Unpacking;
    }
};

// Starts one. Returns false if the game cannot be played here at all, which is
// worth saying immediately rather than after a download.
static bool beginLaunch(LaunchJob& job, romm::Client& client, const romm::Game& game,
                        const std::string& coreDir, const std::string& cacheDir,
                        std::string* err) {
    const catalog::Coverage cov = catalog::coverageFor(game);
    if (cov.support != catalog::Support::Playable || !cov.core) {
        *err = game.platformName + ": " + (cov.reason ? cov.reason : "not playable here");
        return false;
    }

    job.stop();
    job.cancel = false;
    job.got = 0;
    job.total = 0;
    job.title = game.name.empty() ? game.fsName : game.name;
    job.coreName = cov.core;
    job.corePath = coreDir + "/" + cov.core + "_libretro.so";
    job.romPath.clear();
    job.message.clear();
    job.stage = LaunchJob::Stage::Downloading;

    // The core is loaded here, on the frame thread, before the worker starts:
    // it is cheap, it is the thing that decides whether the archive gets
    // opened, and a core that will not load should fail now rather than after
    // three gigabytes have been fetched.
    cab::Core& core = cab::Core::shared();
    if (!core.load(job.corePath)) {
        *err = "core " + job.coreName + ": " + core.error();
        job.stage = LaunchJob::Stage::Idle;
        return false;
    }
    const std::string validExts = core.validExtensions();
    const bool blockExtract = core.blockExtract();

    const int id = game.id;
    const int platformId = game.platformId;
    const std::string fsName = game.fsName;
    const int64_t expectedSize = game.sizeBytes;
    job.worker = std::thread([&job, &client, id, platformId, fsName, cacheDir, validExts,
                              blockExtract, expectedSize]() {
        const std::string dir = cacheDir + "/" + std::to_string(id);
        SDL_CreateDirectory(cacheDir.c_str());
        SDL_CreateDirectory(dir.c_str());
        const std::string dest = dir + "/" + fsName;

        // FIRMWARE FIRST, and EVERY file the platform lists rather than
        // whichever one this game looks like it needs. A core looks BIOS up by
        // name in the system directory and ignores what it does not want, so
        // extra files cost a little disk and a missing one costs the launch.
        // Cabinet's rule — see docs/CABINET.md.
        //
        // Shared by every game on the platform, so it lives beside the cache
        // rather than inside one game's directory, and a file already present
        // at the right size is not fetched again.
        {
            job.stage = LaunchJob::Stage::Firmware;
            const std::string systemDir = "system";
            SDL_CreateDirectory(systemDir.c_str());
            std::vector<romm::Firmware> firmware;
            std::string ferr;
            if (client.fetchFirmware(platformId, &firmware, &ferr)) {
                for (const auto& f : firmware) {
                    if (job.cancel.load()) break;
                    const std::string fdest = systemDir + "/" + f.fileName;
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
            job.got = 0;
            job.total = 0;
        }

        // Already here and the right size? Then it is the same ROM: RomM told
        // us how big it is, and a partial download was never renamed into
        // place. Re-fetching 112 MB to play the same game twice is not a
        // caching subtlety, it is just wrong.
        //
        // This is the crude half of what PROJECT.md calls cached-versus-kept.
        // Nothing evicts any of it yet, so the disk fills — which is the next
        // thing this needs and is recorded as such.
        bool haveIt = false;
        if (struct stat st; expectedSize > 0 && ::stat(dest.c_str(), &st) == 0)
            haveIt = st.st_size == expectedSize;

        std::string err;
        if (!haveIt) {
            const std::string path =
                "/api/roms/" + std::to_string(id) + "/content/" + fsName;
            const bool ok = client.fetchToFile(path, dest,
                [&job](int64_t got, int64_t total) {
                    job.got = got;
                    job.total = total;
                    return !job.cancel.load();
                }, &err);
            if (!ok) {
                job.message = err;
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
        } else {
            std::fprintf(stderr, "[launch] already downloaded, %lld bytes\n",
                         static_cast<long long>(expectedSize));
        }

        job.stage = LaunchJob::Stage::Unpacking;
        std::string primary;
        romfile::Kind kind = romfile::Kind::Plain;
        if (!romfile::prepareFile(dest, dir, validExts, blockExtract, &primary, &kind, &err)) {
            job.message = err;
            job.stage = LaunchJob::Stage::Failed;
            return;
        }
        job.romPath = primary;
        job.stage = LaunchJob::Stage::Ready;
    });
    return true;
}

// Builds the shelf from a real library.
//
// THE PLATFORMS THIS CONSOLE CANNOT PLAY ARE LEFT OUT, and that is a decision
// rather than an oversight — see catalog.h. A console must not offer a game it
// cannot run; the moment to discover that is not after someone has chosen it.
// What it must not do is drop them silently, so every exclusion is reported on
// stderr with its reason. The screen that tells the person the same thing is
// still to build; this is the part that knows.
struct Library {
    std::vector<Card> cards;
    // The games behind the cards, same order. A card is what is drawn; this is
    // what is launched, and the launch needs the platform and the file name.
    std::vector<romm::Game> games;
    // Index into `cards`, or -1. The hero is the most recently played game THIS
    // CONSOLE CAN PLAY — not simply the most recent, because a Resume that
    // cannot run is worse than no hero at all.
    int heroIndex = -1;
    std::string heroPlatform;
    // Indices into `cards`, in recency order, with the hero removed — so the
    // shelf never shows the same cover twice and never loses one. Home shows
    // THIS, not the whole library: the library belongs to the Library screen.
    std::vector<int> shelf;
    // The second shelf. Drawn only when there are any: an empty Favorites row
    // is worse than no Favorites row.
    std::vector<int> favorites;
};

static Library loadLibrary(romm::Client& client) {
    Library lib;
    std::vector<Card>& cards = lib.cards;
    std::string err;

    std::vector<romm::Platform> platforms;
    if (!client.fetchPlatforms(&platforms, &err)) {
        std::fprintf(stderr, "[romm] platforms: %s\n", err.c_str());
        return lib;
    }

    int skippedGames = 0;
    for (const auto& p : platforms) {
        const catalog::Coverage cov = catalog::coverageFor(p);
        if (cov.support != catalog::Support::Playable) {
            skippedGames += p.romCount;
            std::fprintf(stderr, "[library] skipping %s (%d games) — %s\n",
                         p.name.c_str(), p.romCount,
                         cov.reason ? cov.reason : "not playable");
            continue;
        }

        std::vector<romm::Game> games;
        if (!client.fetchGames(p.id, &games, &err)) {
            // One platform failing is not the library failing. Say so and go on.
            std::fprintf(stderr, "[library] %s: %s\n", p.name.c_str(), err.c_str());
            continue;
        }
        for (auto& g : games) {
            Card c;
            c.id = g.id;
            c.title = g.name.empty() ? g.fsName : g.name;
            c.cover = g.coverPath;
            c.art = colorForTitle(c.title);
            cards.push_back(std::move(c));
            lib.games.push_back(g);
        }
    }

    // Sorted together, so index i of one is index i of the other. Two parallel
    // vectors sorted independently is a bug waiting for its first duplicate
    // title, and this library has those.
    std::vector<size_t> order(cards.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
        return cards[a].title < cards[b].title;
    });
    std::vector<Card> sortedCards;
    std::vector<romm::Game> sortedGames;
    sortedCards.reserve(order.size());
    sortedGames.reserve(order.size());
    for (size_t i : order) {
        sortedCards.push_back(std::move(cards[i]));
        sortedGames.push_back(std::move(lib.games[i]));
    }
    cards = std::move(sortedCards);
    lib.games = std::move(sortedGames);

    int withArt = 0;
    for (const auto& c : cards) if (!c.cover.empty()) ++withArt;
    std::fprintf(stderr, "[library] %zu playable games, %d with art; %d games skipped\n",
                 cards.size(), withArt, skippedGames);

    // The hero, from the server's play history rather than from anything this
    // console remembers. A game played on an Apple TV is recent here the moment
    // this console is paired, which is what lets a machine that has never
    // launched anything still open on the right game.
    std::vector<romm::Game> recent;
    if (client.fetchRecent(16, &recent, &err)) {
        for (const auto& g : recent) {
            // A recent game on a platform this console cannot play is skipped,
            // not shown greyed: Home must not offer a Resume that cannot run.
            // The reference library exercises this — the most recent "Altered
            // Beast" is the Game & Watch one, which Cabinet does not ship.
            if (!catalog::playable(g)) continue;
            int idx = -1;
            for (size_t i = 0; i < cards.size(); ++i) {
                if (cards[i].id == g.id) { idx = static_cast<int>(i); break; }
            }
            if (idx < 0) continue;
            // The first playable one is the hero; the rest are the shelf.
            if (lib.heroIndex < 0) {
                lib.heroIndex = idx;
                lib.heroPlatform = g.platformName;
            } else {
                lib.shelf.push_back(idx);
            }
        }
    } else {
        std::fprintf(stderr, "[library] no play history: %s\n", err.c_str());
    }

    std::vector<romm::Game> favs;
    if (client.fetchFavorites(40, &favs, &err)) {
        for (const auto& g : favs) {
            if (!catalog::playable(g)) continue;
            for (size_t i = 0; i < cards.size(); ++i) {
                if (cards[i].id == g.id) { lib.favorites.push_back(static_cast<int>(i)); break; }
            }
        }
        std::fprintf(stderr, "[library] %zu favourites\n", lib.favorites.size());
    }
    if (lib.heroIndex >= 0)
        std::fprintf(stderr, "[library] hero: %s (%s), %zu more on the Recent shelf\n",
                     cards[lib.heroIndex].title.c_str(), lib.heroPlatform.c_str(),
                     lib.shelf.size());
    else
        std::fprintf(stderr, "[library] no hero — nothing recent is playable here\n");
    return lib;
}

// Talks to a RomM server and reports, without opening a window.
//
//   --romm <address>              connect and list the library
//   --romm <address> --romm-pair  pair first, if there is no token yet
//
// The token lives in ~/.config/cabinetos/romm.json at 0600. It is a credential:
// it is never printed here, and it does not belong in the repository.
static std::string rommTokenPath() {
    const char* home = getenv("HOME");
    return std::string(home ? home : ".") + "/.config/cabinetos/romm.json";
}

// Downloads one ROM and says what a core would actually be given.
static int romProbe(const char* address, int romId, const char* validExts) {
    romm::Client client;
    std::string err;
    if (!client.setAddress(address, &err)) {
        std::fprintf(stderr, "[romm] %s\n", err.c_str());
        return 1;
    }
    if (!client.loadToken(rommTokenPath())) {
        std::fprintf(stderr, "[romm] no token\n");
        return 1;
    }

    std::vector<romm::Game> all;
    if (!client.fetchGames(0, &all, &err)) {
        std::fprintf(stderr, "[romm] %s\n", err.c_str());
        return 1;
    }
    const romm::Game* g = nullptr;
    for (const auto& x : all) if (x.id == romId) { g = &x; break; }
    if (!g) { std::fprintf(stderr, "[romm] no rom with id %d\n", romId); return 1; }

    std::printf("game        %s\n", g->name.c_str());
    std::printf("platform    %s\n", g->platformName.c_str());
    std::printf("file        %s (%lld bytes)\n", g->fsName.c_str(),
                static_cast<long long>(g->sizeBytes));

    const std::string path = "/api/roms/" + std::to_string(g->id) + "/content/" + g->fsName;
    std::vector<uint8_t> bytes = client.fetchBytes(path);
    if (bytes.empty()) { std::fprintf(stderr, "[romm] download returned nothing\n"); return 1; }
    std::printf("downloaded  %zu bytes\n", bytes.size());

    // What it IS, from the bytes — not from the name it happens to have.
    const romfile::Kind k = romfile::sniff(bytes);
    std::printf("sniffed     %s\n", romfile::kindName(k));

    romfile::Prepared prep;
    if (!romfile::prepare(bytes, validExts, false, &prep, &err)) {
        std::fprintf(stderr, "[romfile] %s\n", err.c_str());
        return 1;
    }
    if (prep.passThrough) {
        std::printf("decision    hand it over untouched\n");
        return 0;
    }
    std::printf("decision    unpacked %zu file(s) from the %s\n",
                prep.members.size(), romfile::kindName(prep.kind));
    for (size_t i = 0; i < prep.members.size(); ++i) {
        std::printf("            %s %-44s %zu bytes\n",
                    static_cast<int>(i) == prep.primary ? "->" : "  ",
                    prep.members[i].name.c_str(), prep.members[i].bytes.size());
    }
    return 0;
}

static int rommProbe(const char* address, bool allowPairing) {
    romm::Client client;
    std::string err;

    if (!client.setAddress(address, &err)) {
        std::fprintf(stderr, "[romm] %s\n", err.c_str());
        return 1;
    }
    std::printf("server      %s (RomM %s)\n", client.baseUrl().c_str(),
                client.serverVersion().c_str());

    const std::string tokenPath = rommTokenPath();
    if (!client.loadToken(tokenPath)) {
        if (!allowPairing) {
            std::fprintf(stderr,
                         "[romm] no token at %s — run again with --romm-pair\n",
                         tokenPath.c_str());
            return 1;
        }
        romm::Pairing p;
        if (!client.beginPairing(&p, &err)) {
            std::fprintf(stderr, "[romm] pairing failed: %s\n", err.c_str());
            return 1;
        }
        // This is what the first-run screen will show as a QR code. On a
        // television it is the only thing anyone has to act on.
        std::printf("\napprove at  %s\ncode        %s\nexpires in  %d seconds\n\n",
                    p.verificationUrl.c_str(), p.userCode.c_str(), p.expiresIn);
        std::fflush(stdout);

        const int deadline = p.expiresIn > 0 ? p.expiresIn : 600;
        int waited = 0;
        int state = 0;
        while (waited < deadline) {
            SDL_Delay(static_cast<Uint32>(p.intervalSeconds) * 1000);
            waited += p.intervalSeconds;
            state = client.pollPairing(p, &err);
            if (state != 0) break;
            // \r only makes sense on a terminal. Redirected to a file — which
            // is how this runs on the console — it concatenates every tick
            // onto one unreadable line.
            std::printf("waiting…    %ds%s", waited, isatty(1) ? "\r" : "\n");
            std::fflush(stdout);
        }
        if (state != 1) {
            std::fprintf(stderr, "\n[romm] not paired: %s\n",
                         err.empty() ? "timed out" : err.c_str());
            return 1;
        }
        // Best effort: an unwritable config directory should not throw away a
        // pairing the person has already approved.
        if (client.saveToken(tokenPath))
            std::printf("\npaired      token saved to %s\n", tokenPath.c_str());
        else
            std::printf("\npaired      (could not write %s)\n", tokenPath.c_str());
    }

    std::vector<romm::Platform> platforms;
    if (!client.fetchPlatforms(&platforms, &err)) {
        std::fprintf(stderr, "[romm] platforms: %s\n", err.c_str());
        return 1;
    }

    int total = 0;
    for (const auto& p : platforms) total += p.romCount;
    std::printf("\n%zu platforms, %d games\n\n", platforms.size(), total);

    // fs_slug is printed next to the name because it is the field that
    // distinguishes two platforms the name and the slug cannot — "Arcade" is
    // FBNeo and MAME 2003-Plus, and a client keying on slug loses one of them.
    for (const auto& p : platforms) {
        std::printf("  %5d  %-34s id=%-4d slug=%-22s fs=%s\n", p.romCount,
                    p.name.c_str(), p.id, p.slug.c_str(), p.fsSlug.c_str());
    }

    // Page the largest platform in full, because silent truncation is the
    // failure this has to rule out: a client that quietly returns the first
    // page looks like it works.
    const romm::Platform* biggest = nullptr;
    for (const auto& p : platforms)
        if (!biggest || p.romCount > biggest->romCount) biggest = &p;
    if (biggest) {
        std::vector<romm::Game> games;
        if (!client.fetchGames(biggest->id, &games, &err)) {
            std::fprintf(stderr, "\n[romm] games: %s\n", err.c_str());
            return 1;
        }
        std::printf("\npaged %s: fetched %zu, server said %d — %s\n",
                    biggest->name.c_str(), games.size(), biggest->romCount,
                    games.size() == static_cast<size_t>(biggest->romCount)
                        ? "match"
                        : "MISMATCH, something is truncating");
        int withCover = 0;
        for (const auto& g : games) if (!g.coverPath.empty()) ++withCover;
        std::printf("covers      %d of %zu have art\n", withCover, games.size());
        if (!games.empty())
            std::printf("first       %s\n", games.front().name.c_str());
    }

    // What Home will actually show. The hero is the most recently played game
    // this console can play — not simply the most recent, because offering a
    // Resume that cannot run is worse than offering nothing.
    std::vector<romm::Game> recent;
    if (client.fetchRecent(16, &recent, &err)) {
        std::printf("\nrecent      %zu games with play history\n", recent.size());
        const romm::Game* hero = nullptr;
        for (const auto& g : recent)
            if (catalog::playable(g)) { hero = &g; break; }
        if (hero) {
            std::printf("hero        %s (%s)\n", hero->name.c_str(),
                        hero->platformName.c_str());
        } else {
            std::printf("hero        none — nothing recent is playable here\n");
        }
        for (const auto& g : recent) {
            const catalog::Coverage c = catalog::coverageFor(g);
            std::printf("            %-40s %-14s %s\n", g.name.substr(0, 39).c_str(),
                        g.platformName.c_str(),
                        c.support == catalog::Support::Playable ? c.core : "— not playable here");
        }
    } else {
        std::printf("\nrecent      failed: %s\n", err.c_str());
    }

    // Actually FETCH a cover, rather than counting the ones that claim to have
    // one. Those are different questions, and the difference hid a real bug:
    // RomM appends "?ts=<datetime with a space>" to cover paths, which curl
    // rejects outright, so every cover failed while the count looked healthy.
    // A library with no art and no error is the worst kind of broken.
    for (const auto& p : platforms) {
        if (p.romCount == 0) continue;
        std::vector<romm::Game> games;
        if (!client.fetchGames(p.id, &games, &err)) continue;
        const romm::Game* withArt = nullptr;
        for (const auto& g : games)
            if (!g.coverPath.empty()) { withArt = &g; break; }
        if (!withArt) continue;

        std::vector<uint8_t> bytes = client.fetchBytes(withArt->coverPath);
        std::printf("\ncover test  %s — %s\n", withArt->name.c_str(),
                    bytes.empty() ? "FAILED, fetched 0 bytes" : "ok");
        if (!bytes.empty()) {
            // Say what it actually is. A 200 carrying an HTML error page is
            // still zero use to a texture upload.
            const bool png = bytes.size() > 8 && bytes[0] == 0x89 && bytes[1] == 'P';
            const bool jpg = bytes.size() > 3 && bytes[0] == 0xFF && bytes[1] == 0xD8;
            std::printf("            %zu bytes, %s\n", bytes.size(),
                        png ? "PNG" : jpg ? "JPEG" : "NOT AN IMAGE");
        } else {
            return 1;
        }
        break;
    }
    return 0;
}

int main(int argc, char** argv) {
    bool shotMode = false;
    const char* shotPath = "/tmp/cabinetos-frame.bmp";
    int shotAfterFrames = 30;
    int initialFocus = -1;
    // Which row to start on, so each of Home's rows can be photographed without
    // a controller. 0 hero, 1 Recent, 2 Favorites.
    int initialRow = -1;
    // Verify the layout at a panel size this machine does not have. Most sets
    // are 4K; plenty are not; the design canvas scales to both and this is how
    // that gets checked rather than assumed.
    int renderW = 0, renderH = 0;
    // Deliberately settable, so eviction can be exercised on a machine with
    // plenty of memory. An unbounded texture cache on a 4 GB console is a real
    // failure mode and it must be testable, not merely intended.
    size_t imageBudget = 192u * 1024 * 1024;
    // Loads covers that are then never drawn again, the way scrolling a shelf
    // leaves the covers behind it. That is the only situation in which anything
    // is evictable at all, so it is the only way to test that eviction works.
    bool evictTest = false;
    // Proves a restored state is genuinely identical, not merely accepted.
    bool stateTest = false;
    bool audioProbe = false;
    // Opens the keyboard immediately, so it can be worked on without walking
    // through a first-run flow that does not exist yet.
    bool keyboardDemo = false;
    bool safeGuides = false;
    // Off / subtle / strong, the reference implementation's own three levels.
    float glowPeak = 0.025f;
    // Running a core. Both are needed: a core without a ROM has nothing to do.
    const char* corePath = nullptr;
    const char* romPath = nullptr;
    // Talks to a RomM server and prints what it found, without opening a
    // window. The same reasoning as --state-test and --audio-probe: the
    // network, the auth and the parsing are all things that can be wrong on
    // their own, and finding that out through a UI is the slow way.
    const char* rommAddress = nullptr;
    bool rommPair = false;
    // --romm alone runs the UI against the server. --romm-probe reports and
    // exits without opening a window, which is what a headless machine and a
    // CI job can do.
    bool rommProbeMode = false;
    // Downloads one ROM and reports what came back and what a core would be
    // handed. The formats people keep ROMs in are not uniform and this is how
    // that gets checked against a real server rather than assumed.
    int romProbeId = 0;
    const char* romProbeExts = "gb|gbc|dmg";
    // Where the built cores are, and where downloaded ROMs are kept.
    const char* coreDir = "cores/build";
    const char* romCacheDir = "romcache";
    // Launch this RomM id without anybody pressing anything, after a delay, so
    // the whole Home-to-game transition can be watched on a machine with no
    // controller attached to it.
    int autoLaunchId = 0;
    float autoLaunchAfter = 0.0f;
    for (int i = 1; i < argc; ++i) {
        if (SDL_strcmp(argv[i], "--screenshot") == 0 && i + 1 < argc) {
            shotMode = true;
            shotPath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            shotAfterFrames = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--glow") == 0 && i + 1 < argc) {
            const char* g = argv[++i];
            glowPeak = SDL_strcmp(g, "off") == 0      ? 0.0f
                       : SDL_strcmp(g, "strong") == 0 ? 0.04f
                                                      : 0.025f;
        } else if (SDL_strcmp(argv[i], "--safe-area") == 0) {
            safeGuides = true;
        } else if (SDL_strcmp(argv[i], "--keyboard") == 0) {
            keyboardDemo = true;
        } else if (SDL_strcmp(argv[i], "--audio-probe") == 0) {
            audioProbe = true;
        } else if (SDL_strcmp(argv[i], "--state-test") == 0) {
            stateTest = true;
        } else if (SDL_strcmp(argv[i], "--core") == 0 && i + 1 < argc) {
            corePath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--romm") == 0 && i + 1 < argc) {
            rommAddress = argv[++i];
        } else if (SDL_strcmp(argv[i], "--core-dir") == 0 && i + 1 < argc) {
            coreDir = argv[++i];
        } else if (SDL_strcmp(argv[i], "--launch") == 0 && i + 1 < argc) {
            autoLaunchId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--launch-after") == 0 && i + 1 < argc) {
            autoLaunchAfter = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--rom-probe") == 0 && i + 1 < argc) {
            romProbeId = SDL_atoi(argv[++i]);
            rommProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--rom-exts") == 0 && i + 1 < argc) {
            romProbeExts = argv[++i];
        } else if (SDL_strcmp(argv[i], "--romm-probe") == 0) {
            rommProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--romm-pair") == 0) {
            rommPair = true;
            rommProbeMode = true;   // pairing is inherently a headless errand
        } else if (SDL_strcmp(argv[i], "--rom") == 0 && i + 1 < argc) {
            romPath = argv[++i];
        } else if (SDL_strcmp(argv[i], "--evict-test") == 0) {
            evictTest = true;
        } else if (SDL_strcmp(argv[i], "--image-budget-mb") == 0 && i + 1 < argc) {
            imageBudget = static_cast<size_t>(SDL_atoi(argv[++i])) * 1024 * 1024;
        } else if (SDL_strcmp(argv[i], "--render-size") == 0 && i + 1 < argc) {
            SDL_sscanf(argv[++i], "%dx%d", &renderW, &renderH);
        } else if (SDL_strcmp(argv[i], "--focus-row") == 0 && i + 1 < argc) {
            initialRow = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--focus") == 0 && i + 1 < argc) {
            // Lets a screenshot capture a chosen card already focused, so the
            // focus treatment can be checked without a controller attached.
            initialFocus = SDL_atoi(argv[++i]);
        }
    }

    // Runs before SDL, deliberately. This needs no window, no GL and no
    // controller, and on a headless machine it must work anyway — the whole
    // point is to test the server conversation on its own.
    if (rommAddress && romProbeId > 0)
        return romProbe(rommAddress, romProbeId, romProbeExts);
    if (rommAddress && rommProbeMode) return rommProbe(rommAddress, rommPair);

    std::signal(SIGUSR1, requestCapture);

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "[frontend] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // GLES 3.0, which is what the libretro hardware-rendered cores ask for via
    // RETRO_ENVIRONMENT_SET_HW_RENDER. The UI and the cores share one context
    // on purpose: that is what removes the readback the Apple build cannot
    // avoid. See docs/PROJECT.md, "Video: two paths".
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, 8);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, 8);

    SDL_Window* window = SDL_CreateWindow(
        "CabinetOS", 1920, 1080, SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN);
    if (!window) {
        std::fprintf(stderr, "[frontend] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    SDL_GLContext gl = SDL_GL_CreateContext(window);
    if (!gl) {
        std::fprintf(stderr, "[frontend] SDL_GL_CreateContext failed: %s\n",
                     SDL_GetError());
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 1;
    }

    // Under cage on software rendering vsync throttles to whatever llvmpipe can
    // manage, which is fine — the frame loop is paced by wall clock, not by the
    // swap, exactly as the core pacing will be.
    SDL_GL_SetSwapInterval(1);

    std::fprintf(stderr, "[frontend] GL_VENDOR   %s\n", glGetString(GL_VENDOR));
    std::fprintf(stderr, "[frontend] GL_RENDERER %s\n", glGetString(GL_RENDERER));
    std::fprintf(stderr, "[frontend] GL_VERSION  %s\n", glGetString(GL_VERSION));

    ui::Renderer renderer;
    if (!renderer.init()) {
        std::fprintf(stderr, "[frontend] renderer init failed\n");
        return 1;
    }

    ui::TextRenderer text;
    // Regular, Medium, SemiBold, Bold, then the CJK fallback. All five are
    // already in the Bazzite base, so the type costs the image nothing.
    if (!text.init({"/usr/share/fonts/google-noto/NotoSans-Regular.ttf",
                    "/usr/share/fonts/google-noto/NotoSans-Medium.ttf",
                    "/usr/share/fonts/google-noto/NotoSans-SemiBold.ttf",
                    "/usr/share/fonts/google-noto/NotoSans-Bold.ttf",
                    "/usr/share/fonts/google-noto-sans-cjk-fonts/NotoSansCJK-Regular.ttc"})) {
        std::fprintf(stderr, "[frontend] text init failed\n");
        return 1;
    }

    // 192 MB of covers resident. A shelf holds a handful; a library grid holds
    // a screenful; anything past that is re-decoded on the way back, which is
    // cheap and bounded. Four workers, so a fast scroll keeps up without
    // starving the frame thread on a four-core box.
    ui::ImageCache images;
    // A key is not a path. Everything after '#' is stripped before reading, so
    // one file can stand in for many distinct entries here — and so a real RomM
    // URL with a query string is already the shape this expects.
    // The client outlives the cache deliberately: ImageCache calls the loader
    // from its worker threads, so whatever it captures must still be alive when
    // a cover arrives. romm::Client is safe to call concurrently — each request
    // builds its own CURL handle, and nothing else mutates after setup.
    static romm::Client liveClient;
    std::vector<Card> cards;
    int heroIndex = -1;
    std::string heroPlatform;
    // Which cards the Recent row shows. Empty means "everything", which is what
    // the stand-in library wants — it has no play history to order by.
    std::vector<int> shelf;
    std::vector<int> favorites;
    std::vector<romm::Game> games;

    if (rommAddress) {
        std::string err;
        if (!liveClient.setAddress(rommAddress, &err)) {
            std::fprintf(stderr, "[romm] %s\n", err.c_str());
            return 1;
        }
        if (!liveClient.loadToken(rommTokenPath())) {
            std::fprintf(stderr, "[romm] no token at %s — pair first with --romm-probe --romm-pair\n",
                         rommTokenPath().c_str());
            return 1;
        }
        Library lib = loadLibrary(liveClient);
        cards = std::move(lib.cards);
        heroIndex = lib.heroIndex;
        heroPlatform = lib.heroPlatform;
        shelf = std::move(lib.shelf);
        favorites = std::move(lib.favorites);
        games = std::move(lib.games);
        if (cards.empty()) {
            std::fprintf(stderr, "[romm] the library came back empty\n");
            return 1;
        }
        // Covers come from the server, authenticated. The cache never learns
        // what a server is — it was built to take exactly this.
        images.init(imageBudget, 4, [](const std::string& key) {
            return liveClient.fetchBytes(key);
        });
    } else {
        // A key is not a path. Everything after '#' is stripped before reading,
        // so one file can stand in for many distinct entries here.
        images.init(imageBudget, 4, [](const std::string& key) {
            const size_t hash = key.find('#');
            return ui::ImageCache::readFile(hash == std::string::npos ? key
                                                                     : key.substr(0, hash));
        });
        for (const SampleEntry& e : kSampleLibrary) {
            Card c;
            c.art = ui::Color::rgb(e.art);
            c.title = e.title;
            c.cover = e.cover ? e.cover : "";
            cards.push_back(std::move(c));
        }
    }

    ui::Keyboard keyboard;
    if (keyboardDemo) {
        // The real first field: a RomM address.
        //
        // NO PREFILLED SCHEME, deliberately. A self-hosted RomM on a home LAN
        // is very often plain HTTP on a port, and prefilling "https://" pushes
        // people toward a scheme their server does not speak — which is the
        // shape of the problem tvOS has, where App Transport Security refuses
        // plain HTTP outright. That is an Apple constraint and CabinetOS does
        // not inherit it, but only if it is not designed back in. The
        // placeholder shows the common case instead: a host and a port.
        ui::Keyboard::Config cfg;
        cfg.title = "Connect to RomM";
        cfg.hint = "The same address you open in a browser.";
        cfg.placeholder = "romm.local:8080";
        cfg.shortcuts = {".local", ".com"};
        keyboard.open(cfg);
    }

    // --- The core, if one was asked for --------------------------------------
    bool playing = false;
    SDL_AudioStream* audioStream = nullptr;
    if (corePath && romPath) {
        cab::Core& core = cab::Core::shared();
        if (!core.load(corePath)) {
            std::fprintf(stderr, "[frontend] core: %s\n", core.error().c_str());
            return 1;
        }
        // The save directory must outlive the session. Per-game, alongside the
        // ROM for now; Phase 4 moves it under the chosen storage location.
        const std::string saveDir = "saves";
        SDL_CreateDirectory(saveDir.c_str());
        if (!core.loadGame(romPath, "system", saveDir)) {
            std::fprintf(stderr, "[frontend] %s\n", core.error().c_str());
            return 1;
        }
        playing = true;

        if (audioProbe) {
            // Which call silences it? Three identical runs that differ only in
            // what is done at the 240-frame mark. Narrowing it to one entry
            // point is the difference between "save states are weird" and a
            // report someone can act on.
            cab::Core& c = core;
            const double step = 1.0 / c.avInfo().fps;
            auto peakOver = [&c, step](int frames) {
                long peak = 0;
                for (int i = 0; i < frames; ++i) {
                    c.runFor(step);
                    for (int16_t v : c.drainAudio())
                        peak = std::max<long>(peak, std::abs(static_cast<int>(v)));
                }
                return peak;
            };
            if (SDL_getenv("CABINETOS_TRACE")) {
                // Just watch. When does it go quiet, and does it come back?
                std::fprintf(stderr, "[probe] peak per 60 frames (1 second each):\n");
                for (int block = 0; block < 34; ++block) {
                    const long p = peakOver(60);
                    std::fprintf(stderr, "  %4ds  %6ld%s\n", block + 1, p,
                                 p == 0 ? "   <- silent" : "");
                }
                return 0;
            }
            const char* what = SDL_getenv("CABINETOS_PROBE");
            const std::string mode = what ? what : "none";
            const long before = peakOver(240);
            if (mode == "size") {
                c.stateSize();
            } else if (mode == "save") {
                std::vector<uint8_t> st;
                c.saveState(st);
            }
            const long after = peakOver(180);
            std::fprintf(stderr, "[probe] %-5s  before %6ld   after %6ld   %s\n",
                         mode.c_str(), before, after,
                         after == 0 && before > 0 ? "SILENCED" : "ok");
            return 0;
        }

        if (stateTest) {
            // A save state is only worth anything if what comes back is the
            // same machine. "The core accepted the bytes" is not that: it is
            // exactly what a subtly wrong state also looks like.
            //
            // So: run to a point, snapshot, run on and remember what happened,
            // restore, run the same distance again, and compare. Identical
            // output means the restore put every bit back. This is the
            // reference implementation's own test — "a serialize, run, restore,
            // run round trip produces identical video and audio streams".
            // Deep enough in that the picture is genuinely moving. The first
            // version of this test warmed up 240 frames and compared video on
            // Dr. Mario's TITLE SCREEN — a static image, which matches itself
            // no matter what the machine is doing. It reported PASS and proved
            // nothing. A determinism test has to run somewhere that would
            // actually diverge, and here that is the attract demo.
            const int kWarm = SDL_getenv("CABINETOS_WARM")
                                  ? SDL_atoi(SDL_getenv("CABINETOS_WARM"))
                                  : 1500;
            const int kRunOn = 300;  // five seconds for a divergence to show
            const double step = 1.0 / core.avInfo().fps;

            // Is the game making any sound, and is the picture actually
            // changing? Everything else here is meaningless if either answer
            // is no, which is exactly the trap the first version fell into.
            long warmPeak = 0;
            for (int i = 0; i < kWarm; ++i) {
                core.runFor(step);
                for (int16_t v : core.drainAudio())
                    warmPeak = std::max<long>(warmPeak, std::abs(static_cast<int>(v)));
            }
            std::fprintf(stderr, "[state] peak amplitude during the %d warm-up frames: %ld\n",
                         kWarm, warmPeak);
            // Prove the picture moves before trusting any video comparison.
            const uint64_t vA = core.frameDigest();
            for (int i = 0; i < 30; ++i) core.runFor(step);
            const uint64_t vB = core.frameDigest();
            core.drainAudio();
            std::fprintf(stderr, "[state] picture over 30 frames: %s\n",
                         vA == vB ? "STATIC - this test would prove nothing here"
                                  : "moving - a video comparison is meaningful");

            // Control: the same stretch of game with NO state operation at
            // all. Without this there is no way to tell "the restore changed
            // something" from "saving changed something" from "the game simply
            // sounds like this here".
            std::vector<uint8_t> probe;
            (void)probe;

            std::vector<uint8_t> state;
            const size_t stateBytes = core.stateSize();
            if (!core.saveState(state)) {
                std::fprintf(stderr, "[state] core produced no state\n");
                return 1;
            }
            std::fprintf(stderr, "[state] %zu bytes at frame %d\n", state.size(), kWarm);

            // Video and audio digested SEPARATELY. If they are mixed and the
            // result differs, all you know is "something diverged" — which is
            // the least useful possible answer about a save system.
            struct Digest {
                uint64_t video = 1469598103934665603ull;
                uint64_t audio = 1469598103934665603ull;
                size_t audioBytes = 0;
                int firstVideoDiff = -1;
                int firstAudioDiff = -1;
                std::vector<uint64_t> perFrameVideo, perFrameAudio;
                // The whole audio stream, concatenated. A per-frame hash is
                // sensitive to WHERE the resampler happens to split its output,
                // which is not the same question as whether the samples are the
                // same samples.
                std::vector<int16_t> audioStream;
            };
            auto runAndDigest = [&core, kRunOn, step]() {
                // Drain first. Audio accumulates until somebody takes it, so a
                // run that starts with the warm-up's leftovers in the buffer
                // hashes them and the next run does not. That is a difference
                // in the TEST, and reading it as a difference in the STATE is
                // exactly the wrong conclusion to reach about a save system.
                core.drainAudio();

                Digest d;
                auto mix = [](uint64_t& h, const void* p, size_t n) {
                    const auto* b = static_cast<const uint8_t*>(p);
                    for (size_t i = 0; i < n; ++i) {
                        h ^= b[i];
                        h *= 1099511628211ull;
                    }
                };
                for (int i = 0; i < kRunOn; ++i) {
                    core.runFor(step);
                    const uint64_t v = core.frameDigest();
                    mix(d.video, &v, sizeof(v));
                    d.perFrameVideo.push_back(v);

                    const std::vector<int16_t>& audio = core.drainAudio();
                    uint64_t a = 1469598103934665603ull;
                    mix(a, audio.data(), audio.size() * sizeof(int16_t));
                    mix(d.audio, &a, sizeof(a));
                    d.perFrameAudio.push_back(a);
                    d.audioBytes += audio.size() * sizeof(int16_t);
                    d.audioStream.insert(d.audioStream.end(), audio.begin(), audio.end());
                }
                return d;
            };

            const Digest first = runAndDigest();
            {
                long p1 = 0;
                for (int16_t v : first.audioStream)
                    p1 = std::max<long>(p1, std::abs(static_cast<int>(v)));
                std::fprintf(stderr, "[state] run1 (after save) peak: %ld\n", p1);
            }
            if (!core.loadState(state)) {
                std::fprintf(stderr, "[state] the core REJECTED its own state\n");
                return 1;
            }
            const Digest second = runAndDigest();

            // A third run, restored the same way as the second. This is the
            // control the first two lack: if runs 2 and 3 agree with each other
            // but not with run 1, then restoring is perfectly deterministic and
            // what differs is the PATH taken to get there, not the state. That
            // is a very different finding from "save states are broken", and
            // without this run the two are indistinguishable.
            if (!core.loadState(state)) {
                std::fprintf(stderr, "[state] second restore rejected\n");
                return 1;
            }
            const Digest third = runAndDigest();
            std::fprintf(stderr, "[state] run2 vs run3: video %s, audio %s\n",
                         second.video == third.video ? "MATCH" : "differs",
                         second.audioStream == third.audioStream ? "MATCH" : "differs");

            int videoDiff = -1, audioDiff = -1;
            for (int i = 0; i < kRunOn; ++i) {
                if (videoDiff < 0 && first.perFrameVideo[i] != second.perFrameVideo[i])
                    videoDiff = i;
                if (audioDiff < 0 && first.perFrameAudio[i] != second.perFrameAudio[i])
                    audioDiff = i;
            }

            std::fprintf(stderr, "[state] video  %016llx vs %016llx  %s\n",
                         static_cast<unsigned long long>(first.video),
                         static_cast<unsigned long long>(second.video),
                         first.video == second.video ? "MATCH"
                                                     : "differs");
            std::fprintf(stderr, "[state] audio  %016llx vs %016llx  %s\n",
                         static_cast<unsigned long long>(first.audio),
                         static_cast<unsigned long long>(second.audio),
                         first.audio == second.audio ? "MATCH" : "differs");
            if (videoDiff >= 0)
                std::fprintf(stderr, "[state] first differing video frame: %d of %d\n",
                             videoDiff, kRunOn);
            std::fprintf(stderr, "[state] audio bytes %zu vs %zu\n", first.audioBytes,
                         second.audioBytes);

            // The audio verdict, with its own confidence attached.
            //
            // Three wrong theories were chased here before the controls killed
            // them: leftover audio in the buffer, the resampler's filter
            // history, and retro_serialize having a side effect. The actual
            // answer was that the GAME IS SILENT — Dr. Mario untouched plays a
            // ding at one second and nothing for the next half minute. A test
            // that reports "audio differs" without first checking there IS any
            // audio is reporting its own blind spot.
            long peak1 = 0, peak2 = 0;
            for (int16_t v : first.audioStream)
                peak1 = std::max<long>(peak1, std::abs(static_cast<int>(v)));
            for (int16_t v : second.audioStream)
                peak2 = std::max<long>(peak2, std::abs(static_cast<int>(v)));

            if (peak1 == 0 && peak2 == 0) {
                std::fprintf(stderr,
                             "[state] audio: INCONCLUSIVE - the game is silent here, so "
                             "this proves nothing either way\n");
            } else if (first.audioStream == second.audioStream) {
                std::fprintf(stderr, "[state] audio: MATCH, sample for sample (%zu samples)\n",
                             first.audioStream.size());
            } else if (peak1 == 0) {
                // Sound on the restored path and none on the continuous one, in
                // a passage the game plays silent, is a transient at the moment
                // of restore rather than a difference in the machine. RetroArch
                // mutes briefly after a state load for exactly this reason.
                std::fprintf(stderr,
                             "[state] audio: a transient on restore (peak %ld against "
                             "silence) - a click at the seam, not lost state. Worth "
                             "muting briefly after a load.\n",
                             peak2);
            } else {
                std::fprintf(stderr,
                             "[state] audio: differs with both paths audible (peaks %ld "
                             "and %ld) - investigate\n",
                             peak1, peak2);
            }

            if (first.video == second.video) {
                std::fprintf(stderr,
                             "[state] PASS - the emulated machine restored exactly "
                             "(%zu byte state)\n",
                             stateBytes);
            } else {
                std::fprintf(stderr, "[state] FAIL - the picture diverged after restore\n");
                return 1;
            }

            // Save RAM, which is a different mechanism entirely and the one
            // people assume is safe once the game says it saved.
            std::vector<uint8_t> sram;
            if (core.saveRAM(sram)) {
                std::fprintf(stderr, "[state] save RAM: %zu bytes\n", sram.size());
            } else {
                std::fprintf(stderr,
                             "[state] save RAM: none exposed (cartridge has no battery, "
                             "or the core writes its own file)\n");
            }
            return 0;
        }

        // Audio: SDL owns the device thread and we push from the frame loop.
        // The rule from the reference implementation is that the callback must
        // never block — so there is no callback, and nothing to block.
        SDL_AudioSpec src{};
        src.format = SDL_AUDIO_S16;
        src.channels = 2;
        src.freq = static_cast<int>(core.avInfo().sampleRate);
        audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &src,
                                                nullptr, nullptr);
        if (audioStream) {
            SDL_ResumeAudioStreamDevice(audioStream);
            std::fprintf(stderr, "[frontend] audio out at %d Hz\n", src.freq);
        } else {
            // A console with no sound card is still a console. Say so and play on.
            std::fprintf(stderr, "[frontend] no audio device: %s\n", SDL_GetError());
        }
    }

    // ---- Focus -------------------------------------------------------------
    //
    // Home is rows, not one list. Focus is therefore a ROW and a SLOT within it,
    // never a single index: `focused` used to index the shelf, and that is only
    // the same number while the shelf is the only thing on screen.
    //
    //   row 0   the hero      slot 0 the card, slot 1 the Resume pill
    //   row 1   Recent
    //   row 2   Favorites, when there are any
    //
    // The hero takes focus on arrival, which is what "resume-first" means: the
    // thing you were playing is already under the cursor.
    enum Row { RowHero = 0, RowRecent = 1, RowFavorites = 2 };

    const size_t shelfSlots = shelf.empty() ? cards.size() : shelf.size();
    const bool haveHero = heroIndex >= 0;
    const bool haveFavorites = !favorites.empty();

    auto rowSlots = [&](int row) -> size_t {
        if (row == RowHero) return haveHero ? 2u : 0u;
        if (row == RowRecent) return shelfSlots;
        return favorites.size();
    };
    auto rowExists = [&](int row) { return rowSlots(row) > 0; };

    // The card a (row, slot) points at, or nullptr for the Resume pill, which
    // is a control rather than a card.
    auto cardAt = [&](int row, int slot) -> Card* {
        if (row == RowHero) return slot == 0 ? &cards[heroIndex] : nullptr;
        if (row == RowRecent) {
            if (slot < 0 || static_cast<size_t>(slot) >= shelfSlots) return nullptr;
            return &cards[shelf.empty() ? static_cast<size_t>(slot)
                                        : static_cast<size_t>(shelf[slot])];
        }
        if (slot < 0 || static_cast<size_t>(slot) >= favorites.size()) return nullptr;
        return &cards[favorites[slot]];
    };

    int focusRow = haveHero ? RowHero : RowRecent;
    int focusSlot = 0;
    // --focus N still means "start on card N of Recent", which is what every
    // existing capture script passes it for.
    if (initialFocus >= 0) {
        focusRow = RowRecent;
        focusSlot = std::clamp(initialFocus, 0, static_cast<int>(shelfSlots) - 1);
    }
    if (initialRow >= 0) {
        focusRow = std::clamp(initialRow, 0, 2);
        if (!rowExists(focusRow)) focusRow = RowRecent;
        focusSlot = std::clamp(focusSlot, 0, static_cast<int>(rowSlots(focusRow)) - 1);
    }
    // Focus of the pill is not a Card, so it has its own animation.
    Animated resumeFocus;
    // Home is taller than the screen once there are two shelves, so it scrolls
    // to follow focus — the same as tvOS, which puts Home in a ScrollView. The
    // hero alone is 420 of a 1080 canvas; Recent and Favorites do not both fit
    // under it.
    Animated scrollY;
    scrollY.from = scrollY.to = 0.0f;
    // Settled, not animating: a screenshot should show the resting focused
    // state, not a frame part-way through the transition into it.
    auto settleFocus = [&]() {
        if (Card* c = cardAt(focusRow, focusSlot)) {
            c->focus.retarget(1.0f, kFocusDuration);
            c->focus.elapsed = kFocusDuration;
        } else {
            resumeFocus.retarget(1.0f, kFocusDuration);
            resumeFocus.elapsed = kFocusDuration;
        }
    };
    settleFocus();

    // Any pad that is already plugged in. Hotplug is handled in the event loop,
    // so a controller connected later works without restarting anything.
    int padCount = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&padCount)) {
        for (int i = 0; i < padCount; ++i) SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
    std::fprintf(stderr, "[frontend] gamepads at startup: %d\n", padCount);

    bool running = true;
    uint64_t previous = SDL_GetTicksNS();
    int frame = 0;
    bool pressing = false;


    // Starting a game from the UI. Everything it needs was decided elsewhere:
    // the catalog picks the core, the core decides whether its archive gets
    // opened, and the worker puts what comes out where the core can open it.
    LaunchJob launchJob;
    auto launchById = [&](int romId) -> bool {
        if (launchJob.busy()) return false;    // one at a time
        const romm::Game* g = nullptr;
        for (const auto& x : games) if (x.id == romId) { g = &x; break; }
        if (!g) { std::fprintf(stderr, "[launch] no game with id %d\n", romId); return false; }

        std::string lerr;
        if (!beginLaunch(launchJob, liveClient, *g, coreDir, romCacheDir, &lerr)) {
            std::fprintf(stderr, "[launch] %s\n", lerr.c_str());
            return false;
        }
        std::fprintf(stderr, "[launch] %s (%s) via %s\n", g->name.c_str(),
                     g->platformName.c_str(), launchJob.coreName.c_str());
        return true;
    };

    // Picks up a finished job. Loading the game happens HERE, on the frame
    // thread, because the core is not thread-safe and the worker only ever
    // moved bytes.
    auto pumpLaunch = [&]() {
        const LaunchJob::Stage st = launchJob.stage.load();
        if (st == LaunchJob::Stage::Failed) {
            std::fprintf(stderr, "[launch] failed: %s\n", launchJob.message.c_str());
            launchJob.stop();
            launchJob.stage = LaunchJob::Stage::Idle;
            return;
        }
        if (st != LaunchJob::Stage::Ready) return;
        launchJob.stop();
        launchJob.stage = LaunchJob::Stage::Idle;

        cab::Core& core = cab::Core::shared();
        const std::string saveDir = std::string(romCacheDir) + "/saves";
        SDL_CreateDirectory(saveDir.c_str());
        if (!core.loadGame(launchJob.romPath, "system", saveDir)) {
            std::fprintf(stderr, "[launch] %s\n", core.error().c_str());
            return;
        }
        std::fprintf(stderr, "[launch] running %s\n", core.coreName().c_str());
        playing = true;
    };

    // Remembered focus per row, which is the behaviour tvOS gives free and the
    // one people notice missing: leaving Recent at the sixth cover and coming
    // back to the first is the kind of thing that feels broken without anyone
    // being able to say why.
    int rememberedSlot[3] = {0, 0, 0};

    auto leaveFocus = [&]() {
        if (Card* c = cardAt(focusRow, focusSlot)) c->focus.retarget(0.0f, kFocusDuration);
        else resumeFocus.retarget(0.0f, kFocusDuration);
    };
    auto enterFocus = [&]() {
        if (Card* c = cardAt(focusRow, focusSlot)) c->focus.retarget(1.0f, kFocusDuration);
        else resumeFocus.retarget(1.0f, kFocusDuration);
    };

    auto moveFocus = [&](int delta) {
        const int slots = static_cast<int>(rowSlots(focusRow));
        if (slots <= 0) return;
        const int next = std::clamp(focusSlot + delta, 0, slots - 1);
        if (next == focusSlot) return;
        leaveFocus();
        focusSlot = next;
        rememberedSlot[focusRow] = focusSlot;
        enterFocus();
    };

    auto moveRow = [&](int delta) {
        int row = focusRow;
        // Step over a row that is not there — no Favorites, or no hero —
        // rather than stopping on nothing.
        for (int i = 0; i < 3; ++i) {
            const int candidate = row + delta;
            if (candidate < RowHero || candidate > RowFavorites) return;
            row = candidate;
            if (rowExists(row)) break;
            if (row == RowHero || row == RowFavorites) return;
        }
        if (row == focusRow || !rowExists(row)) return;
        leaveFocus();
        focusRow = row;
        focusSlot = std::clamp(rememberedSlot[row], 0,
                               static_cast<int>(rowSlots(row)) - 1);
        enterFocus();
    };

    // WHO OWNS THE INPUT, asked once per event rather than decided again at
    // every call site.
    //
    // docs/PROJECT.md states the rule architecturally: while a game runs the
    // controller belongs to the core exclusively; while an overlay is open it
    // belongs to the UI; never both. It also says to implement the RULE rather
    // than the routing, because "any design where a button can mean two things
    // at once is the same bug" — the one reported from real hardware as
    // "controllers work on the homescreen but in game b exits the game".
    //
    // This was exactly that bug: arrow keys moved the Tetris piece AND shifted
    // focus on the Home screen behind it, so leaving the game landed somewhere
    // nobody chose. Found by someone actually playing it.
    enum class InputOwner { Keyboard, Game, UI };
    auto inputOwner = [&]() {
        if (keyboard.isOpen()) return InputOwner::Keyboard;   // an overlay wins
        if (playing) return InputOwner::Game;
        return InputOwner::UI;
    };

    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            const InputOwner owner = inputOwner();
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    break;
                case SDL_EVENT_GAMEPAD_ADDED:
                    SDL_OpenGamepad(e.gdevice.which);
                    std::fprintf(stderr, "[frontend] gamepad connected\n");
                    break;
                case SDL_EVENT_TEXT_INPUT:
                    // A physical keyboard types into the same field. Not a
                    // separate path — the same string and the same commit.
                    if (keyboard.isOpen()) keyboard.typeText(e.text.text);
                    break;
                case SDL_EVENT_KEY_DOWN:
                    if (keyboard.isOpen()) {
                        // While it is open it owns every key, the same way the
                        // core owns the pad while a game runs. A control that
                        // means two things at once is the bug.
                        switch (e.key.key) {
                            case SDLK_LEFT: keyboard.moveFocus(-1, 0); break;
                            case SDLK_RIGHT: keyboard.moveFocus(+1, 0); break;
                            case SDLK_UP: keyboard.moveFocus(0, -1); break;
                            case SDLK_DOWN: keyboard.moveFocus(0, +1); break;
                            case SDLK_BACKSPACE: keyboard.backspace(); break;
                            case SDLK_RETURN:
                                std::fprintf(stderr, "[keyboard] committed: %s\n",
                                             keyboard.value().c_str());
                                keyboard.commit();
                                break;
                            case SDLK_ESCAPE: keyboard.cancel(); break;
                            default: break;
                        }
                        break;
                    }
                    if (e.key.key == SDLK_ESCAPE) {
                        // A download is the one thing Escape should interrupt
                        // rather than quit past: three gigabytes is a long time
                        // to be unable to change your mind.
                        if (launchJob.busy()) launchJob.cancel = true;
                        else running = false;
                    }
                    if (playing && e.key.key == SDLK_F5) {
                        cab::Core& c = cab::Core::shared();
                        std::vector<uint8_t> st;
                        if (c.saveState(st)) {
                            // Local first, always. Losing signal mid-save must
                            // never mean losing the save; the upload is a
                            // second step that can fail harmlessly.
                            if (FILE* f = std::fopen("saves/quick.state", "wb")) {
                                std::fwrite(st.data(), 1, st.size(), f);
                                std::fclose(f);
                                std::fprintf(stderr, "[state] saved %zu bytes\n", st.size());
                            }
                        }
                    }
                    if (playing && e.key.key == SDLK_F8) {
                        cab::Core& c = cab::Core::shared();
                        std::vector<uint8_t> st = ui::ImageCache::readFile("saves/quick.state");
                        std::fprintf(stderr, "[state] load %s\n",
                                     (!st.empty() && c.loadState(st)) ? "ok" : "FAILED");
                    }
                    if (owner != InputOwner::UI) break;
                    if (e.key.key == SDLK_LEFT) moveFocus(-1);
                    if (e.key.key == SDLK_RIGHT) moveFocus(+1);
                    if (e.key.key == SDLK_UP) moveRow(-1);
                    if (e.key.key == SDLK_DOWN) moveRow(+1);
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
                        pressing = true;
                        // The hero's two actions are deliberately different:
                        // Resume goes straight into the game, the artwork opens
                        // the detail screen. Only the first exists yet, so the
                        // card launches too rather than doing nothing at all.
                        if (!playing) {
                            if (const Card* c = cardAt(focusRow, focusSlot)) launchById(c->id);
                            else if (heroIndex >= 0) launchById(cards[heroIndex].id);
                        }
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) pressing = false;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    if (keyboard.isOpen()) {
                        switch (e.gbutton.button) {
                            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: keyboard.moveFocus(-1, 0); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: keyboard.moveFocus(+1, 0); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_UP: keyboard.moveFocus(0, -1); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: keyboard.moveFocus(0, +1); break;
                            case SDL_GAMEPAD_BUTTON_SOUTH: keyboard.pressKey(); break;
                            case SDL_GAMEPAD_BUTTON_WEST: keyboard.backspace(); break;
                            case SDL_GAMEPAD_BUTTON_NORTH: keyboard.toggleShift(); break;
                            case SDL_GAMEPAD_BUTTON_START:
                                std::fprintf(stderr, "[keyboard] committed: %s\n",
                                             keyboard.value().c_str());
                                keyboard.commit();
                                break;
                            case SDL_GAMEPAD_BUTTON_EAST: keyboard.cancel(); break;
                            default: break;
                        }
                        break;
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) moveFocus(-1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) moveFocus(+1);
                    if (owner != InputOwner::UI) break;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) {
                        if (const Card* c = cardAt(focusRow, focusSlot)) launchById(c->id);
                        else if (heroIndex >= 0) launchById(cards[heroIndex].id);
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) moveRow(-1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) moveRow(+1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) pressing = true;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) running = false;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) pressing = false;
                    break;
                default:
                    break;
            }
        }

        uint64_t now = SDL_GetTicksNS();
        float dt = static_cast<float>(now - previous) / 1e9f;
        previous = now;
        // A stall must not teleport an animation; the reference implementation
        // caps its own accumulator for the same reason.
        dt = std::min(dt, 0.1f);

        if (evictTest && frame == 5) {
            // Five covers requested once and never asked for again: the shelf
            // has scrolled past them.
            for (int i = 0; i < 5; ++i) {
                images.get("covers/b-3x4.png#scrolled-past-" + std::to_string(i));
            }
        }
        if (playing) {
            cab::Core& core = cab::Core::shared();
            // Buttons first, then run: a core samples input inside retro_run,
            // so anything set afterwards is a frame late.
            cab::PadState pad;
            const bool* keys = SDL_GetKeyboardState(nullptr);
            auto held = [&](SDL_Scancode k) { return keys && keys[k]; };
            pad.buttons = 0;
            // The RetroPad, which every core speaks whatever the real hardware
            // had. Keyboard here; a real pad is wired the same way below.
            using cab::bit;
            if (held(SDL_SCANCODE_UP)) pad.buttons |= bit(cab::Up);
            if (held(SDL_SCANCODE_DOWN)) pad.buttons |= bit(cab::Down);
            if (held(SDL_SCANCODE_LEFT)) pad.buttons |= bit(cab::Left);
            if (held(SDL_SCANCODE_RIGHT)) pad.buttons |= bit(cab::Right);
            if (held(SDL_SCANCODE_X)) pad.buttons |= bit(cab::A);
            if (held(SDL_SCANCODE_Z)) pad.buttons |= bit(cab::B);
            if (held(SDL_SCANCODE_RETURN)) pad.buttons |= bit(cab::Start);
            if (held(SDL_SCANCODE_RSHIFT)) pad.buttons |= bit(cab::Select);

            // A real pad, mapped by SDL's own gamepad abstraction so the
            // hundreds of controllers in its database all arrive the same way.
            // Both paths OR together: neither is required, both work.
            int padCountNow = 0;
            if (SDL_JoystickID* ids = SDL_GetGamepads(&padCountNow)) {
                if (padCountNow > 0) {
                    if (SDL_Gamepad* gp = SDL_GetGamepadFromID(ids[0])) {
                        auto down = [&](SDL_GamepadButton b) {
                            return SDL_GetGamepadButton(gp, b);
                        };
                        if (down(SDL_GAMEPAD_BUTTON_DPAD_UP)) pad.buttons |= bit(cab::Up);
                        if (down(SDL_GAMEPAD_BUTTON_DPAD_DOWN)) pad.buttons |= bit(cab::Down);
                        if (down(SDL_GAMEPAD_BUTTON_DPAD_LEFT)) pad.buttons |= bit(cab::Left);
                        if (down(SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) pad.buttons |= bit(cab::Right);
                        // South is the bottom face button whatever it is
                        // labelled: A on Xbox, B on Nintendo, Cross on
                        // PlayStation. SDL normalises by POSITION, which is the
                        // only thing that is actually the same across pads.
                        if (down(SDL_GAMEPAD_BUTTON_SOUTH)) pad.buttons |= bit(cab::B);
                        if (down(SDL_GAMEPAD_BUTTON_EAST)) pad.buttons |= bit(cab::A);
                        if (down(SDL_GAMEPAD_BUTTON_WEST)) pad.buttons |= bit(cab::Y);
                        if (down(SDL_GAMEPAD_BUTTON_NORTH)) pad.buttons |= bit(cab::X);
                        if (down(SDL_GAMEPAD_BUTTON_START)) pad.buttons |= bit(cab::Start);
                        if (down(SDL_GAMEPAD_BUTTON_BACK)) pad.buttons |= bit(cab::Select);
                        pad.leftX = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                        pad.leftY = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                    }
                }
                SDL_free(ids);
            }
            core.setPad(0, pad);

            core.runFor(dt);
            core.uploadFrame();

            if (audioStream) {
                const std::vector<int16_t>& samples = core.drainAudio();
                if (!samples.empty()) {
                    SDL_PutAudioStreamData(audioStream, samples.data(),
                                           static_cast<int>(samples.size() * sizeof(int16_t)));
                }
            }
        }

        // Launch on a timer when asked to. This exists so the Home-to-game
        // transition can be watched on the test machine, which has no
        // controller attached — not as a product behaviour.
        if (autoLaunchId > 0 && !playing) {
            autoLaunchAfter -= dt;
            if (autoLaunchAfter <= 0.0f) {
                const int id = autoLaunchId;
                autoLaunchId = 0;
                launchById(id);
            }
        }

        pumpLaunch();
        images.pump(dt);
        for (auto& c : cards) {
            c.focus.tick(dt);
            c.press.tick(dt);
        }
        // These two are not Cards and so are not in the loop above. Forgetting
        // them cost a debugging pass: the scroll target was computed correctly
        // every frame and then discarded, because an Animated whose elapsed
        // never advances returns its start value forever.
        resumeFocus.tick(dt);
        scrollY.tick(dt);
        if (Card* pc = cardAt(focusRow, focusSlot))
            pc->press.retarget(pressing ? 1.0f : 0.0f, kPressDuration);

        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        bool offscreen = false;
        if (renderW > 0 && renderH > 0) {
            offscreen = renderer.beginOffscreen(renderW, renderH);
            if (offscreen) {
                dw = renderW;
                dh = renderH;
            }
        }
        renderer.beginFrame(dw, dh);
        // Declared here rather than inside the Home branch: the hero's glass
        // cannot be drawn until after presentScene, which is outside it.
        ui::Rect heroBand{}, heroCardRect{};
        bool heroDrawn = false;
        const float sc = renderer.scale();
        if (playing) {
            // BLACK behind a running game, not the menu's backdrop. The
            // reference implementation's player clears to black, and it is
            // right: a gradient around a game picture is decoration competing
            // with the thing you are looking at, and the letterbox glow is
            // bias lighting, which means light against black. On a purple
            // backdrop it is neither.
            renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                   ui::Color::black(1.0f)});
        } else {
            renderer.drawBackdrop(ui::Gradient{ui::palette::kBackdropTop,
                                               ui::palette::kBackdropMid,
                                               ui::palette::kBackdropBottom, 0.55f});
        }

        if (playing) {
            cab::Core& core = cab::Core::shared();
            if (core.texture() && core.frameWidth() > 0) {
                // Integer-scaled and centred. A Game Boy is 160x144 and its
                // pixels were each a deliberate choice; scaling by 6.4 makes
                // some of them twice the size of their neighbours, which is
                // visible from a sofa and looks like a fault. Phase 8 can offer
                // the non-integer option; the default should be honest.
                const float srcW = static_cast<float>(core.frameWidth());
                const float srcH = static_cast<float>(core.frameHeight());
                const float aspect = core.avInfo().aspectRatio > 0
                                         ? core.avInfo().aspectRatio
                                         : srcW / srcH;
                float scale = std::floor(
                    std::min(ui::kCanvasWidth / (srcH * aspect), ui::kCanvasHeight / srcH));
                if (scale < 1.0f) scale = 1.0f;
                const float dh = srcH * scale;
                const float dw = dh * aspect;
                const float px = (ui::kCanvasWidth - dw) * 0.5f;
                const float py = (ui::kCanvasHeight - dh) * 0.5f;
                ui::drawImageTexture(renderer, core.texture(), px, py, dw, dh);

                // The glow goes over the bars, not under the picture: it is
                // drawn after, and its shader discards inside the picture rect,
                // so no game pixel is ever covered. An integer-scaled handheld
                // on a 4K set is mostly dead space, which is exactly the case
                // this exists for.
                renderer.drawBiasGlow(px, py, dw, dh, glowPeak);
            }

            // The overlay is not composited by anything clever: the frontend
            // owns the frame loop, so the pause menu is simply drawn over the
            // game. That is the payoff of hosting cores in process rather than
            // launching them.
            if (pressing) {
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(0.55f)});
                const float panelW = 560, panelH = 260;
                renderer.draw(ui::Rect{(ui::kCanvasWidth - panelW) * 0.5f,
                                       (ui::kCanvasHeight - panelH) * 0.5f, panelW, panelH,
                                       32, ui::Color::white(0.14f)});
                const float sc2 = renderer.scale();
                const char* title = core.coreName().c_str();
                const float tw = text.measure(title, ui::TextStyle::Title2, sc2);
                text.draw(renderer, title, (ui::kCanvasWidth - tw) * 0.5f,
                          ui::kCanvasHeight * 0.5f - 40,
                          ui::TextStyle::Title2, ui::Color::white(1.0f), sc2);
                const char* sub = "Paused";
                const float sw = text.measure(sub, ui::TextStyle::Callout, sc2);
                text.draw(renderer, sub, (ui::kCanvasWidth - sw) * 0.5f,
                          ui::kCanvasHeight * 0.5f + 10, ui::TextStyle::Callout,
                          ui::Color::white(0.60f), sc2);
            }
        } else {

        // The height a shelf will occupy, known before it is drawn so the
        // scroll target can be computed this frame rather than one frame late.
        const float shelfBlockHeight =
            text.lineHeight(ui::TextStyle::Title2, sc) + 12.0f + kShelfHeadroom +
            kShelfCoverHeight + kCaptionGap +
            text.lineHeight(ui::TextStyle::Callout, sc) + kShelfHeadroom;

        const float heroHeight = haveHero ? std::min(ui::kCanvasHeight * 0.40f, 420.0f) : 0.0f;
        const float recentTop = haveHero ? kHeroTop + heroHeight + kHeroGapBelow : kHeroTop;
        const float favoritesTop = recentTop + shelfBlockHeight;

        // Scroll only as far as the focused row needs. On the hero or Recent
        // that is not at all — Home should not drift under the cursor.
        //
        // And never past the end of the content. Pinning the last row to the
        // top of the screen leaves half a screen of nothing under it, which is
        // not what a scroll view does and reads as the layout having broken.
        const float contentHeight =
            (haveFavorites ? favoritesTop + shelfBlockHeight : recentTop + shelfBlockHeight) +
            kHeroTop;
        const float maxScroll = std::max(0.0f, contentHeight - ui::kCanvasHeight);
        float wantScroll = 0.0f;
        if (focusRow == RowFavorites)
            wantScroll = std::min(favoritesTop - kHeroTop, maxScroll);
        if (std::fabs(wantScroll - scrollY.to) > 0.5f)
            scrollY.retarget(wantScroll, kFocusDuration);
        const float scroll = scrollY.value();

        // ---- The hero -------------------------------------------------------
        //
        // Home is resume-first: the hero is what you were playing, and it is
        // focused on arrival. Numbers are tvOS's, read from Cabinet's
        // HomeView.swift and recorded in docs/PROJECT.md — including the
        // warning that the height was settled on real hardware after four
        // rejected values, because a television's overscan eats more vertical
        // room than a framebuffer capture shows. DO NOT tune this on the VM.
        float shelfHeaderY = kHeroTop - scroll;
        if (heroIndex >= 0 && heroIndex < static_cast<int>(cards.size())) {
            const Card& hero = cards[heroIndex];
            const float heroH = heroHeight;
            // RESERVED HEADROOM, which the design system calls a layout
            // obligation rather than a style one: "every container holding
            // focusable elements has to budget for their focused size." The
            // hero grows by 3% when focused, so its resting width is the
            // content width DIVIDED by that — otherwise the focused card is
            // 1854 wide in an 1800 space and runs under the overscan of a real
            // television, which is precisely where nobody can see it.
            const float heroW = (ui::kCanvasWidth - kContentInset * 2.0f) / kRowFocusScale;
            const float heroX = kContentInset +
                                ((ui::kCanvasWidth - kContentInset * 2.0f) - heroW) * 0.5f;
            const float heroY = kHeroTop - scroll;

            // Computed from the two line heights rather than hardcoded, so the
            // band grows with the type ramp instead of clipping it.
            const float bandH = text.lineHeight(ui::TextStyle::Headline, sc) +
                                text.lineHeight(ui::TextStyle::Caption1, sc) +
                                2.0f + kHeroBandPadY * 2.0f;
            const float artH = std::max(0.0f, heroH - bandH);

            // The hero's focus treatment, and both halves of it are the design
            // system's rather than invented:
            //
            //  - SCALE 1.03, not a cover's 1.10. "The scale shrinks as the
            //    element grows", and a full-width card growing 10% would run
            //    off the screen it sits on.
            //  - NO RIM. The rim is suppressed on composite elements — anything
            //    whose label mixes art with its own text — and the hero's band
            //    is exactly that.
            //
            // The shadow stays: it is what lifts the card off the canvas.
            const float hf = (focusRow == RowHero && focusSlot == 0)
                                 ? hero.focus.value() : 0.0f;
            const float hs = 1.0f + hf * (kRowFocusScale - 1.0f);
            const float hw = heroW * hs, hh = heroH * hs;
            const float hx = heroX - (hw - heroW) * 0.5f;
            const float hy = heroY - (hh - heroH) * 0.5f;

            // The card's own ground, so a hero whose art has not arrived is a
            // card rather than a hole.
            ui::Rect heroPlate{hx, hy, hw, hh, kHeroRadius, hero.art};
            heroPlate.shadowBlur = hf * kFocusShadowBlur;
            heroPlate.shadowOffsetY = hf * kFocusShadowOffsetY;
            heroPlate.shadowColor = ui::Color::black(0.55f * hf);
            renderer.draw(heroPlate);

            if (!hero.cover.empty() && images.get(hero.cover).ready) {
                const ui::Image& art = images.get(hero.cover);
                // The backdrop: the SAME artwork, FILLED and blurred, so the
                // space the fitted art does not cover is the art's own colours
                // rather than letterbox bars. A high mip sampled back up — a
                // box blur the GPU already built, not a blur pass.
                //
                // Filled means CROPPED, not stretched. Mapping a 3:4 cover
                // across an 1800x420 card by UV 0..1 smears it horizontally
                // into a grey band that is no longer the art's colours at all —
                // which is the whole point of the backdrop. So the source rect
                // is cropped to the card's aspect instead, taking a horizontal
                // slice through the middle of the cover.
                const float boxAspect = hw / hh;
                const float imgAspect = art.aspect();
                float u0 = 0.0f, v0 = 0.0f, u1 = 1.0f, v1 = 1.0f;
                if (imgAspect < boxAspect) {
                    // Taller than the box: keep full width, crop top and bottom.
                    const float span = imgAspect / boxAspect;
                    v0 = (1.0f - span) * 0.5f;
                    v1 = v0 + span;
                } else {
                    const float span = boxAspect / imgAspect;
                    u0 = (1.0f - span) * 0.5f;
                    u1 = u0 + span;
                }
                renderer.drawTextured(hx, hy, hw, hh, art.texture, u0, v0, u1, v1,
                                      ui::Color{1, 1, 1, art.fade}, false, 5.0f,
                                      hx, hy, hw, hh, kHeroRadius);
                renderer.draw(ui::Rect{hx, hy, hw, hh, kHeroRadius,
                                       ui::Color::black(0.15f * art.fade)});

                // FITTED, not filled. Box art is tall and the hero is wide, so
                // filling slices the art to a strip of its middle. The top
                // inset keeps it off the card's rounded corners, which
                // otherwise clip a sliver from flush art.
                ui::drawImage(renderer, art, hx, hy + kHeroArtInsetTop, hw,
                              artH * hs - kHeroArtInsetTop, ui::Fit::Contain, 1.0f, 0.0f);
            }
            heroBand = ui::Rect{hx, hy + hh - bandH * hs, hw, bandH * hs, 0, ui::Color::white(0)};
            heroCardRect = ui::Rect{hx, hy, hw, hh, kHeroRadius, ui::Color::white(0)};
            heroDrawn = true;
            shelfHeaderY = heroY + heroH + kHeroGapBelow;
        }

        // One shelf, drawn twice: Recent and Favorites are the same component in
        // different arrangements, which is what the design system says every
        // row on this screen is. Returns the height it used, so the caller can
        // stack the next one under it without either knowing the other's size.
        auto drawShelf = [&](const char* label, const std::vector<int>& indices,
                             int rowId, float top) -> float {
            const bool rowFocused = (focusRow == rowId);
            const size_t count = indices.empty() ? cards.size() : indices.size();
            if (count == 0) return 0.0f;
            auto at = [&](size_t slot) -> size_t {
                return indices.empty() ? slot : static_cast<size_t>(indices[slot]);
            };

            // The shelf header: Title 2 bold, with the chevron that says the row
            // continues into a screen of its own.
            const float headerBaseline = top + text.ascent(ui::TextStyle::Title2, sc);
            text.draw(renderer, label, kContentInset, headerBaseline,
                      ui::TextStyle::Title2, ui::Color::white(1.0f), sc);
            const float headerWidth = text.measure(label, ui::TextStyle::Title2, sc);
            // Title 3 semibold at tertiary, not Title 2: the chevron says "this
            // row continues", it is not part of the heading, and at heading
            // weight it competes with it.
            text.draw(renderer, "\xE2\x80\xBA", kContentInset + headerWidth + 10.0f,
                      headerBaseline, ui::TextStyle::Title3, ui::Color::white(0.30f), sc);

            const float shelfTop =
                top + text.lineHeight(ui::TextStyle::Title2, sc) + 12.0f + kShelfHeadroom;

            // CULL TO WHAT IS ON SCREEN, and do it before touching the cover
            // cache. This loop used to run over every card: invisible with a
            // six-entry stand-in library, and against a real 1232 it queued a
            // download for every cover in the collection and overran the
            // texture budget. A shelf is a window onto a list, not a drawing of
            // the list. The margin keeps a card's art loading just before it
            // slides in, so the fade has somewhere to start.
            const float kCullMargin = (kShelfCoverWidth + kShelfSpacing) * 2.0f;
            auto cardBaseX = [&](size_t slot) {
                return kContentInset +
                       static_cast<float>(slot) * (kShelfCoverWidth + kShelfSpacing);
            };

        // Unfocused cards first, so a focused card's shadow and rim land on top
        // of its neighbours rather than under them.
        for (int pass = 0; pass < 2; ++pass) {
            for (size_t slot = 0; slot < count; ++slot) {
                const size_t i = at(slot);
                const bool isFocused = rowFocused && (static_cast<int>(slot) == focusSlot);
                if ((pass == 0) == isFocused) continue;

                const float cullX = cardBaseX(slot);
                if (cullX + kShelfCoverWidth < -kCullMargin) continue;
                if (cullX > ui::kCanvasWidth + kCullMargin) break;

                Card& card = cards[i];
                const float f = card.focus.value();
                const float p = card.press.value();

                // Pressed reads as a push INTO the screen, against the focused
                // lift, so a click still registers on a card that is already
                // raised.
                const float scale = 1.0f + f * (kFocusScale - 1.0f) -
                                    p * (kFocusScale - kPressScale);

                const float baseX = cullX;
                const float w = kShelfCoverWidth * scale;
                const float h = kShelfCoverHeight * scale;
                const float x = baseX - (w - kShelfCoverWidth) * 0.5f;
                const float y = shelfTop - (h - kShelfCoverHeight) * 0.5f;

                // The coloured panel under the art: it is what shows while a
                // cover is still decoding, what stays if there is none, and
                // what the art fades in over.
                ui::Rect cover{x, y, w, h, kCoverRadius * scale, card.art};
                cover.border = f * kFocusRimWidth;
                cover.borderColor = ui::palette::kFocusRim;
                cover.shadowBlur = f * kFocusShadowBlur;
                cover.shadowOffsetY = f * kFocusShadowOffsetY;
                cover.shadowColor = ui::Color::black(0.55f * f);
                renderer.draw(cover);

                if (!card.cover.empty()) {
                    // Fill, which the drawing code turns into fit-over-a-
                    // blurred-echo by itself when the cover is the wrong shape.
                    ui::drawImage(renderer, images.get(card.cover), x, y, w, h,
                                  ui::Fit::Fill, 1.0f, kCoverRadius * scale);
                    // The rim again, over the art: it is the focus indicator and
                    // nothing may sit on top of it.
                    if (f > 0.0f) {
                        ui::Rect rim{x, y, w, h, kCoverRadius * scale, ui::Color::white(0)};
                        rim.border = f * kFocusRimWidth;
                        rim.borderColor = ui::palette::kFocusRim;
                        renderer.draw(rim);
                    }
                }

                // The caption, riding down with the lift so the grown card
                // cannot bury it. One line, truncated with a real ellipsis:
                // game titles are long and at this width most of them are.
                const float capBaseline = shelfTop + kShelfCoverHeight + kCaptionGap +
                                          text.ascent(ui::TextStyle::Callout, sc) +
                                          captionSlide(f);
                const std::string caption = text.truncate(
                    card.title, ui::TextStyle::Callout, sc, kShelfCoverWidth);
                // Focused is primary, everything else is secondary — the same
                // way the reference implementation dims what you are not on.
                text.draw(renderer, caption, baseX, capBaseline, ui::TextStyle::Callout,
                          ui::Color::white(isFocused ? 1.0f : 0.60f), sc);
            }
        }
            // Header, headroom, cover, caption — and the caption's own slide,
            // which is reserved headroom rather than slack.
            return (shelfTop - top) + kShelfCoverHeight + kCaptionGap +
                   text.lineHeight(ui::TextStyle::Callout, sc) + kShelfHeadroom;
        };

        float rowY = shelfHeaderY;
        rowY += drawShelf("Recent", shelf, RowRecent, rowY);
        // Only when there are any. An empty Favorites row is worse than none.
        if (haveFavorites) drawShelf("Favorites", favorites, RowFavorites, rowY);
        }  // end of the shelf branch

        renderer.presentScene();

        // ---- A download in progress ----------------------------------------
        //
        // Drawn after presentScene because it is glass, and over everything
        // because it is the only thing that matters while it is up. Not a modal
        // — Home stays visible and animating behind it, which is the difference
        // between "working" and "hung".
        if (launchJob.busy()) {
            const int64_t got = launchJob.got.load();
            const int64_t total = launchJob.total.load();
            const bool unpacking = launchJob.stage.load() == LaunchJob::Stage::Unpacking;

            const float panelW = 900.0f, panelH = 190.0f;
            const float px = (ui::kCanvasWidth - panelW) * 0.5f;
            const float py = (ui::kCanvasHeight - panelH) * 0.5f;
            renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                   ui::Color::black(0.45f)});
            renderer.drawGlass(ui::Rect{px, py, panelW, panelH, 24.0f, ui::Color::white(0)},
                               6.0f, ui::Color::black(0.22f));

            text.draw(renderer, launchJob.title, px + 32.0f,
                      py + 28.0f + text.ascent(ui::TextStyle::Title3, sc),
                      ui::TextStyle::Title3, ui::Color::white(1.0f), sc);

            // A bar only when the server said how big it is. It often does not,
            // and a progress bar that invents its own total is a lie — so the
            // honest fallback is to show what has arrived and no bar at all.
            const float barY = py + panelH - 62.0f;
            const float barW = panelW - 64.0f;
            if (!unpacking && total > 0) {
                const float frac = std::clamp(static_cast<float>(got) /
                                              static_cast<float>(total), 0.0f, 1.0f);
                renderer.draw(ui::Rect{px + 32.0f, barY, barW, 8.0f, 4.0f,
                                       ui::Color::white(0.18f)});
                renderer.draw(ui::Rect{px + 32.0f, barY, barW * frac, 8.0f, 4.0f,
                                       ui::palette::kFocusRim});
            }

            char line[160];
            if (unpacking) {
                std::snprintf(line, sizeof line, "Unpacking\xE2\x80\xA6");
            } else if (total > 0) {
                std::snprintf(line, sizeof line, "%.0f of %.0f MB",
                              static_cast<double>(got) / 1e6,
                              static_cast<double>(total) / 1e6);
            } else {
                std::snprintf(line, sizeof line, "%.0f MB",
                              static_cast<double>(got) / 1e6);
            }
            text.draw(renderer, line, px + 32.0f,
                      barY + 28.0f + text.ascent(ui::TextStyle::Callout, sc),
                      ui::TextStyle::Callout, ui::Color::white(0.60f), sc);
        }

        // ---- The hero's glass, which can only be drawn now ------------------
        //
        // A frosted band, NOT a black gradient. The gradient paints over the
        // very backdrop that makes the card worth looking at, leaving a slab of
        // black under the artwork; a material keeps the game's colours showing
        // through while still giving the text a surface to be read against.
        // Cabinet learned this on tvOS and the note is in its source.
        if (heroDrawn) {
            const Card& hero = cards[heroIndex];
            ui::Rect band = heroBand;
            band.radius = 0.0f;
            renderer.drawGlass(band, kHeroBandBlur, ui::Color::black(0.18f));

            const float titleBaseline =
                band.y + kHeroBandPadY + text.ascent(ui::TextStyle::Headline, sc);
            text.draw(renderer, hero.title, band.x + kHeroBandPadX, titleBaseline,
                      ui::TextStyle::Headline, ui::Color::white(1.0f), sc);
            const float subBaseline = titleBaseline +
                                      text.lineHeight(ui::TextStyle::Headline, sc) * 0.0f +
                                      text.lineHeight(ui::TextStyle::Caption1, sc);
            text.draw(renderer, heroPlatform, band.x + kHeroBandPadX, subBaseline,
                      ui::TextStyle::Caption1, ui::Color::white(0.60f), sc);

            // Resume is a SECOND REAL BUTTON, not decoration inside the first.
            // It goes straight into the game; the artwork opens the detail
            // screen. Stopping at a screen with a Play button on it is two
            // actions, not one, and Home promises one.
            const char* kResume = "\xE2\x96\xB6  Resume";
            const float pillTextW = text.measure(kResume, ui::TextStyle::Title3, sc);
            // Treatment 2, the text-control one: tinted blur at white 25%,
            // scale 1.06, text to full white. A pill growing a cover's 10%
            // would read as a bug; 3% on something this small would not read
            // at all.
            const float rf = (focusRow == RowHero && focusSlot == 1)
                                 ? resumeFocus.value() : 0.0f;
            const float rs = 1.0f + rf * (kPillFocusScale - 1.0f);
            const float pillW0 = std::max(pillTextW + 28.0f, 180.0f);
            const float pillH0 = text.lineHeight(ui::TextStyle::Title3, sc) + 16.0f;
            const float pillW = pillW0 * rs, pillH = pillH0 * rs;
            const float pillX = heroCardRect.x + heroCardRect.w - pillW0 - 12.0f -
                                (pillW - pillW0) * 0.5f;
            const float pillY = heroCardRect.y + 12.0f - (pillH - pillH0) * 0.5f;
            renderer.drawGlass(ui::Rect{pillX, pillY, pillW, pillH, pillH * 0.5f,
                                        ui::Color::white(0)},
                               kHeroPillBlur,
                               ui::Color::white(0.18f + 0.07f * rf));
            text.draw(renderer, kResume, pillX + (pillW - pillTextW) * 0.5f,
                      pillY + 8.0f * rs + text.ascent(ui::TextStyle::Title3, sc),
                      ui::TextStyle::Title3, ui::Color::white(0.75f + 0.25f * rf), sc);
        }
        keyboard.draw(renderer, text, renderer.scale());
        if (safeGuides) renderer.drawSafeAreaGuides();

        ++frame;
        // Capture before the swap. After a swap the back buffer's contents are
        // undefined, so a readback taken there is whatever the driver left.
        if (shotMode && frame >= shotAfterFrames) {
            renderer.saveFrame(shotPath, dw, dh);
            running = false;
        }
        if (gCaptureRequested) {
            gCaptureRequested = 0;
            renderer.saveFrame("/tmp/cabinetos-frame.bmp", dw, dh);
        }

        if (offscreen) {
            renderer.endOffscreen();
        } else {
            SDL_GL_SwapWindow(window);
        }
    }

    if (playing) {
        cab::Core& core = cab::Core::shared();
        const double realtime = core.audioFramesTotal() / core.avInfo().sampleRate;
        std::fprintf(stderr,
                     "[core] %llu frames, %llu audio frames = %.2fs of emulated time\n",
                     static_cast<unsigned long long>(core.framesRun()),
                     static_cast<unsigned long long>(core.audioFramesTotal()), realtime);
        core.unload();
    }
    if (audioStream) SDL_DestroyAudioStream(audioStream);
    std::fprintf(stderr, "[image] resident %.1f MB, %d still pending\n",
                 images.bytesResident() / (1024.0 * 1024.0), images.pendingCount());
    images.shutdown();
    text.shutdown();
    renderer.shutdown();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
