// CabinetOS frontend — the program, and the screens it moves between.
//
// This file owns everything a screen is not allowed to: the RomM client, the
// core, the download worker, the upload worker, the cache, and the navigation
// between them. The screens themselves are in screens.cpp and return an Action
// rather than doing anything; the decisions about what an Action costs are all
// here, which is why a screen can never start a download by accident.
//
//   Home       resume-first, and the one screen that is not a list. The hero is
//              what you were playing; its artwork opens the launch screen and
//              its Resume pill goes straight into the game.
//   Library    every system and every collection on the server, as a tile grid.
//   Grid       one system's or one collection's games.
//   Launch     a full-screen cover over whatever was behind it: play, or
//              download and keep.
//   The player a full-screen cover over THAT, so quitting a game returns to the
//              launch screen and backing out again returns to the browsing.
//
// See docs/PROJECT.md, "The design system" and "Navigation model".

#include <SDL3/SDL.h>

#include <dirent.h>
#include <sys/stat.h>

#include <cctype>
#include <cerrno>
#include <cstring>
#include <ctime>

#include <atomic>
#include <csignal>
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

#include <unistd.h>

#include <json-c/json.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "core.h"
#include "design.h"
#include "image.h"
#include "keyboard.h"
#include "cache.h"
#include "catalog.h"
#include "dirsave.h"
#include "filesave.h"
#include "romfile.h"
#include "romm.h"
#include "screens.h"
#include "storage.h"
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

// The design system's numbers, the animation primitive and the card live in
// design.h now, because the screens are separate translation units and two
// copies of a focus scale is how a shelf and a grid end up disagreeing about
// what focus looks like.
using namespace design;   // NOLINT — every name in it is a design-system value

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

// Uploads, off the thread that draws.
//
// A game must never stop because a file is going to a server. On a LAN with a
// 60 KB Game Boy state the old blocking version was imperceptible; on a slow
// link, or with a memory card, it stutters, and a server that does not answer
// leaves curl waiting thirty seconds with the console looking dead. That is the
// same fault the download had and it gets the same treatment.
//
// THE SPLIT THAT MATTERS: the frame thread reads the core and writes the local
// copy, and only the network goes to the worker. Reading the core has to happen
// on the frame thread because a core is not thread-safe, and writing the local
// copy has to happen before the upload is even queued, or "local first" stops
// being true the moment the process dies between the two.
//
// Sixty kilobytes to a local disk is well under a millisecond. The network is
// the part that can take thirty seconds, and the network is the part that moves.
class Uploader {
public:
    struct Job {
        int romId = 0;
        std::string emulator;
        std::string fileName;
        std::vector<uint8_t> data;
        bool isState = false;
    };

    // An upload that has not reached the server is the ONE irreplaceable thing
    // on this machine, and until this existed nothing recorded that one was
    // outstanding — so a crash between writing a state and sending it left no
    // trace that anything was owed. The marker makes it a fact on disk, and its
    // bytes count against the save floor when somebody asks to keep a game.
    //
    // The marker belongs to a PERSON now, because the thing it is tracking is a
    // save. It lands in `users/<id> - <name>/pending/`.
    void start(romm::Client* client) {
        client_ = client;
        worker_ = std::thread([this] { run(); });
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
        if (worker_.joinable()) worker_.join();
    }

    void push(Job job) {
        // Recorded BEFORE it is queued, so the window in which the console owes
        // the server something and does not know it is zero.
        cache::markPending(storage::currentUser(), job.romId, job.fileName,
                           static_cast<int64_t>(job.data.size()));
        {
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push_back(std::move(job));
            ++pending_;
        }
        wake_.notify_one();
    }

    int pending() const { return pending_.load(); }

private:
    void run() {
        for (;;) {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
                // Drain on the way out rather than dropping: a queued upload is
                // a save someone has already made, and quitting is not a reason
                // to discard it.
                if (queue_.empty()) return;
                job = std::move(queue_.front());
                queue_.pop_front();
            }

            std::string err;
            const bool ok = job.isState
                ? client_->uploadState(job.romId, job.emulator, job.fileName, job.data, &err)
                : client_->uploadSave(job.romId, job.emulator, job.fileName, job.data, &err);
            // Cleared only on success. A failed upload leaves the marker, which
            // is the point: the file is still on disk, it still has not reached
            // RomM, and the console still owes it.
            if (ok) cache::clearPending(storage::currentUser(), job.romId, job.fileName);
            std::fprintf(stderr, "[%s] %s %s\n", job.isState ? "state" : "save",
                         ok ? "uploaded" : "upload failed, kept locally:",
                         ok ? job.emulator.c_str() : err.c_str());
            --pending_;
        }
    }

    romm::Client* client_ = nullptr;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> queue_;
    std::atomic<int> pending_{0};
    bool stopping_ = false;
};

// Saves and states, and which way they travel.
//
// THE DISK COPY IS WRITTEN FIRST, ALWAYS, before any upload is attempted. That
// is Cabinet's guarantee and the reason is plain: losing signal must never mean
// losing a save. The upload is a second step that is allowed to fail, and a
// file that failed to upload is still sitting on disk to try again.
//
// A save and a state are different things and are kept apart. The save is the
// game's own — a cartridge battery, a memory card — it outlives everything, and
// it uploads with overwrite so a game keeps one card rather than one per
// session. A state is a snapshot of this exact build and a history of them is
// the point, so they never overwrite.
struct GameSession {
    int romId = 0;
    std::string title;
    // The server's own file name for the game, without its extension. A
    // save's row on RomM is named after it — see saveRowName.
    std::string fsStem;
    // TWO TAGS, NOT ONE, and the difference is what lets five platforms' saves
    // travel at all. A STATE is a photograph of the emulator's insides and the
    // tag is the promise that the build about to load it is the build that
    // wrote it; a SAVE in the file-writing class is the emulated machine's own
    // storage — a VMU image, a board's NVRAM — and no build flag moves it. So
    // Flycast has a save tag and no state tag, which is exactly right: its
    // pinned commit does not reproduce what the reference implementation
    // ships, and a memory card does not care. See catalog::saveTag.
    //
    // Empty means: do not upload, we cannot vouch for it.
    std::string saveTag;
    std::string stateTag;
    // Where this person's copies live, which is no longer beside the ROM.
    //
    //   users/<id> - <name>/saves/<platform>/<romId>/<core>/
    //   users/<id> - <name>/states/<platform>/<romId>/<core>/
    //
    // The save directory is ALSO the one handed to the core, so a core that
    // writes its own file — a Sega CD's .brm, MAME's nvram, PSP's memory stick
    // — writes it into the same place. That is what closes the oldest fault in
    // this subsystem: there were two save directories and a save written one
    // way was not seen the other way, and every core on the machine shared one
    // flat pile so a file did not say which game wrote it.
    std::string saveDir;
    std::string stateDir;
    std::vector<uint8_t> saveAtLaunch;   // to tell whether it actually changed

    // --- Directory saves, which is PSP and nothing else -------------------
    //
    // Where the tree lives, and what was in it before the game ran. The
    // baseline is how a save is attributed to the game that wrote it: this
    // console gives every core ONE shared save directory, so
    // `PSP/SAVEDATA` accumulates a folder per PSP game ever played, and
    // uploading the whole thing under one rom id would file four games'
    // saves against whichever was launched last.
    //
    // So the rule is: a folder that was created or touched while this game
    // was running belongs to this game. That needs no disc id, no parsing
    // of the ISO, and no table.
    //
    // THE SAVE DIRECTORY IS NOW PER ROM, which is the answer this comment
    // called "the better answer eventually", so the baseline no longer has
    // anything to disambiguate — the tree holds one game's saves. It is kept
    // because it still answers a second question the layout does not: whether
    // this game wrote anything at all this run, which is what decides between
    // an upload and a no-op.
    std::string dirSaveRoot;                  // empty for every core but PPSSPP
    std::vector<cab::DirEntry> dirAtLaunch;

    // --- Saves the core writes as a FILE ----------------------------------
    //
    // Empty for the cores that expose a battery, which this frontend reads out
    // of the core's own memory above. For the rest — Dreamcast, arcade, 3DO,
    // Sega CD, Neo Geo Pocket, DS — this is where the game's progress actually
    // lives, and each entry carries what was in the file once the restore had
    // run and before the game had a chance to write. See filesave.h.
    std::vector<cab::FileSaveState> fileSaves;
};

static std::string sanitisedStem(const std::string& title) {
    std::string out;
    for (char c : title) {
        const unsigned char u = static_cast<unsigned char>(c);
        // Anything above ASCII is passed through untouched. This is a UTF-8
        // string and those bytes are the tail of a multi-byte character: the
        // first version tested ASCII-only and turned "Pokémon" into "Pok__mon",
        // which is merely ugly — a Japanese title would have come out as
        // nothing but underscores, and a ROM library is full of those.
        if (u >= 0x80 || std::isalnum(u) || c == '-' || c == ' ' || c == '_' ||
            c == '(' || c == ')' || c == '.' || c == ',' || c == '\'') {
            out += c;
        } else {
            out += '_';
        }
    }
    while (!out.empty() && (out.back() == ' ' || out.back() == '.')) out.pop_back();
    return out.empty() ? "game" : out;
}

static bool writeLocal(const std::string& path, const std::vector<uint8_t>& data) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    const bool ok = std::fwrite(data.data(), 1, data.size(), f) == data.size();
    std::fclose(f);
    return ok;
}

// A directory save, zipped and sent — PSP and nothing else. See dirsave.h for
// why the archive is a zip and where it is rooted.
//
// WHICH FOLDERS. Only the ones created or touched since the game started, and
// then those folders WHOLE rather than the individual files that changed: a
// save slot is one thing, and shipping half of it would produce an archive that
// restores a PARAM.SFO without its DATA.BIN. See GameSession for why the
// baseline is needed at all.
static void syncDirSave(GameSession& sess, Uploader& up) {
    std::map<std::string, cab::DirEntry> before;
    for (const cab::DirEntry& e : sess.dirAtLaunch) before[e.relPath] = e;

    const std::vector<cab::DirEntry> now = cab::listTree(sess.dirSaveRoot);
    std::set<std::string> touchedFolders;
    for (const cab::DirEntry& e : now) {
        auto it = before.find(e.relPath);
        if (it != before.end() && it->second == e) continue;
        const size_t slash = e.relPath.find('/');
        touchedFolders.insert(slash == std::string::npos ? e.relPath
                                                         : e.relPath.substr(0, slash));
    }
    if (touchedFolders.empty()) return;   // the game saved nothing. Normal.

    std::vector<std::string> paths;
    for (const cab::DirEntry& e : now) {
        const size_t slash = e.relPath.find('/');
        const std::string top =
            slash == std::string::npos ? e.relPath : e.relPath.substr(0, slash);
        if (touchedFolders.count(top)) paths.push_back(e.relPath);
    }

    std::vector<uint8_t> zip;
    std::string zerr;
    if (!cab::zipTree(sess.dirSaveRoot, paths, &zip, &zerr)) {
        std::fprintf(stderr, "[save] could not archive the save folder: %s\n", zerr.c_str());
        return;
    }
    // Local first, always. The network is the part that can fail, and a save
    // that only exists in a pending upload is a save that can be lost.
    const std::string name = sanitisedStem(sess.title) + ".zip";
    const std::string path = sess.saveDir + "/" + name;
    if (!writeLocal(path, zip)) {
        std::fprintf(stderr, "[save] could not write %s\n", path.c_str());
        return;
    }
    std::fprintf(stderr, "[save] %zu file(s) in %zu folder(s), %zu bytes zipped to %s\n",
                 paths.size(), touchedFolders.size(), zip.size(), path.c_str());
    sess.dirAtLaunch = now;   // this is the new baseline; do not send it twice

    if (sess.saveTag.empty()) return;
    up.push(Uploader::Job{sess.romId, sess.saveTag, name, std::move(zip), false});
}

// The name a save travels under on RomM, and it is the REFERENCE
// IMPLEMENTATION'S rather than this console's own. That is the whole point.
//
// `POST /api/saves` is sent with `overwrite=true`, and RomM matches a row for
// overwrite BY FILENAME ALONE — the emulator tag is not part of it. So the
// filename is what decides whether a person ends up with one memory card per
// game or one per device, and this console was quietly choosing the second:
// it named rows after the game's TITLE while the reference names them after
// the server's own file name, so the same card arrived twice. It is visible on
// the reference server today — Lumines has a `Lumines - Puzzle Fusion (USA)
// (Cabinet).srm` from an Apple TV and a `Lumines.zip` from here, both tagged
// `ppsspp-native`, both the same save.
//
// `<fs name without extension> (Cabinet).<region>`, then:
//
//   * the STEM is the server's `fs_name`, so `Ikaruga (Japan)`, `lethalen`;
//   * the `(Cabinet)` marker keeps the row distinct from anything RomM's own
//     web player wrote, which a bare `<name>.srm` would silently take over;
//   * arcade puts the core inside the marker, because one game has two of
//     these and they must not overwrite each other;
//   * the REGION is the extension, so Sega CD's external RAM cartridge is its
//     own row rather than something that lands on top of the internal RAM.
//
// WHAT CHANGES FOR SAVES ALREADY ON A SERVER: a row this console wrote under
// the old name is left where it is and stops being updated. Nothing is lost —
// a restore takes the newest row for the tag whatever it is called, so the new
// one wins from the first save onward — and the orphan can be deleted by hand.
//
// PSP IS THE ONE EXCEPTION AND IT IS DELIBERATE. Its row is still
// `<title>.zip`, because the reference's own PSP row is an Apple archive
// wearing an `.srm` extension and the fixed build that writes a zip instead
// has not been seen from here yet. Renaming ours onto that row would overwrite
// a save with a container the other end may not read. docs/NEXT-SESSION.md
// says to settle it from the first save that build uploads; until then two
// rows is the safe answer and one row is not.
static std::string saveRowName(const std::string& fsStem, const std::string& coreRowName,
                               const std::string& region) {
    std::string base = sanitisedStem(fsStem) + " (Cabinet";
    if (!coreRowName.empty()) base += " " + coreRowName;
    return base + ")." + region;
}

// Which region a row on the server belongs to, judged by the extension this
// console and the reference implementation both upload under. Anything else —
// a card somebody made in RomM's web player, a file from another emulator —
// reads as the main save, which is right: that is the only region a foreign
// row could ever be.
static std::string regionOfRow(const std::string& fileName) {
    if (fileName.size() > 5 && fileName.compare(fileName.size() - 5, 5, ".cart") == 0)
        return "cart";
    if (fileName.size() > 4 && fileName.compare(fileName.size() - 4, 4, ".rtc") == 0)
        return "rtc";
    return "srm";
}

// Puts a game's save files where the core will look for them, and remembers
// what was in them.
//
// RUN BEFORE `retro_load_game` AND NOWHERE ELSE. Every core in this class
// reads its save file once while the machine is being built and never looks
// again, so a file placed afterwards is a file the game has already decided is
// not there. That is the same rule PSP's directory save taught, and it is the
// reason this cannot be folded into the battery restore below, which happens
// after the load because the core can be handed bytes directly.
//
// WHICH COPY WINS. A save this console still owes the server wins outright —
// it is strictly newer than anything the server has, and without that rule a
// launch after an offline session would fetch the older copy and write it over
// the top. Otherwise the server's own row wins, because that is how a card
// made on another device arrives. Failing both, whatever is on this disk plays.
static std::vector<cab::FileSaveState> restoreFileSaves(
        const std::vector<catalog::SaveFile>& specs, const std::string& fsStem,
        const std::string& saveDir, int romId, const char* tag,
        romm::Client& client) {
    std::vector<cab::FileSaveState> out;
    if (specs.empty()) return out;

    std::vector<romm::Asset> rows;
    if (tag && client.haveToken()) {
        std::string err;
        if (!client.fetchSaves(romId, &rows, &err))
            std::fprintf(stderr, "[save] could not ask the server: %s\n", err.c_str());
    }
    const storage::User& user = storage::currentUser();

    for (const catalog::SaveFile& spec : specs) {
        cab::FileSaveState f;
        f.spec = spec;
        f.path = (spec.inSystemDir ? storage::biosDir() : saveDir) + "/" + spec.path;
        const std::string name = saveRowName(fsStem, spec.coreRowName, spec.region);

        // WHERE THIS PERSON'S COPY LIVES. For every platform but Dreamcast it
        // is the file the core writes, because the core was handed this
        // person's own save directory. Dreamcast's is a copy under the same
        // directory, because Flycast insists on the system directory and the
        // system directory is shared by the whole machine.
        const std::string mine = spec.inSystemDir ? saveDir + "/" + name : f.path;

        // A card left in the system directory by a session that did not shut
        // down cleanly. It is somebody's save and nothing on this machine says
        // whose game it was, so it is kept rather than guessed at — the same
        // answer the folder move gave to the two save piles it could not
        // attribute, and in the same place.
        if (spec.inSystemDir) {
            const std::string stray = cab::writtenFile(spec, f.path);
            if (!stray.empty()) {
                const std::vector<uint8_t> bytes = cab::readBytes(stray);
                if (!bytes.empty() && bytes != cab::readBytes(mine)) {
                    char stamp[32];
                    const std::time_t now = std::time(nullptr);
                    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S",
                                  std::localtime(&now));
                    const std::string kept = storage::userDir(user) +
                                             "/saves/unattributed/system-directory/" +
                                             std::string(stamp) + " " + spec.region + ".bin";
                    if (cab::writeBytes(kept, bytes))
                        std::fprintf(stderr,
                                     "[save] a card was left in the system directory by a "
                                     "session that did not finish; kept at %s\n",
                                     kept.c_str());
                }
                cab::removeFile(stray);
            }
        }

        // What this console already has. Found by scanning rather than by the
        // predicted name, because the name the core chose last time is the one
        // that is actually on the disk.
        const std::string existing =
            spec.inSystemDir ? mine : cab::writtenFile(spec, f.path);
        std::vector<uint8_t> chosen =
            existing.empty() ? std::vector<uint8_t>() : cab::readBytes(existing);
        const bool haveLocal = !chosen.empty();
        const char* from = haveLocal ? "this console" : nullptr;

        if (!cache::isPending(user, romId, name)) {
            const romm::Asset* newest = nullptr;
            for (const romm::Asset& a : rows) {
                if (!tag || a.emulator != tag) continue;
                if (regionOfRow(a.fileName) != spec.region) continue;
                if (!newest || a.updatedAt > newest->updatedAt) newest = &a;
            }
            if (newest) {
                std::vector<uint8_t> data = client.fetchAsset("saves", newest->id);
                // A row that holds nothing a game wrote does not get to
                // replace one that does. Three of the thirteen Dreamcast cards
                // on the reference server are formatted and empty, uploaded by
                // an implementation with no freshness rule, and restoring one
                // of those over a real card would be this feature losing a
                // save rather than moving one.
                if (data.empty()) {
                    std::fprintf(stderr, "[save] %s came back empty\n",
                                 newest->fileName.c_str());
                } else if (haveLocal && !cab::holdsASave(data, spec.untouched)) {
                    std::fprintf(stderr,
                                 "[save] %s holds no save — keeping this console's copy\n",
                                 newest->fileName.c_str());
                } else {
                    chosen = std::move(data);
                    from = "the server";
                }
            }
        } else {
            std::fprintf(stderr, "[save] %s has not reached the server yet — it wins\n",
                         name.c_str());
        }

        if (!chosen.empty()) {
            // LOCAL FIRST, ALWAYS, and for Dreamcast that means two writes:
            // this person's copy, then the working copy the core reads. For
            // everything else the two are the same file.
            if (!cab::writeBytes(mine, chosen)) {
                std::fprintf(stderr, "[save] could not write %s\n", mine.c_str());
            } else if (!spec.inSystemDir || cab::writeBytes(f.path, chosen)) {
                std::fprintf(stderr, "[save] %zu bytes from %s into %s\n", chosen.size(),
                             from ? from : "nowhere", f.path.c_str());
                f.atLaunch = chosen;
                f.hadOne = true;
            } else {
                std::fprintf(stderr, "[save] could not place the card at %s\n",
                             f.path.c_str());
            }
        } else {
            std::fprintf(stderr, "[save] no save anywhere for %s yet\n", spec.path.c_str());
        }
        out.push_back(std::move(f));
    }
    return out;
}

// The saves a core wrote as files, read back after it shut down.
//
// RUN AFTER `retro_unload_game` AND NOWHERE ELSE. Every core in this class
// buffers, and Flycast does not even close the VMU until its device is
// destroyed at teardown, so a capture taken mid-session reads a partially
// written card. The reference implementation captured Dreamcast on pause once
// and uploaded exactly such a half-written image; the game that later restored
// it reported the file corrupt. That is the whole reason this is a separate
// function called from one place rather than part of syncSave.
//
// WHAT TRAVELS. Only a file whose bytes differ from the baseline taken at
// launch — so a session that looked at a title screen and quit sends nothing —
// and, when there was no save anywhere to begin with, only one that holds
// something a game actually wrote. Once a real save has existed every later
// change travels, erasing one included, because losing history is worse than
// an empty row.
static void syncFileSaves(GameSession& sess, Uploader& up) {
    for (cab::FileSaveState& f : sess.fileSaves) {
        const std::string written = cab::writtenFile(f.spec, f.path);
        std::vector<uint8_t> data =
            written.empty() ? std::vector<uint8_t>() : cab::readBytes(written);

        // Dreamcast, and this is the half that closes the folder layout's last
        // rough edge. The card was placed in the system directory because that
        // is the only place Flycast looks; now that it has been read back, it
        // comes out again, so what sits in `bios/` between sessions is
        // firmware and the console's own settings rather than the one file in
        // there that could never be fetched a second time.
        //
        // Taken out whether or not anything changed, and whether or not the
        // upload works: the copy below is written to this person's save
        // directory first, and the thing left in `bios/` is a duplicate either
        // way. Only removed when it was actually read — a file we could not
        // read is left exactly where it is rather than deleted on a guess.
        const bool tidyAway = f.spec.inSystemDir && !written.empty() && !data.empty();

        if (data.empty() || data == f.atLaunch) {
            if (tidyAway) cab::removeFile(written);
            continue;
        }
        if (!f.hadOne && !cab::holdsASave(data, f.spec.untouched)) {
            std::fprintf(stderr,
                         "[save] %s is what the core writes by starting up, not a save — "
                         "not sending it\n", written.c_str());
            if (tidyAway) cab::removeFile(written);
            continue;
        }

        // LOCAL FIRST, ALWAYS. For every platform but Dreamcast the file is
        // already sitting in this person's save directory, because that is the
        // directory the core was handed — so this writes nothing and the
        // guarantee holds for free. Dreamcast's card has to be copied across
        // out of the system directory, and that copy happens before a single
        // byte is offered to the network.
        const std::string name = saveRowName(sess.fsStem, f.spec.coreRowName, f.spec.region);
        const std::string local = sess.saveDir + "/" + name;
        if (f.spec.inSystemDir && !writeLocal(local, data)) {
            std::fprintf(stderr, "[save] could not write %s — leaving the card in %s\n",
                         local.c_str(), written.c_str());
            continue;
        }
        if (tidyAway) cab::removeFile(written);

        std::fprintf(stderr, "[save] %zu bytes from %s\n", data.size(),
                     f.spec.path.c_str());
        f.atLaunch = data;    // the new baseline; do not send it twice
        f.hadOne = true;

        if (sess.saveTag.empty()) continue;
        up.push(Uploader::Job{sess.romId, sess.saveTag, name, std::move(data), false});
    }
}

// Takes a snapshot of the game's own save and sends it, but only when it has
// actually changed. A cartridge with no battery returns nothing, which is a
// normal answer and not a failure.
static void syncSave(GameSession& sess, Uploader& up) {
    // The one platform whose save is a tree. It never reaches the save-RAM path
    // below, because PPSSPP answers RETRO_MEMORY_SAVE_RAM with nothing at all.
    if (!sess.dirSaveRoot.empty()) { syncDirSave(sess, up); return; }
    cab::Core& core = cab::Core::shared();
    std::vector<uint8_t> ram;
    if (!core.readSaveRam(ram) || ram.empty()) return;
    if (ram == sess.saveAtLaunch) return;     // nothing happened worth sending

    const std::string name = saveRowName(sess.fsStem, std::string(), "srm");
    const std::string path = sess.saveDir + "/" + name;
    if (!writeLocal(path, ram)) {
        std::fprintf(stderr, "[save] could not write %s\n", path.c_str());
        return;
    }
    std::fprintf(stderr, "[save] %zu bytes to %s\n", ram.size(), path.c_str());
    sess.saveAtLaunch = ram;   // copied before the move below

    if (sess.saveTag.empty()) return;
    // Queued, not sent. The local copy above is already safe; the network is
    // the part that can take thirty seconds and it does not get to stop the
    // picture.
    up.push(Uploader::Job{sess.romId, sess.saveTag, name, std::move(ram), false});
}

static void saveStateNow(GameSession& sess, Uploader& up) {
    cab::Core& core = cab::Core::shared();
    std::vector<uint8_t> st;
    if (!core.saveState(st) || st.empty()) {
        std::fprintf(stderr, "[state] this core cannot serialize\n");
        return;
    }
    // Named with a timestamp because states accumulate on purpose; a save
    // overwrites, a state does not.
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", std::localtime(&now));
    const std::string name = sanitisedStem(sess.title) + " [" + stamp + "].state";

    storage::makeDirs(sess.stateDir);
    if (!writeLocal(sess.stateDir + "/" + name, st)) {
        std::fprintf(stderr, "[state] could not write locally, not uploading\n");
        return;
    }
    std::fprintf(stderr, "[state] %zu bytes saved locally\n", st.size());

    if (sess.stateTag.empty()) {
        std::fprintf(stderr, "[state] no settled tag for this core — not uploaded\n");
        return;
    }
    up.push(Uploader::Job{sess.romId, sess.stateTag, name, std::move(st), true});
}

// The newest state RomM holds that THIS build can actually restore. A state
// from another emulator is skipped rather than attempted: loading one does not
// fail cleanly, it boots something that looks like the game and is not.
// Finding and fetching a state is network work, so it happens on a worker; only
// applying it touches the core, and that waits for the frame thread. Same split
// as the upload, same reason: nothing that talks to a server may stop the
// picture.
struct StateLoad {
    std::atomic<bool> running{false};
    std::atomic<bool> ready{false};
    std::vector<uint8_t> data;
    std::string note;
    std::thread worker;
    ~StateLoad() { if (worker.joinable()) worker.join(); }
};

static void beginLoadLatestState(StateLoad& load, GameSession& sess, romm::Client& client) {
    if (load.running.load()) return;
    if (sess.stateTag.empty()) {
        std::fprintf(stderr, "[state] no settled tag for this core — refusing to load\n");
        return;
    }
    if (load.worker.joinable()) load.worker.join();
    load.running = true;
    load.ready = false;
    const int romId = sess.romId;
    const std::string tag = sess.stateTag;
    load.worker = std::thread([&load, &client, romId, tag]() {
        std::vector<romm::Asset> states;
        std::string err;
        if (!client.fetchStates(romId, &states, &err)) {
            load.note = err;
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
            if (!best || a.updatedAt > best->updatedAt) best = &a;
        }
        if (!best) {
            load.note = "none for " + tag + " (" + std::to_string(skipped) +
                        " for other emulators)";
        } else {
            load.data = client.fetchAsset("states", best->id);
            load.note = best->fileName;
        }
        load.running = false;
        load.ready = true;
    });
}

// Called once per frame. Applying the state is the only part that touches the
// core, so it happens here and nowhere else.
static void pumpStateLoad(StateLoad& load) {
    if (!load.ready.load()) return;
    load.ready = false;
    if (load.worker.joinable()) load.worker.join();
    if (load.data.empty()) {
        std::fprintf(stderr, "[state] %s\n", load.note.c_str());
        return;
    }
    std::fprintf(stderr, "[state] %s (%zu bytes) -> %s\n", load.note.c_str(),
                 load.data.size(),
                 cab::Core::shared().loadState(load.data) ? "restored" : "REFUSED");
    load.data.clear();
}

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

    // The same fetch serves both things a person can ask for, because they are
    // the same fetch. Pressing Play on a game that is not here downloads it and
    // starts it; choosing Download puts it here and keeps it. The only
    // differences are what happens at the end, so they are two flags rather
    // than two code paths that would drift.
    bool playWhenReady = true;
    bool keepWhenReady = false;

    // Written by the worker before it sets Ready or Failed, read by the frame
    // thread only after it observes one of those. The atomic stage is the
    // handover.
    std::string romPath;
    std::string coreName;
    // Carried through so the session can be built when the game loads: which
    // game it is, which platform it belongs to, and where its files are.
    int romId = 0;
    // WHERE THE GAME LIVES ON DISK, which is `<location>/roms/<platform>/…`
    // when somebody keeps it and `<location>/cache/<platform>/…` when nobody
    // does. One entry, named `<romId> - <title>` — a file when the game is a
    // single payload and a directory when its archive unpacked into several.
    std::string entryPath;
    // ONE SPELLING OF A PLATFORM, EVERYWHERE. RomM's `fs_slug` — "Sony
    // Playstation", not "psx". See storage.h for why the short one lost.
    std::string platformFsSlug;
    // AND THE SHORT ONE AS WELL, for the one question that has to be asked of
    // the platform rather than the core: which file, if any, the core writes
    // its save into. `fs_slug` is what a server's owner can rename; `slug` is
    // RomM's own and is what catalog is keyed on. Both are carried because
    // each answers a different question, and the arcade rows need both.
    std::string platformSlug;
    // The server's own file name for this game, with its extension removed —
    // `Ikaruga (Japan)`, `lethalen`. It is what a save's row on RomM is named
    // after; see saveRowName.
    std::string fsStem;
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

// A kept game's record: the whole library entry, not a subset.
//
// Cabinet's KeptGame embeds the entire Rom captured at keep time so that a kept
// game can be browsed and launched with NO NETWORK AT ALL — cover path and
// platform identifiers included. A subset is how that promise gets broken later
// by a field nobody thought of, so this writes every field this client knows a
// Rom to have, and the day the struct grows, so does the record.
static std::string gameRecordJson(const romm::Game& g) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "id", json_object_new_int(g.id));
    json_object_object_add(o, "platform_id", json_object_new_int(g.platformId));
    json_object_object_add(o, "platform_slug", json_object_new_string(g.platformSlug.c_str()));
    json_object_object_add(o, "platform_fs_slug", json_object_new_string(g.platformFsSlug.c_str()));
    json_object_object_add(o, "platform_name", json_object_new_string(g.platformName.c_str()));
    json_object_object_add(o, "name", json_object_new_string(g.name.c_str()));
    json_object_object_add(o, "fs_name", json_object_new_string(g.fsName.c_str()));
    json_object_object_add(o, "path_cover_small", json_object_new_string(g.coverPath.c_str()));
    json_object_object_add(o, "fs_size_bytes", json_object_new_int64(g.sizeBytes));
    const std::string out = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    json_object_put(o);
    return out;
}

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
static bool beginLaunch(LaunchJob& job, romm::Client& client, const romm::Game& game,
                        const std::string& coreDir, std::string* err,
                        bool playWhenReady = true, bool keepWhenReady = false) {
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
    job.romId = game.id;
    job.platformFsSlug = game.platformFsSlug;
    job.platformSlug = game.platformSlug;
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
    job.message.clear();
    job.playWhenReady = playWhenReady;
    job.keepWhenReady = keepWhenReady;
    job.stage = LaunchJob::Stage::Downloading;

    const storage::User& user = storage::currentUser();

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
    core.setOptionOverrides(catalog::optionOverrides(job.coreName));
    if (!core.load(job.corePath)) {
        *err = "core " + job.coreName + ": " + core.error();
        job.stage = LaunchJob::Stage::Idle;
        return false;
    }
    const std::string validExts = core.validExtensions();
    const bool blockExtract = core.blockExtract();

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
            *err = kerr;
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
                                     : (keepWhenReady ? storage::keepLocation()
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
    const std::string entryPath = job.entryPath;
    job.worker = std::thread([&job, &client, id, platformId, slug, fsSlug, fsName,
                              entryPath, location, validExts, blockExtract, expectedSize]() {
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
            if (client.fetchFirmware(platformId, &firmware, &ferr)) {
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
            if (const catalog::FirmwareAliases fa = catalog::firmwareAliases(slug, fsSlug);
                fa.sizeBytes > 0) {
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
            job.got = 0;
            job.total = 0;
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
                        job.message =
                            "not enough space for this game, and nothing left "
                            "that can be cleared";
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
                job.message = err;
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
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
                    "this game's file is a web page, not a game — it needs "
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
                job.message = "not enough space to unpack this game";
                job.stage = LaunchJob::Stage::Failed;
                return;
            }
        }

        if (!romfile::prepareFile(dest, entryPath, validExts, blockExtract, &primary,
                                  &kind, &err)) {
            job.message = err;
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

    // What the Library screen shows. EVERY platform the server holds, including
    // the ones this console cannot play — those carry their reason instead of a
    // count and cannot be opened. docs/PROJECT.md is explicit that the answer is
    // neither "show everything" nor "hide quietly": somebody who owns Switch
    // games and sees nothing will reasonably conclude the scan failed.
    std::vector<screens::Tile> platformTiles;
    std::vector<screens::Tile> collectionTiles;
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
    // A tile per platform, built whether or not this console can play it. The
    // unplayable ones are why `coverageFor` gives four different answers rather
    // than one boolean: "no core exists", "Cabinet does not ship it", "this
    // console has not built it yet" and "it is built and cannot be driven" lead
    // to different work and to different words on the screen.
    for (const auto& p : platforms) {
        const catalog::Coverage cov = catalog::coverageFor(p);
        screens::Tile tile;
        tile.id = p.id;
        tile.title = catalog::displayName(p);
        tile.art = colorForTitle(tile.title);
        tile.enterable = (cov.support == catalog::Support::Playable);

        if (!tile.enterable) {
            skippedGames += p.romCount;
            // The tile gets the short form, which is all it has room for. The
            // log keeps the full sentence, because a log is read by somebody
            // trying to find out why.
            tile.detail = catalog::shortReason(cov.support);
            std::fprintf(stderr, "[library] %s (%d games) — %s\n", tile.title.c_str(),
                         p.romCount, cov.reason ? cov.reason : tile.detail.c_str());
            lib.platformTiles.push_back(std::move(tile));
            continue;
        }

        std::vector<romm::Game> games;
        if (!client.fetchGames(p.id, &games, &err)) {
            // One platform failing is not the library failing. Say so, give the
            // tile the truth rather than a count it does not have, and go on.
            std::fprintf(stderr, "[library] %s: %s\n", p.name.c_str(), err.c_str());
            tile.enterable = false;
            tile.detail = "could not be read from the server";
            lib.platformTiles.push_back(std::move(tile));
            continue;
        }
        for (auto& g : games) {
            Card c;
            c.id = g.id;
            c.title = g.name.empty() ? g.fsName : g.name;
            c.cover = g.coverPath;
            c.art = colorForTitle(c.title);
            // The tile's own artwork is the first cover in it that exists. Not
            // every platform has any — Game & Watch has none of 171 — and a
            // tile with no art is a normal state rather than a fault.
            if (tile.cover.empty() && !c.cover.empty()) tile.cover = c.cover;
            cards.push_back(std::move(c));
            lib.games.push_back(g);
        }
        char count[48];
        std::snprintf(count, sizeof count, "%zu game%s", games.size(),
                      games.size() == 1 ? "" : "s");
        tile.detail = count;
        lib.platformTiles.push_back(std::move(tile));
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

    // The membership of each tile, filled AFTER the sort because the sort moves
    // every card and an index taken before it points at the wrong game. Keyed
    // on platform id, which is the only unique field — two platforms in the
    // reference library share a name AND a slug.
    {
        std::vector<std::pair<int, size_t>> byPlatform;   // platform id -> tile
        for (size_t t = 0; t < lib.platformTiles.size(); ++t)
            byPlatform.emplace_back(lib.platformTiles[t].id, t);
        for (size_t i = 0; i < lib.games.size(); ++i) {
            for (const auto& [id, t] : byPlatform) {
                if (id != lib.games[i].platformId) continue;
                lib.platformTiles[t].cards.push_back(static_cast<int>(i));
                break;
            }
        }
    }

    // Playable systems first, then alphabetically inside each group. The
    // alternative — one flat alphabet — puts a system this console cannot play
    // in the first tile on the screen, which is the wrong thing to lead with on
    // a console whose whole job is the games it CAN run. The unplayable ones
    // are still all there, below, saying why.
    std::stable_sort(lib.platformTiles.begin(), lib.platformTiles.end(),
                     [](const screens::Tile& a, const screens::Tile& b) {
                         if (a.enterable != b.enterable) return a.enterable;
                         return a.title < b.title;
                     });

    // Collections. A collection is a list of rom ids rather than a property of
    // each game, so it is resolved by looking those ids up in the library that
    // is already here — no second request, and a collection containing games
    // this console cannot play simply comes out shorter.
    std::vector<romm::Collection> collections;
    if (client.fetchCollections(&collections, &err)) {
        for (const auto& col : collections) {
            screens::Tile tile;
            tile.id = col.id;
            tile.title = col.name;
            tile.cover = col.coverPath;
            tile.art = colorForTitle(tile.title);
            for (int romId : col.romIds) {
                for (size_t i = 0; i < cards.size(); ++i) {
                    if (cards[i].id != romId) continue;
                    tile.cards.push_back(static_cast<int>(i));
                    break;
                }
            }
            const size_t have = tile.cards.size();
            char count[96];
            if (have == static_cast<size_t>(col.romCount)) {
                std::snprintf(count, sizeof count, "%zu game%s", have,
                              have == 1 ? "" : "s");
            } else {
                // Say what is missing rather than quietly showing a shorter
                // list: a collection of twelve that opens onto four looks like
                // a bug unless the tile already said why.
                // Short, because a tile's second line holds about sixteen
                // characters beside a cover. "26 of 30 playable here" came back
                // as "26 of 30 playabl...", which says less than "26 of 30".
                std::snprintf(count, sizeof count, "%zu of %d", have, col.romCount);
            }
            tile.detail = count;
            tile.enterable = have > 0;
            lib.collectionTiles.push_back(std::move(tile));
        }
        std::fprintf(stderr, "[library] %zu collection(s)\n", lib.collectionTiles.size());
    } else {
        // Not fatal. A server that will not list collections still has a
        // library, and the switcher's Collections tab says it is empty.
        std::fprintf(stderr, "[library] no collections: %s\n", err.c_str());
    }

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

// Who this console is acting as, settled before anything writes a save.
//
// Saves, states and the decision to keep a game all live under a person now, so
// this is not a nicety: without an answer there is no directory to write into.
// The answer comes from RomM's `/api/users/me` and is cached on disk, because a
// console with no network still has to know whose saves it is holding.
static bool adoptUser(romm::Client& client) {
    std::string err;
    if (storage::resolveCurrentUser(client, &err)) {
        std::fprintf(stderr, "[storage] user %s\n",
                     storage::currentUser().dirName().c_str());
        return true;
    }
    std::fprintf(stderr,
                 "[storage] no RomM user (%s) — saves and keeps have nobody to "
                 "belong to until this console is paired\n", err.c_str());
    return false;
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
        // SAY IT LOUDLY WHEN THE TOKEN DOES NOT LAND, and do not call it
        // "paired". The old wording was `paired (could not write ...)` — a
        // success word with the failure in brackets after it — which is
        // exactly how the first console ever installed came up on the
        // stand-in library with nobody able to say why. The pairing itself
        // genuinely succeeded; what failed is the only part that lasts.
        if (client.saveToken(tokenPath)) {
            std::printf("\npaired      token saved to %s\n", tokenPath.c_str());
        } else {
            std::printf("\n");
            std::fflush(stdout);
            std::fprintf(stderr,
                         "[romm] PAIRED, BUT THE TOKEN COULD NOT BE SAVED to %s (%s).\n"
                         "[romm] This console will not stay paired. Fix the path and\n"
                         "[romm] run --romm-probe --romm-pair again.\n",
                         tokenPath.c_str(), std::strerror(errno));
            return 1;
        }
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
    int autoDownloadId = 0;
    int autoUnkeepId = 0;
    bool storageReport = false;
    bool coreOptionsAudit = false;
    const char* initialScreen = nullptr;
    int initialTile = 0;
    int initialTab = 0;
    int initialGame = 0;
    bool rommProbeMode = false;
    // Downloads one ROM and reports what came back and what a core would be
    // handed. The formats people keep ROMs in are not uniform and this is how
    // that gets checked against a real server rather than assumed.
    int romProbeId = 0;
    const char* romProbeExts = "gb|gbc|dmg";
    // Where the built cores are. Where games and saves go is storage.h's
    // answer, not a constant here — see --storage-root.
    //
    // Left null so the two defaults below can be TRIED rather than one of them
    // assumed — see the resolution block after the argument loop. --core-dir
    // still wins outright.
    const char* coreDir = nullptr;
    // Launch this RomM id without anybody pressing anything, after a delay, so
    // the whole Home-to-game transition can be watched on a machine with no
    // controller attached to it.
    int autoLaunchId = 0;
    // Runs the whole save/state round trip once the game is up: write a state,
    // upload it, then fetch the newest one back and restore it. Headless, so
    // the sync can be proved on a machine nobody is sitting at.
    bool syncTest = false;
    // Opens the overlay once the game is up, so it can be looked at on a
    // machine with nothing attached to it.
    bool overlayDemo = false;
    // Opens the overlay and takes Exit to Home, so the whole leave-a-game path
    // can be proved on a machine with nothing attached.
    bool overlayExitDemo = false;
    int overlayExitAfter = 120;   // frames of play before the overlay quits it
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
        } else if (SDL_strcmp(argv[i], "--storage-root") == 0 && i + 1 < argc) {
            storage::setRoot(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--launch") == 0 && i + 1 < argc) {
            autoLaunchId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--download") == 0 && i + 1 < argc) {
            // The same errand as --launch and for the same reason: this machine
            // has no controller, so the only way to exercise the thing a person
            // would press is to press it from here. It calls exactly what the
            // launch screen's row calls, guards and all.
            autoDownloadId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--unkeep") == 0 && i + 1 < argc) {
            autoUnkeepId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--overlay-exit") == 0) {
            overlayExitDemo = true;
            // Optional frame count: --overlay-exit 2000 plays for 2000 frames
            // and then quits through the overlay, which is the only way to
            // exercise the quit-time save path on a machine with no controller.
            if (i + 1 < argc && argv[i + 1][0] != '-') overlayExitAfter = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--overlay") == 0) {
            overlayDemo = true;
        } else if (SDL_strcmp(argv[i], "--sync-test") == 0) {
            syncTest = true;
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
        } else if (SDL_strcmp(argv[i], "--core-options-off") == 0) {
            // The control. See core.h: this is the behaviour this host had
            // before it captured option tables, kept so the difference can be
            // measured rather than asserted.
            cab::Core::setAnswerOptions(false);
        } else if (SDL_strcmp(argv[i], "--core-options") == 0) {
            // Every option every built core declares, and what it is answered
            // with. This is the audit docs/PROJECT.md asked for and nobody had
            // run: an unanswered option is not the default, it is zero, and
            // until this existed there was no way to see which were which.
            coreOptionsAudit = true;
        } else if (SDL_strcmp(argv[i], "--storage") == 0) {
            storageReport = true;
        } else if (SDL_strcmp(argv[i], "--tab") == 0 && i + 1 < argc) {
            initialTab = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--screen") == 0 && i + 1 < argc) {
            // Opens straight onto a screen, so every one of them can be
            // photographed from a machine with no controller and no way to show
            // anyone a picture. "it looked right here" is not something this
            // project can say, so each screen has to be capturable by itself.
            //
            //   --screen library
            //   --screen grid --tile 3
            //   --screen detail --game 1234
            initialScreen = argv[++i];
        } else if (SDL_strcmp(argv[i], "--tile") == 0 && i + 1 < argc) {
            initialTile = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            initialGame = SDL_atoi(argv[++i]);
        }
    }

    // --- Where the cores are, and which server this console talks to -------
    //
    // BOTH ARE TRIED RATHER THAN ASSUMED, because this one binary now runs in
    // two places that disagree about both answers:
    //
    //   the image      /usr/lib/cabinetos/cores, installed by the build, and a
    //                  server address in /etc/cabinetos/session.env that the
    //                  session script exports before starting this;
    //   a source tree  cores/build, where cores/build-core.sh leaves them, and
    //                  --romm on the command line.
    //
    // An explicit flag always wins. What is chosen is printed, for the same
    // reason the storage root below is: a console reading the wrong directory
    // reports every platform as "not built on this console yet", which reads
    // as twenty-one broken cores rather than as one wrong path.
    if (!coreDir) {
        struct stat cs;
        coreDir = (::stat("cores/build", &cs) == 0 && S_ISDIR(cs.st_mode))
                      ? "cores/build"
                      : "/usr/lib/cabinetos/cores";
    }
    std::fprintf(stderr, "[cores] %s\n", coreDir);

    // A console has no command line. Nothing is baked into the image — this
    // repository is public and somebody's LAN address does not belong in it —
    // so the address comes from the machine, and the session script is what
    // puts it in the environment. The first-run screen writes the same file
    // when it exists; see docs/PROJECT.md, open question 15.
    if (!rommAddress) {
        if (const char* env = getenv("CABINETOS_ROMM"); env && *env) rommAddress = env;
    }

    // --- Where everything lives, decided before anything writes a byte ------
    //
    // Printed rather than assumed. A console quietly writing somewhere nobody
    // expected is the kind of bug that costs an afternoon, and the one line it
    // takes to prevent that is this one.
    {
        std::string serr;
        if (!storage::ensureTree(&serr)) {
            std::fprintf(stderr, "[storage] %s\n", serr.c_str());
            return 1;
        }
        std::fprintf(stderr, "[storage] root %s\n", storage::root().c_str());
        const std::vector<std::string> locs = storage::locations();
        for (size_t i = 1; i < locs.size(); ++i)
            std::fprintf(stderr, "[storage] games drive %s\n", locs[i].c_str());
        // SAID ONCE AND THEN NEVER AGAIN. A console that complains every boot
        // about a drive somebody removed on purpose is worse than one that says
        // nothing, so having reported it the console forgets that drive. The
        // screen version of this waits with the rest of the UI; the games on it
        // already behave correctly, because nothing is found and not found
        // means fetch it.
        if (const std::string gone = storage::missingDriveToReport(); !gone.empty())
            std::fprintf(stderr,
                         "[storage] the games drive at %s is not connected — games "
                         "kept on it will be fetched from RomM again\n", gone.c_str());
    }

    // Runs before SDL, deliberately. This needs no window, no GL and no
    // controller, and on a headless machine it must work anyway — the whole
    // point is to test the server conversation on its own.
    // The probe reports, per game, whether this console can play it — and it
    // asked that question WITHOUT being told where the cores are, so on a real
    // console it answered "not playable here" for every platform while all
    // twenty-one cores sat installed and working. Found on the A9 Pro,
    // 2026-09-19. Harmless to the product and corrosive to the diagnosis: a
    // tool that lies is worse than one that says nothing.
    catalog::setCoreDirectory(coreDir);

    if (rommAddress && romProbeId > 0)
        return romProbe(rommAddress, romProbeId, romProbeExts);
    if (rommAddress && rommProbeMode) return rommProbe(rommAddress, rommPair);

    // Every option every built core declares, and what it is answered with.
    //
    // Runs before SDL, because loading a core and reading its table needs no
    // window, no GL and no controller — and because the answer has to be
    // checkable in CI, on a machine with no screen.
    //
    // WHAT TO LOOK FOR: a core with ZERO declared options is the suspicious
    // case, not the clean one. It means either the core genuinely has none, or
    // it declares them through an API generation this host does not read — and
    // the second is indistinguishable from the first without going and looking.
    if (coreOptionsAudit) {
        DIR* d = opendir(coreDir);
        if (!d) {
            std::fprintf(stderr, "[options] no core directory at %s\n", coreDir);
            return 1;
        }
        std::vector<std::string> sos;
        while (struct dirent* e = readdir(d)) {
            const std::string name = e->d_name;
            if (name.size() > 3 && name.compare(name.size() - 3, 3, ".so") == 0)
                sos.push_back(name);
        }
        closedir(d);
        std::sort(sos.begin(), sos.end());

        int totalOptions = 0, totalOverridden = 0, coresWithNone = 0;
        for (const std::string& so : sos) {
            cab::Core& core = cab::Core::shared();
            // Before load(), because a core may read its options inside
            // retro_init and several do.
            //
            // The directories go in for the same reason, and the audit used to
            // skip them: with no system directory PPSSPP looked for its own
            // assets at a relative path, found none, and printed "Core system
            // files missing, expect bugs" during an audit that is supposed to
            // report what a core does in the product. An instrument that sets
            // the core up differently from the way the product does is
            // measuring something else.
            core.setDirectories(storage::biosDir(), storage::scratchSavesDir(so));
            storage::makeDirs(storage::scratchSavesDir(so));
            core.setOptionOverrides(catalog::optionOverrides(so));
            if (!core.load(std::string(coreDir) + "/" + so)) {
                std::printf("%-24s  FAILED TO LOAD: %s\n", so.c_str(),
                            core.error().c_str());
                continue;
            }
            const std::vector<cab::Core::OptionReport> opts = core.options();
            std::printf("\n%s  —  %d option(s)\n", so.c_str(),
                        static_cast<int>(opts.size()));
            if (opts.empty()) ++coresWithNone;
            for (const auto& o : opts) {
                totalOptions++;
                if (o.overridden) totalOverridden++;
                std::printf("  %-34s %-18s %s%s\n", o.key.c_str(), o.chosen.c_str(),
                            o.overridden ? "OURS, core says " : "core default",
                            o.overridden ? o.defaultValue.c_str() : "");
            }
            for (const std::string& k : core.undeclaredOptionAsks())
                std::printf("  !! asked but never declared: %s\n", k.c_str());
            core.unload();
        }
        std::printf("\n%d core(s), %d option(s) answered, %d of them ours.\n",
                    static_cast<int>(sos.size()), totalOptions, totalOverridden);
        if (coresWithNone > 0)
            std::printf("%d core(s) declared NO options — worth checking by hand.\n",
                        coresWithNone);
        return 0;
    }

    // What the disk actually holds, without opening a window.
    //
    // This is the Storage screen's data, and the Storage screen is the ONE
    // exception to the cache being invisible: nobody cares what is cached until
    // they go looking, and when they do they should find it. Until that screen
    // exists this is how anyone — or anything in CI — checks that keeping,
    // eviction and the floors agree with each other.
    if (storageReport) {
        romm::Client sclient;
        if (rommAddress && sclient.setAddress(rommAddress, nullptr))
            sclient.loadToken(rommTokenPath());
        adoptUser(sclient);

        std::printf("root            %s\n", storage::root().c_str());
        std::printf("pending upload  %10.2f GB\n", cache::pendingBytes() / 1e9);
        std::printf("system reserve  %10.2f GB\n", cache::kSystemReserveBytes / 1e9);

        // PER LOCATION, because the floors are a fact about a filesystem and
        // the whole reason `roms/` and `cache/` repeat is that there is more
        // than one of them.
        const std::vector<std::string> locs = storage::locations();
        for (size_t i = 0; i < locs.size(); ++i) {
            const std::string& loc = locs[i];
            const std::vector<cache::Entry> evictable = cache::candidates(loc);
            int64_t evictableBytes = 0;
            for (const auto& e : evictable) evictableBytes += e.bytes;
            std::printf("\n%-15s %s%s\n", i == 0 ? "internal" : "games drive",
                        loc.c_str(),
                        loc == storage::keepLocation() ? "   <- kept games go here" : "");
            std::printf("  free          %10.2f GB\n", cache::freeBytes(loc) / 1e9);
            std::printf("  save floor    %10.2f GB\n", cache::saveFloorBytes(loc) / 1e9);
            std::printf("  evictable     %10.2f GB  in %zu game(s)\n",
                        evictableBytes / 1e9, evictable.size());
            // Oldest first is the whole eviction order, so printing it in order
            // is printing what would go, in the order it would go.
            for (const auto& e : evictable)
                std::printf("    evictable  %8.1f MB  rom %d  %s\n", e.bytes / 1e6,
                            e.romId, e.path.c_str());
        }

        // WHO KEPT WHAT, which is the question the old boolean could not
        // answer. A game with two keepers is a game one person releasing must
        // not take away from the other.
        const std::vector<int> kept = cache::allKeptRoms();
        std::printf("\nkept            %zu game(s)\n", kept.size());
        for (int romId : kept) {
            const cache::Placement p = cache::find(romId);
            std::string who;
            for (int id : cache::keepers(romId))
                who += (who.empty() ? "" : ", ") + std::to_string(id);
            std::printf("  rom %-6d kept by user(s) %-12s %s%s\n", romId, who.c_str(),
                        p.present ? p.entryPath.c_str() : "NOT ON THIS DISK",
                        p.present && p.isDirectory ? "/   (a set of files)" : "");
        }
        for (const storage::User& u : storage::knownUsers())
            std::printf("user            %s\n", u.dirName().c_str());
        return 0;
    }

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
    std::vector<screens::Tile> platformTiles, collectionTiles;

    if (rommAddress) {
        std::string err;
        // Where the cores are, so the catalog can tell "the manifest has a core
        // for this" apart from "this console has it built".
        catalog::setCoreDirectory(coreDir);
        if (!liveClient.setAddress(rommAddress, &err)) {
            std::fprintf(stderr, "[romm] %s\n", err.c_str());
            return 1;
        }
        if (!liveClient.loadToken(rommTokenPath())) {
            std::fprintf(stderr, "[romm] no token at %s — pair first with --romm-probe --romm-pair\n",
                         rommTokenPath().c_str());
            return 1;
        }
        adoptUser(liveClient);
        Library lib = loadLibrary(liveClient);
        cards = std::move(lib.cards);
        heroIndex = lib.heroIndex;
        heroPlatform = lib.heroPlatform;
        shelf = std::move(lib.shelf);
        favorites = std::move(lib.favorites);
        games = std::move(lib.games);
        platformTiles = std::move(lib.platformTiles);
        collectionTiles = std::move(lib.collectionTiles);
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
        // THE DEVELOPER PATH USES THE SAME TREE AS THE PRODUCT, under a user
        // id of 0 that cannot be mistaken for a person. It used to have a
        // `saves/` directory of its own, which is half of the fault open
        // question 18 opened with: a PSP save written by `--core` was not the
        // one the library path could see.
        //
        // Set before load(), not before loadGame(): retro_init happens inside
        // load(), and a core is allowed to read the directories there.
        const std::string saveDir = storage::scratchSavesDir(corePath);
        storage::makeDirs(saveDir);
        core.setDirectories(storage::biosDir(), saveDir);
        // The same overrides the library path applies, so --core plays the
        // core the same way the product does. See beginLaunch.
        core.setOptionOverrides(catalog::optionOverrides(corePath));
        if (!core.load(corePath)) {
            std::fprintf(stderr, "[frontend] core: %s\n", core.error().c_str());
            return 1;
        }
        if (!core.loadGame(romPath, storage::biosDir(), saveDir)) {
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
    // THE TOP BAR IS A ROW, above the hero. It is drawn OVER the hero's top
    // edge rather than above it, because Home has 60 points of slack and a bar
    // costs about 105 — docs/PROJECT.md priced the three options as "either
    // the bar fits, or Home's hero comes down, or the bar goes elsewhere", and
    // overlaying it is "elsewhere". It is the only one that does not undo
    // Home fitting on one screen.
    //
    // Always visible rather than revealed by pressing Up. Up from the hero is
    // a dead input today and it is tempting to spend it, but a console that
    // hides Settings behind an undiscoverable gesture is constraint 3 — the
    // one about leaving somebody stuck — and showing it costs nothing.
    //
    // No "Home" item: Home is the root and Back returns to it, so a
    // destination that does nothing when you are already there only teaches
    // people the bar is decorative. Cabinet's iOS reaches the same shape from
    // the other side, three destinations with Settings demoted, on the rule
    // that "reach should track frequency".
    enum Row { RowBar = 0, RowHero = 1, RowRecent = 2, RowFavorites = 3 };
    enum BarItem { BarLibrary = 0, BarSearch, BarSettings, BarCount };
    const char* kBarLabels[BarCount] = { "Library", "Search", "Settings" };

    const size_t shelfSlots = shelf.empty() ? cards.size() : shelf.size();
    const bool haveHero = heroIndex >= 0;
    const bool haveFavorites = !favorites.empty();

    auto rowSlots = [&](int row) -> size_t {
        if (row == RowBar) return static_cast<size_t>(BarCount);
        if (row == RowHero) return haveHero ? 2u : 0u;
        if (row == RowRecent) return shelfSlots;
        return favorites.size();
    };
    auto rowExists = [&](int row) { return rowSlots(row) > 0; };

    // The card a (row, slot) points at, or nullptr for the Resume pill, which
    // is a control rather than a card.
    auto cardAt = [&](int row, int slot) -> Card* {
        if (row == RowBar) return nullptr;   // destinations, not cards
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
        focusRow = std::clamp(initialRow, 0, static_cast<int>(RowFavorites));
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

    // ---- Where we are -------------------------------------------------------
    //
    // A stack, not a mode flag. Each tab owns a navigation stack and Library
    // pushes to a grid; backing out has to land where the person came from
    // rather than at a fixed home. The launch screen is a full-screen COVER
    // over whatever was behind it, and the player is a cover over that — which
    // is what makes quitting a game return to the launch screen and backing out
    // again return to the browsing.
    enum class Screen { Home, Library, Grid, Detail };
    std::vector<Screen> stack{Screen::Home};
    auto here = [&]() { return stack.back(); };

    screens::LibraryScreen libraryScreen;
    screens::GridScreen gridScreen;
    screens::DetailScreen detailScreen;
    libraryScreen.build(platformTiles, collectionTiles);

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
    GameSession session;
    // One worker for every upload. Started here and drained on the way out, so
    // quitting does not discard a save someone has already made.
    Uploader uploader;
    uploader.start(&liveClient);
    StateLoad stateLoad;

    // The in-game overlay: a scrim, a panel and buttons drawn over the game
    // surface. No compositing trick — the frontend owns the frame loop, which
    // is what makes this simple and is the direct payoff of hosting cores in
    // process rather than launching them.
    bool overlayOpen = false;
    // Both stick clicks together are the overlay hotkey — see where they are
    // read. Held state rather than a chord test at press time, because SDL
    // delivers the two presses as separate events.
    bool l3Down = false, r3Down = false;
    int overlaySlot = 0;
    Animated overlayFade;
    overlayFade.smooth = true;    // 350 ms ease-in-out, per the design system
    Animated overlayFocus;
    enum OverlayItem { OvResume = 0, OvSaveState, OvLoadState, OvExit, OvCount };
    const char* kOverlayLabels[OvCount] = {
        "Resume", "Save state", "Load latest state", "Exit to Home",
    };
    auto launchById = [&](int romId) -> bool {
        if (launchJob.busy()) return false;    // one at a time
        const romm::Game* g = nullptr;
        for (const auto& x : games) if (x.id == romId) { g = &x; break; }
        if (!g) { std::fprintf(stderr, "[launch] no game with id %d\n", romId); return false; }

        std::string lerr;
        if (!beginLaunch(launchJob, liveClient, *g, coreDir, &lerr)) {
            std::fprintf(stderr, "[launch] %s\n", lerr.c_str());
            return false;
        }
        std::fprintf(stderr, "[launch] %s (%s) via %s\n", g->name.c_str(),
                     g->platformName.c_str(), launchJob.coreName.c_str());
        return true;
    };

    // Human-readable bytes, for the one refusal a person is ever shown.
    auto gigabytes = [](int64_t b) {
        char buf[32];
        std::snprintf(buf, sizeof buf, "%.1f GB", static_cast<double>(b) / 1e9);
        return std::string(buf);
    };

    // DOWNLOAD, WHICH IS THE ONLY DELIBERATE STORAGE ACT IN THE PRODUCT.
    //
    // The cache is invisible by decision: pressing Play fetches a game that is
    // not here and says nothing about it, because the feedback that matters —
    // a progress bar, and Escape to back out — already exists at the only
    // moment it is useful. So this row is not "is it cached". It is "put this
    // game on the machine and do not take it away again", which is a KEEP, and
    // keeping is the one place the console is allowed to say no.
    //
    // Both floors are checked here, before a byte moves, because refusing after
    // a two-gigabyte download would be the same answer at a much higher price.
    auto downloadById = [&](int romId) -> void {
        if (launchJob.busy()) return;    // one at a time
        const romm::Game* g = nullptr;
        for (const auto& x : games) if (x.id == romId) { g = &x; break; }
        if (!g) return;

        // Against the disk the game will actually occupy. A kept game going on
        // the games drive cannot threaten the internal disk's floors at all,
        // which is the quiet second benefit of the split — see open question 14.
        const cache::Placement where = cache::find(romId);
        const std::string keepOn =
            where.present ? where.location : storage::keepLocation();
        const cache::KeepVerdict v = cache::mayKeep(keepOn, romId, g->sizeBytes);
        if (!v.allowed) {
            // The one failure the person ever sees, and the number is what makes
            // it actionable: without it "the disk is full" is a dead end.
            detailScreen.setNotice("Not enough room — the disk is full of things "
                                   "you asked me to keep. Remove " +
                                   gigabytes(v.shortfallBytes) + " to keep this one.");
            std::fprintf(stderr,
                         "[keep] refused %s: %lld reclaimable against a %lld floor\n",
                         g->name.c_str(), static_cast<long long>(v.reclaimableBytes),
                         static_cast<long long>(v.floorBytes));
            return;
        }

        std::string derr;
        if (!beginLaunch(launchJob, liveClient, *g, coreDir, &derr,
                         /*playWhenReady=*/false, /*keepWhenReady=*/true)) {
            detailScreen.setNotice(derr);
            std::fprintf(stderr, "[download] %s\n", derr.c_str());
            return;
        }
        detailScreen.setNotice("");
        std::fprintf(stderr, "[download] %s (%.0f MB)\n", g->name.c_str(),
                     static_cast<double>(g->sizeBytes) / 1e6);
    };

    // "Remove download" removes the download. If nobody else is keeping the
    // game, the bytes go and the space comes back — because reclaiming space is
    // why a person presses a row with that name on it.
    //
    // The exception is the game being played right now, which the core has open;
    // that one drops into the cache and goes when the session ends.
    //
    // WHAT ACTUALLY HAPPENED IS NOT THIS FUNCTION'S TO SAY, and the first
    // version said it anyway — it printed "released to the cache" every time,
    // including on the run where the game stayed exactly where it was because a
    // second person still wanted it. cache::unkeep reports what it did.
    auto removeDownload = [&](int romId) {
        const bool nowPlaying = playing && session.romId == romId;
        cache::unkeep(storage::currentUser(), romId, /*keepTheBytes=*/nowPlaying);
        detailScreen.setKept(false);
        detailScreen.setNotice("");
    };

    // Opening the launch screen for a card. Everything it shows is decided
    // here, so the screen holds no opinion about where any of it came from.
    auto openDetail = [&](int cardIndex) {
        if (cardIndex < 0 || cardIndex >= static_cast<int>(cards.size())) return;
        screens::GameDetail d;
        d.cardIndex = cardIndex;
        d.romId = cards[cardIndex].id;
        d.title = cards[cardIndex].title;
        d.cover = cards[cardIndex].cover;
        d.art = cards[cardIndex].art;
        for (const auto& g : games) {
            if (g.id != d.romId) continue;
            d.platform = g.platformName;
            d.sizeBytes = g.sizeBytes;
            const catalog::Coverage cov = catalog::coverageFor(g);
            d.playable = cov.support == catalog::Support::Playable;
            d.reason = cov.reason ? cov.reason : "not playable on this console";
            break;
        }
        d.kept = cache::isKeptBy(storage::currentUser(), d.romId);
        detailScreen.open(std::move(d));
        stack.push_back(Screen::Detail);
    };

    // What a screen asked for, and whether the app can do it. A screen never
    // reaches the disk, the network or a core; it returns one of these.
    // Declared before `apply` because `apply` is what it calls, and defined
    // after it for the same reason. std::function rather than a lambda, which
    // is what lets the two refer to each other at all.
    std::function<bool(screens::Nav)> navigate;

    auto apply = [&](const screens::Result& res) {
        switch (res.action) {
            case screens::Action::None:
                break;
            case screens::Action::Back:
                if (stack.size() > 1) stack.pop_back();
                break;
            case screens::Action::OpenTile: {
                const auto& tiles = libraryScreen.visible();
                if (res.value < 0 || res.value >= static_cast<int>(tiles.size())) break;
                gridScreen.open(tiles[res.value].title, tiles[res.value].cards);
                stack.push_back(Screen::Grid);
                break;
            }
            case screens::Action::OpenGame:
                openDetail(res.value);
                break;
            case screens::Action::Play:
                launchById(res.value);
                break;
            case screens::Action::Download:
                downloadById(res.value);
                break;
            case screens::Action::RemoveDownload:
                removeDownload(res.value);
                break;
        }
    };

    // THE HERO'S TWO ACTIONS, AND THEY MUST NOT COLLAPSE INTO ONE.
    //
    // Resume — the pill — goes straight into the game, with the previous
    // choices already made. The ARTWORK opens the launch screen, which is where
    // a different state, a different core or an export is chosen. Cabinet's own
    // comment on this: stopping at a screen with a Play button on it is two
    // actions, not one, and Home promises one.
    //
    // Until this session the artwork launched too, because there was no launch
    // screen for it to open and doing nothing at all was worse. There is one
    // now, so the distinction is real.
    auto activateHome = [&]() {
        // The top bar. Library works; the other two are drawn because the bar
        // has to be laid out against its real contents, and they say nothing
        // when pressed rather than pretending — a destination that goes
        // nowhere is a promise the product does not keep, and neither screen
        // exists yet.
        if (focusRow == RowBar) {
            if (focusSlot == BarLibrary) {
                libraryScreen.enter();
                stack.push_back(Screen::Library);
            } else {
                std::fprintf(stderr, "[nav] %s is not built yet\n",
                             kBarLabels[focusSlot]);
            }
            return;
        }
        if (focusRow == RowHero && focusSlot == 1) {
            if (heroIndex >= 0) launchById(cards[heroIndex].id);
            return;
        }
        // Every cover on Home opens the launch screen, the same as a cover
        // anywhere else. A shelf card that launched directly would be a second
        // Resume that nothing on the screen says is one.
        if (const Card* c = cardAt(focusRow, focusSlot)) {
            for (size_t i = 0; i < cards.size(); ++i) {
                if (cards[i].id == c->id) { openDetail(static_cast<int>(i)); return; }
            }
        }
    };

    navigate = [&](screens::Nav n) -> bool {
        switch (here()) {
            case Screen::Home: return false;
            case Screen::Library: apply(libraryScreen.key(n)); return true;
            case Screen::Grid: apply(gridScreen.key(n)); return true;
            case Screen::Detail: apply(detailScreen.key(n)); return true;
        }
        return false;
    };

    // Opening straight onto a screen, for a capture. This walks the SAME route
    // a person would: the Library is entered, a tile is opened, the launch
    // screen is opened from a card. A capture that built a screen some other
    // way would be photographing something the product cannot reach.
    if (initialScreen) {
        libraryScreen.enter();
        stack.push_back(Screen::Library);
        if (initialTab > 0) {
            // Walks the switcher the way a person would, rather than setting a
            // field: the tab change resets the grid position, and a capture
            // that skipped that would be photographing a state the product
            // cannot be in.
            libraryScreen.key(screens::Nav::Right);
            libraryScreen.key(screens::Nav::Activate);
        }
        if (SDL_strcmp(initialScreen, "library") == 0 && initialTile > 0)
            libraryScreen.focusTile(initialTile);
        if (SDL_strcmp(initialScreen, "grid") == 0 ||
            SDL_strcmp(initialScreen, "detail") == 0) {
            apply(screens::Result{screens::Action::OpenTile, initialTile});
        }
        if (SDL_strcmp(initialScreen, "detail") == 0) {
            int card = -1;
            for (size_t i = 0; i < cards.size(); ++i) {
                if (initialGame > 0 ? cards[i].id == initialGame : false) {
                    card = static_cast<int>(i);
                    break;
                }
            }
            // With no --game, the first game of the opened tile, which is what
            // somebody checking the layout wants and needs no id to hand.
            if (card < 0 && here() == Screen::Grid)
                apply(gridScreen.key(screens::Nav::Activate));
            else if (card >= 0)
                openDetail(card);
        }
    }

    // Picks up a finished job. Loading the game happens HERE, on the frame
    // thread, because the core is not thread-safe and the worker only ever
    // moved bytes.
    auto pumpLaunch = [&]() {
        const LaunchJob::Stage st = launchJob.stage.load();
        if (st == LaunchJob::Stage::Failed) {
            std::fprintf(stderr, "[launch] failed: %s\n", launchJob.message.c_str());
            // A promise the console could not deliver is not a promise. The
            // record went in before the download so the download would be safe
            // from eviction; it comes out again if the download never finished,
            // or the disk fills with reserved space holding nothing.
            // UNDOING A PROMISE, NOT RECLAIMING SPACE, so the bytes stay and
            // become evictable. The file may be a game that was already on the
            // disk before anybody pressed Download, and throwing that away
            // because a firmware fetch failed would be its own small disaster.
            if (launchJob.keepWhenReady)
                cache::unkeep(storage::currentUser(), launchJob.romId,
                              /*keepTheBytes=*/true);
            if (here() == Screen::Detail &&
                detailScreen.game().romId == launchJob.romId) {
                detailScreen.setKept(
                    cache::isKeptBy(storage::currentUser(), launchJob.romId));
                detailScreen.setNotice(launchJob.message);
            }
            launchJob.stop();
            launchJob.stage = LaunchJob::Stage::Idle;
            return;
        }
        if (st != LaunchJob::Stage::Ready) return;
        launchJob.stop();
        launchJob.stage = LaunchJob::Stage::Idle;

        // A download that was asked for rather than needed stops here. The game
        // is on the disk, prepared exactly as a launch would have prepared it,
        // so playing it later costs nothing — and nothing about it is guessed
        // at a second time.
        if (!launchJob.playWhenReady) {
            std::fprintf(stderr, "[download] %s is here%s\n", launchJob.title.c_str(),
                         launchJob.keepWhenReady ? " and kept" : "");
            if (here() == Screen::Detail &&
                detailScreen.game().romId == launchJob.romId)
                detailScreen.setKept(
                    cache::isKeptBy(storage::currentUser(), launchJob.romId));
            return;
        }

        cab::Core& core = cab::Core::shared();
        // THIS PERSON'S SAVES, FOR THIS GAME, FROM THIS CORE. It is also the
        // directory the core itself is handed, so a Sega CD's .brm and a PSP's
        // memory stick land in the same place as the battery snapshot this
        // frontend takes — which is the whole of what "two save directories"
        // and "one flat pile" cost.
        const storage::User& user = storage::currentUser();
        const std::string saveDir = storage::savesDir(
            user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
        storage::makeDirs(saveDir);

        // A DIRECTORY save comes down BEFORE the game is loaded, unlike the
        // battery below, which the core can be handed afterwards. PPSSPP mounts
        // the memory stick while the game boots, so a folder that arrives later
        // is a folder the game has already decided is not there.
        const char* dirSub = catalog::directorySaveRoot(launchJob.coreName.c_str());
        const char* launchTag = catalog::saveTag(launchJob.coreName.c_str());
        if (dirSub && launchTag && liveClient.haveToken()) {
            const std::string root = saveDir + "/" + dirSub;
            std::vector<romm::Asset> saves;
            std::string serr;
            if (liveClient.fetchSaves(launchJob.romId, &saves, &serr)) {
                const romm::Asset* newest = nullptr;
                for (const auto& a : saves) {
                    if (a.emulator != launchTag) continue;
                    if (!newest || a.updatedAt > newest->updatedAt) newest = &a;
                }
                if (newest) {
                    std::vector<uint8_t> data = liveClient.fetchAsset("saves", newest->id);
                    // SNIFFED, never taken from the name. The reference
                    // implementation's PSP saves are an Apple directory archive
                    // wearing an `.srm` extension, so the filename says nothing
                    // at all about what is inside. Anything that is not a zip is
                    // left alone rather than guessed at — better a missing save
                    // than a corrupted memory stick.
                    if (cab::looksLikeZip(data)) {
                        std::string uerr;
                        if (cab::unzipTree(data, root, &uerr)) {
                            std::fprintf(stderr, "[save] unpacked %s (%zu bytes) into %s\n",
                                         newest->fileName.c_str(), data.size(), root.c_str());
                        } else {
                            std::fprintf(stderr, "[save] %s would not unpack: %s\n",
                                         newest->fileName.c_str(), uerr.c_str());
                        }
                    } else if (!data.empty()) {
                        std::fprintf(stderr,
                                     "[save] %s is not a zip (%02x %02x %02x %02x) — left alone\n",
                                     newest->fileName.c_str(), data[0], data[1],
                                     data.size() > 2 ? data[2] : 0,
                                     data.size() > 3 ? data[3] : 0);
                    }
                }
            }
        }

        // AND THE SAVES THE CORE WRITES AS A FILE, which is most of the ones
        // on a real server and none of the ones this console used to handle.
        // Before the load for the same reason the directory save is: these
        // cores read their file once while the machine is being built. See
        // filesave.h.
        //
        // The stem is the basename of what the core is handed, without its
        // extension, because that is what these cores name the save after —
        // `lethalen.nv`, `Lunar - The Silver Star.brm`.
        std::vector<catalog::SaveFile> saveSpecs;
        {
            std::string stem = launchJob.romPath;
            if (const size_t slash = stem.find_last_of('/'); slash != std::string::npos)
                stem.erase(0, slash + 1);
            if (const size_t dot = stem.find_last_of('.'); dot != std::string::npos)
                stem.erase(dot);
            saveSpecs = catalog::saveFiles(launchJob.platformSlug,
                                           launchJob.platformFsSlug, stem);
        }
        std::vector<cab::FileSaveState> restored =
            restoreFileSaves(saveSpecs, launchJob.fsStem, saveDir, launchJob.romId,
                             launchTag, liveClient);

        if (!core.loadGame(launchJob.romPath, storage::biosDir(), saveDir)) {
            std::fprintf(stderr, "[launch] %s\n", core.error().c_str());
            return;
        }
        // Played now, so it is the LAST thing eviction should take rather than
        // whatever its download time says. The ENTRY's own mtime is the record —
        // see cache.h — so a game downloaded and never started stays oldest, and
        // it works the same whether the entry is one file or a folder.
        cache::touch(launchJob.entryPath);
        // The session, and the game's own save restored into it BEFORE the
        // first frame. A battery save is the game's progress; it has to be in
        // place when the game boots, not offered as a choice afterwards.
        session = GameSession{};
        session.romId = launchJob.romId;
        session.title = launchJob.title;
        session.saveDir = saveDir;
        session.fsStem = launchJob.fsStem;
        session.stateDir = storage::statesDir(
            user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
        session.fileSaves = std::move(restored);
        if (launchTag) session.saveTag = launchTag;
        if (const char* tag = catalog::emulatorTag(launchJob.coreName.c_str())) {
            session.stateTag = tag;
        } else {
            std::fprintf(stderr,
                         "[sync] no settled tag for %s — states stay local%s\n",
                         launchJob.coreName.c_str(),
                         launchTag ? ", saves travel" : ", and so do saves");
        }
        // The baseline for a directory save, taken AFTER the restore above and
        // BEFORE the game has had a chance to write anything. What changes
        // between here and the quit is what this game saved. See GameSession.
        if (dirSub) {
            session.dirSaveRoot = saveDir + "/" + dirSub;
            session.dirAtLaunch = cab::listTree(session.dirSaveRoot);
            std::fprintf(stderr, "[save] %s holds %zu file(s) at launch\n",
                         session.dirSaveRoot.c_str(), session.dirAtLaunch.size());
        }

        if (core.saveRamSize() > 0 && !session.saveTag.empty()) {
            std::vector<romm::Asset> saves;
            std::string serr;
            if (liveClient.fetchSaves(session.romId, &saves, &serr)) {
                const romm::Asset* newest = nullptr;
                for (const auto& a : saves) {
                    if (a.emulator != session.saveTag) continue;
                    if (!newest || a.updatedAt > newest->updatedAt) newest = &a;
                }
                if (newest) {
                    std::vector<uint8_t> data = liveClient.fetchAsset("saves", newest->id);
                    if (!data.empty() && core.writeSaveRam(data)) {
                        session.saveAtLaunch = data;
                        std::fprintf(stderr, "[save] restored %s (%zu bytes)\n",
                                     newest->fileName.c_str(), data.size());
                    }
                }
            }
        }
        if (session.saveAtLaunch.empty()) core.readSaveRam(session.saveAtLaunch);
        std::fprintf(stderr, "[save] battery is %zu bytes\n", core.saveRamSize());

        // Options again, AFTER the game is loaded. Some cores declare nothing
        // until they know what they are running: FBNeo's and MAME's options are
        // per-driver dipswitches, so their table does not exist until a machine
        // is chosen. A count taken only at core-load time reports zero for them
        // and looks like a core with nothing to configure.
        {
            const std::vector<cab::Core::OptionReport> opts = core.options();
            int asked = 0;
            for (const auto& o : opts) if (o.asked) ++asked;
            std::fprintf(stderr, "[options] %zu declared, %d asked for so far\n",
                         opts.size(), asked);
            for (const std::string& k : core.undeclaredOptionAsks())
                std::fprintf(stderr, "[options] asked but never declared: %s\n",
                             k.c_str());
        }

        // OPEN THE AUDIO DEVICE. Until 2026-09-19 this happened ONLY on the
        // `--core` developer path, so **a game launched from the library had
        // no sound at all** — `audioStream` stayed null and nothing was ever
        // opened. Found on the A9 Pro by MMagTech simply listening, with
        // Crazy Taxi 2 running and PipeWire reporting zero streams.
        //
        // It survived this long because every audio claim in this project was
        // made by COUNTING SAMPLES out of drainAudio() rather than by hearing
        // anything — "2,384 frames, zero audio" is a sample count. A headless
        // VM has nothing to listen with, so the one test nobody could run is
        // the one that would have caught it.
        //
        // Opened here rather than at startup because the rate comes from the
        // core: av_info is not known until a game is loaded.
        if (!audioStream) {
            SDL_AudioSpec src{};
            src.format = SDL_AUDIO_S16;
            src.channels = 2;
            src.freq = static_cast<int>(core.avInfo().sampleRate);
            audioStream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK,
                                                    &src, nullptr, nullptr);
            if (audioStream) {
                SDL_ResumeAudioStreamDevice(audioStream);
                std::fprintf(stderr, "[frontend] audio out at %d Hz\n", src.freq);
            } else {
                // A console with no sound card is still a console.
                std::fprintf(stderr, "[frontend] no audio device: %s\n", SDL_GetError());
            }
        }

        std::fprintf(stderr, "[launch] running %s\n", core.coreName().c_str());
        playing = true;
    };

    // Remembered focus per row, which is the behaviour tvOS gives free and the
    // one people notice missing: leaving Recent at the sixth cover and coming
    // back to the first is the kind of thing that feels broken without anyone
    // being able to say why.
    int rememberedSlot[4] = {0, 0, 0, 0};

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
        for (int i = 0; i < 4; ++i) {
            const int candidate = row + delta;
            if (candidate < RowBar || candidate > RowFavorites) return;
            row = candidate;
            if (rowExists(row)) break;
            if (row == RowBar || row == RowFavorites) return;
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
    enum class InputOwner { Keyboard, Overlay, Game, UI };
    auto inputOwner = [&]() {
        if (keyboard.isOpen()) return InputOwner::Keyboard;
        // The overlay takes the pad FROM the core while it is open, which is
        // the whole rule: never both. tvOS does this by turning the focus
        // engine off during play; here it is this one line.
        if (overlayOpen) return InputOwner::Overlay;
        if (playing) return InputOwner::Game;
        return InputOwner::UI;
    };

    // Leaving a game. The save goes up FIRST — this is the trigger that matters
    // most, because nobody should lose progress by quitting — and only then is
    // the core torn down.
    // Quitting is DEFERRED while a core is still booting, and the waiting has
    // to happen out here in the frame loop rather than inside unloadGame.
    //
    // The first attempt did it in unloadGame, spinning on retro_run until the
    // core produced audio. It never did, and the reason is the useful part:
    // PPSSPP's emulation thread only advances when the frontend COMPLETES a
    // frame, not merely when retro_run is called. A tight loop with no present
    // in it makes no progress at all — which is the same signature that makes
    // --state-test unable to warm this core up, and now has one explanation
    // rather than two mysteries.
    //
    // So the exit sets a flag, the ordinary loop keeps running and drawing, and
    // the quit completes once the machine is up. Capped, because somebody
    // quitting must not wait on a core that is never going to boot.
    bool exitPending = false;
    uint64_t exitWaitStart = 0;
    auto finishExit = [&]() {
        syncSave(session, uploader);
        cab::Core::shared().unloadGame();
        // AFTER the unload, for the one platform whose save is a tree. The
        // battery above is read out of the core's own memory and is finished
        // the moment the game stopped writing to it; a directory save is files
        // on a disk, and retro_unload_game is where a core flushes them. The
        // reference implementation captures this class after shutdown for
        // exactly that reason, and it is the same rule that lost saves here
        // once already when it was ignored.
        //
        // Safe to run twice: syncDirSave compares against its own baseline and
        // sends nothing when nothing moved.
        if (!session.dirSaveRoot.empty()) syncDirSave(session, uploader);
        // And the same trigger for the same reason, for the class that is one
        // file rather than a tree. This is where a Dreamcast's card is read
        // back out of the system directory and taken out of `bios/`.
        syncFileSaves(session, uploader);
        playing = false;
        overlayOpen = false;
        overlayFade.retarget(0.0f, kOverlayFade);
        std::fprintf(stderr, "[overlay] exited to Home\n");
    };

    auto exitToHome = [&]() {
        if (!cab::Core::shared().running()) {
            // Still building the machine. Close the overlay so the quit looks
            // like it was accepted — it has been — and let the loop finish it.
            exitPending = true;
            exitWaitStart = SDL_GetTicksNS();
            overlayOpen = false;
            overlayFade.retarget(0.0f, kOverlayFade);
            std::fprintf(stderr, "[overlay] quit accepted; waiting for the core to boot\n");
            return;
        }
        finishExit();
    };

    auto pumpExit = [&]() {
        if (!exitPending || !playing) return;
        // WALL CLOCK, NOT FRAMES, and that distinction is the whole fix. The
        // first version capped the wait at 120 FRAMES, which crashed in one
        // configuration and not another for a reason that looked like magic: a
        // capture at 1920x1080 and one at the 1024x768 window differ threefold
        // in fill rate on a software rasteriser, so the same 120 frames are
        // seconds in one and an instant in the other. The core's boot takes the
        // time it takes.
        const double waited = (SDL_GetTicksNS() - exitWaitStart) / 1e9;
        const bool up = cab::Core::shared().running();
        if (up || waited > 15.0) {
            std::fprintf(stderr, "[overlay] quitting after %.1fs%s\n", waited,
                         up ? "" : " — the core never booted, unloading anyway");
            exitPending = false;
            finishExit();
        }
    };

    auto overlayActivate = [&]() {
        switch (overlaySlot) {
            case OvResume:
                overlayOpen = false;
                overlayFade.retarget(0.0f, kOverlayFade);
                break;
            case OvSaveState: saveStateNow(session, uploader); break;
            case OvLoadState: beginLoadLatestState(stateLoad, session, liveClient); break;
            case OvExit: exitToHome(); break;
            default: break;
        }
    };

    auto toggleOverlay = [&]() {
        if (!playing) return;
        overlayOpen = !overlayOpen;
        overlaySlot = 0;
        overlayFade.retarget(overlayOpen ? 1.0f : 0.0f, kOverlayFade);
        overlayFocus.retarget(1.0f, kOverlayFocusDuration);
        overlayFocus.elapsed = kOverlayFocusDuration;
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
                        // In a game, Escape is the overlay — not a quit. A
                        // download is the one thing it should interrupt.
                        if (launchJob.busy()) launchJob.cancel = true;
                        else if (playing) toggleOverlay();
                        // Back, while there is anywhere to go back to. Quitting
                        // from the middle of the Library would throw away the
                        // whole stack the person had walked down.
                        else if (stack.size() > 1) navigate(screens::Nav::Back);
                        else running = false;
                    }
                    if (owner == InputOwner::Overlay) {
                        if (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN) {
                            const int delta = (e.key.key == SDLK_DOWN) ? 1 : -1;
                            overlaySlot = std::clamp(overlaySlot + delta, 0, OvCount - 1);
                            overlayFocus.retarget(0.0f, 0.0f);
                            overlayFocus.elapsed = 0.0f;
                            overlayFocus.retarget(1.0f, kOverlayFocusDuration);
                        }
                        if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE)
                            overlayActivate();
                        break;
                    }
                    // F5 writes a state, F8 restores the newest one THIS build can
                    // load, F6 pushes the game's own save. Quitting syncs the
                    // save by itself; F6 is for testing without quitting.
                    if (playing && e.key.key == SDLK_F5) saveStateNow(session, uploader);
                    if (playing && e.key.key == SDLK_F8) beginLoadLatestState(stateLoad, session, liveClient);
                    if (playing && e.key.key == SDLK_F6) syncSave(session, uploader);
                    if (owner != InputOwner::UI) break;
                    // Anything pushed on top of Home owns its own focus model,
                    // so the press goes there and this function does not get an
                    // opinion about it. Home is handled below because it is the
                    // one screen whose focus lives here.
                    if (here() != Screen::Home) {
                        switch (e.key.key) {
                            case SDLK_LEFT:  navigate(screens::Nav::Left); break;
                            case SDLK_RIGHT: navigate(screens::Nav::Right); break;
                            case SDLK_UP:    navigate(screens::Nav::Up); break;
                            case SDLK_DOWN:  navigate(screens::Nav::Down); break;
                            case SDLK_RETURN:
                            case SDLK_SPACE: navigate(screens::Nav::Activate); break;
                            case SDLK_BACKSPACE: navigate(screens::Nav::Back); break;
                            default: break;
                        }
                        break;
                    }
                    if (e.key.key == SDLK_LEFT) moveFocus(-1);
                    if (e.key.key == SDLK_RIGHT) moveFocus(+1);
                    if (e.key.key == SDLK_UP) moveRow(-1);
                    if (e.key.key == SDLK_DOWN) moveRow(+1);
                    // Until the navigation bar exists, this is the door to the
                    // Library. See docs/NEXT-SESSION.md: the bar the design
                    // system specifies costs exactly the vertical slack Home has
                    // left, which is a measurement only a television can settle.
                    if (e.key.key == SDLK_L) {
                        libraryScreen.enter();
                        stack.push_back(Screen::Library);
                    }
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) {
                        pressing = true;
                        if (!playing) activateHome();
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
                    // Guarded by the owner and by which screen is in front. It
                    // was neither, which meant a d-pad left moved Home's focus
                    // while a game was running — the same shape as the bug the
                    // input-owner rule above exists to prevent.
                    if (owner == InputOwner::UI && here() == Screen::Home) {
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT) moveFocus(-1);
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT) moveFocus(+1);
                    } else if (owner == InputOwner::UI) {
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_LEFT)
                            navigate(screens::Nav::Left);
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_RIGHT)
                            navigate(screens::Nav::Right);
                    }
                    // BOTH STICK CLICKS, NOT START. Start is the pause button
                    // on nearly every system this console emulates, and taking
                    // it meant a game could never be paused — MMagTech, on the
                    // A9, 2026-09-19, before playing the first real game on it.
                    //
                    // L3+R3 is Cabinet's default and its reasoning carries:
                    // RetroArch's alternative to Select+Start, chosen because
                    // analog trigger pairs collide with real gameplay —
                    // braking and accelerating together in a racing game is
                    // the obvious case — "while clicking both sticks at once
                    // has no gameplay meaning in anything this app runs".
                    //
                    // Still hardcoded here. Cabinet makes it remappable and
                    // GLOBAL rather than per-controller, on the grounds that
                    // the risk comes from the game and not the pad model, and
                    // lets the second button be cleared for single-button
                    // mode. That belongs with the remapping screen.
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK)
                        l3Down = true;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                        r3Down = true;
                    if (l3Down && r3Down && (playing || overlayOpen)) {
                        l3Down = r3Down = false;   // one toggle per pair, not per frame
                        toggleOverlay();
                        break;
                    }
                    if (owner == InputOwner::Overlay) {
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP)
                            overlaySlot = std::max(0, overlaySlot - 1);
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN)
                            overlaySlot = std::min(OvCount - 1, overlaySlot + 1);
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) overlayActivate();
                        // East is Back, and Back from the overlay is Resume.
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) toggleOverlay();
                        break;
                    }
                    if (owner != InputOwner::UI) break;
                    if (here() != Screen::Home) {
                        switch (e.gbutton.button) {
                            case SDL_GAMEPAD_BUTTON_DPAD_UP:
                                navigate(screens::Nav::Up); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                                navigate(screens::Nav::Down); break;
                            case SDL_GAMEPAD_BUTTON_SOUTH:
                                navigate(screens::Nav::Activate); break;
                            // East is Back everywhere in this product, which is
                            // the rule the in-game overlay already follows.
                            case SDL_GAMEPAD_BUTTON_EAST:
                                navigate(screens::Nav::Back); break;
                            default: break;
                        }
                        break;
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) activateHome();
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_UP) moveRow(-1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_DPAD_DOWN) moveRow(+1);
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) pressing = true;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START) running = false;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_STICK)
                        l3Down = false;
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_STICK)
                        r3Down = false;
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

                        // THE SHOULDERS, THE TRIGGERS AND THE RIGHT STICK.
                        // None of these were mapped until 2026-09-19 — the
                        // pad sent a d-pad, four face buttons, Start, Select
                        // and one stick, and RetroPad's other six inputs went
                        // nowhere. Found by MMagTech on the A9 trying to play
                        // Crazy Taxi, where the triggers ARE drive and
                        // reverse, so the game could not be played at all.
                        //
                        // It survived because nothing headless presses a
                        // button: every measurement to date drove the pad
                        // from code or watched an attract demo.
                        if (down(SDL_GAMEPAD_BUTTON_LEFT_SHOULDER))  pad.buttons |= bit(cab::L);
                        if (down(SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) pad.buttons |= bit(cab::R);
                        // L3/R3 also open the overlay as a pair. They still
                        // reach the core individually: Cabinet's rule is that
                        // the hotkey "applies regardless of what either button
                        // is otherwise bound to", and the overlay pauses the
                        // core the instant it opens anyway.
                        if (down(SDL_GAMEPAD_BUTTON_LEFT_STICK))  pad.buttons |= bit(cab::L3);
                        if (down(SDL_GAMEPAD_BUTTON_RIGHT_STICK)) pad.buttons |= bit(cab::R3);

                        // Analog triggers as digital L2/R2, which is what a
                        // RetroPad's L2/R2 are for every core in this set.
                        // Half travel: a hair-trigger fires on the spring's
                        // own slop and a full-travel one never fires on a worn
                        // pad.
                        const int kTrigger = 16384;
                        if (SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > kTrigger)
                            pad.buttons |= bit(cab::L2);
                        if (SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > kTrigger)
                            pad.buttons |= bit(cab::R2);

                        pad.leftX = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTX) / 32767.0f;
                        pad.leftY = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFTY) / 32767.0f;
                        pad.rightX = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTX) / 32767.0f;
                        pad.rightY = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHTY) / 32767.0f;
                    }
                }
                SDL_free(ids);
            }
            core.setPad(0, pad);

            // Wall-clock pacing is the product's, and it is wrong for a
            // capture. `--frames 180` asks for 180 drawn frames, and with
            // nothing to wait for offscreen those 180 take under a tenth of a
            // second — so the core is paced against a tenth of a second and
            // emulates FIVE frames, which is a black boot screen for every
            // console ever made. The first run of Mupen64Plus reported exactly
            // that and it looked like the core had failed.
            //
            // So a capture steps one emulated frame per drawn frame: `--frames`
            // then means what it says, and the same command gives the same
            // picture on a fast machine and a slow one. The instrument being
            // the thing that is wrong has cost this project a day already.
            // THE OVERLAY PAUSES THE EMULATOR. It did not until 2026-09-19,
            // so the game carried on being played behind the menu — in Crazy
            // Taxi you would still be driving while reading it. Cabinet has
            // always done this (`openMenu` sets `renderer.paused = true`);
            // this console simply never did, because nobody had opened the
            // overlay with a game they were actually playing.
            //
            // Not the game's own pause: the core stops being stepped, which
            // works for every system whether or not it has a pause button.
            if (overlayOpen) {
                // Nothing to step. The last frame stays uploaded, so the
                // menu sits over a frozen picture rather than a black one.
            } else if (shotMode) {
                core.runFor(1.0 / std::max(core.avInfo().fps, 1.0));
            } else {
                core.runFor(dt);
            }
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
        // The same errand as the launch above, for the action a person takes on
        // the launch screen. It goes through downloadById, so the floors are
        // checked exactly as they would be for a press.
        if (autoDownloadId > 0 && !launchJob.busy()) {
            const int id = autoDownloadId;
            autoDownloadId = 0;
            downloadById(id);
        }
        if (autoUnkeepId > 0) {
            const int id = autoUnkeepId;
            autoUnkeepId = 0;
            removeDownload(id);
        }
        if (autoLaunchId > 0 && !playing) {
            autoLaunchAfter -= dt;
            if (autoLaunchAfter <= 0.0f) {
                const int id = autoLaunchId;
                autoLaunchId = 0;
                launchById(id);
            }
        }

        pumpLaunch();
        pumpExit();
        pumpStateLoad(stateLoad);
        if (overlayDemo && playing && !overlayOpen) { overlayDemo = false; toggleOverlay(); }
        if (overlayExitDemo && playing) {
            static int t = 0;
            // How long the game is left running before the overlay quits it.
            // Was a fixed 120 frames, which is two seconds — long enough to
            // watch the transition and far too short for anything else. A PSP
            // game is still BOOTING at that point, and a save cannot be tested
            // at all because the game has not had time to write one.
            if (++t == overlayExitAfter) { toggleOverlay(); overlaySlot = OvExit; }
            if (t == overlayExitAfter + 60) { overlayExitDemo = false; overlayActivate(); }
        }

        // The round trip, once, a couple of seconds into the game so there is
        // something in memory worth snapshotting.
        if (syncTest && playing) {
            static int sinceStart = 0;
            if (++sinceStart == 150) {
                std::fprintf(stderr, "[sync-test] --- saving ---\n");
                const uint64_t t0 = SDL_GetTicksNS();
                saveStateNow(session, uploader);
                // Forced: at a title screen with no input the battery has not
                // changed, and "nothing to send" is the correct behaviour but
                // proves nothing about whether sending works.
                session.saveAtLaunch.clear();
                syncSave(session, uploader);
                // The same forcing for the saves the core writes as files, and
                // it is the only way to see that half send anything: those are
                // captured after the unload rather than here, and a headless
                // run cannot press the buttons that would make a game save.
                // So the baseline is dropped now and the quit-time capture
                // treats whatever the core flushed as new. `hadOne` goes with
                // it, because the freshness guard is the other thing under
                // test and a forced run must not be stopped by it.
                for (cab::FileSaveState& f : session.fileSaves) {
                    f.atLaunch.clear();
                    f.hadOne = true;
                }
                std::fprintf(stderr,
                             "[sync-test] %zu file save(s) will be sent at the quit\n",
                             session.fileSaves.size());
                // What the game actually paid. Everything after this point is
                // on the worker, so this is the whole cost to the picture.
                std::fprintf(stderr, "[sync-test] frame thread blocked %.2f ms\n",
                             (SDL_GetTicksNS() - t0) / 1e6);
                std::fprintf(stderr, "[sync-test] --- loading back ---\n");
                beginLoadLatestState(stateLoad, session, liveClient);
                std::fprintf(stderr, "[sync-test] --- done ---\n");
            }
        }
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
        overlayFade.tick(dt);
        overlayFocus.tick(dt);
        {
            // Every screen ticks, not only the one in front: a screen that is
            // pushed over keeps its focus animation settled rather than
            // resuming mid-transition when the person comes back to it.
            screens::Ctx ctx{renderer, text, images, renderer.scale(), &cards};
            libraryScreen.tick(dt);
            gridScreen.tick(dt, ctx);
            detailScreen.tick(dt);
        }
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
                //
                // A VERTICAL ARCADE BOARD TURNS THE PICTURE AND THE LAYOUT
                // WITH IT. Its monitor was bolted into the cabinet sideways,
                // so the board renders sideways and asks for a quarter turn.
                // After the turn the picture is TALL: the rows running down
                // the screen are the source's columns and the aspect is the
                // inverse of the stored one.
                const bool quarterTurn = core.rotatedQuarterTurn();
                //
                // AND A TURNED PICTURE IGNORES THE CORE'S DECLARED ASPECT.
                // This looks like throwing away the better answer and is the
                // opposite: FBNeo reports a vertical board's aspect ALREADY
                // turned — 0.75 for DoDonPachi DaiOuJou, which is the 3:4 of
                // the cabinet's tube on its side, not the 448x224 sitting in
                // the framebuffer. Inverting that would apply the turn twice
                // and stretch the picture. Cabinet shipped it the wrong way
                // round first and the comment in `aspectFitVertices` is the
                // record: it "stretched every vertical game".
                //
                // The declared aspect is still what a picture with square
                // pixels needs and Saturn is unplayable without it, so it
                // keeps winning everywhere else. Nothing is lost by dropping
                // it here, for the reason Cabinet gives: the platforms whose
                // pixels are not square never rotate, and the boards that
                // rotate are square-pixel.
                const float declaredAspect = quarterTurn ? 0.0f : core.avInfo().aspectRatio;
                const float aspect = declaredAspect > 0 ? declaredAspect : srcW / srcH;
                //
                // EXCEPT for a hardware-rendered core, where integer scaling
                // is the wrong idea rather than a stricter one. A Dreamcast's
                // output is already a rendering of a 3D scene at whatever
                // internal resolution the core was asked for — there is no
                // grid of deliberate pixels to preserve — and at 3x internal
                // resolution the frame is 1920x1440, where flooring to an
                // integer gives zero, clamps to one, and draws 360 rows off
                // the bottom of the screen.
                //
                // Everything from here is in terms of the picture as SHOWN
                // rather than as stored, which is the only version of it the
                // screen has an opinion about.
                const float shownAspect = quarterTurn ? 1.0f / aspect : aspect;
                const float shownRows = quarterTurn ? srcW : srcH;
                //
                // AND A TURNED PICTURE FILLS THE HEIGHT RATHER THAN INTEGER
                // SCALING. MMagTech's call, 2026-09-19, asked because nothing
                // makes a vertical game fill a horizontal screen without
                // lying and the two honest answers differ. A 240x320 board
                // integer-scaled into 1080 points gives 3x, a 720-point-tall
                // window with 180 points of dead space above and below it on
                // top of the pillarboxing that is already unavoidable; filling
                // the height gives 3.375x and the largest true-shaped picture
                // the panel can show. The dot grid argument that earns integer
                // scaling a Game Boy is worth less here than the 33% of the
                // screen it costs.
                const bool integerScale = !core.hardwareRendered() && !quarterTurn;
                float scale = std::min(ui::kCanvasWidth / (shownRows * shownAspect),
                                       ui::kCanvasHeight / shownRows);
                if (integerScale) {
                    scale = std::floor(scale);
                    if (scale < 1.0f) scale = 1.0f;
                }
                const float dh = shownRows * scale;
                const float dw = dh * shownAspect;
                const float px = (ui::kCanvasWidth - dw) * 0.5f;
                const float py = (ui::kCanvasHeight - dh) * 0.5f;
                // Where the picture actually sits in that texture. A
                // software core answers "all of it, the right way up"; a
                // hardware core answers a corner of a larger target with its
                // rows the other way round, and neither the layout above nor
                // the draw below has to know which.
                float u0, v0, u1, v1;
                core.frameUV(u0, v0, u1, v1);
                // Opaque, always. A game's frame is a picture, and whatever is
                // in its alpha channel is the emulated machine's own state
                // rather than a compositing instruction. Found on PPSSPP:
                // Lumines leaves the PSP framebuffer's alpha at nearly zero,
                // the blend took it literally, and the whole 1920x1080 capture
                // peaked at RGB (4,4,4) — a picture that was there all along
                // and read as a core that renders black.
                //
                // The turn goes to the DRAW and the shape goes to the layout,
                // and they are two different things. frameUV has already said
                // where the picture is in the texture and which way up its
                // rows are; rotation says how the picture it found is turned,
                // and the two compose — which is what a hardware-rendered
                // vertical board needs.
                ui::drawImageTexture(renderer, core.texture(), px, py, dw, dh, u0, v0, u1,
                                     v1, true, static_cast<int>(core.rotation()));

                // The glow goes over the bars, not under the picture: it is
                // drawn after, and its shader discards inside the picture rect,
                // so no game pixel is ever covered. An integer-scaled handheld
                // on a 4K set is mostly dead space, which is exactly the case
                // this exists for.
                //
                // It reshapes itself for a vertical board for free, and that
                // is worth saying because it looks like it should need work.
                // The shader ramps from the picture's edge to the screen's in
                // each direction separately, so handing it a tall rect lights
                // two wide bars at the sides and nothing above or below, where
                // a picture that fills the height leaves no room to ramp
                // across. The rect is the whole interface.
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
        } else if (here() != Screen::Home) {
            // Everything past Home draws itself. The world half goes here; the
            // frosted half goes after presentScene, below, because glass reads
            // the scene through itself.
            screens::Ctx ctx{renderer, text, images, sc, &cards};
            switch (here()) {
                case Screen::Library: libraryScreen.draw(ctx); break;
                case Screen::Grid: gridScreen.draw(ctx); break;
                case Screen::Detail: detailScreen.draw(ctx); break;
                case Screen::Home: break;   // unreachable, and the compiler asks
            }
        } else {

        // The height a shelf will occupy, known before it is drawn so the
        // scroll target can be computed this frame rather than one frame late.
        // No caption row. The focused card's title rides in the shelf header
        // instead, which is what makes Home fit in one screen — see the
        // budget in design.h.
        const float shelfBlockHeight =
            text.lineHeight(ui::TextStyle::Title3, sc) + 12.0f + kShelfHeadroom +
            kShelfCoverHeight + kShelfHeadroom;

        const float heroHeight = haveHero ? kHeroHeight : 0.0f;
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
            // THE ART FILLS THE HERO. The title used to sit in a full-width
            // strip across the bottom, which cost about 106 of the hero's 340
            // points — a third of the artwork — to carry two short lines.
            // MMagTech, 2026-09-19: "the long strip where the title is for
            // the hero is taking a lot of space that could be used if we
            // found a better way to format/visualise that area." It is a
            // plate sized to its own text now, at the bottom left, with the
            // art running the full height behind it.
            const float artH = heroH;

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

            // The shelf header: Title 3, with the chevron that says the row
            // continues into a screen of its own.
            //
            // TITLE 3 AND NOT TITLE 2, changed on the panel 2026-09-19. At
            // Title 2 the headings read too large beside 210-point covers —
            // the heading was shouting over the artwork it labels. The room
            // it frees goes to the hero.
            const float headerBaseline = top + text.ascent(ui::TextStyle::Title3, sc);
            text.draw(renderer, label, kContentInset, headerBaseline,
                      ui::TextStyle::Title3, ui::Color::white(1.0f), sc);
            const float headerWidth = text.measure(label, ui::TextStyle::Title3, sc);
            // A step below the heading, not level with it: the chevron says
            // "this row continues", it is not part of the heading, and at
            // heading size it competes with it.
            text.draw(renderer, "\xE2\x80\xBA", kContentInset + headerWidth + 10.0f,
                      headerBaseline, ui::TextStyle::Callout, ui::Color::white(0.30f), sc);

            // THE FOCUSED CARD'S TITLE, HERE RATHER THAN UNDER THE COVER.
            //
            // A caption under every cover reserved about 45 points per shelf
            // and Home could not afford two shelves with it. Putting the one
            // title you are actually reading into the header costs nothing
            // vertically, and it is only ever one line because only one card
            // is focused.
            //
            // Only on the focused row: a title under an unfocused shelf would
            // be a label for something nobody is pointing at, and two of them
            // at once reads as two headings.
            if (rowFocused && !cards.empty()) {
                const size_t slot = static_cast<size_t>(
                    std::clamp(focusSlot, 0, static_cast<int>(count) - 1));
                const std::string& title = cards[at(slot)].title;
                const float titleX = kContentInset + headerWidth + 10.0f +
                                     text.measure("\xE2\x80\xBA", ui::TextStyle::Callout, sc) +
                                     24.0f;
                const float room = ui::kCanvasWidth - kContentInset - titleX;
                text.draw(renderer, text.truncate(title, ui::TextStyle::Callout, sc, room),
                          titleX, headerBaseline, ui::TextStyle::Callout,
                          ui::Color::white(0.60f), sc);
            }

            const float shelfTop =
                top + text.lineHeight(ui::TextStyle::Title3, sc) + 12.0f + kShelfHeadroom;

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

                // No caption here any more — the focused card's title is in
                // the shelf header. At 158 points wide a caption truncated
                // most titles to nothing useful anyway.
            }
        }
            // Header, headroom, cover, headroom. No caption row: the focused
            // title is in the header, so it costs nothing here.
            return (shelfTop - top) + kShelfCoverHeight + kShelfHeadroom;
        };

        float rowY = shelfHeaderY;
        rowY += drawShelf("Recent", shelf, RowRecent, rowY);
        // Only when there are any. An empty Favorites row is worse than none.
        if (haveFavorites) drawShelf("Favorites", favorites, RowFavorites, rowY);
        }  // end of the shelf branch

        // The overlay's scrim belongs to the WORLD, not to the overlay, so it
        // is drawn before the scene is presented. Put it after and the panel's
        // glass samples the UN-dimmed picture underneath: over a bright game
        // the panel comes out milky and its own labels stop being readable,
        // which is exactly what happened the first time. Dimming the picture is
        // what a scrim is for; the glass should be blurring the dimmed thing.
        if (overlayFade.value() > 0.001f) {
            renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                   ui::Color::black(0.55f * overlayFade.value())});
        }

        renderer.presentScene();

        if (!playing && here() != Screen::Home) {
            screens::Ctx ctx{renderer, text, images, sc, &cards};
            switch (here()) {
                case Screen::Library: libraryScreen.drawGlass(ctx); break;
                case Screen::Grid: gridScreen.drawGlass(ctx); break;
                case Screen::Detail: detailScreen.drawGlass(ctx); break;
                case Screen::Home: break;
            }
        }

        // ---- The in-game overlay -------------------------------------------
        //
        // No compositing trick: the frontend owns the frame loop, so this is a
        // scrim, a panel and some buttons drawn over whatever the game just
        // rendered. That is the direct payoff of hosting cores in process
        // rather than launching them.
        //
        // Drawn after presentScene because the panel is glass, and it reads the
        // game's own picture through itself.
        const float ovl = overlayFade.value();
        if (ovl > 0.001f) {
            const float panelH = static_cast<float>(OvCount) * kOverlayButtonHeight +
                                 static_cast<float>(OvCount - 1) * kOverlayButtonGap +
                                 64.0f;
            const float px = (ui::kCanvasWidth - kOverlayPanelWidth) * 0.5f;
            // Rises slightly as it arrives rather than only fading: a panel that
            // just materialises reads as a glitch.
            const float py = (ui::kCanvasHeight - panelH) * 0.5f + (1.0f - ovl) * 24.0f;
            renderer.drawGlass(ui::Rect{px, py, kOverlayPanelWidth, panelH,
                                        kOverlayPanelRadius, ui::Color::white(0)},
                               6.0f, ui::Color::black(0.30f * ovl));

            for (int i = 0; i < OvCount; ++i) {
                const bool on = (i == overlaySlot);
                const float f = on ? overlayFocus.value() : 0.0f;
                // 1.04, the pause menu's own tier. A full-width button growing
                // a cover's tenth would run into its neighbours.
                const float sc2 = 1.0f + f * (kOverlayFocusScale - 1.0f);
                const float bw = (kOverlayPanelWidth - 64.0f) * sc2;
                const float bh = kOverlayButtonHeight * sc2;
                const float bx = px + (kOverlayPanelWidth - bw) * 0.5f;
                const float by = py + 32.0f +
                                 i * (kOverlayButtonHeight + kOverlayButtonGap) -
                                 (bh - kOverlayButtonHeight) * 0.5f;

                ui::Rect btn{bx, by, bw, bh, 16.0f,
                             ui::Color::white((on ? 0.22f : 0.08f) * ovl)};
                renderer.draw(btn);

                const char* label = kOverlayLabels[i];
                const float tw = text.measure(label, ui::TextStyle::Title3, sc);
                text.draw(renderer, label, bx + (bw - tw) * 0.5f,
                          by + (bh - text.lineHeight(ui::TextStyle::Title3, sc)) * 0.5f +
                              text.ascent(ui::TextStyle::Title3, sc),
                          ui::TextStyle::Title3,
                          ui::Color::white((on ? 1.0f : 0.60f) * ovl), sc);
            }
        }

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

            // A PLATE SIZED TO ITS TEXT, not a strip across the whole hero.
            // Wide enough for the longer of the two lines and no wider, so
            // the artwork carries the rest of the width. Still a material
            // rather than flat black: Cabinet learned on tvOS that a
            // material keeps the game's colours showing through while giving
            // the text a surface to be read against.
            const float titleW = text.measure(hero.title, ui::TextStyle::Headline, sc);
            const float platW = text.measure(heroPlatform, ui::TextStyle::Caption1, sc);
            ui::Rect plate = band;
            plate.w = std::min(std::max(titleW, platW) + kHeroBandPadX * 2.0f,
                               band.w * 0.55f);
            plate.radius = kHeroRadius;
            renderer.drawGlass(plate, kHeroBandBlur, ui::Color::black(0.32f));

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
            // CALLOUT, NOT TITLE 3, changed on the panel 2026-09-19: at Title
            // 3 with a 180-point floor this read as a primary action on a
            // detail screen rather than a button in the corner of a banner,
            // and it was the loudest thing on Home. Callout is the design
            // system's floor for anything a person reads rather than glances
            // at, so it is as small as this may go.
            const char* kResume = "\xE2\x96\xB6  Resume";
            const float pillTextW = text.measure(kResume, ui::TextStyle::Callout, sc);
            // Treatment 2, the text-control one: tinted blur at white 25%,
            // scale 1.06, text to full white. A pill growing a cover's 10%
            // would read as a bug; 3% on something this small would not read
            // at all.
            const float rf = (focusRow == RowHero && focusSlot == 1)
                                 ? resumeFocus.value() : 0.0f;
            const float rs = 1.0f + rf * (kPillFocusScale - 1.0f);
            const float pillW0 = std::max(pillTextW + 24.0f, 150.0f);
            const float pillH0 = text.lineHeight(ui::TextStyle::Callout, sc) + 12.0f;
            const float pillW = pillW0 * rs, pillH = pillH0 * rs;
            // IN THE TITLE BAND, NOT THE TOP-RIGHT CORNER. That corner now
            // belongs to the account chip, which is where the reference
            // implementation reserves it — and Resume reads better here
            // anyway, beside the name of the game it resumes rather than
            // floating in a corner it does not own.
            const float pillX = band.x + band.w - pillW0 - kHeroBandPadX -
                                (pillW - pillW0) * 0.5f;
            const float pillY = band.y + (band.h - pillH0) * 0.5f -
                                (pillH - pillH0) * 0.5f;
            renderer.drawGlass(ui::Rect{pillX, pillY, pillW, pillH, pillH * 0.5f,
                                        ui::Color::white(0)},
                               kHeroPillBlur,
                               ui::Color::white(0.18f + 0.07f * rf));
            text.draw(renderer, kResume, pillX + (pillW - pillTextW) * 0.5f,
                      pillY + 6.0f * rs + text.ascent(ui::TextStyle::Callout, sc),
                      ui::TextStyle::Callout, ui::Color::white(0.75f + 0.25f * rf), sc);

            // --- The top bar, drawn OVER the hero -------------------------
            //
            // A scrim first, because this is text on artwork and some covers
            // are bright at the top. It is the cheapest way to keep the
            // destinations legible without dimming the whole hero — and it
            // is the part most likely to need tuning on a television, where
            // contrast and overscan both bite hardest at the top edge.
            const float barH = 64.0f;
            renderer.draw(ui::Rect{heroCardRect.x, heroCardRect.y, heroCardRect.w, barH,
                                   kHeroRadius, ui::Color::black(0.45f)});

            const float barBaseline =
                heroCardRect.y + (barH - text.lineHeight(ui::TextStyle::Callout, sc)) * 0.5f +
                text.ascent(ui::TextStyle::Callout, sc);
            float bx = heroCardRect.x + 24.0f;
            for (int i = 0; i < BarCount; ++i) {
                const bool on = (focusRow == RowBar && focusSlot == i);
                const float w = text.measure(kBarLabels[i], ui::TextStyle::Callout, sc);
                if (on) {
                    // Treatment 2, the text-control one, the same as Resume:
                    // a tinted pill rather than a scale, because a
                    // destination growing would shove its neighbours along.
                    renderer.draw(ui::Rect{bx - 14.0f, heroCardRect.y + 10.0f,
                                           w + 28.0f, barH - 20.0f,
                                           (barH - 20.0f) * 0.5f,
                                           ui::Color::white(kFocusedTint)});
                }
                text.draw(renderer, kBarLabels[i], bx, barBaseline, ui::TextStyle::Callout,
                          ui::Color::white(on ? 1.0f : 0.65f), sc);
                bx += w + 44.0f;
            }

            // The account, at the far right — the corner the reference
            // implementation reserves for it. `TVAccountChip`: "the signed-in
            // RomM username beside a small circular avatar, in Home's
            // top-right corner". Name and avatar, as MMagTech asked for.
            //
            // A lettered disc stands in until the real avatar is fetched,
            // which is Cabinet's fallback too — it uses a person glyph.
            // Account switching is its own topic and nothing here is
            // focusable yet.
            const storage::User me = storage::currentUser();
            const std::string who = me.valid() ? me.name : std::string("Not signed in");
            const float discD = barH - 26.0f;
            const float discX = heroCardRect.x + heroCardRect.w - 24.0f - discD;
            const float nameW = text.measure(who, ui::TextStyle::Callout, sc);
            renderer.draw(ui::Rect{discX, heroCardRect.y + 13.0f, discD, discD,
                                   discD * 0.5f, ui::Color::white(0.22f)});
            if (!who.empty()) {
                const std::string initial(1, static_cast<char>(std::toupper(
                    static_cast<unsigned char>(who[0]))));
                const float iw = text.measure(initial, ui::TextStyle::Callout, sc);
                text.draw(renderer, initial, discX + (discD - iw) * 0.5f,
                          heroCardRect.y + 13.0f +
                              (discD - text.lineHeight(ui::TextStyle::Callout, sc)) * 0.5f +
                              text.ascent(ui::TextStyle::Callout, sc),
                          ui::TextStyle::Callout, ui::Color::white(0.90f), sc);
            }
            text.draw(renderer, who, discX - 12.0f - nameW, barBaseline,
                      ui::TextStyle::Callout, ui::Color::white(0.65f), sc);
        }
        keyboard.draw(renderer, text, renderer.scale());
        if (safeGuides) renderer.drawSafeAreaGuides();

        ++frame;
        // Capture before the swap. After a swap the back buffer's contents are
        // undefined, so a readback taken there is whatever the driver left.
        if (shotMode && frame >= shotAfterFrames) {
            renderer.saveFrame(shotPath, dw, dh);
            // Whether the running core can produce a state AT THIS POINT, which
            // is a different question from whether the round trip is exact and
            // is the only half of it a capture can answer.
            //
            // It is here because --state-test cannot answer it for every core.
            // Its warm-up is a tight loop of retro_run with no wall clock in
            // it, and a core that emulates on a thread of its own — PPSSPP is
            // the only one — barely advances in that loop: no sound, a static
            // picture and a zero-byte state after three thousand calls, while
            // the same core reaches its attract demo on the ordinary launch
            // path. A capture has a real frame loop under it, so the number
            // here is about the core rather than about the instrument.
            if (playing) {
                cab::Core& c = cab::Core::shared();
                std::fprintf(stderr, "[state] serialize size at capture: %zu bytes\n",
                             c.stateSize());
            }
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
        // Whether the second brake did anything. It is reported rather than
        // assumed, because a brake nobody can see is indistinguishable from a
        // brake that is not there — and this one has never engaged on either
        // PSP game measured. See Core::runFor.
        if (core.governorSkips() > 0)
            std::fprintf(stderr, "[core] the audio governor held it back %llu times\n",
                         static_cast<unsigned long long>(core.governorSkips()));
        core.unload();
    }
    if (audioStream) SDL_DestroyAudioStream(audioStream);
    std::fprintf(stderr, "[image] resident %.1f MB, %d still pending\n",
                 images.bytesResident() / (1024.0 * 1024.0), images.pendingCount());
    // Drained rather than abandoned: anything still queued is a save somebody
    // has already made, and quitting is not a reason to throw it away. This is
    // the one place a wait for the network is correct, because there is no
    // picture left to stop.
    if (uploader.pending() > 0)
        std::fprintf(stderr, "[sync] finishing %d upload(s)\n", uploader.pending());
    uploader.shutdown();

    images.shutdown();
    text.shutdown();
    renderer.shutdown();
    SDL_GL_DestroyContext(gl);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
