#include "states.h"

#include <dirent.h>
#include <sys/stat.h>

#include <algorithm>
#include <cstdio>
#include <ctime>

#include "core.h"
#include "disc.h"
#include "discs.h"
#include "fileio.h"
#include "image.h"
#include "storage.h"

// A SAVE STATE GOES IN ON ITS OWN DISC (#162 item 9). The disc is in the
// state's name ("(Disc 2)"), so a state from any device carries it; it is
// put in before the state, because only PS1's core restores it by itself.
//
// A CORE WITH NO DRIVE (3DO) CANNOT TAKE ANOTHER DISC'S STATE: the state is
// the machine without its disc, and nothing can put that disc in, so it
// would run on in the wrong disc's data. It is refused and the notice names
// its disc; Insert and restart on that disc, then Load latest state.
static int gStateOnDisc = 0;   // set when a state was refused for its disc
bool loadStateOnItsDisc(GameSession& sess, const std::string& name,
                        const std::vector<uint8_t>& bytes) {
    gStateOnDisc = 0;
    if (!sess.discs.empty() && sess.discChange == disc::Change::Insert) {
        const int want = disc::discInStateName(name);
        if (want > 0 && want != sess.disc) discSwapNow(sess, want, "for the state");
    }
    if (!sess.discs.empty() && sess.discChange == disc::Change::StartOver) {
        const int want = disc::discInStateName(name);
        if (want > 0 && want != sess.disc) {
            std::fprintf(stderr, "[disc] the state is on disc %d, disc %d is in: not loaded\n",
                         want, sess.disc);
            gStateOnDisc = want;
            return false;
        }
    }
    const bool ok = cab::Core::shared().loadState(bytes);
    if (ok && !sess.discs.empty()) syncDiscFromCore(sess);
    return ok;
}

std::string stateRefusedText() {
    return gStateOnDisc > 0 ? "That state is on Disc " + std::to_string(gStateOnDisc)
                            : std::string("Couldn't load that state");
}

void saveStateNow(GameSession& sess, Uploader& up, MenuNotice& notice) {
    cab::Core& core = cab::Core::shared();
    if (sess.discInserting) {
        notice.say("Not while the disc is changing", Tone::Info);
        return;
    }
    std::vector<uint8_t> st;
    if (!core.saveState(st) || st.empty()) {
        std::fprintf(stderr, "[state] this core cannot serialize\n");
        notice.say("No save states for this system", Tone::Info);
        return;
    }
    // Named with a timestamp because states accumulate on purpose; a save
    // overwrites, a state does not.
    //
    // CABINET'S NAME, TO THE CHARACTER (#99, MMagTech 2026-09-26): the game's
    // file name without its extension, then the UTC time to the millisecond
    // with its separators dashed, "Super Mario World (USA) [2026-09-26
    // 17-45-12-345]" (TVPlayerView.swift, stateFileStem). It was the title
    // and local time to the second, so a list of states read differently
    // depending on where each was made. Nothing here reads the name back:
    // the newest local state is found by date.
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm utc{};
    gmtime_r(&ts.tv_sec, &utc);
    char stamp[48];
    const size_t n = std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &utc);
    std::snprintf(stamp + n, sizeof stamp - n, "-%03ld", ts.tv_nsec / 1000000L);
    std::string base = sess.fsStem.empty() ? sanitisedStem(sess.title) : sess.fsStem;
    std::replace(base.begin(), base.end(), '/', '_');
    // AND ITS DISC, on a game with several (#162): "Panzer Dragoon (Disc 2)
    // [...]". It travels with the state, so loading it puts that disc in.
    if (!sess.discs.empty()) base += disc::stateNamePart(sess.disc);
    const std::string stem = base + " [" + stamp + "]";
    const std::string name = stem + ".state";

    storage::makeDirs(sess.stateDir);
    const std::string path = sess.stateDir + "/" + name;
    if (!writeLocal(path, st)) {
        std::fprintf(stderr, "[state] could not write locally, not uploading\n");
        notice.say("Couldn't save the state", Tone::Problem);
        return;
    }
    std::fprintf(stderr, "[state] %zu bytes saved locally\n", st.size());

    // THE PICTURE (#99): the game's own frame, the menu not in it, upright,
    // at the core's resolution, as Cabinet sends. Kept beside the state, so a
    // state that has to wait for the server still has it when it goes. A
    // state without one is still a state: nothing here stops the save.
    std::vector<uint8_t> png;
    std::string shotName, shotPath;
    {
        std::vector<uint8_t> rgba;
        unsigned w = 0, h = 0;
        if (core.snapshot(rgba, w, h) && ui::encodePNG(rgba, w, h, png)) {
            shotName = stem + ".png";
            shotPath = sess.stateDir + "/" + shotName;
            if (!writeLocal(shotPath, png)) shotPath.clear();
            std::fprintf(stderr, "[state] picture %ux%u, %zu bytes\n", w, h, png.size());
        } else {
            png.clear();
            std::fprintf(stderr, "[state] no picture for this state\n");
        }
    }

    if (sess.stateTag.empty()) {
        std::fprintf(stderr, "[state] no settled tag for this core — not uploaded\n");
        // SAVED, and honest about the half that did not happen. A state this
        // console cannot tag is one no other device will be offered, which is
        // worth knowing before somebody relies on it being there.
        notice.say("Saved on this console only", Tone::Info);
        return;
    }
    // NOT "saved" YET. The bytes are on the disk, which is the guarantee that
    // matters, but the sentence a person reads should not claim the server has
    // it before the server has it. The frame loop finishes this sentence when
    // the uploader reports back — see Uploader::stateOutcome.
    notice.say("Saving\xE2\x80\xA6", Tone::Busy);
    Uploader::Job job{sess.romId, sess.stateTag, name, std::move(st), true};
    job.localPath = path;
    job.shotName = shotName;
    job.shot = std::move(png);
    job.shotPath = shotPath;
    up.push(std::move(job));
}

// A SCREENSHOT (#79): the shortcut button and Y. The game's own frame, as a
// state's picture is (the menu is never in it), saved on this console first
// and then sent to this person's gallery on RomM, retried like a save when
// RomM is away. Named as a state is, the game's file name and the UTC time,
// so two never collide. No gallery on the console: they are seen and
// deleted in RomM (decided 2026-09-27).
void screenshotNow(GameSession& sess, Uploader& up, MenuNotice& notice) {
    cab::Core& core = cab::Core::shared();
    std::vector<uint8_t> rgba, png;
    unsigned w = 0, h = 0;
    if (!core.snapshot(rgba, w, h) || !ui::encodePNG(rgba, w, h, png)) {
        std::fprintf(stderr, "[screenshot] no picture to take\n");
        notice.say("No screenshots for this system", Tone::Info);
        return;
    }
    struct timespec ts{};
    clock_gettime(CLOCK_REALTIME, &ts);
    struct tm utc{};
    gmtime_r(&ts.tv_sec, &utc);
    char stamp[48];
    const size_t n = std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", &utc);
    std::snprintf(stamp + n, sizeof stamp - n, "-%03ld", ts.tv_nsec / 1000000L);
    std::string base = sess.fsStem.empty() ? sanitisedStem(sess.title) : sess.fsStem;
    std::replace(base.begin(), base.end(), '/', '_');
    const std::string name = base + " [" + stamp + "].png";
    const std::string dir =
        storage::screenshotsDir(storage::currentUser()) + "/" + std::to_string(sess.romId);
    storage::makeDirs(dir);
    const std::string path = dir + "/" + name;
    if (!writeLocal(path, png)) {
        notice.say("Couldn't save the screenshot", Tone::Problem);
        return;
    }
    std::fprintf(stderr, "[screenshot] %ux%u, %zu bytes, %s\n", w, h, png.size(), name.c_str());
    notice.say("Screenshot saved", Tone::Done);
    Uploader::Job job;
    job.romId = sess.romId;
    job.emulator = "screenshot";
    job.fileName = name;
    job.data = std::move(png);
    job.localPath = path;
    job.isScreenshot = true;
    up.push(std::move(job));
}

static std::string newestLocalState(const GameSession& sess);

// `wantId`: a particular state, picked on the launch screen, rather than the
// newest. Everything else is the same, the fallback included.
void beginLoadLatestState(StateLoad& load, GameSession& sess,
                          romm::Client& client, MenuNotice& notice, int wantId,
                          bool quiet) {
    if (load.running.load()) return;
    if (sess.discInserting) {
        if (!quiet) notice.say("Not while the disc is changing", Tone::Info);
        return;
    }

    load.quiet = quiet;
    if (!quiet) notice.say("Loading\xE2\x80\xA6", Tone::Busy);
    if (sess.stateTag.empty()) {
        // THIS SYSTEM'S STATES NEVER LEAVE THE CONSOLE, so the newest one here
        // IS the latest. Until 2026-09-23 this refused outright — so on NES,
        // SNES, N64 and six others Save state wrote a state that Load latest
        // state would then refuse to find.
        const std::string local = newestLocalState(sess);
        const std::vector<uint8_t> bytes =
            local.empty() ? std::vector<uint8_t>{} : cab::readBytes(local);
        if (bytes.empty()) {
            std::fprintf(stderr, "[state] no settled tag and nothing saved here\n");
            if (!quiet) notice.say("No saved state for this game", Tone::Info);
            return;
        }
        const bool ok = loadStateOnItsDisc(sess, local, bytes);
        std::fprintf(stderr, "[state] local-only %s -> %s\n", local.c_str(),
                     ok ? "restored" : "REFUSED");
        notice.say(ok ? "State loaded" : stateRefusedText(), ok ? Tone::Done : Tone::Problem);
        load.loaded = ok;
        return;
    }
    if (load.worker.joinable()) load.worker.join();
    load.running = true;
    load.ready = false;
    load.serverFailed = false;
    const int romId = sess.romId;
    const std::string tag = sess.stateTag;
    load.worker = std::thread([&load, &client, romId, tag, wantId]() {
        std::vector<romm::Asset> states;
        std::string err;
        if (!client.fetchStates(romId, &states, &err)) {
            // The server could not be reached or would not answer, which is
            // Cabinet's offline case by another name. The newest state on this
            // machine is what is left, and it is better than a refusal.
            load.note = err;
            load.serverFailed = true;
            load.tryLocal = true;
            load.running = false;
            load.ready = true;
            return;
        }
        const romm::Asset* best = nullptr;
        int skipped = 0;
        for (const auto& a : states) {
            // A state from another emulator is SKIPPED, never attempted.
            // Loading one does not fail cleanly: it boots something that looks
            // like the game and is not.
            if (a.emulator != tag) { ++skipped; continue; }
            if (wantId) {
                if (a.id == wantId) best = &a;
                continue;
            }
            if (!best || a.updatedAt > best->updatedAt) best = &a;
        }
        if (!best) {
            load.note = "none for " + tag + " (" + std::to_string(skipped) +
                        " for other emulators)";
            load.tryLocal = true;
        } else {
            load.data = client.fetchAsset("states", best->id);
            load.note = best->fileName;
        }
        load.running = false;
        load.ready = true;
    });
}

// The newest .state file this console wrote for this game, or empty.
//
// THE FALLBACK, NOT THE FIRST CHOICE — settled against the reference 2026-09-21.
//
// MMagTech: *"i just did save on a couple of games and it looked like nothing
// happened even tried loading after saving."* Two faults with one symptom.
// Nothing LOOKED like it happened because neither button said anything — see
// MenuNotice. And loading after saving genuinely did nothing, because the
// upload queued moments earlier had not landed and the server was asked.
//
// The first fix here was to read this file FIRST. That was wrong, and reading
// Cabinet settled it — `TVPlayerView.loadLatestState`, whose own comment is
// *"offline falls back to the newest local state, online the server stays the
// source of truth"*. It is right, and the reason is that states live on RomM so
// that "latest" can mean latest across every device a person owns. A console
// that preferred its own copy would quietly stop being one of those devices.
//
// Cabinet gets away with it because its Save AWAITS the upload and only then
// says "Saved to RomM.", so by the time you could press Load the server has it.
// This console queues on a worker instead — right for a machine that must not
// stall its frame loop — so the race was real here and not there. It is closed
// where it belongs, in Save's own reporting, rather than by changing what
// "latest" means.
static std::string newestLocalState(const GameSession& sess) {
    DIR* d = ::opendir(sess.stateDir.c_str());
    if (!d) return {};
    std::string best;
    time_t bestAt = 0;
    while (struct dirent* e = ::readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() < 7 || name.compare(name.size() - 6, 6, ".state") != 0) continue;
        const std::string full = sess.stateDir + "/" + name;
        struct stat st;
        if (::stat(full.c_str(), &st) != 0 || !S_ISREG(st.st_mode)) continue;
        if (best.empty() || st.st_mtime > bestAt) { best = full; bestAt = st.st_mtime; }
    }
    ::closedir(d);
    return best;
}

// Called once per frame. Applying the state is the only part that touches the
// core, so it happens here and nowhere else.
void pumpStateLoad(StateLoad& load, GameSession& sess,
                   MenuNotice& notice) {
    if (!load.ready.load()) return;
    load.ready = false;
    if (load.worker.joinable()) load.worker.join();
    if (load.data.empty()) {
        std::fprintf(stderr, "[state] %s\n", load.note.c_str());
        // THE FALLBACK. The server had nothing for this core or could not be
        // asked; this machine's own newest state is what is left. Read here
        // rather than on the worker because loading one touches the core.
        if (load.tryLocal) {
            load.tryLocal = false;
            if (const std::string local = newestLocalState(sess); !local.empty()) {
                const std::vector<uint8_t> bytes = cab::readBytes(local);
                if (!bytes.empty()) {
                    const bool ok = loadStateOnItsDisc(sess, local, bytes);
                    std::fprintf(stderr, "[state] fell back to local %s -> %s\n",
                                 local.c_str(), ok ? "restored" : "REFUSED");
                    notice.say(ok ? "State loaded" : stateRefusedText(),
                               ok ? Tone::Done : Tone::Problem);
                    load.loaded = ok;
                    return;
                }
            }
        }
        // NOT `note` itself, which is a line for the journal ("none for
        // gambatte-native (3 for other emulators)", or a server error) and was
        // shown on the television raw until 2026-09-23. It stays in the log
        // above; the person gets the sentence that means the same thing.
        if (!load.quiet)
            notice.say(load.serverFailed ? "Couldn't reach RomM" : "No saved state for this game",
                       load.serverFailed ? Tone::Problem : Tone::Info);
        return;
    }
    const bool ok = loadStateOnItsDisc(sess, load.note, load.data);
    std::fprintf(stderr, "[state] %s (%zu bytes) -> %s\n", load.note.c_str(),
                 load.data.size(), ok ? "restored" : "REFUSED");
    // A REFUSED state is the one that matters most. It means the bytes were
    // found and the core would not take them, which is a different problem from
    // there being none — and silently carrying on with the game running from
    // where it was is indistinguishable from nothing having happened.
    notice.say(ok ? "State loaded" : stateRefusedText(), ok ? Tone::Done : Tone::Problem);
    load.loaded = ok;
    load.data.clear();
}
