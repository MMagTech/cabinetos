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
#include <unordered_map>
#include <set>
#include <string>
#include <vector>

#include "core.h"
#include "design.h"
#include "idle.h"
#include "image.h"
#include "keyboard.h"
#include "cache.h"
#include "catalog.h"
#include "covercache.h"
#include "dirsave.h"
#include "filesave.h"
#include "firstrun.h"
#include "gpu.h"
#include "vkhost.h"
#include "net.h"
#include "qr.h"
#include "romfile.h"
#include "romm.h"
#include "screens.h"
#include "settings.h"
#include "sound.h"
#include "setup.h"
#include "accounts.h"
#include "storage.h"
#include "text.h"
#include "overlaywin.h"
#include "pin.h"
#include "choice.h"
#include "downloads.h"
#include "power.h"
#include "prefs.h"
#include "server.h"
#include "update.h"
#include "files.h"
#include "drives.h"
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

    // THE OUTCOME OF THE MOST RECENT STATE UPLOAD, so the pause menu can say
    // what happened rather than what was attempted. 0 nothing new, 1 it reached
    // the server, 2 it did not and is still on the disk.
    //
    // Cabinet awaits its upload and reports "Saved to RomM." or "Waiting for
    // signal to upload." This console queues on a worker instead, which is the
    // right shape for a machine that must not stall its frame loop — but it
    // meant Save said nothing at all, and then "load latest" went looking on a
    // server that had not received it yet. One atomic closes that gap.
    std::atomic<int> stateOutcome{0};

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
            if (job.isState) stateOutcome.store(ok ? 1 : 2);
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
    // False on PlayStation 2, GameCube and later: no Save state, no Load
    // latest state, true to those consoles. See catalog::snapshotsAllowed.
    bool snapshots = true;
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

// The same thing, spelled the way Cabinet for Mac spells it.
//
// `cabinet-604.ps2`, `cabinet-937.USA.raw`. Only PlayStation 2 and GameCube
// use this, and only because a save for those two ALREADY EXISTS on the
// server written by the Mac — RomM matches a row for overwrite by filename
// alone, so a console that invented its own name would leave the person with
// two cards per game and restore neither. See catalog::SaveFile::macRowName.
static std::string macSaveRowName(int romId, const std::string& region,
                                  const std::string& writtenName) {
    // WHAT DOLPHIN ACTUALLY WROTE, where it wrote something. It stamps the
    // region into the name, and the card SIZE too when the card is not the
    // default — `cabinet-934.USA.251.raw` on the reference server. Neither is
    // predictable from here, so the file on disk is the authority and this is
    // only the fallback for a row that does not exist yet.
    if (!writtenName.empty()) return writtenName;
    return "cabinet-" + std::to_string(romId) + "." + region;
}

// Which region a row on the server belongs to, judged by the extension this
// console and the reference implementation both upload under. Anything else —
// a card somebody made in RomM's web player, a file from another emulator —
// reads as the main save, which is right: that is the only region a foreign
// row could ever be.
static std::string regionOfRow(const std::string& fileName) {
    auto endsWith = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return fileName.size() > n && fileName.compare(fileName.size() - n, n, ext) == 0;
    };
    if (endsWith(".cart")) return "cart";
    if (endsWith(".rtc")) return "rtc";
    // PlayStation 2 and GameCube, whose rows are named the Mac's way —
    // `cabinet-604.ps2`, `cabinet-937.USA.raw`. Without these two lines every
    // such row reads as "srm" and the restore looks straight past the card the
    // person actually made. See catalog::SaveFile::macRowName.
    if (endsWith(".ps2")) return "ps2";
    if (endsWith(".raw")) return "raw";
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
// Dolphin's own configuration, written before the core boots.
//
// WHY A FILE RATHER THAN A CORE OPTION: the libretro core does not expose the
// slot A device or the memory card path at all, and left alone Dolphin uses
// its default — a GCI FOLDER of loose files, one per save. Cabinet for Mac
// sets MAIN_SLOT_A and MAIN_MEMCARD_A_PATH directly and so produces a
// whole-card `.raw`. Those are two different save formats for one platform
// across two halves of the same product, which is not a thing to leave to
// chance. The core reads this file on startup, so writing it is how this side
// makes the same two choices.
//
// MemoryCardSize IS PINNED, and that is the subtle one. Dolphin puts the
// card's size in the FILENAME as well as its region — one of the cards on the
// reference server is `cabinet-934.USA.251.raw`, a 2 MB card, beside two 16 MB
// ones. RomM matches a save row by filename alone, so leaving the size to
// Dolphin means the row's identity depends on a setting nobody controls: the
// day it changes, the card lands under a new name, the old row is orphaned and
// the save silently does not come back. Cabinet for Mac leaves it at -1 and
// has the same latent fault.
static void writeDolphinConfig(const std::string& saveDir) {
    const std::string dir = saveDir + "/User/Config";
    const std::string path = dir + "/Dolphin.ini";
    std::string ini;
    ini += "# Written by CabinetOS before every GameCube launch. See\n";
    ini += "# writeDolphinConfig in frontend/src/main.cpp.\n";
    ini += "[Core]\n";
    // ExpansionInterface::EXIDeviceType::MemoryCard, and None for slot B.
    ini += "SlotA = 1\n";
    ini += "SlotB = 255\n";
    ini += "MemcardAPath = " + saveDir + "/card.raw\n";
    ini += "MemoryCardSize = 2\n";
    if (!cab::writeBytes(path, std::vector<uint8_t>(ini.begin(), ini.end()))) {
        // NOT fatal, and said out loud. The game still runs; it writes its
        // save somewhere this console does not sync, which is exactly the kind
        // of failure that looks like the save feature simply not working.
        std::fprintf(stderr,
                     "[save] could not write %s - GameCube will write loose GCI "
                     "files and nothing will sync\n", path.c_str());
    }
}

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
        const std::string name =
            spec.macRowName ? macSaveRowName(romId, spec.region, std::string())
                            : saveRowName(fsStem, spec.coreRowName, spec.region);

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
        // THE NAME DOLPHIN CHOSE, not the one that was asked for. `written`
        // is what the capture actually found on disk — `card.USA.raw`, or
        // `card.USA.251.raw` for a card that is not the default size — and for
        // the platforms that follow the Mac's naming that spelling IS the row
        // identity on the server. Uploading under the requested name instead
        // would make a second row every time the size or region moved.
        std::string writtenName = written;
        if (const size_t slash = writtenName.find_last_of('/');
            slash != std::string::npos)
            writtenName.erase(0, slash + 1);
        (void)writtenName;
        const std::string name =
            f.spec.macRowName
                ? macSaveRowName(sess.romId, f.spec.region, std::string())
                : saveRowName(sess.fsStem, f.spec.coreRowName, f.spec.region);
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

// WHAT THE PAUSE MENU SAYS BACK — new 2026-09-21.
//
// MMagTech, in the menu: *"are the save and load from the menu not wired in?"*
// They were, and had been for days. Every outcome of both went to stderr and
// nowhere else, so pressing Save state looked identical whether it wrote two
// megabytes, refused because the core cannot serialise, or failed to write at
// all — and "did that do anything?" is the one question a save button must
// never leave a person asking.
//
// The same fault as the invisible launch refusal fixed earlier today, in the
// one menu where somebody is most likely to be doing something they want
// confirmed. It fades after a few seconds because it is a receipt, not a state.
//
// A PILL NEAR THE FOOT OF THE SCREEN, NOT A LINE IN THE MENU — 2026-09-23.
// It used to be small text in a strip reserved at the bottom of the pause
// panel, and MMagTech: it *"looks like it was added there with no thought to
// the visuals of the menu."* The strip also left the Power menu with an empty
// band under its last button. So it is its own object now, the way PS5, Switch
// and Apple TV confirm things, and it is not tied to the menu: an upload that
// finishes after Resume still says so, over the game.
//
// ONE PLACE FOR EVERY SHORT-LIVED MESSAGE ON THE CONSOLE, so they all look the
// same and `--notice-gallery` can show every one of them on the panel.
struct MenuNotice {
    // The dot's colour says which kind of answer it is before the words do.
    enum class Tone { Done, Busy, Info, Problem };
    std::string text;
    Tone tone = Tone::Info;
    float life = 0.0f;
    float age = 0.0f;
    void say(std::string t, Tone k) {
        text = std::move(t);
        tone = k;
        // Busy holds until the answer replaces it; a stuck "Saving…" is still
        // gone in fifteen seconds rather than for ever.
        life = k == Tone::Busy ? 15.0f : 3.2f;
        age = 0.0f;
    }
    void tick(float dt) {
        if (life > 0.0f) {
            life -= dt;
            age += dt;
        }
    }
    float alpha() const {
        if (life <= 0.0f) return 0.0f;
        const float in = std::min(1.0f, age / 0.25f);
        const float out = life < 0.5f ? life / 0.5f : 1.0f;   // the last half second fades
        return std::min(in, out);
    }
};
using Tone = MenuNotice::Tone;

// CREDITS AND LICENCES, Settings > About. One line per project whose work
// this console is: what it does, and its licence. The same facts as
// docs/LICENCES.md, which is the record; change one, change both. One list,
// not a Credits list and a Licences list naming the same projects twice
// (MMagTech, 2026-09-25). Full licence texts are in the image under
// /usr/share/licenses/, not on the television.
struct Credit { const char* name; const char* what; };
constexpr Credit kCredits[] = {
    {"CabinetOS", "This console \xC2\xB7 MIT"},
    {"Bazzite", "The base system \xC2\xB7 Apache 2.0"},
    {"Universal Blue", "Image tooling \xC2\xB7 Apache 2.0"},
    {"Fedora", "Under Bazzite \xC2\xB7 Per package"},
    {"gamescope", "Compositor, by Valve \xC2\xB7 BSD 2-clause"},
    {"FinalBurn Neo", "Arcade \xC2\xB7 Non-commercial"},
    {"MAME 2003-Plus", "Arcade \xC2\xB7 Non-commercial"},
    {"Snes9x", "SNES \xC2\xB7 Non-commercial"},
    {"Genesis Plus GX", "Sega 8 and 16-bit \xC2\xB7 Non-commercial"},
    {"PicoDrive", "32X \xC2\xB7 Non-commercial"},
    {"Opera", "3DO \xC2\xB7 Non-commercial"},
    {"Flycast", "Dreamcast, Naomi \xC2\xB7 GPL v2"},
    {"PPSSPP", "PSP \xC2\xB7 GPL v2+"},
    {"PCSX2", "PlayStation 2 \xC2\xB7 GPL v3+"},
    {"Dolphin", "GameCube \xC2\xB7 GPL v2+"},
    {"mupen64plus-next", "Nintendo 64 \xC2\xB7 GPL v2"},
    {"PCSX ReARMed", "PlayStation \xC2\xB7 GPL v2"},
    {"Beetle Saturn", "Saturn \xC2\xB7 GPL v2"},
    {"Beetle PCE Fast", "TurboGrafx-16 \xC2\xB7 GPL v2"},
    {"Beetle NeoPop", "Neo Geo Pocket \xC2\xB7 GPL v2"},
    {"Beetle VB", "Virtual Boy \xC2\xB7 GPL v2"},
    {"FCEUmm", "NES \xC2\xB7 GPL v2"},
    {"Gambatte", "Game Boy \xC2\xB7 GPL v2"},
    {"ProSystem", "Atari 7800 \xC2\xB7 GPL v2"},
    {"Stella 2014", "Atari 2600 \xC2\xB7 GPL v2"},
    {"melonDS", "Nintendo DS \xC2\xB7 GPL v3"},
    {"DraStic FreeBIOS", "DS BIOS \xC2\xB7 BSD 2-clause"},
    {"vecx", "Vectrex \xC2\xB7 GPL v3"},
    {"mGBA", "Game Boy Advance \xC2\xB7 MPL 2.0"},
    {"FFmpeg", "Inside PPSSPP \xC2\xB7 LGPL v2.1+"},
    {"rapidyaml, c4core", "Inside PCSX2 \xC2\xB7 MIT"},
    {"SDL3", "Input and audio \xC2\xB7 zlib"},
    {"Mesa", "Graphics \xC2\xB7 MIT"},
    {"FreeType", "Text \xC2\xB7 FreeType licence"},
    {"libjpeg-turbo, libpng", "Cover art \xC2\xB7 BSD, libpng"},
    {"libcurl", "Talking to RomM \xC2\xB7 curl"},
    {"json-c", "RomM's answers \xC2\xB7 MIT"},
    {"libarchive", "Game archives \xC2\xB7 BSD 2-clause"},
    {"zlib", "Compression \xC2\xB7 zlib"},
    {"Noto Sans", "The type \xC2\xB7 OFL 1.1"},
};

// EVERY MESSAGE THE PILL CAN SHOW, for `--notice-gallery`, which walks through
// them on the television one every four seconds so their words and their look
// can be judged together — including the ones that are hard to cause for real.
// A copy of the strings at their call sites: add a message there, add it here.
struct GalleryNotice { const char* text; Tone tone; };
constexpr GalleryNotice kNoticeGallery[] = {
    {"Saving\xE2\x80\xA6", Tone::Busy},
    {"Saved to RomM", Tone::Done},
    {"Saved. Will upload when RomM is back", Tone::Info},
    {"Saved on this console only", Tone::Info},
    {"Save states aren't available for this system", Tone::Info},
    {"Couldn't save the state", Tone::Problem},
    {"Loading\xE2\x80\xA6", Tone::Busy},
    {"State loaded", Tone::Done},
    {"No saved state for this game", Tone::Info},
    {"Couldn't reach RomM", Tone::Problem},
    {"Couldn't load that state", Tone::Problem},
    {"Download removed", Tone::Done},
    {"Removed. Someone else keeps it, so no space came back", Tone::Info},
    {"Removed. Others keep it, so no space came back", Tone::Info},
    {"Removed. The space comes back when you stop playing it", Tone::Info},
    {"Removed, but its files couldn't be deleted", Tone::Problem},
    {"Update available", Tone::Info},
    {"Updated to 2026.09.28", Tone::Done},
    {"Update didn't apply", Tone::Problem},
    {"Couldn't check for updates", Tone::Problem},
    {"Couldn't update", Tone::Problem},
    {"External drive connected", Tone::Done},
    {"Couldn't use the external drive", Tone::Problem},
    {"Safe to unplug", Tone::Done},
    {"Couldn't eject the external drive", Tone::Problem},
    {"External drive removed", Tone::Info},
    {"External drive isn't exFAT or NTFS", Tone::Problem},
    {"Storage almost full", Tone::Info},
    {"Couldn't format the drive", Tone::Problem},
};

// A drive's size as Storage says it: "2.02 TB", "125 GB". TWO DECIMALS FROM
// 1 TB UP: with one, a 2.05 TB drive with 23 GB used read "2.0 TB free of
// 2.0 TB", which looked impossible (MMagTech, 2026-09-25). Decimal units, as
// the drive's box, the Mac and the PS5 count; Windows calls the same drive
// 1.86 TB because it counts in binary units and still says TB.
static std::string driveSize(int64_t b) {
    char buf[32];
    const double g = static_cast<double>(b) / 1e9;
    if (g >= 1000.0) std::snprintf(buf, sizeof buf, "%.2f TB", g / 1000.0);
    else std::snprintf(buf, sizeof buf, "%.0f GB", g);
    return std::string(buf);
}

static void saveStateNow(GameSession& sess, Uploader& up, MenuNotice& notice) {
    cab::Core& core = cab::Core::shared();
    std::vector<uint8_t> st;
    if (!core.saveState(st) || st.empty()) {
        std::fprintf(stderr, "[state] this core cannot serialize\n");
        notice.say("Save states aren't available for this system", Tone::Info);
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
        notice.say("Couldn't save the state", Tone::Problem);
        return;
    }
    std::fprintf(stderr, "[state] %zu bytes saved locally\n", st.size());

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
    // The server had nothing for this core, or could not be asked. The frame
    // thread then tries this machine's own newest state — reading it there
    // rather than here because loading one touches the core.
    bool tryLocal = false;
    // Set when a state went in. The menu closes on it: the person is back in
    // the game at the point they loaded, which is what they pressed for.
    bool loaded = false;
    // The server could not be asked at all, as opposed to having nothing.
    bool serverFailed = false;
    std::vector<uint8_t> data;
    std::string note;
    std::thread worker;
    ~StateLoad() { if (worker.joinable()) worker.join(); }
};

static std::string newestLocalState(const GameSession& sess);

static void beginLoadLatestState(StateLoad& load, GameSession& sess,
                                 romm::Client& client, MenuNotice& notice) {
    if (load.running.load()) return;

    notice.say("Loading\xE2\x80\xA6", Tone::Busy);
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
            notice.say("No saved state for this game", Tone::Info);
            return;
        }
        const bool ok = cab::Core::shared().loadState(bytes);
        std::fprintf(stderr, "[state] local-only %s -> %s\n", local.c_str(),
                     ok ? "restored" : "REFUSED");
        notice.say(ok ? "State loaded" : "Couldn't load that state",
                   ok ? Tone::Done : Tone::Problem);
        load.loaded = ok;
        return;
    }
    if (load.worker.joinable()) load.worker.join();
    load.running = true;
    load.ready = false;
    load.serverFailed = false;
    const int romId = sess.romId;
    const std::string tag = sess.stateTag;
    load.worker = std::thread([&load, &client, romId, tag]() {
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

// Called once per frame. Applying the state is the only part that touches the
// core, so it happens here and nowhere else.
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

static void pumpStateLoad(StateLoad& load, const GameSession& sess,
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
                    const bool ok = cab::Core::shared().loadState(bytes);
                    std::fprintf(stderr, "[state] fell back to local %s -> %s\n",
                                 local.c_str(), ok ? "restored" : "REFUSED");
                    notice.say(ok ? "State loaded" : "Couldn't load that state",
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
        notice.say(load.serverFailed ? "Couldn't reach RomM" : "No saved state for this game",
                   load.serverFailed ? Tone::Problem : Tone::Info);
        return;
    }
    const bool ok = cab::Core::shared().loadState(load.data);
    std::fprintf(stderr, "[state] %s (%zu bytes) -> %s\n", load.note.c_str(),
                 load.data.size(), ok ? "restored" : "REFUSED");
    // A REFUSED state is the one that matters most. It means the bytes were
    // found and the core would not take them, which is a different problem from
    // there being none — and silently carrying on with the game running from
    // where it was is indistinguishable from nothing having happened.
    notice.say(ok ? "State loaded" : "Couldn't load that state",
               ok ? Tone::Done : Tone::Problem);
    load.loaded = ok;
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
    // How long this job has been doing something a person would wait for, in
    // seconds. Advanced by the frame loop rather than by the worker: it is a
    // fact about what has been on the screen, not about the work.
    float busyFor = 0.0f;
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
    // WHERE THE PERSON WAS WHEN THEY PRESSED PLAY, as an integer the launch
    // machinery does not have to understand. See `startedOn` at the press site
    // and the ready branch in pumpLaunch: a download that finishes while
    // somebody has moved on somewhere else must not drag them out of it.
    int startedOn = -1;

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
static float gPs2Upscale = 1.0f;
static int gPs2Anisotropy = 0;

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
        core.setPs2(storage::imageAssetsDir() + "/pcsx2/resources", gPs2Upscale, gPs2Anisotropy);
    }

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
    // Every platform's full name, qualified where two share one ("Arcade
    // (FinalBurn Neo)"), for anything that names a system outside a tile:
    // Settings' Downloads groups by it.
    std::map<int, std::string> platformNames;
    // What a tile's cached cover is validated against: the platform's own
    // `updated_at` and `rom_count`, as the server reported them this boot.
    // Filled for playable platforms only, because nothing else gets a cover.
    std::map<int, covercache::Tile> tileCache;
    // Every game this console has fetched so far, keyed by rom id, so a game
    // that is recent AND a favourite AND in a platform's grid is one card
    // rather than three. THE VALUE IS AN INDEX INTO `cards`, which is why
    // `cards` is append-only — see below.
    std::map<int, int> byRomId;
};

// Adds a game to the store if it is not already there, and answers where it is.
//
// `cards` IS APPEND-ONLY, AND THIS IS THE REASON. Every shelf, every tile and
// the hero hold plain indices into it, so sorting or erasing would silently
// re-point all of them at the wrong game. Ordering belongs to each list rather
// than to the store: the Recent shelf is in recency order, a platform grid is
// alphabetical, and neither needs the store itself to be either.
//
// The library used to be sorted wholesale and the membership lists rebuilt
// afterwards, with a comment explaining that the sort had to come first. That
// worked because everything arrived in one pass. Nothing arrives in one pass
// any more.
static int appendGame(Library& lib, const romm::Game& g) {
    auto it = lib.byRomId.find(g.id);
    if (it != lib.byRomId.end()) return it->second;
    Card c;
    c.id = g.id;
    c.title = g.name.empty() ? g.fsName : g.name;
    c.cover = g.coverPath;
    c.coverLarge = g.coverLargePath;
    c.platform = g.platformName;
    c.art = colorForTitle(c.title);
    const int idx = static_cast<int>(lib.cards.size());
    lib.cards.push_back(std::move(c));
    lib.games.push_back(g);
    lib.byRomId.emplace(g.id, idx);
    return idx;
}

// The games behind one tile, fetched when somebody opens it and not before.
//
// THIS IS THE WHOLE POINT OF OPEN QUESTION 28. A tile knows its own count from
// the server without holding a single game — `romCount` on the platform, and
// on the collection — so boot draws the Library screen having fetched nothing,
// and a grid costs one request at the moment it is walked into.
//
// Returns false only when the request failed. A tile that legitimately holds
// nothing this console can play returns true with an empty membership, which
// is a different thing and reads differently on the screen.
static bool loadTileGames(romm::Client& client, Library& lib, screens::Tile& tile,
                          bool isCollection, bool* more = nullptr) {
    if (!tile.cards.empty()) return true;   // already walked into once
    const std::string filter =
        (isCollection ? "collection_id=" : "platform_ids=") + std::to_string(tile.id);
    std::vector<romm::Game> games;
    std::string err;
    // PAGED, AND CHECKED AGAINST `total`. This asked for ten thousand in one
    // request and called that enough, with a comment claiming it matched
    // fetchGames. It did not: one request is not paging, and a platform past
    // the limit would have come back SHORT AND SUCCESSFUL — the exact failure
    // fetchGames' own comment calls the worst possible one, because the grid
    // would simply show fewer games and say nothing.
    //
    // A full MAME set is tens of thousands of roms in a single platform, so
    // this is not a hypothetical library.
    constexpr int kPage = 500;
    int total = 0;
    // ONLY THE FIRST PAGE, AND THE CALLER DRAWS IT. The rest is somebody
    // else's job — see GridFill. A grid used to fetch the whole platform
    // before anything appeared, which is fine at 141 games and is ten seconds
    // for a full MAME set in one platform.
    {
        std::vector<romm::Game> page;
        // fetchRoms writes the limit itself, so only the offset rides on the
        // filter.
        if (!client.fetchRoms(filter, kPage, &page, &err, &total)) {
            std::fprintf(stderr, "[library] %s: %s\n", tile.title.c_str(), err.c_str());
            return false;
        }
        games = std::move(page);
    }
    if (more) *more = (total > static_cast<int>(games.size()));
    int unplayable = 0;
    for (const auto& g : games) {
        // A collection can hold games from systems this console cannot run.
        // Platform grids cannot, since the tile would not be enterable, but
        // the check is cheap and the two paths share it.
        if (!catalog::playable(g)) { ++unplayable; continue; }
        tile.cards.push_back(appendGame(lib, g));
        if (tile.cover.empty()) {
            const int i = tile.cards.back();
            if (!lib.cards[i].cover.empty()) tile.cover = lib.cards[i].cover;
        }
    }
    // NOT SORTED HERE. RomM returns roms in title order already, which is what
    // the grid's letter-jump needs, and a later page has to concatenate onto
    // this one — a sort would fight the append and move cards out from under
    // whoever is looking at them.
    if (unplayable > 0) {
        // Say what is missing rather than quietly showing a shorter list: a
        // collection of twelve that opens onto four looks like a bug unless
        // the tile already said why. Short, because a tile's second line holds
        // about sixteen characters beside a cover.
        char count[96];
        std::snprintf(count, sizeof count, "%zu of %zu",
                      tile.cards.size(), tile.cards.size() + unplayable);
        tile.detail = count;
    }
    std::fprintf(stderr, "[library] %s: %zu game(s)%s\n", tile.title.c_str(),
                 tile.cards.size(),
                 unplayable ? " (some not playable here)" : "");
    return true;
}

// What the console knows the moment it finishes booting.
//
// FOUR CALLS, AND NOT ONE OF THEM IS THE CATALOGUE — open question 28. This
// used to walk every platform and page through all of them, sixteen hundred
// games on the reference server, before anything was drawn. It was the
// dominant cost of a boot and it was PROPORTIONAL TO THE LIBRARY, so somebody
// with twenty thousand games waited proportionally longer every single time
// they turned the console on, and nothing in the design had noticed.
//
// Nothing needed it. A platform tile's count was already on the platform
// object; a collection's was already on the collection. Home shows about
// fourteen covers and every one of them comes from the recents and favourites
// calls, which are bounded. A grid does not exist until somebody walks into
// it, and search asks the server.
//
// So boot is CONSTANT rather than proportional, which is the whole prize: the
// person with twenty thousand games boots as fast as the person with two
// hundred.
static Library loadLibrary(romm::Client& client) {
    Library lib;
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
        lib.platformNames[p.id] = catalog::displayName(p);
        screens::Tile tile;
        tile.id = p.id;
        // THE NAME AND THE QUALIFIER ARE TWO FACTS ON A TILE, not one string.
        // Joined, "Arcade (FinalBurn Neo)" and "Arcade (MAME 2003-Plus)" both
        // came out as "Arcade" over "(FinalBurn ..." and "(MAME 200..." at this
        // width — so the qualifier that exists SOLELY to tell the two tiles
        // apart was the part being cut off. See catalog::displayQualifier.
        tile.title = p.name.empty() ? p.slug : p.name;
        const std::string qualifier = catalog::displayQualifier(p);
        // The colour a coverless tile falls back to is keyed on the FULL name,
        // so two platforms that differ only by qualifier do not come out the
        // same colour.
        tile.art = colorForTitle(catalog::displayName(p));
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

        // THE COUNT COMES OFF THE PLATFORM, which is the finding that made all
        // of this cheap: it was there the whole time, and boot was fetching
        // sixteen hundred games to draw a number the server had already sent.
        char count[48];
        std::snprintf(count, sizeof count, "%d game%s", p.romCount,
                      p.romCount == 1 ? "" : "s");
        tile.detail = qualifier.empty() ? std::string(count)
                                        : qualifier + "  \xC2\xB7  " + count;
        lib.tileCache[p.id] = covercache::Tile{"", p.updatedAt, p.romCount};
        // `tile.cover` is deliberately left empty. It used to be the first
        // cover among the platform's games, which cost a fetch per platform —
        // 1.55 s of a 1.8 s boot, measured against the reference server. The
        // tiles come up in their colour and the covers are filled in behind
        // Home; see fillTileCovers. MMagTech chose that over both paying for
        // them at boot and using RomM's platform logo, 2026-09-22.
        lib.platformTiles.push_back(std::move(tile));
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

    // Collections. The membership used to be resolved against a catalogue that
    // was already in memory — "no second request", said the comment, and that
    // was true only because boot had paid for all of them. It is one request
    // now, made when the collection is opened, exactly like a platform.
    std::vector<romm::Collection> collections;
    if (client.fetchCollections(&collections, &err)) {
        for (const auto& col : collections) {
            screens::Tile tile;
            tile.id = col.id;
            tile.title = col.name;
            tile.cover = col.coverPath;
            tile.art = colorForTitle(tile.title);
            char count[48];
            std::snprintf(count, sizeof count, "%d game%s", col.romCount,
                          col.romCount == 1 ? "" : "s");
            tile.detail = count;
            // The server's count, not how many are playable here — that is not
            // known until it is opened, and loadTileGames rewrites the line to
            // "4 of 12" at that point if the two differ.
            tile.enterable = col.romCount > 0;
            lib.collectionTiles.push_back(std::move(tile));
        }
        std::fprintf(stderr, "[library] %zu collection(s)\n", lib.collectionTiles.size());
    } else {
        // Not fatal. A server that will not list collections still has a
        // library, and the switcher's Collections tab says it is empty.
        std::fprintf(stderr, "[library] no collections: %s\n", err.c_str());
    }

    // The hero, from the server's play history rather than from anything this
    // console remembers. A game played on an Apple TV is recent here the moment
    // this console is paired, which is what lets a machine that has never
    // launched anything still open on the right game.
    //
    // THE GAMES COME STRAIGHT OFF THIS CALL NOW. They used to be looked up in
    // the catalogue and DROPPED IF NOT FOUND — which, once boot stopped
    // fetching the catalogue, would have quietly emptied Home while every one
    // of these calls still succeeded. That is the trap in this change and it
    // is why the store is filled from here rather than searched.
    // TIMED, because once on the A9 these two took eleven seconds during an
    // account switch when they take a fifth of one at boot, and nothing else
    // in the log said which.
    const Uint64 tRecent = SDL_GetTicks();
    std::vector<romm::Game> recent;
    const bool gotRecent = client.fetchRecent(16, &recent, &err);
    std::fprintf(stderr, "[library] play history in %llu ms\n",
                 static_cast<unsigned long long>(SDL_GetTicks() - tRecent));
    if (gotRecent) {
        for (const auto& g : recent) {
            // A recent game on a platform this console cannot play is skipped,
            // not shown greyed: Home must not offer a Resume that cannot run.
            // The reference library exercises this — the most recent "Altered
            // Beast" is the Game & Watch one, which Cabinet does not ship.
            if (!catalog::playable(g)) continue;
            const int idx = appendGame(lib, g);
            // THE MOST RECENT ONE STAYS ON THE SHELF — changed 2026-09-21.
            //
            // `heroIndex` is what Home focuses on when it opens, which is the
            // whole of "resume-first". It is an index into the shelf's first
            // slot rather than a separate object on the screen.
            if (lib.heroIndex < 0) {
                lib.heroIndex = idx;
                lib.heroPlatform = g.platformName;
            }
            lib.shelf.push_back(idx);
        }
    } else {
        std::fprintf(stderr, "[library] no play history: %s\n", err.c_str());
    }

    const Uint64 tFavs = SDL_GetTicks();
    std::vector<romm::Game> favs;
    const bool gotFavs = client.fetchFavorites(40, &favs, &err);
    std::fprintf(stderr, "[library] favourites in %llu ms\n",
                 static_cast<unsigned long long>(SDL_GetTicks() - tFavs));
    if (gotFavs) {
        for (const auto& g : favs) {
            if (!catalog::playable(g)) continue;
            lib.favorites.push_back(appendGame(lib, g));
        }
        std::fprintf(stderr, "[library] %zu favourites\n", lib.favorites.size());
    }

    int withArt = 0;
    for (const auto& c : lib.cards) if (!c.cover.empty()) ++withArt;
    // NOT "playable games" any more, and the wording matters: this is what the
    // console is holding, which is Home's worth of it. The library's size is
    // the server's business now.
    std::fprintf(stderr, "[library] %zu game(s) in hand, %d with art; "
                 "%zu platform tile(s), %d games on systems this console cannot play\n",
                 lib.cards.size(), withArt, lib.platformTiles.size(), skippedGames);

    if (lib.heroIndex >= 0)
        std::fprintf(stderr, "[library] resume: %s (%s), %zu on the Recent shelf\n",
                     lib.cards[lib.heroIndex].title.c_str(), lib.heroPlatform.c_str(),
                     lib.shelf.size());
    else
        std::fprintf(stderr, "[library] nothing recent is playable here\n");
    return lib;
}

// Talks to a RomM server and reports, without opening a window.
//
//   --romm <address>              connect and list the library
//   --romm <address> --romm-pair  pair first, if there is no token yet
//
// The token lives in ~/.config/cabinetos/romm.json at 0600. It is a credential:
// it is never printed here, and it does not belong in the repository.
// The keyboard for a server address, in Settings and on the startup screen:
// first run's, without its hint, because the people here know what an
// address is (docs/SETTINGS.md, Network).
// TURN OFF SCREEN AFTER, docs/SETTINGS.md, System. Saved as the word in
// config/settings.json; the frame loop hands the seconds to idle::Watch.
// The starting value is 15 minutes, what the console did before there
// was a choice. NO "NEVER" AND NO HOUR: both were built and dropped the
// same day, on MMagTech's word, because on an OLED a lit menu is burn-in
// and half an hour is long enough. A saved word this list does not know
// is 15. At file scope because the startup screen's wait uses it too.
struct ScreenOff { const char* name; const char* word; double seconds; };
static constexpr ScreenOff kScreenOff[] = {
    {"10 minutes", "10m", 10 * 60.0}, {"15 minutes", "15m", 15 * 60.0},
    {"30 minutes", "30m", 30 * 60.0},
};
constexpr int kScreenOffCount = sizeof kScreenOff / sizeof kScreenOff[0];
static int savedScreenOff() {
    const std::string w = prefs::get("screen_off_after", "15m");
    for (int i = 0; i < kScreenOffCount; ++i)
        if (w == kScreenOff[i].word) return i;
    return 1;
}

static ui::Keyboard::Config serverKeyboardConfig(const std::string& initial) {
    ui::Keyboard::Config cfg;
    cfg.title = "RomM server";
    cfg.initial = initial;
    cfg.placeholder = "192.168.1.10:6005";
    cfg.shortcuts = {".local", ":8080"};
    return cfg;
}

static std::string trimmedAddress(std::string a) {
    while (!a.empty() && a.back() == ' ') a.pop_back();
    while (!a.empty() && a.front() == ' ') a.erase(0, 1);
    return a;
}

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

// --- The first-run mechanisms, each reportable without a screen -------------
//
// None of the four things first run needs is a picture — a state machine, a QR
// renderer, NetworkManager plumbing, and a way to know it is the first run at
// all — which is why they can be built before the look is settled. But
// "not a picture" only helps if there is a way to SEE each of them work, and on
// this project that means a command that prints a fact rather than a screen
// somebody has to describe over the telephone.
//
// So there are three, in the same shape as --storage and --core-options: they
// run before SDL, need no window, no GL and no controller, and can be run over
// SSH against the real console without disturbing the session on the television.

// What the network is, what is on the air, and whether this console is still
// allowed to save a connection.
//
// THE LAST LINE IS THE POINT OF THE WHOLE COMMAND. Wi-Fi configuration works
// today only because the session user is in `wheel`, and Phase 6 is exactly the
// change that takes it out. That failure presents as a network bug on a machine
// whose network is fine, so the console answers the question directly.
static int networkProbe(bool doScan) {
    if (!net::available()) {
        std::printf("NetworkManager  NOT RUNNING — nothing below can be answered\n");
        return 1;
    }
    const net::Status s = net::status();
    std::printf("NetworkManager  running\n");
    std::printf("online          %s\n", s.online ? "yes" : "NO");
    if (s.online) {
        std::printf("link            %s %s%s%s\n",
                    s.link == net::Link::Ethernet ? "ethernet" : "wi-fi",
                    s.device.c_str(),
                    s.ipv4.empty() ? "" : "  ",
                    s.ipv4.c_str());
        if (!s.connection.empty())
            std::printf("connection      %s\n", s.connection.c_str());
    }
    std::printf("ethernet        %s\n",
                !s.ethernetPresent ? "no wired port on this machine"
                : s.ethernetUp     ? "up"
                                   : "present, no link");
    std::printf("wi-fi           %s\n",
                !s.wifiPresent  ? "no radio on this machine"
                : !s.wifiEnabled ? "PRESENT BUT THE RADIO IS OFF"
                : s.wifiUp       ? "connected"
                                 : "on, not connected");

    // Asked of this very process, because the grant depends on the session the
    // caller is in — which is why the answer over SSH and the answer on the
    // television can legitimately differ, and why this line names which it is.
    const std::string verdict = net::polkitVerdict();
    std::printf("save a network  polkit says %s%s\n", verdict.c_str(),
                verdict == "yes" ? "" :
                "  <-- Wi-Fi CANNOT be saved; see 60-cabinetos-network.rules");
    std::printf("asked as        %s\n",
                getenv("SSH_CONNECTION") ? "an SSH session (not the console's own)"
                                         : "a local session");

    if (!doScan) return 0;
    if (!s.wifiPresent) {
        std::printf("\nno radio, so nothing to scan for\n");
        return 0;
    }
    std::vector<net::Network> found;
    std::string err;
    std::printf("\nscanning…\n");
    if (!net::scan(&found, &err)) {
        std::printf("scan failed: %s\n", err.c_str());
        return 1;
    }
    if (found.empty()) {
        // A real answer, not an error. A console in a cupboard hears nothing.
        std::printf("nothing on the air\n");
        return 0;
    }
    std::printf("\n%zu network%s\n\n", found.size(), found.size() == 1 ? "" : "s");
    for (const net::Network& n : found) {
        std::printf("  %s%s %3d%%  %-14s %s%s\n",
                    n.active ? "*" : " ",
                    n.known ? " saved" : "      ",
                    n.signal,
                    n.security.empty() ? "open" : n.security.c_str(),
                    n.ssid.c_str(),
                    n.enterprise ? "   [802.1X — not supported]" : "");
    }
    return 0;
}

// A QR code, on a terminal, at a size a phone will read straight off the
// screen. That is the whole test: the encoder is proved end to end — string in,
// URL back out of a real camera — on a machine with no console and no server.
static int qrProbe(const char* text, const char* pbmPath) {
    std::string err;
    const qr::Code code = qr::encode(text, &err);
    if (!code.valid()) {
        std::fprintf(stderr, "[qr] %s\n", err.c_str());
        return 1;
    }
    std::printf("%s", qr::toText(code).c_str());
    std::printf("\n%d x %d modules, version %d — %s\n", code.size, code.size,
                (code.size - 17) / 4, text);
    // THE QUIET ZONE IS THE RENDERER'S, AND IT IS NOT OPTIONAL. Measured:
    // the same code drawn flush to the edge of an image does not decode at all,
    // while with four modules of margin it decodes every time. It is the
    // commonest reason a perfectly correct code will not scan.
    std::printf("four modules of quiet zone are included above, and whatever "
                "draws this owes it the same\n");
    if (pbmPath) {
        if (!qr::writePbm(code, pbmPath)) {
            std::fprintf(stderr, "[qr] could not write %s\n", pbmPath);
            return 1;
        }
        std::printf("wrote %s\n", pbmPath);
    }
    return 0;
}

// Does completing setup actually STICK?
//
// THIS TESTS THE ONLY CODE IN FIRST RUN THAT HAS NEVER RUN. Every walkthrough
// of the flow so far has used `--setup`, which forces it on an already
// configured machine and deliberately writes nothing — so `setServerAddress`,
// `markCompleted` and the token save have never once been executed by the
// product. Three separate faults found on 2026-09-20 were in code that looked
// correct and had simply never been exercised: SDL text input that was never
// enabled, a key press that never reported a commit, and a frame that never
// reached the window.
//
// AND THE FAILURE THIS GUARDS IS THE WORST ONE THE FEATURE HAS. If the marker
// does not persist, a console completes setup and boots straight back into
// setup, for ever, with no way past it — on a machine somebody has just
// installed. It would look exactly like a console that cannot be set up at all.
//
// It runs against a scratch root and touches nothing real, so it is safe on any
// machine and belongs in CI.
static int firstRunWriteTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    // Somewhere that is nobody's console.
    char root[] = "/tmp/cabinetos-firstrun-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[first-run] could not make a scratch root\n");
        return 1;
    }
    storage::setRoot(root);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[first-run] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("scratch root  %s\n\n", root);

    // 1. A machine with nothing on it has not been set up.
    //
    // NOTE WHAT THIS CANNOT ISOLATE: `serverAddress()` reads $CABINETOS_ROMM and
    // /etc/cabinetos/session.env before it reads our file, by design — root's
    // answer outranks the session's. On a console that is configured by hand,
    // those are set, so "is it configured" can legitimately be true here. That
    // is the adoption rule working, not a failure, and the test says which case
    // it is rather than pretending the environment is clean.
    const bool envConfigured = !firstrun::serverAddress().empty();
    const firstrun::Completion before = firstrun::completion();
    if (envConfigured) {
        std::printf("  note  this machine has a server address from %s, so the\n"
                    "        'not set up' case cannot be checked here\n",
                    firstrun::serverAddressSource().c_str());
    } else {
        check(!before.done, "a machine with nothing on it is not set up");
    }

    // 2. The address is written, and comes back.
    const std::string want = "cabinetos-write-test.invalid:6005";
    std::string werr;
    check(firstrun::setServerAddress(want, &werr),
          "setServerAddress reports success");
    if (!werr.empty()) std::printf("        (%s)\n", werr.c_str());

    // Read the FILE, not the resolver, because the resolver correctly prefers
    // root's answer and would hide whether ours landed at all.
    {
        const std::string path = storage::configDir() + "/server.json";
        FILE* f = std::fopen(path.c_str(), "rb");
        std::string body;
        if (f) {
            char buf[512];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
            std::fclose(f);
        }
        check(body.find(want) != std::string::npos,
              "config/server.json holds the address that was written");
    }

    // 3. The marker is written, and says the flow was WALKED.
    check(firstrun::markCompleted(/*adopted=*/false, &werr),
          "markCompleted reports success");
    const firstrun::Completion after = firstrun::completion();
    check(after.done, "the console now reports itself set up");
    check(after.why == firstrun::Why::Completed,
          "and records that the flow was walked, not adopted");
    check(!after.when.empty(), "with a timestamp");

    // 4. THE ONE THAT MATTERS ON A REBOOT: it survives being read fresh. The
    // marker is a file, so this is the same question as "does the next boot see
    // it" — and a `completion()` that answered from memory would pass every
    // check above while a rebooted console went round the loop again.
    {
        const std::string path = storage::configDir() + "/first-run.json";
        struct stat st;
        check(::stat(path.c_str(), &st) == 0 && st.st_size > 0,
              "config/first-run.json is on disk and not empty");
    }

    // 5. Adopted is a different answer and has to stay different, because it is
    // how a machine set up by hand is told from one that walked the flow.
    check(firstrun::markCompleted(/*adopted=*/true, &werr), "markCompleted(adopted)");
    check(firstrun::completion().why == firstrun::Why::AdoptedExisting,
          "an adopted machine records itself as adopted");

    // 6. SIGN OUT (issue #61): everything tied to the server goes, and
    // nothing else. A console's worth of files in the scratch root, and a
    // scratch HOME for the tokens and the PIN, so no real one is touched.
    {
        const std::string r = root, home = r + "/home";
        setenv("HOME", home.c_str(), 1);
        auto put = [](const std::string& path) {
            storage::makeDirs(path.substr(0, path.rfind('/')));
            if (FILE* f = std::fopen(path.c_str(), "wb")) { std::fputs("x\n", f); std::fclose(f); }
        };
        const std::vector<std::string> gone = {
            r + "/roms/snes/1 - Kept.sfc",       r + "/cache/snes/2 - Cached.sfc",
            r + "/users/1 - A/saves/snes/1/x.srm", r + "/users/1 - A/pending/1-x.srm",
            r + "/covers/srv_6005/a.jpg",        r + "/config/user.json",
            r + "/config/accounts.json",         home + "/.config/cabinetos/accounts/1.json",
            home + "/.config/cabinetos/pin",     home + "/.config/cabinetos/romm.json",
        };
        const std::vector<std::string> kept = {
            r + "/bios/scph5501.bin", r + "/config/settings.json",
        };
        for (const auto& p : gone) put(p);
        for (const auto& p : kept) put(p);
        check(server::unsentSaves() == 1, "an unsent save is counted before signing out");
        check(server::signOut(&werr), "signOut reports success");
        const firstrun::Completion out = firstrun::completion();
        check(!out.done && out.why == firstrun::Why::SignedOut && out.clearing,
              "signed out: first run is needed, and the clearing is still owed");
        server::finishSignOut();
        bool allGone = true;
        for (const auto& p : gone)
            if (storage::exists(p)) { allGone = false; std::printf("        left: %s\n", p.c_str()); }
        check(allGone, "games, saves, covers, accounts, tokens and the PIN are gone");
        bool allKept = true;
        for (const auto& p : kept) allKept = allKept && storage::exists(p);
        check(allKept, "BIOS and the console's settings stay");
        check(storage::exists(r + "/roms") && storage::exists(r + "/cache") &&
                  storage::exists(r + "/users"),
              "the empty folders are back");
        const firstrun::Completion cleared = firstrun::completion();
        check(!cleared.done && cleared.why == firstrun::Why::SignedOut && !cleared.clearing,
              "still signed out, and cleared");
        check(!accounts::pinIsSet(), "no PIN");

        // 7. CHANGE SERVER ADDRESS keeps the covers, filed under the address.
        if (server::addressIsOurs()) {
            firstrun::setServerAddress("old.invalid:6005", &werr);
            put(r + "/covers/" + storage::safeSegment("old.invalid:6005") + "/a.jpg");
            check(server::changeAddress("old.invalid:6005", "new.invalid:6005", &werr),
                  "changeAddress reports success");
            check(firstrun::serverAddress() == "new.invalid:6005", "the new address is read back");
            check(storage::exists(r + "/covers/" + storage::safeSegment("new.invalid:6005") +
                                  "/a.jpg"),
                  "the covers moved with it");
        } else {
            std::printf("  note  the address is set by %s, so changing it cannot be checked "
                        "here\n", firstrun::serverAddressSource().c_str());
        }
    }

    // Leave nothing behind. A test that litters is a test nobody runs twice.
    std::string rm = "rm -rf ";
    rm += root;
    if (std::system(rm.c_str()) != 0)
        std::printf("\n  (could not remove %s)\n", root);

    std::printf("\nfirst-run writes: %d failure%s\n", failures,
                failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

// Every combination of facts the flow can be handed, walked to the end.
//
// WHY THIS EXISTS RATHER THAN A DEMONSTRATION ON ONE MACHINE. The reference
// console is online over a cable with a paired token, so walking it there
// proves the happy path and nothing else — and the rules that matter most are
// the REFUSALS. Open question 15b's whole argument is that first run is a
// linear path with no skip, because on the far side of a skip there is nothing
// to show. A rule like that is only worth anything if it cannot be got round,
// and the way to know is to try every way round it rather than to read the code
// again.
//
// It needs no network, no server, no pad and no screen, so it runs in CI and it
// runs on a laptop.
// Who this console knows, and which one it is acting as. Read-only: it writes
// nothing and is safe on a machine somebody is playing on.
// One game: who keeps it, and how many copies of it are on this machine.
//
// EXISTS TO ANSWER ONE QUESTION AND IT IS THE RIGHT ONE TO BE ABLE TO ASK:
// when two people keep the same game, is it on the disk twice? It must not be.
// The bytes live at `<loc>/roms/<platform>/<romId> - <title>` with no user
// anywhere in the path, so one game is one copy however many people want it —
// but that is a structural argument, and this prints the fact instead.
static int keepersProbe(int romId) {
    const std::vector<int> who = cache::keepers(romId);
    const std::vector<cache::Placement> copies = cache::findAll(romId);

    std::printf("rom           %d\n", romId);
    std::printf("keepers       %zu", who.size());
    for (int id : who) std::printf(" %d", id);
    std::printf("\n");

    std::printf("copies        %zu\n", copies.size());
    int64_t total = 0;
    for (const cache::Placement& p : copies) {
        const int64_t bytes = storage::treeBytes(p.entryPath);
        total += bytes;
        std::printf("  %-6s %s (%lld bytes)\n", p.kept ? "kept" : "cache",
                    p.entryPath.c_str(), static_cast<long long>(bytes));
    }
    std::printf("on disk       %lld bytes\n", static_cast<long long>(total));

    if (copies.size() > 1) {
        // findAll's own comment says more than one is nobody's mistake — the
        // drive-unplug case makes a second copy legitimately, and dedupe is
        // what collapses it. So this is a finding to act on, not a failure.
        std::printf("\nMORE THAN ONE COPY. See cache::dedupe — this is the "
                    "unplugged-drive case, not a keep fault.\n");
    } else if (who.size() > 1 && copies.size() == 1) {
        std::printf("\n%zu people keep this game and there is ONE copy of it, "
                    "which is the whole rule.\n", who.size());
    }
    return 0;
}

static int accountsProbe() {
    std::printf("list          %s\n", accounts::listPath().c_str());
    {
        // The shape, not a real path: 0 is never an account id.
        const std::string one = accounts::tokenPath(0);
        std::printf("tokens        %s<id>.json\n",
                    one.substr(0, one.rfind('/') + 1).c_str());
    }

    const std::vector<accounts::Account> list = accounts::all();
    const int active = accounts::activeId();
    if (list.empty()) {
        std::printf("\nno accounts — this console has not been paired\n");
        return 0;
    }

    std::printf("\n%zu account%s:\n", list.size(), list.size() == 1 ? "" : "s");
    for (const accounts::Account& a : list) {
        struct stat st{};
        const bool haveToken = ::stat(accounts::tokenPath(a.id).c_str(), &st) == 0;
        std::printf("  %s %d - %s%s%s\n",
                    a.id == active ? "*" : " ", a.id, a.name.c_str(),
                    a.avatar.empty() ? "  (no avatar)" : "",
                    haveToken ? "" : "  NO TOKEN ON DISK");
    }
    if (storage::currentUser().valid())
        std::printf("\n* is the account this console acts as. Saves go to %s\n",
                    storage::userDir(storage::currentUser()).c_str());
    else
        std::printf("\n* is the account this console acts as. No user is "
                    "resolved in this process, so no save path to show.\n");
    std::printf("PIN           %s\n", accounts::pinIsSet() ? "set" : "not set");
    return 0;
}

// The store's rules, asserted against a scratch root rather than reasoned
// about. Every case here is a REFUSAL, because the happy path is the part that
// already works — the same argument --first-run-rules is built on, and it found
// a real deadlock on its first run.
static int accountsTest() {
    int failures = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("  %s  %s\n", ok ? "ok  " : "FAIL", what);
        if (!ok) ++failures;
    };

    char root[] = "/tmp/cabinetos-accounts-XXXXXX";
    if (!mkdtemp(root)) {
        std::fprintf(stderr, "[accounts] could not make a scratch root\n");
        return 1;
    }
    // Both halves have to move: the list follows the storage root, the tokens
    // follow HOME. Missing either one writes into the real console.
    storage::setRoot(root);
    setenv("HOME", root, 1);
    std::string serr;
    if (!storage::ensureTree(&serr)) {
        std::fprintf(stderr, "[accounts] scratch root unusable: %s\n", serr.c_str());
        return 1;
    }
    std::printf("scratch root  %s\n\n", root);

    std::string err;
    accounts::Account alice; alice.id = 1; alice.name = "MMagTech";
    accounts::Account bob;   bob.id = 2;   bob.name = "Someone Else";

    check(accounts::all().empty(), "a fresh console knows nobody");
    check(accounts::activeId() == 0, "and is acting as nobody");

    check(!accounts::add(alice, "", &err), "an account with no token is refused");
    accounts::Account noId; noId.name = "nobody";
    check(!accounts::add(noId, "t", &err), "an account with no id is refused");

    check(accounts::add(alice, "alice-token", &err), "the first account is added");
    check(accounts::activeId() == 1, "and becomes active, having nobody to take over from");

    check(accounts::add(bob, "bob-token", &err), "a second account is added");
    check(accounts::activeId() == 1,
          "and does NOT take over from whoever is signed in");
    check(accounts::all().size() == 2, "both are listed");

    check(accounts::add(alice, "alice-again", &err), "re-pairing an existing id succeeds");
    check(accounts::all().size() == 2, "and replaces rather than making a second row");

    check(!accounts::remove(1, &err), "removing the ACTIVE account is refused");
    check(!accounts::remove(99, &err), "removing an unknown account is refused");
    check(accounts::remove(2, &err), "removing an inactive account works");
    check(accounts::all().size() == 1, "and it is gone from the list");

    check(accounts::add(bob, "bob-token", &err) && accounts::setActive(2, &err),
          "someone else can be added and signed in");
    check(accounts::ownerId() == 1, "the first account added owns the console");
    check(!accounts::remove(1, &err),
          "removing the OWNER is refused, even with someone else signed in");
    check(accounts::setActive(1, &err) && accounts::remove(2, &err),
          "and the other one can go once the owner is back");

    check(!accounts::setActive(99, &err), "switching to an unknown account is refused");

    // The list and the tokens can disagree if somebody has been in here by
    // hand. That has to refuse rather than sign the console out silently.
    accounts::add(bob, "bob-token", &err);
    ::unlink(accounts::tokenPath(2).c_str());
    check(!accounts::setActive(2, &err), "switching to an account whose token is gone is refused");


    check(!accounts::pinIsSet(), "no PIN by default");
    check(!accounts::checkPin("0000"), "and nothing matches when none is set");
    check(accounts::setPin("4821", &err), "a PIN can be set");
    check(accounts::pinIsSet() && accounts::checkPin("4821"), "and it matches");
    check(!accounts::checkPin("4822"), "and a wrong one does not");
    check(accounts::setPin("", &err) && !accounts::pinIsSet(), "an empty PIN clears it");

    std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "all good",
                failures, failures == 1 ? "" : "s");
    std::printf("scratch root left at %s\n", root);
    return failures ? 1 : 0;
}

static int firstRunRules() {
    int cases = 0, failures = 0;
    auto fail = [&](const char* what, const firstrun::Facts& f) {
        ++failures;
        std::printf("  FAIL  %s\n        online=%d wired=%d wifiHw=%d wifiUp=%d "
                    "addr=%d checked=%d answered=%d token=%d pads=%d\n",
                    what, f.online, f.wiredOnline, f.wifiPresent, f.wifiConfigured,
                    f.haveServerAddress, f.serverChecked, f.serverAnswered,
                    f.havePairedToken, f.gamepadCount);
    };

    for (int bits = 0; bits < 512; ++bits) {
        firstrun::Facts f;
        f.online          = bits & 1;
        f.wiredOnline     = bits & 2;
        f.wifiPresent     = bits & 4;
        f.wifiConfigured  = bits & 8;
        f.haveServerAddress = bits & 16;
        f.serverAnswered  = bits & 32;
        f.havePairedToken = bits & 64;
        f.gamepadCount    = (bits & 128) ? 1 : 0;
        f.serverChecked   = bits & 256;

        // Nonsense the machine can never be handed: a wired link that is up
        // while nothing is online, or a radio connected with no radio. Skipped
        // rather than asserted about, because a rule about an impossible state
        // is a rule nobody can act on.
        if (f.wiredOnline && !f.online) continue;
        if (f.wifiConfigured && !f.wifiPresent) continue;
        if (f.wifiConfigured && !f.online) continue;
        // net::status() only ever reports online because of a wired device or a
        // radio, so "online by neither" cannot be handed to the machine. It is
        // skipped rather than asserted about — but the Wi-Fi gate is written to
        // survive it anyway, because that is one net.cpp change away from being
        // reachable.
        if (f.online && !f.wiredOnline && !f.wifiConfigured) continue;
        // A server cannot have answered without having been asked.
        if (f.serverAnswered && !f.serverChecked) continue;
        ++cases;

        firstrun::Machine m;
        m.update(f);

        int guard = 0;
        for (; guard < 16 && !m.finished(); ++guard) {
            const firstrun::Step at = m.step();
            const firstrun::Gate g = m.gate();

            // Every refusal explains itself. A setup screen that stops with no
            // sentence is worse than a black one.
            if (g == firstrun::Gate::Blocked && m.because().empty())
                fail("a Blocked step with nothing to say", f);

            // advance() and skip() are the only enforcement there is, so they
            // must refuse on anything but their own gate.
            if (g != firstrun::Gate::Ready) {
                firstrun::Machine copy = m;
                if (copy.advance()) fail("advance() got past a gate that was not ready", f);
            }
            if (g != firstrun::Gate::Skippable) {
                firstrun::Machine copy = m;
                if (copy.skip()) fail("skip() got past a step that may not be skipped", f);
            }

            (void)at;
            if (g == firstrun::Gate::Blocked) break;
            if (g == firstrun::Gate::Skippable) m.skip();
            else m.advance();
        }
        if (guard >= 16) fail("the chain did not terminate", f);

        // THE THREE HARD GATES. Finishing setup without any one of them is a
        // console with nothing to show, which is the thing 15b forbids.
        if (m.finished()) {
            if (!f.online)          fail("finished setup while offline", f);
            if (!f.serverAnswered)  fail("finished setup with no server answering", f);
            if (!f.havePairedToken) fail("finished setup with no token", f);
        }

        // A machine with a radio, no cable and no Wi-Fi must not get past the
        // network step at all.
        if (!f.online && m.step() != firstrun::Step::Network)
            fail("got past the network step while offline", f);

        // The one soft gate, and it has to STAY soft: refusing to finish
        // without a controller would break the keyboard guarantee the whole
        // flow exists to make.
        if (f.online && f.serverAnswered && f.havePairedToken && !m.finished())
            fail("a fully configured machine could not finish setup", f);
    }

    std::printf("first-run rules: %d fact combinations, %d failure%s\n", cases,
                failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}

// Where a real console would be in setup right now, and where it would stop.
//
// It WALKS the chain rather than describing it, advancing and skipping exactly
// as the rules allow, and halts at the first gate that refuses — which is the
// same thing a person would hit. A description can drift from the rules; a walk
// cannot.
static int firstRunProbe(romm::Client& client, const char* startAt, bool tryServer) {
    const firstrun::Completion done = firstrun::completion();
    std::printf("first run       %s\n",
                done.why == firstrun::Why::SignedOut
                      ? (done.clearing ? "NEEDED, signed out; games and accounts not cleared yet"
                                       : "NEEDED, signed out")
                : !done.done             ? "NEEDED — this console is not set up"
                : done.why == firstrun::Why::AdoptedExisting
                      ? "not needed — this machine is already configured, so it "
                        "is treated as set up"
                      : "not needed — setup was completed here");
    if (!done.when.empty()) std::printf("marked          %s\n", done.when.c_str());

    const std::string address = firstrun::serverAddress();
    std::printf("server address  %s  (from %s)\n",
                address.empty() ? "none" : address.c_str(),
                firstrun::serverAddressSource().c_str());

    firstrun::Facts f = firstrun::observe(client, 0);

    // Only if asked: it is the one part of this command that talks to anything
    // over the network, and a report should not quietly cost a round trip.
    if (tryServer && !address.empty()) {
        std::string err;
        romm::Client probe;
        f.serverAnswered = probe.setAddress(address, &err);
        std::printf("server answers  %s%s%s\n", f.serverAnswered ? "yes" : "no",
                    f.serverAnswered ? " — RomM " : " — ",
                    f.serverAnswered ? probe.serverVersion().c_str() : err.c_str());
    }

    std::printf("\nwhat it sees\n");
    std::printf("  online              %s\n", f.online ? "yes" : "no");
    std::printf("  online over a cable %s\n", f.wiredOnline ? "yes" : "no");
    std::printf("  wi-fi radio         %s\n", f.wifiPresent ? "present" : "none");
    std::printf("  wi-fi connected     %s\n", f.wifiConfigured ? "yes" : "no");
    std::printf("  server address      %s\n", f.haveServerAddress ? "yes" : "no");
    std::printf("  server answered     %s\n",
                f.serverAnswered ? "yes" : tryServer ? "no" : "not asked");
    std::printf("  paired token        %s\n", f.havePairedToken ? "yes" : "no");
    // NOT COUNTED HERE, and said so rather than printed as a zero: this whole
    // command runs before SDL, so there is nothing to ask. A report that shows
    // "0" for a question it never put is the kind of instrument that costs an
    // afternoon.
    std::printf("  gamepads            not counted — this runs before SDL\n");

    firstrun::Machine m;
    m.update(f);
    if (startAt && !m.openAt(startAt)) {
        std::fprintf(stderr, "[first-run] no step called '%s'\n", startAt);
        return 1;
    }

    std::printf("\nwalking the chain\n\n");
    for (int guard = 0; guard < 16; ++guard) {
        const firstrun::Gate g = m.gate();
        const std::string why = m.because();
        std::printf("  %-11s %s\n", firstrun::name(m.step()),
                    g == firstrun::Gate::Ready       ? "ready"
                    : g == firstrun::Gate::Skippable ? "may be skipped"
                                                     : "BLOCKED");
        if (!why.empty()) std::printf("              \"%s\"\n", why.c_str());
        if (m.finished()) {
            std::printf("\nsetup would complete.\n");
            return 0;
        }
        if (g == firstrun::Gate::Ready) {
            m.advance();
        } else if (g == firstrun::Gate::Skippable) {
            m.skip();
        } else {
            std::printf("\nsetup would stop here, and this is the correct "
                        "behaviour: there is no way past this step.\n");
            return 0;
        }
    }
    std::fprintf(stderr, "[first-run] the chain did not terminate — that is a bug\n");
    return 1;
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

    // `--romm-pair` MEANS PAIR, and since 2026-09-21 that means ADD SOMEBODY.
    //
    // It used to load `~/.config/cabinetos/romm.json` first and skip pairing
    // whenever that file existed — which the handover recorded as "it does
    // nothing if a token already exists", with a workaround of pointing HOME
    // at an empty directory. That was a sensible shape when a console had one
    // token and re-pairing was a mistake. With accounts it is exactly backwards:
    // adding a second person to a console that already has one is the whole
    // point of the flag, and every console that has been set up already has an
    // account.
    //
    // So the flag pairs unconditionally, and without it the probe acts as
    // whoever the console is acting as.
    if (!allowPairing) {
        if (!accounts::loadActiveToken(client)) {
            std::fprintf(stderr,
                         "[romm] no account on this console — run again with "
                         "--romm-pair\n");
            return 1;
        }
    } else {
        // WHOEVER APPROVES THIS IS WHO GETS ADDED. The console does not choose;
        // the browser session that approves the code does, and `recordPairing`
        // then asks the server which user that was. Approving as somebody who
        // already has an account here re-pairs them rather than making a second
        // row — see `accounts::add`.
        std::printf("\nSign in to RomM as the person you are ADDING before you\n"
                    "approve this — whoever approves it is who gets added.\n");
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
        std::string aerr;
        accounts::Paired paired;
        if (accounts::recordPairing(client, &paired, &aerr)) {
            // WHO WAS ACTUALLY ADDED, asked rather than assumed. Reporting
            // `activeId()` here would name the wrong person for every account
            // after the first, because adding somebody deliberately does not
            // switch to them.
            std::printf("\n%s   %d - %s\n",
                        paired.isNew ? "added      " : "RE-PAIRED  ",
                        paired.id, paired.name.c_str());
            if (!paired.isNew)
                std::printf("            NOBODY WAS ADDED — that account was already\n"
                            "            here, so its token was refreshed instead.\n"
                            "            Approve as the person you are ADDING.\n");
            std::printf("token       %s\n", accounts::tokenPath(paired.id).c_str());
            if (paired.id == accounts::activeId())
                std::printf("active      yes — this console was already acting as them\n");
            else
                std::printf("active      no  — still acting as %d. Switch from the chip.\n",
                            accounts::activeId());
        } else {
            std::printf("\n");
            std::fflush(stdout);
            std::fprintf(stderr,
                         "[romm] PAIRED, BUT IT WAS NOT RECORDED: %s\n"
                         "[romm] This console will not stay paired. Fix that and\n"
                         "[romm] run --romm-probe --romm-pair again.\n",
                         aerr.empty() ? std::strerror(errno) : aerr.c_str());
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
    // a controller. 0 Recent, 1 Favorites. The bar is not a row any more — see
    // the Row enum — so a capture of it wants --focus-bar, not --row 0.
    int initialRow = -1;
    // What to type into Search for a capture. See the route below.
    const char* searchQuery = nullptr;
    // Integer scaling for a game's picture: OFF by default since 2026-09-21,
    // so every system fills the height. See the draw site for what that trades.
    bool integerScaling = false;
    // THE SWITCH SETTINGS WILL OWN, as a flag until Settings exists. MMagTech:
    // *"a navigation sound of some sort would be nice and later we have the
    // option in settings to turn it off."* The off half is built now so that
    // when the screen arrives it has something to set rather than something to
    // implement.
    // --ui-sound overrides the saved level for this run only, for tests.
    bool uiSoundFlag = false;
    const char* uiSoundLevels = nullptr;
    bool uiSound = true;
    float uiSoundVolume = 0.22f;
    // HOME'S BACKDROP, TUNABLE WITHOUT A COMPILER.
    //
    // design.h is still where these live and still what ships — these start as
    // its values and are only moved by a flag. The reason the flag exists is
    // that the three of them can only be judged on a television, and the loop
    // from "try 0.55" to "see 0.55 on the panel" is otherwise a rebuild, a
    // deploy and a restarted session for one number. MMagTech on this session:
    // *"its going to be all over the place i have alot to tweak on the ui."*
    //
    // Whatever wins goes back into design.h. A number that only exists on a
    // command line is not a decision, it is a thing somebody once tried.
    float backdropFill = kHomeBackdropFill;
    float backdropScrim = kHomeBackdropScrim;
    float backdropTexels = kHomeBackdropTexels;
    // The two that decide how it MOVES rather than how it looks, and they are
    // the pair most worth arguing with on a panel: how long it waits before it
    // notices, and how long it takes to get there.
    float backdropDelay = kHomeBackdropDelay;
    float backdropFade = kHomeBackdropFade;
    // And the bar's own three, for the same reason. Where the room above and
    // below the top bar goes is a judgement about a television and nothing
    // else — a capture cannot show whether two rows of text are crowding each
    // other from a sofa, and this project has been wrong about the top edge of
    // a screen before.
    float barTop = kBarTop;
    float barHeight = kBarHeight;
    float barGapBelow = kBarGapBelow;
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
    // Idle handling — idle.h. `--idle-scale 0.01` shrinks every idle timer a
    // hundredfold so a dim can be looked at in seconds; `--shift-every 5`
    // walks the pixel shift fast enough to watch; `--no-idle` turns the dim
    // and the blank off, for a capture that has to stay lit.
    double idleScale = 1.0;
    double shiftEvery = idle::kShiftEverySeconds;
    bool idleOff = false;
    // Opens the Power menu at startup, so a capture can show it.
    bool powerMenuDemo = false;
    // Walks the notification pill through every message it can show.
    bool noticeGallery = false;
    // `--menu-fade 4` stretches the menus' fade so a person can watch it in
    // slow motion and say what is wrong with it. Tuning only.
    float overlayFadeSeconds = kOverlayFade;
    // `--menu-rise 0` drops the slide the menus arrive with. Tuning only.
    float overlayRise = 24.0f;
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
    int autoSwitchAccountId = 0;
    int autoUnkeepId = 0;
    bool storageReport = false;
    bool coreOptionsAudit = false;
    // The same audit with the prose and the value list, which is what turns
    // "here are 78 keys" into something a person can decide from.
    bool coreOptionsDetail = false;
    // One core option, set from the command line, for finding out what a value
    // actually does before writing it into catalog::optionOverrides.
    //
    // It exists because the alternative is a rebuild per value, and an option
    // is exactly the kind of thing that has to be tried rather than reasoned
    // about — `catalog::optionOverrides` already says so, and open question 7
    // asks for Cabinet's whole per-platform table to be brought across "with a
    // reason recorded beside each choice", which means somebody has to be able
    // to see the difference each one makes. Two of the four overrides in that
    // table today were found by a launch failing, not by reading.
    //
    // These are applied ON TOP of catalog::optionOverrides, so this can also be
    // used to take a shipped override back off and see what it was buying.
    std::map<std::string, std::string> cliOptionOverrides;
    const char* initialScreen = nullptr;
    int initialTile = 0;
    const char* navScript = nullptr;
    int initialTab = 0;
    int initialGame = 0;
    bool rommProbeMode = false;
    // --- The first-run mechanisms ------------------------------------------
    //
    // Each of the four things first run needs that is NOT a picture has a way
    // to be seen working from a shell: the state machine, the QR renderer, the
    // NetworkManager plumbing, and knowing whether it is the first run at all.
    // See the probes above.
    bool networkProbeMode = false;
    bool networkScan = false;
    // What this machine's graphics hardware can do, before anything is created.
    // See gpu.h: the A9 and the test VM differ here in the one way that decides
    // whether PlayStation 2 and GameCube can be played at all.
    bool gpuProbeMode = false;
    bool firstRunProbeMode = false;
    int keepersRomId = 0;
    int focusBarSlot = -1;
    bool startupShot = false;
    bool accountsProbeMode = false;
    // --server-check <address>: would this address be accepted as the same
    // server by Change server address? Says which answer, and exits.
    const char* serverCheckAddress = nullptr;
    bool accountsTestMode = false;
    bool firstRunRulesMode = false;
    bool firstRunWriteMode = false;
    // Runs the setup flow even on a console that is already configured, so it
    // can be looked at and photographed. It never writes anything — see
    // setup::Options::dryRun — because the only machines anybody here can try
    // it on are ones that are already set up.
    bool forceSetup = false;
    // The escape hatch: start the console without setup on a machine that
    // cannot complete it. Not a product affordance, a development one.
    bool noSetup = false;
    const char* setupStep = nullptr;
    bool firstRunServerCheck = false;
    const char* firstRunStep = nullptr;
    const char* qrText = nullptr;
    const char* qrPbm = nullptr;
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
    // See --overlay-test below. A test instrument for open question 24.
    bool overlayTest = false;
    // Opens the overlay once the game is up, so it can be looked at on a
    // machine with nothing attached to it. See --overlay for the frame count,
    // which is what makes the picture underneath it a GAME rather than black.
    bool overlayDemo = false;
    int overlayDemoAfter = 0;
    // --load-state N: after N frames of play, what the pause menu's "Load
    // latest state" does. The other half of --sync-test, which loads back the
    // state it has just made; this loads whatever is newest on the server, so
    // a state from an Apple TV can be tried here without a controller.
    int loadStateAfter = 0;
    // --state-check N: after N frames, save a state IN MEMORY, play on, load
    // it, and save again at once. Equal bytes mean the load put the machine
    // back exactly. Nothing is written to disk or sent anywhere; it asks only
    // "does a state this console makes load on this console".
    int stateCheckAfter = 0;
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
        } else if (SDL_strcmp(argv[i], "--idle-scale") == 0 && i + 1 < argc) {
            idleScale = SDL_atof(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--shift-every") == 0 && i + 1 < argc) {
            shiftEvery = SDL_atof(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--no-idle") == 0) {
            idleOff = true;
        } else if (SDL_strcmp(argv[i], "--power-menu") == 0) {
            powerMenuDemo = true;
        } else if (SDL_strcmp(argv[i], "--notice-gallery") == 0) {
            noticeGallery = true;
        } else if (SDL_strcmp(argv[i], "--menu-rise") == 0 && i + 1 < argc) {
            overlayRise = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--menu-fade") == 0 && i + 1 < argc) {
            overlayFadeSeconds = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--integer-scale") == 0) {
            integerScaling = true;
            std::fprintf(stderr, "[picture] integer scaling, with bars\n");
        } else if (SDL_strcmp(argv[i], "--query") == 0 && i + 1 < argc) {
            searchQuery = argv[++i];
        } else if (SDL_strcmp(argv[i], "--ui-sound") == 0 && i + 1 < argc) {
            const char* v = argv[++i];
            uiSoundFlag = true;
            uiSound = !(SDL_strcmp(v, "off") == 0 || SDL_strcmp(v, "0") == 0);
            if (uiSound) uiSoundVolume = static_cast<float>(SDL_atof(v)) > 0
                                             ? static_cast<float>(SDL_atof(v))
                                             : uiSoundVolume;
            std::fprintf(stderr, "[sound] %s, volume %.2f\n",
                         uiSound ? "on" : "off", uiSoundVolume);
        } else if (SDL_strcmp(argv[i], "--ui-sound-levels") == 0 && i + 1 < argc) {
            // quiet,medium,loud, e.g. --ui-sound-levels 0.07,0.22,0.65
            uiSoundLevels = argv[++i];
        } else if (SDL_strcmp(argv[i], "--home-bar") == 0 && i + 1 < argc) {
            // top,height,gap — e.g. --home-bar 36,56,52
            float t = barTop, h = barHeight, g = barGapBelow;
            if (std::sscanf(argv[++i], "%f,%f,%f", &t, &h, &g) >= 1) {
                barTop = std::clamp(t, 0.0f, 300.0f);
                barHeight = std::clamp(h, 20.0f, 200.0f);
                barGapBelow = std::clamp(g, 0.0f, 300.0f);
            }
            std::fprintf(stderr, "[home] bar top %.0f height %.0f gap %.0f\n",
                         barTop, barHeight, barGapBelow);
        } else if (SDL_strcmp(argv[i], "--home-backdrop") == 0 && i + 1 < argc) {
            // fill,scrim,texels — e.g. --home-backdrop 0.6,0.22,28
            float f = backdropFill, s = backdropScrim, t = backdropTexels;
            float d = backdropDelay, fade = backdropFade;
            if (std::sscanf(argv[++i], "%f,%f,%f,%f,%f", &f, &s, &t, &d, &fade) >= 1) {
                backdropFill = std::clamp(f, 0.0f, 1.0f);
                backdropScrim = std::clamp(s, 0.0f, 1.0f);
                backdropTexels = std::clamp(t, 1.0f, 512.0f);
                backdropDelay = std::clamp(d, 0.0f, 3.0f);
                backdropFade = std::clamp(fade, 0.05f, 5.0f);
            }
            std::fprintf(stderr,
                         "[home] backdrop fill %.2f scrim %.2f texels %.0f "
                         "delay %.2fs fade %.2fs\n",
                         backdropFill, backdropScrim, backdropTexels,
                         backdropDelay, backdropFade);
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
        } else if (SDL_strcmp(argv[i], "--overlay-test") == 0) {
            // CAN THIS CONSOLE'S OWN MENU BE DRAWN ONTO NOTHING?
            //
            // A TEST INSTRUMENT, not a product mode, and the one thing that
            // could kill the compositing route in open question 24. Everything
            // proved there was proved with flat rectangles pushed through X11.
            // The pause menu is signed-distance fields in a GLES context that
            // has always had an opaque frame to draw onto, and a renderer that
            // gets premultiplied alpha wrong produces dark haloes around every
            // letter — invisible to any assertion, obvious on a television.
            //
            // It puts the pause menu up, over nothing, as a gamescope overlay.
            // Put a game under it with tools/gamescope-overlay-test.sh and LOOK.
            overlayTest = true;
        } else if (SDL_strcmp(argv[i], "--ps2-upscale") == 0 && i + 1 < argc) {
            // HOW MANY TIMES THE PLAYSTATION 2'S OWN RESOLUTION TO RENDER AT.
            //
            // It renders 640x448, so on a 3840x2160 panel showing a 4:3
            // picture — 2880x2160 of real screen — the arithmetic is
            // 2160/448 = 4.8. **FIVE IS THE FIRST VALUE THAT IS GENUINELY 4K
            // AND SIX CLEARS IT.** Below that the panel is stretching.
            //
            // A flag rather than a constant because this is the single largest
            // thing anybody can change about how a PlayStation 2 game looks,
            // and finding the right number is a matter of sitting in front of
            // the television rather than of reasoning. It is also the shape
            // open question 23 will want — one quality setting for the whole
            // console, not a per-emulator menu.
            gPs2Upscale = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--ps2-aniso") == 0 && i + 1 < argc) {
            // Anisotropic filtering: 0, 2, 4, 8 or 16. PCSX2 ships it OFF.
            // It sharpens surfaces seen at a steep angle, which on a
            // PlayStation 2 is the road ahead in every racing game. It cannot
            // add detail that was never rendered, so it is the smaller of the
            // two levers.
            gPs2Anisotropy = SDL_atoi(argv[++i]);
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
        } else if (SDL_strcmp(argv[i], "--switch-account") == 0 && i + 1 < argc) {
            // No switcher screen yet, so this is how the teardown is exercised.
            autoSwitchAccountId = SDL_atoi(argv[++i]);
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
            // Optional frame count: --overlay 300 PLAYS FIRST and opens the
            // menu after 300 frames. Without it the menu opens the instant the
            // game is up, which pauses the core before it has drawn anything —
            // so the panel appears over black and looks like the game failed to
            // render. That cost a confusing ten minutes on 2026-09-21; the game
            // was fine and the flag was the fault.
            if (i + 1 < argc && argv[i + 1][0] != '-') overlayDemoAfter = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--state-check") == 0 && i + 1 < argc) {
            stateCheckAfter = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--load-state") == 0 && i + 1 < argc) {
            loadStateAfter = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--sync-test") == 0) {
            syncTest = true;
        } else if (SDL_strcmp(argv[i], "--launch-after") == 0 && i + 1 < argc) {
            autoLaunchAfter = static_cast<float>(SDL_atof(argv[++i]));
        } else if (SDL_strcmp(argv[i], "--rom-probe") == 0 && i + 1 < argc) {
            romProbeId = SDL_atoi(argv[++i]);
            rommProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--rom-exts") == 0 && i + 1 < argc) {
            romProbeExts = argv[++i];
        } else if (SDL_strcmp(argv[i], "--network") == 0) {
            networkProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--network-scan") == 0) {
            networkProbeMode = true;
            networkScan = true;
        } else if (SDL_strcmp(argv[i], "--gpu-probe") == 0) {
            gpuProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--qr") == 0 && i + 1 < argc) {
            qrText = argv[++i];
        } else if (SDL_strcmp(argv[i], "--qr-out") == 0 && i + 1 < argc) {
            qrPbm = argv[++i];
        } else if (SDL_strcmp(argv[i], "--setup") == 0) {
            forceSetup = true;
        } else if (SDL_strcmp(argv[i], "--setup-step") == 0 && i + 1 < argc) {
            forceSetup = true;
            setupStep = argv[++i];
        } else if (SDL_strcmp(argv[i], "--no-setup") == 0) {
            noSetup = true;
        } else if (SDL_strcmp(argv[i], "--startup-screen") == 0) {
            startupShot = true;
        } else if (SDL_strcmp(argv[i], "--focus-bar") == 0) {
            // A capture of the bar's own focus, which nothing could take until
            // now — line 2716 has referred to this flag since the bar was
            // built and it was never actually added. An optional slot follows:
            // 0 Library, 1 Search, 2 Settings, 3 the account chip.
            focusBarSlot = 0;
            if (i + 1 < argc && argv[i + 1][0] != '-') focusBarSlot = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--keepers") == 0 && i + 1 < argc) {
            keepersRomId = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--server-check") == 0 && i + 1 < argc) {
            serverCheckAddress = argv[++i];
        } else if (SDL_strcmp(argv[i], "--accounts") == 0) {
            accountsProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--accounts-test") == 0) {
            accountsTestMode = true;
        } else if (SDL_strcmp(argv[i], "--first-run-rules") == 0) {
            firstRunRulesMode = true;
        } else if (SDL_strcmp(argv[i], "--first-run-writes") == 0) {
            firstRunWriteMode = true;
        } else if (SDL_strcmp(argv[i], "--first-run") == 0) {
            firstRunProbeMode = true;
        } else if (SDL_strcmp(argv[i], "--first-run-step") == 0 && i + 1 < argc) {
            firstRunProbeMode = true;
            firstRunStep = argv[++i];
        } else if (SDL_strcmp(argv[i], "--first-run-check-server") == 0) {
            firstRunProbeMode = true;
            firstRunServerCheck = true;
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
        } else if (SDL_strcmp(argv[i], "--core-no-hw-render") == 0) {
            // The other control. See core.h.
            cab::Core::setRefuseHWRender(true);
        } else if (SDL_strcmp(argv[i], "--core-options") == 0) {
            // Every option every built core declares, and what it is answered
            // with. This is the audit docs/PROJECT.md asked for and nobody had
            // run: an unanswered option is not the default, it is zero, and
            // until this existed there was no way to see which were which.
            coreOptionsAudit = true;
        } else if (SDL_strcmp(argv[i], "--core-options-detail") == 0) {
            coreOptionsAudit = true;
            coreOptionsDetail = true;
        } else if (SDL_strcmp(argv[i], "--core-option") == 0 && i + 1 < argc) {
            //   --core-option dolphin_shader_compilation_mode=Synchronous
            // Repeatable. See cliOptionOverrides.
            const std::string kv = argv[++i];
            const size_t eq = kv.find('=');
            if (eq == std::string::npos || eq == 0) {
                std::fprintf(stderr, "[frontend] --core-option wants key=value, got '%s'\n",
                             kv.c_str());
                return 1;
            }
            cliOptionOverrides[kv.substr(0, eq)] = kv.substr(eq + 1);
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
        } else if (SDL_strcmp(argv[i], "--nav") == 0 && i + 1 < argc) {
            // Presses, in order, after --screen has put the console somewhere:
            // "up,right,a". For checking a route headless, the way a person
            // would walk it, through the same door their pad goes through.
            navScript = argv[++i];
        } else if (SDL_strcmp(argv[i], "--game") == 0 && i + 1 < argc) {
            initialGame = SDL_atoi(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--update-dir") == 0 && i + 1 < argc) {
            // System update's status read from here instead of /run, so each
            // of its states can be put on the television by writing a file
            // (tools/ui-loop.sh). A `version` file here stands in for the
            // image's own. update.h.
            update::setDir(argv[++i]);
        } else if (SDL_strcmp(argv[i], "--files-dir") == 0 && i + 1 < argc) {
            // File access's state and password read from here instead, for
            // the same reason as --update-dir. files.h.
            files::setDir(argv[++i]);
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
    //
    // TWO PLACES, AND THE ENVIRONMENT WINS. /etc/cabinetos/session.env is
    // root's and is how a console is set up by hand today; config/server.json
    // is what the first-run screen writes, because the session user cannot
    // write /etc and inventing a privileged helper for one string that is not a
    // secret would be a mechanism for nothing. firstrun::serverAddress() holds
    // that order in one place so nothing else has to know it.
    static std::string resolvedAddress;
    if (!rommAddress) {
        resolvedAddress = firstrun::serverAddress();
        if (!resolvedAddress.empty()) rommAddress = resolvedAddress.c_str();
    }

    // Neither of these needs a storage tree, a window, GL or a controller, so
    // they answer before anything is created on disk. --qr in particular should
    // not cost a console a directory it did not have.
    if (accountsTestMode) return accountsTest();
    if (firstRunRulesMode) return firstRunRules();
    if (firstRunWriteMode) return firstRunWriteTest();
    if (qrText) return qrProbe(qrText, qrPbm);
    if (networkProbeMode) return networkProbe(networkScan);
    if (gpuProbeMode) {
        cab::gpu::report();
        return 0;
    }

    // --- Where everything lives, decided before anything writes a byte ------
    //
    // Printed rather than assumed. A console quietly writing somewhere nobody
    // expected is the kind of bug that costs an afternoon, and the one line it
    // takes to prevent that is this one.
    //
    // USB drives are mounted first, so the tree below and everything after it
    // see a drive that was plugged in before the console started. drives.h.
    if (!shotMode) drives::start();
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

    // Needs the storage tree, because the marker and the server address both
    // live in it — and it must run before anything else looks at the server,
    // since "is this console set up at all" is the question underneath all of
    // them.
    if (firstRunProbeMode) {
        romm::Client fr;
        fr.loadToken(rommTokenPath());
        return firstRunProbe(fr, firstRunStep, firstRunServerCheck);
    }

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
    // NOT when a specific core and ROM were named. --core-options-detail then
    // means "show me THAT core's table with a game loaded", which is the only
    // way to see the three that declare nothing until they know what they are
    // running — Dolphin, FBNeo and MAME. Sweeping the directory instead would
    // report zero for exactly the cores the question is about.
    if (coreOptionsAudit && !(corePath && romPath) && autoLaunchId == 0) {
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
                if (!coreOptionsDetail) {
                    std::printf("  %-34s %-18s %s%s\n", o.key.c_str(), o.chosen.c_str(),
                                o.overridden ? "OURS, core says " : "core default",
                                o.overridden ? o.defaultValue.c_str() : "");
                    continue;
                }
                // EVERYTHING THE CORE SAID, which is the difference between an
                // audit and a list. A key and a value cannot answer "what
                // would this do if I changed it" — that needs the prose the
                // core wrote and every value it will accept, and both are
                // already captured and were simply never printed.
                std::printf("  %s\n", o.key.c_str());
                if (!o.desc.empty()) std::printf("      what   %s\n", o.desc.c_str());
                std::printf("      now    %s%s\n", o.chosen.c_str(),
                            o.overridden ? "   (ours)" : "");
                if (!o.defaultValue.empty() && o.overridden)
                    std::printf("      core   %s\n", o.defaultValue.c_str());
                if (!o.values.empty()) {
                    std::printf("      takes ");
                    for (size_t i = 0; i < o.values.size(); ++i)
                        std::printf("%s%s", i ? " | " : " ", o.values[i].c_str());
                    std::printf("\n");
                }
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
    // Read-only and needs no network: the list is on disk. It goes here rather
    // than with --first-run-rules because it reports the REAL console, so it
    // has to run after the storage root is settled.
    if (accountsProbeMode) return accountsProbe();
    if (serverCheckAddress) {
        std::string detail;
        const server::Check r = server::check(serverCheckAddress, &detail);
        std::printf("%s: %s%s%s\n", serverCheckAddress,
                    r == server::Check::Same        ? "same server"
                    : r == server::Check::Different ? "different server"
                    : r == server::Check::NoServer  ? "no server"
                                                    : "could not tell",
                    detail.empty() ? "" : " (", detail.empty() ? "" : (detail + ")").c_str());
        return r == server::Check::Same ? 0 : 1;
    }
    if (keepersRomId > 0) return keepersProbe(keepersRomId);

    if (storageReport) {
        romm::Client sclient;
        if (rommAddress && sclient.setAddress(rommAddress, nullptr))
            sclient.loadToken(rommTokenPath());
        adoptUser(sclient);

        std::printf("root            %s\n", storage::root().c_str());
        std::printf("pending upload  %10.2f GB\n", cache::pendingBytes() / 1e9);
        std::printf("system reserve  %10.2f GB\n", cache::kSystemReserveBytes / 1e9);
        // WHERE A KEPT GAME WOULD LAND, by size: the main drive up to 80%,
        // then the extra drive with the most room (cache::keepLocation).
        for (const double g : {1.0, 5.0, 50.0})
            std::printf("keep a %2.0f GB game  -> %s\n", g,
                        cache::keepLocation(static_cast<int64_t>(g * 1e9)).c_str());

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
                        loc == cache::keepLocation(0) ? "   <- the next kept game goes here" : "");
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

    // THE OVERLAY SLOT IS AN X11 IDEA, so the test mode has to be an X11
    // client. gamescope's STEAM_OVERLAY and STEAM_INPUT_FOCUS are window
    // properties its Xwayland half reads; a native Wayland client can reach the
    // external-overlay plane but not the one that takes input. Under gamescope
    // Xwayland is always there, so this costs nothing — but it is a real
    // constraint on the design and not an artefact of the test.
    if (overlayTest) SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "x11");

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | SDL_INIT_AUDIO)) {
        std::fprintf(stderr, "[frontend] SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    // The interface's own sounds. Opened here rather than lazily, because the
    // first click a person hears should not be the second one they asked for —
    // and a console with no audio device says so once and stays silent.
    sound::init();
    if (uiSoundLevels) {
        float q = 0, m = 0, l = 0;
        if (std::sscanf(uiSoundLevels, "%f,%f,%f", &q, &m, &l) == 3) {
            sound::setLevelVolumes(q, m, l);
            std::fprintf(stderr, "[sound] levels %.2f, %.2f, %.2f\n", q, m, l);
        }
    }
    if (uiSoundFlag) {
        sound::setEnabled(uiSound);
        sound::setVolume(uiSoundVolume);
    } else {
        // The level Settings saved. Nothing saved, or a word this build does
        // not know, is Medium: how the console always sounded.
        sound::Level lv = sound::Level::Medium;
        sound::levelFromWord(prefs::get("interface_sounds", ""), &lv);
        sound::setLevel(lv);
        std::fprintf(stderr, "[sound] interface sounds %s\n", sound::levelName(lv));
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

    // CREATED HIDDEN IN OVERLAY MODE, because gamescope classifies a window
    // when it MAPS. Set the properties afterwards and they land on a window
    // nothing re-examines, and the overlay never appears — which looks exactly
    // like a compositor that refused. Hidden, marked, then shown.
    SDL_WindowFlags windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN;
    if (overlayTest) windowFlags |= SDL_WINDOW_TRANSPARENT | SDL_WINDOW_HIDDEN;

    SDL_Window* window = SDL_CreateWindow("CabinetOS", 1920, 1080, windowFlags);
    if (!window) {
        std::fprintf(stderr, "[frontend] SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return 1;
    }

    if (overlayTest) {
        if (!cab::overlaywin::mark(window, /*takeInput=*/true))
            std::fprintf(stderr, "[overlay] could not mark the window — it will "
                                 "come up as an ordinary one\n");
        SDL_ShowWindow(window);
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

    // Draw onto nothing rather than onto black. Only the two clears change —
    // the blend function was already correct for it.
    if (overlayTest) renderer.setTransparentBackground(true);

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

    // --- FIRST RUN -----------------------------------------------------------
    //
    // Before anything asks a server for anything, because on a console that has
    // never been paired there is no server to ask. Setup runs a loop of its own
    // and returns once the machine is configured; see setup.h for why it is not
    // a mode inside the loop below.
    //
    // A MACHINE THAT IS ALREADY CONFIGURED IS NEVER SHOWN THIS. The reference
    // console was set up by hand over SSH and has been playing games for a day;
    // presenting it with a welcome screen would be a far worse failure than
    // skipping a wizard nobody needed. firstrun::completion() answers "is this
    // machine configured", not "has this flow been run" — and where the answer
    // came from being configured rather than from a marker, the marker is
    // back-filled here so the judgement is made once and recorded.
    if (!noSetup) {
        firstrun::Completion done = firstrun::completion();
        // SIGNED OUT AND NOT YET CLEARED: the last run signed out and started
        // this one. Cleared here, before anything else runs, so nothing can
        // write into a folder being emptied; server.h. Under the startup
        // screen the last run left on.
        if (done.why == firstrun::Why::SignedOut && done.clearing && !shotMode) {
            setup::Deps deps;
            deps.window = window;
            deps.renderer = &renderer;
            deps.text = &text;
            setup::showWaiting(deps, "", "Signing out");
            server::finishSignOut();
            done = firstrun::completion();
        }
        if (forceSetup || !done.done) {
            setup::Deps deps;
            deps.window = window;
            deps.renderer = &renderer;
            deps.text = &text;
            setup::Options opts;
            // Back from Sign out: the network is set up and stays, so the
            // server step (docs/SETTINGS.md, Network).
            opts.startStep = setupStep ? setupStep
                             : done.why == firstrun::Why::SignedOut ? "server"
                                                                    : nullptr;
            opts.signedOut = done.why == firstrun::Why::SignedOut;
            // A capture of a setup screen must never write a marker or an
            // address: it is a photograph, and the machine it is taken on is
            // usually one that is already set up.
            opts.dryRun = forceSetup || shotMode;
            opts.screenshotPath = shotMode ? shotPath : nullptr;
            opts.frames = shotAfterFrames;
            opts.renderWidth = renderW;
            opts.renderHeight = renderH;
            const setup::Outcome outcome = setup::run(deps, opts);
            if (outcome != setup::Outcome::Completed) {
                renderer.shutdown();
                SDL_Quit();
                return 0;
            }
            // Setup writes the address; pick it up, because the resolution done
            // before SDL started ran on a console that had none.
            resolvedAddress = firstrun::serverAddress();
            rommAddress = resolvedAddress.empty() ? nullptr : resolvedAddress.c_str();
        } else if (done.why == firstrun::Why::AdoptedExisting && done.when.empty()) {
            // Adopted, and no marker on disk yet — so write one. An empty
            // `when` is exactly the case where the answer came from looking at
            // the machine rather than from a file.
            std::string merr;
            if (firstrun::markCompleted(/*adopted=*/true, &merr))
                std::fprintf(stderr,
                             "[first-run] this console was already configured; "
                             "recorded that rather than asking again\n");
        }
    }

    // TEXT INPUT IS OFF BY DEFAULT IN SDL3, and the handler for it below has
    // therefore never once fired. Without this a physical keyboard cannot type
    // a character into the on-screen keyboard — which is not a small gap, it is
    // the input the whole first-run design is built on being guaranteed.
    SDL_StartTextInput(window);

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
    // THE STORE IS ONE OBJECT AND THE OLD NAMES ARE REFERENCES INTO IT.
    //
    // These were eight independent locals until open question 28, which was
    // fine while the whole library arrived in a single call at boot and
    // nothing ever grew afterwards. Grids, collections and search now each
    // append to the store while the console is running, and `appendGame` has
    // to be handed somewhere to append TO — so the pieces live together again.
    //
    // The names below are unchanged on purpose: several hundred lines of this
    // file read `cards[i]` and `shelf`, and rewriting all of them to reach
    // through `lib.` would be a large diff that changed nothing.
    Library lib;
    std::vector<Card>& cards = lib.cards;
    int& heroIndex = lib.heroIndex;
    std::string& heroPlatform = lib.heroPlatform;
    // Which cards the Recent row shows. Empty means "everything", which is what
    // the stand-in library wants — it has no play history to order by.
    std::vector<int>& shelf = lib.shelf;
    std::vector<int>& favorites = lib.favorites;
    std::vector<romm::Game>& games = lib.games;
    std::vector<screens::Tile>& platformTiles = lib.platformTiles;
    std::vector<screens::Tile>& collectionTiles = lib.collectionTiles;

    // WHAT THE SCREEN SAYS WHILE THE CONSOLE IS BUSY.
    //
    // Everything below blocks: reaching the server, adopting the user, and
    // pulling a library that is sixteen hundred games on the reference machine.
    // Until 2026-09-20 the screen showed nothing at all for those seconds — and
    // for up to ninety of them when the server is not up yet — because the
    // frame loop does not exist until after all of it.
    //
    // MMagTech noticed it as the pause after "Start playing" in first run, but
    // it is not a first-run fault: it has happened on every boot this console
    // has ever done. Nobody watches a console boot with a stopwatch.
    // ---- Idle: pixel shift, dim, blank — idle.h, open question 10b -------
    //
    // UP HERE, BEFORE THE SERVER IS ASKED FOR, because the startup screen can
    // now wait for it without end (below), and a bright logo left all night
    // is the burn-in this exists to stop. One watch for both: a screen that
    // went dark while waiting stays dark on Home, and the same press wakes it.
    idle::Watch idleWatch;
    idleWatch.setTimeScale(idleScale);
    idleWatch.setEnabled(!idleOff);
    idle::Level idleShown = idle::Level::Awake;
    Animated dimLayer;
    dimLayer.smooth = true;
    idle::Offset shiftShown;
    auto clockSeconds = [] { return SDL_GetTicksNS() / 1e9; };
    // Once a frame. The timers only ever deepen here; idleWatch.input() is
    // what lifts them.
    auto idleFrame = [&](float dt, bool playingNow, double blankAfter) {
        const double t = clockSeconds();
        idleWatch.setBlankAfter(blankAfter);
        const idle::Level lvl = idleWatch.update(t, playingNow);
        if (lvl != idleShown) {
            std::fprintf(stderr, "[idle] %s -> %s after %.0fs without input\n",
                         idle::name(idleShown), idle::name(lvl), idleWatch.idleFor(t));
            if (lvl == idle::Level::Blank) idle::setDisplayAsleep(true);
            else if (idleShown == idle::Level::Blank) idle::setDisplayAsleep(false);
            const float depth = lvl == idle::Level::Awake ? 0.0f
                                : lvl == idle::Level::Dim ? idle::kDimDepth
                                                          : 1.0f;
            dimLayer.retarget(depth, lvl == idle::Level::Awake ? idle::kWakeFadeSeconds
                                                               : idle::kDimFadeSeconds);
            idleShown = lvl;
        }
        dimLayer.tick(dt);
        const idle::Offset o = idle::pixelShift(t, renderer.scale(), shiftEvery);
        if (o.dx != shiftShown.dx || o.dy != shiftShown.dy) {
            std::fprintf(stderr, "[idle] pixel shift %+d,%+d px\n", o.dx, o.dy);
            renderer.setPixelShift(o.dx, o.dy);
            shiftShown = o;
        }
    };

    setup::Deps waitDeps;
    waitDeps.window = window;
    waitDeps.renderer = &renderer;
    waitDeps.text = &text;

    // A capture of the startup screen, which otherwise exists only for the few
    // seconds between the window appearing and the library arriving — and on a
    // fast server that is too short to photograph by hand. It draws the real
    // thing through the real path rather than reconstructing it.
    if (startupShot) {
        int dw = 0, dh = 0;
        SDL_GetWindowSizeInPixels(window, &dw, &dh);
        // A representative line rather than the bare one: what this screen
        // actually shows during a boot is a count that climbs, and a capture
        // of it saying nothing would be a picture of a state that lasts a
        // fraction of a second.
        setup::showWaiting(waitDeps, "Starting up", "Loading your library. 640 games");
        renderer.saveFrame(shotPath ? shotPath : "/tmp/cabinetos-startup.bmp", dw, dh);
        renderer.shutdown();
        SDL_Quit();
        return 0;
    }

    if (rommAddress) {
        std::string err;
        // Where the cores are, so the catalog can tell "the manifest has a core
        // for this" apart from "this console has it built".
        catalog::setCoreDirectory(coreDir);
        setup::showWaiting(waitDeps, "Starting up",
                           std::string("Looking for your server at ")
                               .append(rommAddress)
                               .append("…")
                               .c_str());
        // WAIT FOR THE NETWORK RATHER THAN GIVING UP ON IT.
        //
        // This used to try once and exit 1, and on a console that is a fault
        // rather than tidiness: the session starts within a couple of seconds
        // of boot and the network is routinely not up yet, so a cold boot
        // reached RomM before the machine had an address and the frontend
        // quit. gamescope exits when its child exits, and the compositor
        // ladder then read that as ITS OWN failure and demoted the machine to
        // software rendering for the rest of the session — see
        // docs/PROJECT.md, open question 22. The ladder no longer draws that
        // conclusion; this is the other half, which is not having the race.
        //
        // IT WAS A BOUNDED WAIT until 2026-09-25 (below), and it is still not
        // the offline console. Ninety seconds covered a boot race and a router
        // coming back after a power cut. It is deliberately NOT the answer to
        // "there is no server"
        // — a console that keeps its library, plays its kept games and fills
        // in when the server returns is open question 22's design and a
        // different piece of work. This is the difference between a machine
        // that recovers from a power cut on its own and one that does not.
        //
        // AND IT OFFERS CHANGE SERVER ADDRESS (issue #60), because this is the
        // one screen a console whose server moved ever shows: it never reaches
        // Settings. After ten seconds, when a boot race is over, a second line
        // says "Press (A) to change the server address"; the same keyboard and
        // the same check as Settings follow, PIN first if one is set. The same
        // server at the new address: the start carries on there. A different
        // server: Sign out is offered here, since Settings is out of reach.
        // MMagTech, 2026-09-25. So the server is tried on a worker and this
        // loop keeps drawing and listening.
        //
        // NO LONGER BOUNDED, 2026-09-25. It gave up at ninety seconds and the
        // session started it again, which bought nothing once this screen
        // retried by itself and cost a black flash every minute and a half
        // while a server was off. MMagTech: wait for as long as it takes. The
        // (A) line is the cue to go and look at the server.
        {
            constexpr double kOfferAfter = 10.0;
            struct Probe {
                std::atomic<bool> stop{false}, answered{false};
                std::mutex m;
                std::string err;
                std::thread th;
            } probe;
            const std::string firstAddress = rommAddress;
            probe.th = std::thread([&probe, firstAddress]() {
                std::string e;
                while (!probe.stop.load()) {
                    if (liveClient.setAddress(firstAddress, &e)) { probe.answered = true; return; }
                    {
                        std::lock_guard<std::mutex> lk(probe.m);
                        probe.err = e;
                    }
                    for (int i = 0; i < 20 && !probe.stop.load(); ++i) SDL_Delay(100);
                }
            });
            struct CheckJob {
                std::thread th;
                std::atomic<bool> done{false};
                server::Check result = server::Check::Failed;
                std::string address;
                Uint64 at = 0;
                ~CheckJob() { if (th.joinable()) th.join(); }
            } check;

            // The pads, which main opens only later: this screen needs them.
            if (SDL_JoystickID* ids = SDL_GetGamepads(nullptr)) {
                for (int i = 0; ids[i]; ++i) SDL_OpenGamepad(ids[i]);
                SDL_free(ids);
            }

            ui::Keyboard addrKeyboard;
            screens::ChoiceScreen question;
            screens::PinScreen pad;
            int pinFails = 0;
            std::string newAddress;
            bool signOutNow = false, quit = false, said = false, swallowText = false;
            const double blankAfter = kScreenOff[savedScreenOff()].seconds;
            const Uint64 start = SDL_GetTicks();
            Uint64 prevNs = SDL_GetTicksNS();

            auto openKeyboard = [&](const std::string& typed, const std::string& why) {
                addrKeyboard.open(serverKeyboardConfig(typed));
                if (!why.empty()) addrKeyboard.sayInField(why, /*problem=*/true, /*keep=*/true);
            };
            auto keyboardResult = [&](ui::KeyboardResult r) {
                if (r != ui::KeyboardResult::Committed) return;
                const std::string addr = trimmedAddress(addrKeyboard.value());
                if (addr.empty()) return;
                addrKeyboard.open(serverKeyboardConfig(addr));
                addrKeyboard.sayInField("Checking\xE2\x80\xA6", /*problem=*/false, /*keep=*/true);
                addrKeyboard.setBusy(true);
                if (check.th.joinable()) check.th.join();
                check.done = false;
                check.address = addr;
                check.at = SDL_GetTicks();
                check.th = std::thread([&check, addr]() {
                    std::string detail;
                    check.result = server::check(addr, &detail);
                    std::fprintf(stderr, "[server] %s at startup: %d%s%s\n", addr.c_str(),
                                 static_cast<int>(check.result), detail.empty() ? "" : ", ",
                                 detail.c_str());
                    check.done = true;
                });
            };
            // What the pad's answer means, from a press or from a typed digit.
            auto pinOutcome = [&](screens::PinScreen::Outcome o) {
                if (o == screens::PinScreen::Outcome::Cancelled) pad.close();
                if (o != screens::PinScreen::Outcome::Entered) return;
                if (!accounts::checkPin(pad.pin())) {
                    pad.reject("That is not the PIN");
                    if (++pinFails >= 5) { pinFails = 0; pad.lockFor(30.0f); }
                    return;
                }
                pad.close();
                openKeyboard(rommAddress, "");
            };
            auto press = [&](screens::Nav n) {
                if (pad.isOpen()) {
                    pinOutcome(pad.key(n));
                    return;
                }
                if (question.isOpen()) {
                    const auto o = question.key(n);
                    if (o == screens::ChoiceScreen::Outcome::None) return;
                    question.close();
                    if (o == screens::ChoiceScreen::Outcome::Chosen && question.chosen() == 0)
                        signOutNow = true;
                    return;
                }
                if (addrKeyboard.isOpen()) {
                    switch (n) {
                        case screens::Nav::Left: addrKeyboard.moveFocus(-1, 0); break;
                        case screens::Nav::Right: addrKeyboard.moveFocus(+1, 0); break;
                        case screens::Nav::Up: addrKeyboard.moveFocus(0, -1); break;
                        case screens::Nav::Down: addrKeyboard.moveFocus(0, +1); break;
                        case screens::Nav::Activate: keyboardResult(addrKeyboard.pressKey()); break;
                        case screens::Nav::Back: addrKeyboard.cancel(); break;
                    }
                    return;
                }
                if (n != screens::Nav::Activate) return;
                if ((SDL_GetTicks() - start) / 1000.0 < kOfferAfter) return;
                if (accounts::pinIsSet())
                    pad.open(screens::PinScreen::Mode::Check, "Enter the PIN",
                             "To change the RomM server");
                else
                    openKeyboard(rommAddress, "");
            };

            while (!probe.answered.load() && newAddress.empty() && !signOutNow && !quit) {
                const Uint64 now = SDL_GetTicks();
                // Once, not once per attempt: a line a second for a minute and
                // a half buries whatever else the boot had to say.
                if (!said && now - start > 2500) {
                    std::lock_guard<std::mutex> lk(probe.m);
                    if (!probe.err.empty()) {
                        said = true;
                        std::fprintf(stderr, "[romm] %s, waiting for it\n", probe.err.c_str());
                    }
                }

                SDL_Event e;
                while (SDL_PollEvent(&e)) {
                    // A PRESS ON A DARK SCREEN ONLY WAKES IT, as on Home; the
                    // text a swallowed key would have typed goes with it.
                    const bool touched =
                        e.type == SDL_EVENT_KEY_DOWN || e.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ||
                        (e.type == SDL_EVENT_GAMEPAD_AXIS_MOTION &&
                         std::abs(static_cast<int>(e.gaxis.value)) > idle::kAxisDeadzone);
                    if (touched) {
                        swallowText = false;
                        if (idleWatch.input(clockSeconds())) {
                            swallowText = e.type == SDL_EVENT_KEY_DOWN;
                            continue;
                        }
                    }
                    if (e.type == SDL_EVENT_TEXT_INPUT && swallowText) {
                        swallowText = false;
                        continue;
                    }
                    switch (e.type) {
                        case SDL_EVENT_QUIT: quit = true; break;
                        case SDL_EVENT_GAMEPAD_ADDED: SDL_OpenGamepad(e.gdevice.which); break;
                        case SDL_EVENT_TEXT_INPUT:
                            if (pad.isOpen()) {
                                for (const char* c = e.text.text; *c && pad.isOpen(); ++c)
                                    pinOutcome(pad.typeDigit(*c));
                            } else if (addrKeyboard.isOpen()) {
                                addrKeyboard.typeText(e.text.text);
                            }
                            break;
                        case SDL_EVENT_KEY_DOWN:
                            switch (e.key.key) {
                                case SDLK_UP: press(screens::Nav::Up); break;
                                case SDLK_DOWN: press(screens::Nav::Down); break;
                                case SDLK_LEFT: press(screens::Nav::Left); break;
                                case SDLK_RIGHT: press(screens::Nav::Right); break;
                                case SDLK_ESCAPE: press(screens::Nav::Back); break;
                                case SDLK_BACKSPACE:
                                    if (pad.isOpen()) pad.deleteDigit();
                                    else if (addrKeyboard.isOpen()) addrKeyboard.backspace();
                                    break;
                                case SDLK_RETURN:
                                case SDLK_KP_ENTER:
                                    if (addrKeyboard.isOpen() && !pad.isOpen() && !question.isOpen())
                                        keyboardResult(addrKeyboard.commit());
                                    else
                                        press(screens::Nav::Activate);
                                    break;
                                default: break;
                            }
                            break;
                        case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                            switch (e.gbutton.button) {
                                case SDL_GAMEPAD_BUTTON_DPAD_UP: press(screens::Nav::Up); break;
                                case SDL_GAMEPAD_BUTTON_DPAD_DOWN: press(screens::Nav::Down); break;
                                case SDL_GAMEPAD_BUTTON_DPAD_LEFT: press(screens::Nav::Left); break;
                                case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: press(screens::Nav::Right); break;
                                case SDL_GAMEPAD_BUTTON_SOUTH: press(screens::Nav::Activate); break;
                                case SDL_GAMEPAD_BUTTON_EAST: press(screens::Nav::Back); break;
                                case SDL_GAMEPAD_BUTTON_WEST:
                                    if (pad.isOpen()) pad.deleteDigit();
                                    else if (addrKeyboard.isOpen()) addrKeyboard.backspace();
                                    break;
                                case SDL_GAMEPAD_BUTTON_NORTH:
                                    if (addrKeyboard.isOpen()) addrKeyboard.toggleShift();
                                    break;
                                case SDL_GAMEPAD_BUTTON_START:
                                    if (addrKeyboard.isOpen() && !pad.isOpen() && !question.isOpen())
                                        keyboardResult(addrKeyboard.commit());
                                    break;
                                default: break;
                            }
                            break;
                        default: break;
                    }
                }

                // An address check finished ("Checking…" up 700 ms at least,
                // as in Settings). B while it ran means leave it alone.
                if (check.done.load() && SDL_GetTicks() - check.at >= 700) {
                    check.done = false;
                    if (addrKeyboard.isOpen() && addrKeyboard.busy()) {
                        const std::string addr = check.address;
                        switch (check.result) {
                            case server::Check::Same: newAddress = addr; break;
                            case server::Check::Different: {
                                addrKeyboard.cancel();
                                const bool unsent = server::unsentSaves() > 0;
                                question.open("A different server",
                                              std::string("Removes every game and account from "
                                                          "this console.\n") +
                                                  (unsent ? "Saves waiting to upload will be lost."
                                                          : "Saves stay on the server."),
                                              {"Sign out", "Cancel"}, 1);
                                break;
                            }
                            case server::Check::NoServer:
                                openKeyboard(addr, "No RomM server there");
                                break;
                            case server::Check::Failed:
                                openKeyboard(addr, "Couldn't check it");
                                break;
                        }
                    }
                }

                const Uint64 ns = SDL_GetTicksNS();
                const float dt = static_cast<float>(ns - prevNs) / 1e9f;
                prevNs = ns;
                question.tick(dt);
                pad.tick(dt);
                idleFrame(dt, /*playingNow=*/false, blankAfter);

                int dw = 0, dh = 0;
                SDL_GetWindowSizeInPixels(window, &dw, &dh);
                if (dw > 0 && dh > 0) {
                    renderer.beginFrame(dw, dh);
                    const double waited = (SDL_GetTicks() - start) / 1000.0;
                    // A NUMBER THAT CHANGES. One unchanging sentence is what a
                    // hung console looks like.
                    const int secs = static_cast<int>(waited);
                    char line[96];
                    if (secs < 60)
                        std::snprintf(line, sizeof line, "Waiting for your server, %ds", secs);
                    else
                        std::snprintf(line, sizeof line, "Waiting for your server, %dm %ds",
                                      secs / 60, secs % 60);
                    setup::drawStartup(renderer, text,
                                       newAddress.empty() ? line : "Connecting", 1.0f,
                                       waited >= kOfferAfter
                                           ? "Press (A) to change the server address"
                                           : nullptr);
                    renderer.presentScene();
                    screens::Ctx ctx{renderer, text, images, renderer.scale()};
                    if (addrKeyboard.isOpen()) addrKeyboard.draw(renderer, text, renderer.scale());
                    question.draw(ctx);
                    pad.draw(ctx);
                    if (const float d = dimLayer.value(); d > 0.001f)
                        renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                               ui::Color::black(d)});
                    SDL_GL_SwapWindow(window);
                }
                // Dark: ten pictures a second is plenty, as on Home.
                if (idleShown == idle::Level::Blank && dimLayer.value() >= 0.999f) SDL_Delay(100);
            }

            probe.stop = true;
            if (!probe.answered.load() && (!newAddress.empty() || signOutNow)) {
                // Said while the old address's last try runs out.
                setup::showWaiting(waitDeps, "",
                                   signOutNow ? "Signing out"
                                              : ("Connecting to " + newAddress).c_str());
            }
            probe.th.join();
            if (quit) return 0;
            if (signOutNow) {
                std::string serr;
                if (!server::signOut(&serr)) {
                    std::fprintf(stderr, "[sign out] could not: %s\n", serr.c_str());
                    return 1;
                }
                // The clearing and first run are the next start's, as from
                // Settings. In place, so gamescope stays up.
                std::fprintf(stderr, "[frontend] starting again\n");
                std::fflush(stderr);
                ::close_range(3, ~0U, 0);
                ::execv("/proc/self/exe", argv);
                std::fprintf(stderr, "[frontend] could not start again: %s\n",
                             std::strerror(errno));
                return 1;
            }
            if (!probe.answered.load() && !newAddress.empty()) {
                std::string cerr;
                const std::string from = rommAddress;
                if (!server::changeAddress(from, newAddress, &cerr))
                    std::fprintf(stderr, "[server] could not save %s: %s\n", newAddress.c_str(),
                                 cerr.c_str());
                resolvedAddress = newAddress;
                rommAddress = resolvedAddress.c_str();
                if (!liveClient.setAddress(rommAddress, &err)) {
                    std::fprintf(stderr, "[romm] %s at the new address\n", err.c_str());
                    return 1;
                }
            }
            if (said) std::fprintf(stderr, "[romm] the server answered\n");
        }
        // WHOEVER PLAYED LAST. The active account's token is what this console
        // starts as — open question 26, decision 2. No account means a machine
        // that has not been paired, which is first run's job and not something
        // to paper over here.
        if (!accounts::loadActiveToken(liveClient)) {
            std::fprintf(stderr,
                         "[romm] no account on this console — pair first with "
                         "--romm-probe --romm-pair\n");
            return 1;
        }
        // THE LIST IS HELD IN A NAMED LOCAL AND IT HAS TO BE. `accounts::find`
        // returns a pointer INTO the vector it is given — which is why it takes
        // one rather than hiding a static — so passing `accounts::all()` inline
        // leaves the pointer dangling at the end of the condition. It segfaulted
        // on the first run, in the first place the function was ever called.
        const std::vector<accounts::Account> known = accounts::all();
        if (const accounts::Account* who = accounts::find(known, accounts::activeId()))
            std::fprintf(stderr, "[accounts] acting as %d - %s\n", who->id,
                         who->name.c_str());
        adoptUser(liveClient);
        // FOUR SMALL CALLS NOW, not the catalogue — open question 28. This
        // used to count games onto the screen as they arrived, because it took
        // several seconds and a still sentence is indistinguishable from a
        // hang. It no longer takes several seconds, so there is no longer a
        // number worth putting on a screen nobody has time to read.
        //
        // THE COUNTING LINE IS GONE AND THAT IS THE POINT, not a loss. It
        // existed to make a slow thing bearable; the slow thing was removed.
        // The server-wait countdown above survives untouched, because a router
        // coming back after a power cut is not something this can make faster.
        setup::showWaiting(waitDeps, "Starting up", "Loading your library");
        lib = loadLibrary(liveClient);
        // The references above name lib's own members, so there is nothing to
        // copy out any more.

        // EMPTY CARDS IS NO LONGER AN EMPTY LIBRARY, and this check had to
        // change with the rest. `cards` now holds recents and favourites
        // rather than the catalogue, so a console whose server is fine but
        // which has never played anything and has no favourites would have
        // been told its library came back empty and refused to start. The
        // question worth asking is whether the SERVER answered, and the
        // platform list is what answers it.
        if (platformTiles.empty()) {
            std::fprintf(stderr, "[romm] the library came back empty\n");
            return 1;
        }
        // Covers come from the server, authenticated. The cache never learns
        // what a server is — it was built to take exactly this.
        //
        // AND IT ASKS THE DISK FIRST. Nothing about a library survives a boot,
        // so every start used to re-fetch every cover it drew. The path RomM
        // hands out carries the art's own timestamp, so a cached file is only
        // ever returned for the exact version that was asked for — see
        // covercache.h. Art this console has already seen costs no network at
        // all on the next boot, which matters most for the hosted server of
        // open question 29.
        covercache::setServer(rommAddress);
        images.init(imageBudget, 4, [](const std::string& key) {
            if (std::vector<uint8_t> have = covercache::read(key); !have.empty())
                return have;
            std::vector<uint8_t> got = liveClient.fetchBytes(key);
            covercache::write(key, got);
            return got;
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
            // The sample library is files on disk at one size, so the large
            // cover IS the cover. Keeping the field filled rather than empty
            // means the drawing code never has to ask which library it is in.
            c.coverLarge = c.cover;
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
        std::map<std::string, std::string> overrides = catalog::optionOverrides(corePath);
        for (const auto& [key, value] : cliOptionOverrides) {
            std::fprintf(stderr, "[core] option from the command line: %s = %s\n",
                         key.c_str(), value.c_str());
            overrides[key] = value;
        }
        core.setOptionOverrides(overrides);
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
    // THE HERO ROW IS GONE — 2026-09-21.
    //
    // Home was bar / hero / Recent / Favorites, where the hero was the most
    // recently played game lifted out of Recent and given an 1800x340 card of
    // its own. The card was mostly not artwork: a 3:4 cover fitted in the
    // middle of a 16:5 box, with the same cover blurred either side to fill the
    // space it could not. MMagTech, looking at it: *"the homepage needs some
    // work"*, and that band was the thing.
    //
    // It is now three rows, and the most recent game is simply the first card
    // on Recent — which is what it was before it was taken out. Resume-first is
    // unchanged and is now cheaper: focus opens on that card, and A launches it
    // rather than opening its launch screen. See `activateHome`.
    //
    // WHAT THE 284 POINTS BOUGHT: the shelf covers are back at the reference
    // implementation's own 260x347. They were cut to 158x210 to fit two shelves
    // under the hero — design.h records that as "about 430 points had to come
    // out of a 1080 canvas". Most of it has come back.
    // THE BAR IS NOT ONE OF HOME'S ROWS — changed 2026-09-21.
    //
    // It was, back when it was drawn on Home and nowhere else. Now it is drawn
    // over every browsing screen, and MMagTech asked the obvious question:
    // *"if the library has the top bar in view shouldnt i be able to up and
    // access it."* Yes. Chrome that is on screen and cannot be reached is worse
    // than chrome that is hidden, because it reads as the console having
    // stopped responding rather than as a thing that is not there.
    //
    // So the bar's focus belongs to the APP — `barFocused` and `barSlot` — and
    // every screen hands focus up to it the same way, through
    // screens::Action::FocusBar. Two mechanisms for one piece of chrome is how
    // a shelf and a grid end up disagreeing about what focus looks like, which
    // is the same reason design.h exists.
    enum Row { RowRecent = 0, RowFavorites = 1 };
    // HOW FAR EACH HOME ROW HAS SCROLLED, in points — 2026-09-23. There was
    // none: every card was drawn at its fixed slot, so moving right past the
    // edge of the screen put focus on a card nobody could see, and A launched
    // it. MMagTech, on the A9: *"this should scroll until the last one."* One
    // per row, and each row keeps its own when focus leaves it.
    Animated shelfScroll[2];
    // THE CHIP IS A BAR SLOT NOW, and it is the only one that is not a
    // capsule: it is drawn as the avatar disc at the far right, so the label
    // loop below stops at BarSettings and the chip takes its own focus rim.
    // Everything else about it — L1/R1 walking onto it, Down leaving the bar —
    // it gets for free by being in this list.
    enum BarItem { BarLibrary = 0, BarSearch, BarSettings, BarAccount, BarCount };
    const char* kBarLabels[BarCount] = { "Library", "Search", "Settings", "Account" };
    // How many of those draw as labelled capsules. The chip draws itself.
    constexpr int kBarCapsules = BarAccount;

    // ASKED, NOT REMEMBERED. These were constants worked out once at startup,
    // and switching accounts replaces the library underneath them: a console
    // that started as someone with an empty Home and switched to someone with
    // sixteen cards could focus none of them (the d-pad clicked, nothing
    // moved), and the other way round it reached for a card that no longer
    // existed and crashed. Found on the A9, 2026-09-24.
    auto shelfSlots = [&]() -> size_t { return shelf.empty() ? cards.size() : shelf.size(); };
    // Whether there is a game to resume. It is shelf slot 0 when there is.
    auto haveResume = [&]() { return heroIndex >= 0 && !shelf.empty(); };
    auto haveFavorites = [&]() { return !favorites.empty(); };
    // A HOME WITH NOTHING ON IT: a new account that has played nothing and
    // starred nothing. MMagTech, switching to one on the A9, 2026-09-24: the
    // screen was "just purple", focus was nowhere, and finding the Library
    // took pressing Up by accident. See the frame loop and drawShelf's caller.
    auto homeEmpty = [&]() { return shelfSlots() == 0 && !haveFavorites(); };

    auto rowSlots = [&](int row) -> size_t {
        if (row == RowRecent) return shelfSlots();
        return favorites.size();
    };
    auto rowExists = [&](int row) { return rowSlots(row) > 0; };

    // The card a (row, slot) points at, or nullptr for the top bar, whose
    // items are destinations rather than cards.
    auto cardAt = [&](int row, int slot) -> Card* {
        if (row == RowRecent) {
            if (slot < 0 || static_cast<size_t>(slot) >= shelfSlots()) return nullptr;
            return &cards[shelf.empty() ? static_cast<size_t>(slot)
                                        : static_cast<size_t>(shelf[slot])];
        }
        if (slot < 0 || static_cast<size_t>(slot) >= favorites.size()) return nullptr;
        return &cards[favorites[slot]];
    };

    // RESUME-FIRST, and it is now one rule rather than a separate object:
    // Home opens on the first card of Recent, which is the most recently played
    // game this console can play.
    int focusRow = RowRecent;
    int focusSlot = 0;
    // Remembered focus per row, which is the behaviour tvOS gives free and the
    // one people notice missing: leaving Recent at the sixth cover and coming
    // back to the first is the kind of thing that feels broken without anyone
    // being able to say why.
    int rememberedSlot[2] = {0, 0};
    // --focus N still means "start on card N of Recent", which is what every
    // existing capture script passes it for.
    if (initialFocus >= 0) {
        focusRow = RowRecent;
        focusSlot = std::clamp(initialFocus, 0, static_cast<int>(shelfSlots()) - 1);
    }
    if (initialRow >= 0) {
        focusRow = std::clamp(initialRow, 0, static_cast<int>(RowFavorites));
        if (!rowExists(focusRow)) focusRow = RowRecent;
        focusSlot = std::clamp(focusSlot, 0, static_cast<int>(rowSlots(focusRow)) - 1);
    }
    // Home is taller than the screen once there are two shelves, so it scrolls
    // to follow focus — the same as tvOS, which puts Home in a ScrollView. The
    // hero alone is 420 of a 1080 canvas; Recent and Favorites do not both fit
    // under it.
    Animated scrollY;
    scrollY.from = scrollY.to = 0.0f;
    // What is lighting the room, and what was lighting it before. Two cache
    // keys and a mix, because a cut between two covers is the one thing this
    // must not look like. `backdropWant` is what focus is asking for, which is
    // not the same as what is on screen until it has stopped asking for long
    // enough — see kHomeBackdropDelay.
    std::string backdropKey, backdropPrevKey, backdropWant;
    // The last game art a BROWSING screen lit the room with. The plain
    // screens (Settings, adding an account) clear the room, and Search with
    // nothing found yet goes back to this rather than staying plain: MMagTech,
    // 2026-09-24, one screen switching between plain and blurred "doesn't feel
    // right".
    std::string lastLitArt;
    float backdropSettle = 0.0f;
    Animated backdropMix;
    backdropMix.from = backdropMix.to = 1.0f;
    // EASE-IN-OUT, the design system's curve for a state change worth watching.
    // Every other animation on these screens is ease-out because it is focus
    // arriving; this one is the room itself changing and it has to leave as
    // deliberately as it arrives.
    backdropMix.smooth = true;
    // Settled, not animating: a screenshot should show the resting focused
    // state, not a frame part-way through the transition into it.
    auto settleFocus = [&]() {
        if (Card* c = cardAt(focusRow, focusSlot)) {
            c->focus.retarget(1.0f, kFocusDuration);
            c->focus.elapsed = kFocusDuration;
        } else {
            // The top bar's items are not Cards and have no animation of their
            // own: focus there is a tinted pill drawn from focusRow/focusSlot.
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
    enum class Screen { Home, Library, Grid, Detail, Search, AddAccount, Settings };
    std::vector<Screen> stack{Screen::Home};
    // Which screen last set the room, so a change of screen can be told apart
    // from focus moving within one. See the backdrop block in the frame loop.
    Screen backdropScreen = Screen::Home;
    bool backdropForScreen = false;
    auto here = [&]() { return stack.back(); };

    screens::LibraryScreen libraryScreen;
    screens::GridScreen gridScreen;
    screens::DetailScreen detailScreen;
    screens::SearchScreen searchScreen;
    screens::AccountScreen accountScreen;
    screens::AddAccountScreen addAccountScreen;
    screens::SettingsScreen settingsScreen;
    // What the docked keyboard held last frame, so the query is re-run when it
    // changes and not sixty times a second when it does not.
    std::string searchTyped;
    // Typed but not yet asked, and how long until it is. SEARCH GOES TO THE
    // SERVER NOW — open question 28 — so the one thing the old in-memory
    // filter never needed is the one thing this cannot do without: somebody
    // typing "castlevania" on a pad must not fire eleven requests.
    std::string searchPending;
    float searchDebounce = 0.0f;
    // Long enough to swallow a run of keypresses, short enough that stopping
    // to look feels like the screen answering rather than catching up. The
    // request itself is 40-80 ms against the reference server.
    constexpr float kSearchDebounce = 0.25f;
    // One page. A television search is somebody looking for a game they can
    // name, not a person paging through four hundred results with a stick —
    // and the heading says how many matched, so a query that is too broad
    // says so rather than pretending the first forty are all of it.
    constexpr int kSearchLimit = 40;

    // Asks the server and hands the screen the answer. Shared by the live
    // typing loop and by `--screen search --query`, so a capture exercises the
    // same path a person does rather than a second implementation of it.
    auto runSearch = [&](const std::string& q) {
        if (q.empty()) return;
        std::vector<romm::Game> found;
        std::string err;
        int total = 0;
        if (!liveClient.fetchRoms("search_term=" + romm::Client::encodeQueryValue(q),
                                  kSearchLimit, &found, &err, &total)) {
            // NOT "nothing matches". The console could not go and look, and
            // saying the library holds nothing like it would be a lie told
            // confidently. See SearchScreen::State.
            std::fprintf(stderr, "[search] %s: %s\n", q.c_str(), err.c_str());
            searchScreen.setFailed(err);
            return;
        }
        std::vector<int> idx;
        idx.reserve(found.size());
        for (const auto& g : found) {
            // Same rule as Home's shelf and a collection's grid: a result this
            // console cannot start must not be offered. The heading's "12 of
            // 52" is what accounts for the difference.
            if (!catalog::playable(g)) continue;
            idx.push_back(appendGame(lib, g));
        }
        std::fprintf(stderr, "[search] %s: %zu of %d\n", q.c_str(), idx.size(), total);
        searchScreen.setResults(q, std::move(idx), total);
    };
    libraryScreen.build(platformTiles, collectionTiles);

    // THE TILE COVERS, FETCHED BEHIND HOME RATHER THAN BEFORE IT.
    //
    // A platform tile's artwork is the first cover among its games, and until
    // open question 28 it came for free because boot had fetched every game
    // anyway. It is one request per platform on its own: 1.55 s against the
    // reference server, measured, which is more than the whole of the rest of
    // a boot. Paying it at startup would have handed most of the win back for
    // thumbnails.
    //
    // MMagTech's call, 2026-09-22, given the choice between paying it, using
    // RomM's platform logo instead, and this: the tiles come up in their
    // colour — which the Library screen already treats as a normal state, not
    // a fault — and the covers arrive a second or so later. The finished
    // screen looks exactly as it did before.
    //
    // IT HAPPENS ON EVERY BOOT, because nothing about a library is kept
    // between them. Within a session it happens once.
    //
    // THE WORKER TOUCHES NOTHING THE FRAME THREAD OWNS. It returns cover
    // PATHS — two ints and a string — and the frame loop is what applies them.
    // Appending to the store from another thread would be a data race against
    // every screen that draws from it.
    struct CoverFill {
        std::mutex m;
        std::vector<std::pair<int, std::string>> done;   // platform id -> cover
        std::vector<int> want;
        std::atomic<size_t> next{0};
        std::atomic<bool> quit{false};
        std::vector<std::thread> workers;
    };
    CoverFill coverFill;
    // STARTED AGAIN BY AN ACCOUNT SWITCH, which builds new tiles with no
    // covers. It used to run at boot only, so after a switch the Library's
    // platforms stayed bare colour until each was opened. MMagTech, on the A9,
    // 2026-09-24. Any fill still running for the last account is stopped and
    // its results dropped first, so no cover lands on the wrong person's tile.
    auto startCoverFill = [&]() {
        coverFill.quit.store(true);
        for (std::thread& w : coverFill.workers)
            if (w.joinable()) w.join();
        coverFill.workers.clear();
        coverFill.quit.store(false);
        coverFill.next.store(0);
        coverFill.want.clear();
        {
            std::lock_guard<std::mutex> lk(coverFill.m);
            coverFill.done.clear();
        }
        // THE TILE MAP FIRST, WHICH IS THE WHOLE POINT. Without it the console
        // asks thirty-six times at every start which cover a tile should use
        // and gets the same thirty-six answers — 1.07 s and a third of a
        // megabyte to learn nothing new. A row is used when the platform's
        // `updated_at` AND `rom_count` still match what they were when it was
        // written; both ride on the platform list boot already fetched, so
        // checking costs no request. See covercache.h for why this is not the
        // library snapshot open question 22 rules out.
        const std::map<int, covercache::Tile> saved = covercache::loadTiles();
        int reused = 0;
        for (screens::Tile& t : platformTiles) {
            if (!t.enterable || !t.cover.empty()) continue;
            auto mine = lib.tileCache.find(t.id);
            auto was = saved.find(t.id);
            if (mine != lib.tileCache.end() && was != saved.end() &&
                !was->second.cover.empty() &&
                was->second.updatedAt == mine->second.updatedAt &&
                was->second.romCount == mine->second.romCount) {
                t.cover = was->second.cover;
                mine->second.cover = was->second.cover;
                // AND THE SCREEN HAS TO BE TOLD, because it was built from
                // these tiles before this ran and holds its own copies. The
                // cold path gets this for free — every cover that arrives goes
                // through learnedTile — so a remembered one that skipped it
                // drew the colour and never the picture. Found by a warm boot
                // requesting exactly one image, the avatar.
                libraryScreen.learnedTile(t.id, "", was->second.cover);
                ++reused;
                continue;
            }
            coverFill.want.push_back(t.id);
        }
        if (reused || !coverFill.want.empty())
            std::fprintf(stderr, "[covers] %d tile(s) remembered, %zu to ask about\n",
                         reused, coverFill.want.size());
        // FOUR, THE SAME AS THE IMAGE CACHE, and for the same reason: these
        // are round trips rather than work, so the number that helps is the
        // number in flight. Thirty-six platforms one at a time is 1.55 s;
        // four at a time is about a quarter of that, which is the difference
        // between tiles that fill in and tiles you watch filling in.
        const unsigned kWorkers = 4;
        for (unsigned w = 0; w < kWorkers; ++w) {
            coverFill.workers.emplace_back([&coverFill]() {
                for (;;) {
                    if (coverFill.quit.load()) return;
                    const size_t i = coverFill.next.fetch_add(1);
                    if (i >= coverFill.want.size()) return;
                    std::vector<romm::Game> one;
                    std::string err;
                    if (!liveClient.fetchRoms(
                            "platform_ids=" + std::to_string(coverFill.want[i]), 1,
                            &one, &err) || one.empty())
                        continue;
                    if (one[0].coverPath.empty()) continue;
                    std::lock_guard<std::mutex> lk(coverFill.m);
                    coverFill.done.emplace_back(coverFill.want[i], one[0].coverPath);
                }
            });
        }
    };
    if (rommAddress) startCoverFill();
    // THE REST OF A BIG GRID, BEHIND THE FIRST PAGE.
    //
    // A grid is proportional to its own platform — about 1 ms a game — so the
    // first page is drawn at once and the remainder arrives behind it. The
    // worker returns GAMES and the frame loop is what puts them in the store
    // and on the screen, for the same reason the cover fill returns paths:
    // appending to the store from another thread would race every screen that
    // draws from it.
    struct GridFill {
        std::mutex m;
        std::vector<romm::Game> arrived;
        std::atomic<bool> running{false};
        std::atomic<bool> quit{false};
        int tileId = 0;
        std::thread th;
        void stop() {
            quit.store(true);
            if (th.joinable()) th.join();
            quit.store(false);
            std::lock_guard<std::mutex> lk(m);
            arrived.clear();
        }
    };
    GridFill gridFill;
    struct GridFillStop {
        GridFill& f;
        ~GridFillStop() { f.stop(); }
    } gridFillStop{gridFill};

    // THE SWEEP AND THE EVICTION, BEHIND EVERYTHING, ONCE PER BOOT.
    //
    // MMagTech's question, and the reason there is no TTL anywhere in this
    // cache: what stops a deleted platform's art sitting on disk forever. A
    // platform that is gone stops appearing in the platform list, so it is
    // orphaned the FIRST time the console sees the server without it.
    //
    // IT IS GIVEN THE LIST THAT CAME BACK, and covercache::sweep refuses an
    // empty one, because absence is not deletion when nothing answered — a
    // sweep on a failed boot would wipe the cache that boot most wants.
    std::thread coverTidy;
    if (rommAddress && !platformTiles.empty()) {
        std::vector<int> live;
        for (const screens::Tile& t : platformTiles) live.push_back(t.id);
        coverTidy = std::thread([live]() {
            covercache::sweep(live);
            covercache::evict(covercache::kBudgetBytes);
        });
    }
    struct CoverTidyStop {
        std::thread& t;
        ~CoverTidyStop() { if (t.joinable()) t.join(); }
    } coverTidyStop{coverTidy};

    // Stops the workers before anything they write into goes out of scope. A
    // thread outliving this frame would be writing into a dead mutex.
    struct CoverFillStop {
        CoverFill& f;
        ~CoverFillStop() {
            f.quit.store(true);
            for (std::thread& t : f.workers) if (t.joinable()) t.join();
        }
    } coverFillStop{coverFill};

    // Any pad that is already plugged in. Hotplug is handled in the event loop,
    // so a controller connected later works without restarting anything.
    int padCount = 0;
    if (SDL_JoystickID* ids = SDL_GetGamepads(&padCount)) {
        for (int i = 0; i < padCount; ++i) SDL_OpenGamepad(ids[i]);
        SDL_free(ids);
    }
    std::fprintf(stderr, "[frontend] gamepads at startup: %d\n", padCount);

    bool running = true;
    // Set when something OUTSIDE the console asked it to stop — systemd on a
    // shutdown or a restart, which SDL turns into SDL_EVENT_QUIT. Kept apart
    // from `running` because a capture ending, or Start on Home, is not a
    // shutdown and must not upload anything.
    bool askedToStop = false;
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
    MenuNotice menuNotice;

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
    // The pause menu's items for THIS game, built each time it opens: the two
    // state items only where the system has snapshots. PlayStation 2 and
    // GameCube get Resume and Exit to Home and nothing to press that says no.
    std::vector<OverlayItem> pauseItems{OvResume, OvSaveState, OvLoadState, OvExit};

    // ---- The Power menu — docs/PROJECT.md, open question 10b ------------
    //
    // THE PAUSE MENU'S PANEL WITH A DIFFERENT LIST, not a second menu. The
    // power button opens it everywhere, and Start opens it on Home. In a game
    // the game is paused and Resume comes first and is focused, so a child
    // pressing the button costs one press of A. Rest is listed only on a
    // machine that can rest.
    //
    // Anything but Resume, from a game, leaves the game through finishExit
    // first — the one way out of a game, which uploads every kind of save — so
    // this is true for every emulator without any of them knowing.
    bool powerMenu = false;
    enum PowerItem { PwResume, PwRest, PwRestart, PwPowerOff };
    std::vector<PowerItem> powerItems;
    bool restAvailable = false;
    // Rest waits for the uploads finishExit queued: a machine that sleeps
    // mid-upload has sent nothing, and the save would sit owed until morning.
    bool restPending = false;
    uint64_t restWaitStart = 0;
    auto ovCount = [&]() {
        return static_cast<int>(powerMenu ? powerItems.size() : pauseItems.size());
    };
    auto ovLabel = [&](int i) -> const char* {
        if (!powerMenu) return kOverlayLabels[pauseItems[i]];
        switch (powerItems[i]) {
            case PwResume: return "Resume";
            // "Sleep", not "Rest": Rest is PlayStation's word alone, and
            // Switch, Xbox, SteamOS, Windows and macOS all say Sleep.
            // MMagTech, 2026-09-22.
            case PwRest: return "Sleep";
            case PwRestart: return "Restart";
            case PwPowerOff: return "Power off";
        }
        return "";
    };
    auto launchById = [&](int romId) -> bool {
        // PRESSING PLAY ON SOMETHING ALREADY COMING JOINS IT — 2026-09-21.
        //
        // A download started by "Download and keep" runs in the background and
        // leaves the person free to browse. Pressing Play on that same game
        // while it is in flight means "and I want to play it when it lands",
        // which is one flag on the job that is already fetching it — not a
        // second fetch of the same bytes, and not a refusal.
        //
        // Any OTHER game is still refused while one is in flight: the fetch is
        // one worker and one entry path, and a second would need both.
        if (launchJob.busy()) {
            if (launchJob.romId == romId && !launchJob.playWhenReady) {
                launchJob.playWhenReady = true;
                launchJob.startedOn = static_cast<int>(here());
                std::fprintf(stderr, "[launch] joining the download of %s\n",
                             launchJob.title.c_str());
                return true;
            }
            return false;    // one at a time
        }
        const romm::Game* g = nullptr;
        for (const auto& x : games) if (x.id == romId) { g = &x; break; }
        if (!g) { std::fprintf(stderr, "[launch] no game with id %d\n", romId); return false; }

        std::string lerr;
        if (!beginLaunch(launchJob, liveClient, *g, coreDir, &lerr)) {
            std::fprintf(stderr, "[launch] %s\n", lerr.c_str());
            return false;
        }
        launchJob.startedOn = static_cast<int>(here());
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
            where.present ? where.location : cache::keepLocation(g->sizeBytes);
        const cache::KeepVerdict v = cache::mayKeep(keepOn, romId, g->sizeBytes);
        if (!v.allowed) {
            // The one failure the person ever sees, and the number is what makes
            // it actionable: without it "the disk is full" is a dead end.
            detailScreen.setNotice("Not enough room. The disk is full of things "
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
        cache::Release r;
        cache::unkeep(storage::currentUser(), romId, /*keepTheBytes=*/nowPlaying, &r);
        detailScreen.setKept(false);

        // SILENCE IS THE RIGHT ANSWER ONLY WHEN THE ROW DID WHAT IT SAYS. The
        // badge going out is feedback enough for a game that is gone and a
        // disk that has the room back. The other three outcomes all leave the
        // bytes where they were, and saying nothing then is the row lying —
        // the same fault as a launch refusal reaching stderr and no further.
        switch (r.what) {
            // CONFIRMATIONS GO IN THE PILL — 2026-09-23 — with the rest of
            // the console's short-lived messages. The screen's own notice is
            // kept for what a person has to ACT on, like a full disk.
            case cache::Release::What::Deleted:
                detailScreen.setNotice("");
                menuNotice.say("Download removed", Tone::Done);
                break;
            case cache::Release::What::Nothing:
                detailScreen.setNotice("");
                break;
            case cache::Release::What::StillKept:
                // Unreachable until this console had more than one account,
                // and the first thing that account switching makes real.
                detailScreen.setNotice("");
                menuNotice.say(r.otherKeepers == 1
                                   ? "Removed. Someone else keeps it, so no space came back"
                                   : "Removed. Others keep it, so no space came back",
                               Tone::Info);
                break;
            case cache::Release::What::Demoted:
                detailScreen.setNotice("");
                menuNotice.say("Removed. The space comes back when you stop playing it",
                               Tone::Info);
                break;
            case cache::Release::What::DeleteFailed:
                detailScreen.setNotice("");
                menuNotice.say("Removed, but its files couldn't be deleted", Tone::Problem);
                break;
        }
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
        d.coverLarge = cards[cardIndex].coverLarge;
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
    // WHICH GAMES ARE KEPT, ASKED ONCE AND NOT PER FRAME. `cache::keptRoms`
    // reads the keep directory; `cache::isKeptBy` is a stat() per game, and
    // twelve covers at sixty frames a second would be seven hundred stats a
    // second to draw a dot. So the answer is pushed onto the cards whenever it
    // can have changed: at startup, and after a download or a removal.
    auto refreshKeeps = [&]() {
        const storage::User me = storage::currentUser();
        if (!me.valid()) return;
        const std::vector<int> kept = cache::keptRoms(me);
        for (Card& c : cards) c.kept = false;
        for (int romId : kept)
            for (Card& c : cards)
                if (c.id == romId) { c.kept = true; break; }
    };

    // SWITCHING WHO THE CONSOLE IS. Open question 26, and the half that
    // `accounts::activate` deliberately does not do.
    //
    // THE LIST OF WHAT BELONGS TO AN ACCOUNT WAS WORKED OUT FROM
    // `storage::userDir`'s children AND FROM WHAT THE STARTUP PATH BUILDS,
    // before this was written rather than after somebody saw their sister's
    // save. It is:
    //
    //   the token           -> accounts::activate
    //   who we are          -> adoptUser, which asks /api/users/me again
    //   recents, favourites -> loadLibrary; they are RomM's play history and
    //   the hero, the shelf    they are per person, not per console
    //   the kept badges     -> refreshKeeps, which reads this person's keeps
    //
    // **THE CATALOGUE IS PER ACCOUNT TOO, AND I HAD THIS WRONG.** This comment
    // used to say the library of games is the same for everybody because there
    // is one server per console, and that the refetch was incidental. Measured
    // 2026-09-21 on the real server: MMagTech sees 1147 games and vivian sees
    // 412. RomM scopes a library to the user, so the catalogue belongs on the
    // list above rather than beside it.
    //
    // The code was already right — `loadLibrary` fetches the lot in one pass,
    // so everything was being replaced regardless. It is the REASONING that was
    // wrong, which matters because the next person to optimise this would have
    // read that comment and skipped the refetch.
    //
    // TWO THINGS REFUSE THE SWITCH, and both are about a save reaching the
    // wrong person rather than about tidiness. **THEY ARE NOT EQUALLY LIVE AND
    // THE DIFFERENCE IS WORTH KNOWING** — MMagTech asked how you would even
    // switch mid-game, and the answer is that you cannot.
    auto switchAccount = [&](int id, std::string* why) -> bool {
        // **UNREACHABLE THROUGH THE UI TODAY, AND KEPT ANYWAY.** While a game
        // runs the core owns the pad outright (`InputOwner::Game`), the bar is
        // not drawn at all (`if (!playing && ...)`), and the only overlay is
        // the pause menu, whose four rows are Resume, Save state, Load latest
        // and Exit. There is no way to reach the chip. Nothing but
        // `--switch-account` can make this branch fire.
        //
        // It stays because the hazard it names is real and the door is one row
        // wide: consoles do offer user switching from a pause menu, and the
        // day anybody adds that row this is what stops a running game's save
        // being filed under the wrong person — the core writes its card at
        // unload and `filesave` puts it under `storage::currentUser()`, which
        // a switch has just changed.
        //
        // **Do not read this as a tested guard.** It has never fired in
        // anger and it cannot until something calls this while a game runs.
        if (playing) {
            if (why) *why = "Quit the game before switching accounts.";
            return false;
        }
        // **THIS ONE IS REACHABLE AND IT IS THE ONE THAT MATTERS.** Exit a
        // game, the save starts going up in the background, you land on Home,
        // and the chip is right there — a window of a few seconds that anybody
        // switching users would walk straight into.
        //
        // AN UPLOAD IN FLIGHT WOULD GO UP AS THE NEW PERSON. The uploader holds
        // a pointer to this very client, so swapping the token underneath it
        // sends the previous account's save to the new account's library. An
        // unsent save is described elsewhere in this file as the one
        // irreplaceable thing on the machine; misfiling one is worse than
        // making somebody wait.
        if (const int owed = uploader.pending(); owed > 0) {
            if (why)
                *why = owed == 1 ? "A save is still going up. One moment."
                                 : "Saves are still going up. One moment.";
            return false;
        }

        std::string err;
        if (!accounts::activate(id, liveClient, &err)) {
            if (why) *why = err;
            return false;
        }
        // ASKED AGAIN, NOT ASSUMED. The token changed, so the answer to "who is
        // this" changed, and every save path below is built from it.
        if (!adoptUser(liveClient)) {
            if (why) *why = "Switched, but the server would not say who that is.";
            return false;
        }

        // BUILT BESIDE THE LIVE ONE AND ONLY THEN SWAPPED IN. The store is a
        // single object now, so a failed switch must not have half-replaced it.
        Library fresh = loadLibrary(liveClient);
        // NOT `cards.empty()`, which this asked until open question 28. Cards
        // are recents and favourites now, so somebody who has played nothing
        // and starred nothing has none — and a real account with a working
        // server would have been refused. The platform list is what says the
        // server answered.
        if (fresh.platformTiles.empty()) {
            // Left as it was rather than blanked: an empty Home is worse than
            // the previous person's, and this is recoverable by switching back.
            if (why) *why = "That account's library came back empty.";
            return false;
        }
        lib = std::move(fresh);
        libraryScreen.build(platformTiles, collectionTiles);
        startCoverFill();
        // HOME STARTS OVER FOR THE NEW PERSON: the first card of Recent, every
        // row scrolled back. Where the last person was is meaningless in a
        // different set of cards, and was out of range whenever the new set
        // was shorter.
        focusRow = RowRecent;
        focusSlot = 0;
        rememberedSlot[0] = rememberedSlot[1] = 0;
        shelfScroll[0].settle(0.0f);
        shelfScroll[1].settle(0.0f);
        scrollY.settle(0.0f);
        if (Card* c = cardAt(focusRow, focusSlot)) c->focus.settle(1.0f);
        refreshKeeps();

        const storage::User& now = storage::currentUser();
        std::fprintf(stderr, "[accounts] switched to %d - %s, %zu games\n", now.id,
                     now.name.c_str(), cards.size());
        return true;
    };

    // ADDING SOMEBODY, ON A CLIENT OF ITS OWN.
    //
    // THE SEPARATE CLIENT IS THE WHOLE POINT and it is the reference
    // implementation's rule too: pairing on the live client would swap the
    // token under whoever is playing, and a half-finished pairing would leave
    // `firstrun` looking at a console it cannot classify. Nothing here touches
    // `liveClient` until `recordPairing` has succeeded — and even then it only
    // writes the account, because adding somebody must not sign anybody out.
    struct AddJob {
        std::thread th;
        std::mutex m;
        std::atomic<bool> running{false};
        romm::Pairing pairing;
        bool haveCode = false;
        bool finished = false;
        bool ok = false;
        accounts::Paired who;
        std::string err;
    };
    auto addJob = std::make_shared<AddJob>();
    // The QR is encoded once, when the code first arrives, not per frame.
    bool shownPairCode = false;
    // Set by --screen add-account: hold the screenshot until there is a code.
    bool waitForPairCode = false;

    auto startAddAccount = [&, addJob]() {
        if (addJob->running.load()) return;
        if (!rommAddress) {
            accountScreen.setNotice("This console has no server address.");
            return;
        }
        addJob->running = true;
        shownPairCode = false;
        { std::lock_guard<std::mutex> lk(addJob->m);
          addJob->haveCode = addJob->finished = addJob->ok = false;
          addJob->err.clear(); }
        const std::string addr = rommAddress;
        if (addJob->th.joinable()) addJob->th.join();
        addJob->th = std::thread([addJob, addr]() {
            romm::Client c;              // ITS OWN. See the comment above.
            std::string err;
            if (!c.setAddress(addr, &err)) {
                std::lock_guard<std::mutex> lk(addJob->m);
                addJob->err = err; addJob->finished = true; addJob->running = false;
                return;
            }
            romm::Pairing p;
            if (!c.beginPairing(&p, &err)) {
                std::lock_guard<std::mutex> lk(addJob->m);
                addJob->err = err; addJob->finished = true; addJob->running = false;
                return;
            }
            { std::lock_guard<std::mutex> lk(addJob->m);
              addJob->pairing = p; addJob->haveCode = true; }

            // ON THE PAIRING'S OWN INTERVAL, and it expires in minutes — the
            // loop ends rather than running for ever, because a code nobody
            // approved is not an error worth blocking on.
            const int tries = p.expiresIn > 0 ? (p.expiresIn / (p.intervalSeconds > 0 ? p.intervalSeconds : 5)) + 1 : 60;
            for (int i = 0; i < tries; ++i) {
                std::this_thread::sleep_for(
                    std::chrono::seconds(p.intervalSeconds > 0 ? p.intervalSeconds : 5));
                const int state = c.pollPairing(p, &err);
                if (state == 0) continue;
                if (state < 0) break;
                std::string aerr;
                accounts::Paired who;
                const bool wrote = accounts::recordPairing(c, &who, &aerr);
                std::lock_guard<std::mutex> lk(addJob->m);
                addJob->ok = wrote;
                addJob->who = who;
                if (!wrote) addJob->err = aerr;
                addJob->finished = true; addJob->running = false;
                return;
            }
            std::lock_guard<std::mutex> lk(addJob->m);
            if (addJob->err.empty()) addJob->err = "that code expired before anybody approved it";
            addJob->finished = true; addJob->running = false;
        });
        addAccountScreen.setBusy(true);
    };

    // Rebuilt from the store, which is the only thing that knows.
    auto refreshAccountRows = [&]() {
        // EVERYBODY EXCEPT WHOEVER IS SIGNED IN. The chip directly above the
        // panel is that person's row already; listing them again is the
        // redundancy MMagTech spotted in the first capture.
        std::vector<screens::AccountRow> rows;
        const int active = accounts::activeId();
        for (const accounts::Account& a : accounts::all())
            if (a.id != active) rows.push_back({a.id, a.name, a.avatar});
        accountScreen.setRows(std::move(rows));
    };

    // WHERE THE BAR'S FOCUS LIVES. Not in Home's row model and not in any
    // screen's: the bar is drawn over all of them, so its cursor belongs to the
    // app. Every screen gets into it the same way (Action::FocusBar) and out of
    // it the same way (Down, or Back).
    // THE SWITCHER IS AN OVERLAY, NOT A DESTINATION, and that is the whole
    // point of it being a panel. It was a stack screen for one build and the
    // capture showed the fault immediately: pushing it made `here()` stop
    // being Home, so Home stopped drawing and the panel hung over an empty
    // purple field. A thing that "expands from the chip" has to have what it
    // expanded over still behind it.
    //
    // So it lives beside `barFocused` rather than in the stack: the app owns
    // it, every screen keeps drawing underneath it, and Back closes it and
    // leaves you exactly where you were.
    bool accountsOpen = false;
    bool barFocused = false;
    int barSlot = 0;
    // Trigger edge state. See the axis handler.
    bool l2Held = false, r2Held = false;
    // HELD-DIRECTION REPEAT. One direction at a time, which is what a d-pad is:
    // pressing a second direction takes over rather than queueing, so a person
    // rolling their thumb around the pad never ends up with two repeats
    // fighting each other.
    // THE CURTAIN. 0 is clear, 1 is black over everything. See design.h.
    Animated curtain;
    // An account switch waiting behind the curtain; see pumpSwitch.
    int switchPendingId = 0;
    // LEAVING: Sign out, or a new server address saved. The curtain is the
    // startup screen with this line under the name, and once it is down (and
    // uploads are through) the app starts itself again; see pumpLeave.
    enum class Leave { None, SignOut, NewAddress };
    Leave leaving = Leave::None;
    std::string leaveLabel;
    Uint64 leaveDownAt = 0;
    bool restartSelf = false;
    std::string switchLabel;
    int switchCurtainFrames = 0;
    Uint64 switchShownAt = 0;    // when the curtain was fully down
    // THE NAME HAS ITS OWN FADE, inside the curtain's. It came in and went out
    // with the curtain, so it sat over the old screen on the way down and over
    // the new Home on the way up: MMagTech saw the two overlap. Now it only
    // shows on a fully black screen: in after the curtain is down, out before
    // it lifts.
    Animated switchText;
    // AN ACCOUNT SWITCH'S CURTAIN IS THE CONSOLE'S PURPLE, not black: the
    // plain gradient boot and Settings stand on. Black is a game's curtain;
    // behind "Switching to vivian" it looked like a screen nobody had
    // designed. MMagTech, 2026-09-24. Set with the switch, cleared once the
    // curtain is fully up again.
    bool switchCurtain = false;
    constexpr float kSwitchTextFade = 0.200f;   // a starting value
    bool switchDone = false;     // the switch has run; waiting out the hold
    bool switchOk = false;
    std::string switchWhy;
    curtain.smooth = true;
    curtain.from = curtain.to = 0.0f;

    bool navHeld = false;
    screens::Nav heldNav = screens::Nav::Down;
    float heldFor = 0.0f, nextRepeat = 0.0f;
    auto holdNav = [&](screens::Nav n) {
        navHeld = true;
        heldNav = n;
        heldFor = 0.0f;
        nextRepeat = kRepeatDelay;
    };
    auto releaseNav = [&](screens::Nav n) {
        // Only the direction that is actually held releases it. Letting go of
        // Left while holding Down must not stop the Down.
        if (navHeld && heldNav == n) navHeld = false;
    };

    std::function<bool(screens::Nav)> navigate;
    // Home's own keys, as one function, so that everything — Home, the Library,
    // the grid — arrives through `navigate` and the bar can be offered the key
    // first in exactly one place. Assigned further down, once moveFocus and
    // moveRow exist.
    std::function<bool(screens::Nav)> homeKey;

    // ---- The PIN ------------------------------------------------------------
    //
    // docs/SETTINGS.md, Accounts, issue #57. The pad is a layer over whatever
    // is showing (pin.h); these decide what an entered PIN means.
    //
    // `askPin` is the one door every protected action goes through: with no
    // PIN set it simply goes ahead, which is the rule that with no PIN every
    // account can do everything. Wi-Fi, Sign out, Change server address,
    // Remove an account and File access call it when they are built.
    screens::PinScreen pinScreen;
    std::function<void()> pinThen;
    // FIVE WRONG TRIES, THEN THIRTY SECONDS. Enough to forgive fumbling with
    // a d-pad, and enough to make guessing ten thousand PINs not worth it for
    // the sibling the PIN is for. Starting values. Counted per console run,
    // not saved: a restart is slower than the wait.
    constexpr int kPinTries = 5;
    constexpr float kPinLockSeconds = 30.0f;
    int pinFails = 0;
    // ONCE PER VISIT TO SETTINGS. Entered once, the PIN is not asked again
    // until Settings is left; the frame loop clears this. MMagTech on the TV,
    // 2026-09-24: a wrong Wi-Fi password meant the PIN again, then the list,
    // then the keyboard. Switching into the owner's account still always asks
    // (it passes `always`), because that is not a Settings visit.
    bool pinUnlocked = false;
    // A CODE THE CONSOLE CHOSE, typed on the same pad: the last step before
    // Format, so erasing a drive is never one press too many. Not the PIN;
    // no lockout, because a wrong code costs nothing.
    std::string pinExpect;
    auto askPin = [&](const std::string& title, const std::string& detail,
                      std::function<void()> then, bool always = false) {
        pinExpect.clear();
        if (!accounts::pinIsSet() || (pinUnlocked && !always)) { then(); return; }
        pinThen = std::move(then);
        pinScreen.open(screens::PinScreen::Mode::Check, title, detail);
        std::fprintf(stderr, "[pin] asked: %s\n", title.c_str());
    };
    auto askCode = [&](const std::string& code, const std::string& title,
                       const std::string& detail, std::function<void()> then) {
        pinExpect = code;
        pinThen = std::move(then);
        pinScreen.open(screens::PinScreen::Mode::Check, title, detail);
        std::fprintf(stderr, "[pin] code asked: %s\n", title.c_str());
    };
    auto choosePin = [&](const std::string& title, const std::string& detail,
                         std::function<void()> then) {
        pinExpect.clear();
        pinThen = std::move(then);
        pinScreen.open(screens::PinScreen::Mode::Choose, title, detail);
    };
    auto pinOutcome = [&](screens::PinScreen::Outcome o) {
        using O = screens::PinScreen::Outcome;
        if (o == O::None) return;
        if (o == O::Cancelled) {
            pinScreen.close();
            pinThen = nullptr;
            pinExpect.clear();
            std::fprintf(stderr, "[pin] left without one\n");
            return;
        }
        if (!pinExpect.empty()) {
            if (pinScreen.pin() != pinExpect) {
                sound::play(sound::Cue::Edge);
                pinScreen.reject("That is not the code");
                return;
            }
            pinExpect.clear();
        } else if (pinScreen.mode() == screens::PinScreen::Mode::Check) {
            if (!accounts::checkPin(pinScreen.pin())) {
                ++pinFails;
                sound::play(sound::Cue::Edge);
                std::fprintf(stderr, "[pin] wrong, %d of %d\n", pinFails, kPinTries);
                pinScreen.reject("That is not the PIN");
                if (pinFails >= kPinTries) {
                    pinFails = 0;
                    pinScreen.lockFor(kPinLockSeconds);
                }
                return;
            }
            pinFails = 0;
            pinUnlocked = true;
            std::fprintf(stderr, "[pin] accepted\n");
        } else {
            std::string err;
            if (!accounts::setPin(pinScreen.pin(), &err)) {
                pinScreen.reject("The PIN could not be saved");
                std::fprintf(stderr, "[pin] could not save: %s\n", err.c_str());
                return;
            }
            std::fprintf(stderr, "[pin] set\n");
        }
        // CLOSED BEFORE THE ACTION RUNS, so an action that opens the pad
        // again (Change PIN: the old one, then the new one) opens it fresh.
        pinScreen.close();
        auto then = std::move(pinThen);
        pinThen = nullptr;
        sound::play(sound::Cue::Activate);
        if (then) then();
    };

    // ---- A question with a few answers (choice.h) -------------------------
    //
    // Same shape as the PIN: the app opens it with what to do with the
    // answer, and the answer is acted on after it closes, so an answer can
    // open another question ("who?" then "are you sure?").
    screens::ChoiceScreen choiceScreen;
    std::function<void(int)> choiceThen;
    auto askChoice = [&](const std::string& title, const std::string& detail,
                         std::vector<std::string> options, int focus,
                         std::function<void(int)> then) {
        choiceThen = std::move(then);
        choiceScreen.open(title, detail, std::move(options), focus);
    };
    auto choiceOutcome = [&](screens::ChoiceScreen::Outcome o) {
        using O = screens::ChoiceScreen::Outcome;
        if (o == O::None) return;
        choiceScreen.close();
        auto then = std::move(choiceThen);
        choiceThen = nullptr;
        if (o == O::Chosen && then) then(choiceScreen.chosen());
    };
    // WHO CAN BE REMOVED: everybody but whoever is signed in (the store
    // refuses them) and the owner (the store refuses them too).
    auto removableAccounts = [&]() {
        std::vector<accounts::Account> out;
        const int active = accounts::activeId(), owner = accounts::ownerId();
        for (const accounts::Account& a : accounts::all())
            if (a.id != active && a.id != owner) out.push_back(a);
        return out;
    };

    // ---- Settings' rows -----------------------------------------------------
    //
    // docs/PROJECT.md, open question 31. The screen is handed rows and hands
    // back an id; everything a row SHOWS is read here, fresh, each time
    // Settings opens or a row changes. Rows marked Unbuilt are agreed and not
    // built: they are there so the whole layout can be judged on the
    // television, and focus never lands on them.
    enum SettingId { SetAddAccount = 1, SetInterfaceSounds, SetPinSet, SetPinChange,
                     SetPinOff, SetRemoveAccount, SetScreenOff, SetWifi, SetServer,
                     SetUpdate, SetUpdateCheck, SetCredits, SetFiles, SetDownloads };
    // One Eject row per USB drive: this plus the drive's index in
    // storage::locations() when the rows were built.
    constexpr int kSetEject = 100;
    // One Format row per blank drive: this plus its index in formatIds, the
    // drives as they were when the rows were built.
    constexpr int kSetFormat = 200;
    std::vector<drives::Unusable> formatable;
    std::map<int, std::string> driveNames;   // a drive row's id -> its name

    // THE DRIVES' NAMES, as Storage shows them: the main drive is
    // "CabinetOS", any other internal disk "Internal", one that can be
    // unplugged "External". With two of one kind, counting drives that cannot
    // be used, the drive's own name tells them apart: its label (the mount
    // point) for one in use. Downloads names a game's drive the same way.
    auto locationNames = [&](const std::vector<std::string>& locs,
                             const std::vector<drives::Unusable>& unusable) {
        std::map<std::string, std::string> out;
        int internals = 0, externals = 0;
        for (size_t i = 1; i < locs.size(); ++i)
            (storage::isExternal(locs[i]) ? externals : internals)++;
        for (const drives::Unusable& u : unusable) (u.external ? externals : internals)++;
        for (size_t i = 0; i < locs.size(); ++i) {
            std::string name = "CabinetOS";
            if (i > 0) {
                const bool external = storage::isExternal(locs[i]);
                name = external ? "External" : "Internal";
                if ((external ? externals : internals) > 1) {
                    // <mount>/CabinetOS: the mount point is named for the drive.
                    std::string mount = locs[i].substr(0, locs[i].rfind('/'));
                    name += " (" + mount.substr(mount.rfind('/') + 1) + ")";
                }
            }
            out[locs[i]] = name;
        }
        return out;
    };

    int screenOffIndex = savedScreenOff();
    std::fprintf(stderr, "[idle] screen off after %s\n", kScreenOff[screenOffIndex].name);

    // ---- System update ------------------------------------------------------
    //
    // docs/SETTINGS.md, System; issue #70. Root does the work and writes one
    // status file (update.h); the frame loop reads it twice a second and this
    // decides what it means on screen. What has to outlive a boot is kept in
    // settings.json: Manual or Weekly, when the last check succeeded and what
    // it found, the version already announced, and the version waiting for a
    // restart with the boot it was staged in, which is how the first start
    // after a restart knows whether it applied.
    update::Status upd;   // the first poll reads it, so a status already there is acted on
    // A state said on screen before root has written it: "Checking…" the
    // moment the row is pressed, or why systemd refused. Root's next write
    // (at >= updSaidAt) replaces it.
    int64_t updSaidAt = 0;
    float updPoll = 0.0f;
    bool updWeekly = prefs::get("update_check", "manual") == "weekly";
    bool updAuto = false;            // the check running is the weekly one
    bool updAskAfterCheck = false;   // a check somebody pressed: offer the download
    float updAutoWait = 0.0f;        // before the weekly check may try again
    bool updReadyPanel = false;      // "Update ready" waits to be asked
    bool updReadyAfterGame = false;  // ...and it landed during a game
    std::string updStartNotice;      // "Updated to", once Home is up
    Tone updStartTone = Tone::Done;
    auto prefNum = [](const char* key) {
        return static_cast<int64_t>(std::strtoll(prefs::get(key, "0").c_str(), nullptr, 10));
    };
    // "0.9 MB", "1.4 GB": one decimal, because a frontend update is under a
    // megabyte and a base image is gigabytes, and both have to read.
    auto updBytes = [](int64_t b) {
        char buf[32];
        const double mb = static_cast<double>(b) / 1e6;
        if (mb >= 1000.0) std::snprintf(buf, sizeof buf, "%.1f GB", mb / 1000.0);
        else std::snprintf(buf, sizeof buf, "%.1f MB", mb);
        return std::string(buf);
    };
    // "0.4 of 0.9 MB": one unit, the total's.
    auto updOf = [](int64_t done, int64_t total) {
        char buf[48];
        const bool gb = total >= 1000000000;
        const double d = static_cast<double>(done) / (gb ? 1e9 : 1e6);
        const double t = static_cast<double>(total) / (gb ? 1e9 : 1e6);
        std::snprintf(buf, sizeof buf, "%.1f of %.1f %s", d, t, gb ? "GB" : "MB");
        return std::string(buf);
    };
    // "Checked today", by the calendar here, not by 24-hour periods.
    auto updChecked = [&]() -> std::string {
        const int64_t at = prefNum("update_checked");
        // Never checked says nothing: the row alone, and pressing it checks.
        // MMagTech, 2026-09-25.
        if (at <= 0) return std::string();
        auto dayOf = [](std::time_t t) {
            std::tm tm{};
            localtime_r(&t, &tm);
            tm.tm_hour = 12;
            tm.tm_min = tm.tm_sec = 0;
            return static_cast<int64_t>(std::mktime(&tm) / 86400);
        };
        const int64_t days = dayOf(std::time(nullptr)) - dayOf(static_cast<std::time_t>(at));
        if (days <= 0) return "Checked today";
        if (days == 1) return "Checked yesterday";
        return "Checked " + std::to_string(days) + " days ago";
    };
    // What the last successful check found, when nothing has run this boot:
    // still newer than what is running, or not.
    auto updFoundNewer = [&]() {
        const std::string found = prefs::get("update_found", "");
        return !found.empty() && found != update::bootedVersion();
    };
    auto updateRow = [&]() {
        using K = screens::SettingsRow::Kind;
        std::string value, detail;
        switch (upd.state) {
            case update::State::Checking:
                value = "Checking\xE2\x80\xA6";
                break;
            case update::State::UpToDate:
                value = "Up to date";
                detail = updChecked();
                break;
            case update::State::Available:
                value = "Update available";
                detail = upd.version + " \xC2\xB7 " + updBytes(upd.size);
                break;
            case update::State::Downloading: {
                const int pct = upd.total > 0
                    ? static_cast<int>(std::min<int64_t>(100, upd.done * 100 / upd.total)) : 0;
                value = std::to_string(pct) + "%";
                detail = updOf(upd.done, upd.total);
                break;
            }
            case update::State::Installing: {
                // Time and bytes, because unpacking has no honest percentage
                // and a spinner would not show it is not frozen.
                const int64_t secs = std::max<int64_t>(0, std::time(nullptr) - upd.since);
                value = "Installing\xE2\x80\xA6";
                detail = (secs >= 60 ? std::to_string(secs / 60) + "m " : std::string()) +
                         std::to_string(secs % 60) + "s, " + updBytes(upd.written) + " written";
                break;
            }
            case update::State::Ready:
                value = "Restart to update";
                detail = upd.version;
                break;
            case update::State::Failed:
                value = upd.download ? "Couldn't update" : "Couldn't check";
                detail = upd.reason;
                break;
            case update::State::None:
                if (updFoundNewer()) {
                    value = "Update available";
                    detail = prefs::get("update_found", "") + " \xC2\xB7 " +
                             updBytes(prefNum("update_size"));
                } else if (prefNum("update_checked") > 0) {
                    value = "Up to date";
                    detail = updChecked();
                }
                break;
        }
        return screens::SettingsRow{K::Action, SetUpdate, "System update", detail, value};
    };
    // ---- File access (docs/SETTINGS.md, Storage; issue #69) -----------------
    //
    // Root's answer, read with the update status. `filesSaidAt` holds a state
    // said on screen before root has written it ("Turning on…"), the way
    // updSaidAt does. Whether it should be on is the owner's choice, kept in
    // settings.json, so a console left with it on turns it on again at boot.
    files::State filesState = files::read();
    int64_t filesSaidAt = 0;
    bool filesPanelWhenOn = false;   // open the panel once root says it is on
    bool filesWant = prefs::get("file_access", "off") == "on";
    if (filesWant && !filesState.on) {
        std::string why;
        if (files::turnOn(&why)) filesSaidAt = std::time(nullptr);
        else std::fprintf(stderr, "[files] could not turn on at start: %s\n", why.c_str());
    }

    // AFTERWARDS: the first start after a restart compares the version the
    // machine booted with the one that was staged. Said once Home is up.
    {
        const std::string pending = prefs::get("update_pending", "");
        if (!pending.empty() && prefs::get("update_pending_boot", "") != update::bootId()) {
            const std::string booted = update::bootedVersion();
            const bool applied = booted == pending;
            std::fprintf(stderr, "[update] staged %s, booted %s: %s\n", pending.c_str(),
                         booted.empty() ? "(no version)" : booted.c_str(),
                         applied ? "applied" : "did not apply");
            updStartNotice = applied ? "Updated to " + pending : "Update didn't apply";
            updStartTone = applied ? Tone::Done : Tone::Problem;
            prefs::set("update_pending", "");
            prefs::set("update_pending_boot", "");
            if (applied) prefs::set("update_found", "");
        }
    }
    // THE NETWORK IS ASKED OFF THE FRAME THREAD. net::status() is three or four
    // nmcli round trips, which is a visible hitch if the screen waits for it,
    // so the row says "Checking" until the answer lands.
    struct SettingsNet {
        std::mutex m;
        bool have = false;
        bool fresh = false;       // arrived and not yet shown
        net::Status st;
        std::thread th;
        ~SettingsNet() { if (th.joinable()) th.join(); }
    };
    SettingsNet settingsNet;

    // ---- Wi-Fi, in Settings > Network (issue #59) -------------------------
    //
    // WHAT IS ON THE AIR, OFF THE FRAME THREAD: what NetworkManager already
    // knows first (quick), then a real scan (seconds on the A9). Each answer
    // rebuilds the rows if Settings is still showing. A scan still running is
    // left to finish rather than waited on.
    struct SettingsWifi {
        std::mutex m;
        bool fresh = false;
        std::vector<net::Network> list;
        std::atomic<bool> running{false};
        std::thread th;
        ~SettingsWifi() { if (th.joinable()) th.join(); }
    };
    SettingsWifi settingsWifi;
    auto askWifi = [&settingsWifi]() {
        if (settingsWifi.running.load()) return;
        if (settingsWifi.th.joinable()) settingsWifi.th.join();
        settingsWifi.running = true;
        settingsWifi.th = std::thread([&settingsWifi]() {
            std::vector<net::Network> got;
            std::string err;
            if (net::cachedScan(&got, &err)) {
                std::lock_guard<std::mutex> lk(settingsWifi.m);
                settingsWifi.list = got;
                settingsWifi.fresh = true;
            }
            got.clear();
            if (net::scan(&got, &err)) {
                std::lock_guard<std::mutex> lk(settingsWifi.m);
                settingsWifi.list = got;
                settingsWifi.fresh = true;
            }
            settingsWifi.running = false;
        });
    };
    // JOINING BLOCKS FOR UP TO 45 SECONDS (net::join), so it is a job too. The
    // row being joined says so; the answer arrives in the notice pill.
    struct WifiJob {
        std::mutex m;
        std::thread th;
        std::atomic<bool> running{false};
        bool done = false, ok = false;
        std::string ssid, err;
        ~WifiJob() { if (th.joinable()) th.join(); }
    };
    WifiJob wifiJob;
    // CHANGE PASSWORD IS FORGET, THEN JOIN. The reason to change a password is
    // that the router's changed, so the old one is worth nothing, and joining
    // over a saved profile can leave NetworkManager with two for one network.
    auto startWifiJoin = [&wifiJob](const std::string& ssid, const std::string& pass,
                                    bool forgetFirst) {
        if (wifiJob.running.load()) return;
        if (wifiJob.th.joinable()) wifiJob.th.join();
        wifiJob.running = true;
        {
            std::lock_guard<std::mutex> lk(wifiJob.m);
            wifiJob.ssid = ssid;
            wifiJob.done = false;
        }
        wifiJob.th = std::thread([&wifiJob, ssid, pass, forgetFirst]() {
            std::string err;
            if (forgetFirst) net::forget(ssid, &err);
            err.clear();
            const bool ok = net::join(ssid, pass, false, &err);
            std::fprintf(stderr, "[wifi] join %s: %s\n", ssid.c_str(),
                         ok ? "joined" : err.c_str());
            // A FAILED JOIN WITH A TYPED PASSWORD LEAVES A SAVED NETWORK
            // BEHIND, holding the wrong password: NetworkManager keeps the
            // profile `device wifi connect` made. It then showed as "Saved",
            // and joining it tried the wrong password again. MMagTech on the
            // TV, 2026-09-24. Removed, so a failed join leaves nothing.
            if (!ok && !pass.empty()) {
                std::string ignored;
                net::forget(ssid, &ignored);
            }
            std::lock_guard<std::mutex> lk(wifiJob.m);
            wifiJob.ok = ok;
            wifiJob.err = err;
            wifiJob.done = true;
            wifiJob.running = false;
        });
    };
    // ---- Change server address (issue #60) -------------------------------
    //
    // IS IT THE SAME SERVER, off the frame thread: reaching an address that
    // is not there costs curl's connect timeout. The keyboard stays up saying
    // "Checking…" while it runs, the Wi-Fi password's shape.
    struct ServerJob {
        std::mutex m;
        std::thread th;
        std::atomic<bool> running{false};
        bool done = false;
        server::Check result = server::Check::Failed;
        std::string address, detail;
        Uint64 startedAt = 0;
        ~ServerJob() { if (th.joinable()) th.join(); }
    };
    ServerJob serverJob;
    auto startServerCheck = [&serverJob](const std::string& address) {
        if (serverJob.running.load()) return;
        if (serverJob.th.joinable()) serverJob.th.join();
        serverJob.running = true;
        {
            std::lock_guard<std::mutex> lk(serverJob.m);
            serverJob.address = address;
            serverJob.done = false;
            serverJob.startedAt = SDL_GetTicks();
        }
        serverJob.th = std::thread([&serverJob, address]() {
            std::string detail;
            const server::Check r = server::check(address, &detail);
            std::fprintf(stderr, "[server] %s: %s%s%s\n", address.c_str(),
                         r == server::Check::Same        ? "the same server"
                         : r == server::Check::Different ? "a different server"
                         : r == server::Check::NoServer  ? "no server"
                                                         : "could not tell",
                         detail.empty() ? "" : ", ", detail.c_str());
            std::lock_guard<std::mutex> lk(serverJob.m);
            serverJob.result = r;
            serverJob.detail = detail;
            serverJob.done = true;
            serverJob.running = false;
        });
    };

    // The networks as the rows show them: one per name, strongest signal,
    // the one in use first, then saved ones, then the rest by signal.
    std::vector<net::Network> wifiShown;
    // Whether the question panel on screen is the Wi-Fi list, so a scan that
    // lands while it is open can refresh it.
    bool wifiPanelOpen = false;
    auto sortWifi = [&]() {
        std::vector<net::Network> seen;
        {
            std::lock_guard<std::mutex> lk(settingsWifi.m);
            seen = settingsWifi.list;
        }
        wifiShown.clear();
        for (const net::Network& n : seen) {
            // 802.1X needs a certificate and an identity, which nobody types
            // with a d-pad, so it is not offered at all.
            if (n.ssid.empty() || n.enterprise) continue;
            auto it = std::find_if(wifiShown.begin(), wifiShown.end(),
                                   [&](const net::Network& w) { return w.ssid == n.ssid; });
            if (it == wifiShown.end()) { wifiShown.push_back(n); continue; }
            it->signal = std::max(it->signal, n.signal);
            it->active = it->active || n.active;
            it->known = it->known || n.known;
        }
        std::stable_sort(wifiShown.begin(), wifiShown.end(),
                         [](const net::Network& a, const net::Network& b) {
                             if (a.active != b.active) return a.active;
                             if (a.known != b.known) return a.known;
                             return a.signal > b.signal;
                         });
    };
    // The list as the panel shows it: names, and "Connected", "Saved" or
    // "Joining…" on the right.
    auto wifiPanelRows = [&](std::vector<std::string>* names, std::vector<std::string>* values) {
        std::string joining;
        if (wifiJob.running.load()) {
            std::lock_guard<std::mutex> lk(wifiJob.m);
            joining = wifiJob.ssid;
        }
        for (const net::Network& n : wifiShown) {
            names->push_back(n.ssid);
            std::string v = n.active ? "Connected" : (n.known ? "Saved" : "");
            if (n.ssid == joining) v = "Joining\xE2\x80\xA6";
            values->push_back(v);
        }
    };
    // THE KEYBOARD OUTSIDE SEARCH. Search owns its docked keyboard; anything
    // else that opens it (a Wi-Fi password) says here what to do with it.
    std::function<void(ui::KeyboardResult)> keyboardThen;
    // A Wi-Fi password on the on-screen keyboard, then the join on a worker.
    // `forgetFirst` is Change password; `why` is "Wrong password" when this
    // is the retry after one. Assigned once buildSettings exists.
    std::function<void(const std::string& ssid, bool forgetFirst, const std::string& why)>
        askWifiPassword;
    // The network the keyboard is waiting on, while it says "Joining…".
    std::string wifiKeyboardFor;
    // A server address on the keyboard; `why` is what went wrong with the
    // last one, said in the field with the address kept behind it. Assigned
    // once buildSettings exists.
    std::function<void(const std::string& typed, const std::string& why)> askServerAddress;
    // The address the keyboard is waiting on, while it says "Checking…".
    std::string serverKeyboardFor;
    auto askNetwork = [&settingsNet]() {
        if (settingsNet.th.joinable()) settingsNet.th.join();
        settingsNet.th = std::thread([&settingsNet]() {
            net::Status st = net::status();
            std::lock_guard<std::mutex> lk(settingsNet.m);
            settingsNet.st = st;
            settingsNet.have = true;
            settingsNet.fresh = true;
        });
    };
    // ---- Downloads (downloads.h), #68 --------------------------------------
    //
    // Every downloaded game, for Settings' Downloads panel, named as the
    // Library names it where the library knows the game. A game still
    // downloading is left out until it has arrived (MMagTech, 2026-09-26): its
    // record is written before its first byte, so it would show half-sized.
    screens::DownloadsPanel downloadsPanel;
    // Assigned once buildSettings exists: a removal rebuilds Storage.
    std::function<void(screens::DownloadsPanel::Outcome)> downloadsOutcome;
    auto downloadItems = [&]() {
        std::unordered_map<int, const romm::Game*> byId;
        for (const romm::Game& g : games) byId[g.id] = &g;
        const std::vector<std::string> locs = storage::locations();
        const std::map<std::string, std::string> names = locationNames(locs, drives::unusable());
        const bool downloading = launchJob.busy() && launchJob.keepWhenReady;
        std::vector<screens::DownloadItem> out;
        for (const cache::Download& d : cache::downloads()) {
            if (downloading && d.romId == launchJob.romId) continue;
            screens::DownloadItem it;
            it.romId = d.romId;
            it.title = d.title;
            it.system = d.platform;
            int platformId = d.platformId;
            if (auto g = byId.find(d.romId); g != byId.end()) {
                if (!g->second->name.empty()) it.title = g->second->name;
                if (g->second->platformId) platformId = g->second->platformId;
            }
            if (auto pn = lib.platformNames.find(platformId); pn != lib.platformNames.end())
                it.system = pn->second;
            it.bytes = d.present ? d.bytes : 0;
            it.missing = !d.present;
            // THE DRIVE ONLY WHEN THERE IS A CHOICE OF DRIVES: with one, the
            // same word on every row says nothing.
            if (d.present && locs.size() > 1)
                if (auto n = names.find(d.location); n != names.end()) it.drive = n->second;
            out.push_back(std::move(it));
        }
        return out;
    };

    auto buildSettings = [&]() {
        using Row = screens::SettingsRow;
        using K = Row::Kind;
        auto gb = driveSize;
        std::vector<screens::SettingsCategory> cats;

        cats.push_back({"Accounts", {
            {K::Action, SetAddAccount, "Add an account",
             "Pair another RomM user with this console", ""},
        }});
        // REMOVE AN ACCOUNT: from this console only. Greyed out when there is
        // nobody it could remove (not the person signed in, not the owner).
        cats.back().rows.push_back(Row{removableAccounts().empty() ? K::Disabled : K::Action,
                                       SetRemoveAccount, "Remove an account", "", ""});
        // THE PIN IS THE OWNER'S, AND ONLY THE OWNER SEES ITS CONTROLS.
        // Anyone else sees whether there is one and whose it is, with nothing
        // to press. MMagTech, 2026-09-24, signed in as claire and offered
        // Change PIN and Turn off PIN: *"why would claire or anyone but me have
        // the option to change the pin or turn it off"*. To change it while
        // somebody else is signed in, the owner switches in, which asks for it.
        {
            auto& rows = cats.back().rows;
            const char* protects = "Protects accounts, Wi-Fi, sign out and file access";
            const bool isOwner = accounts::activeId() == accounts::ownerId();
            const std::vector<accounts::Account> list = accounts::all();
            const accounts::Account* owner = accounts::find(list, accounts::ownerId());
            const std::string ownerName = owner ? owner->name : std::string("the owner");
            if (isOwner && accounts::pinIsSet()) {
                rows.push_back({K::Action, SetPinChange, "Change PIN", protects, ""});
                rows.push_back({K::Action, SetPinOff, "Turn off PIN", "", ""});
            } else if (isOwner) {
                rows.push_back({K::Action, SetPinSet, "Set a PIN", protects, ""});
            } else if (accounts::pinIsSet()) {
                rows.push_back({K::Info, 0, "PIN", "Set by " + ownerName, "On"});
            } else {
                rows.push_back({K::Info, 0, "PIN", "Only " + ownerName + " can set one", "Off"});
            }
            rows.push_back({K::Unbuilt, 0, "RetroAchievements",
                            "Sign in with your own RetroAchievements account", ""});
        }

        cats.push_back({"Controllers", {
            {K::Unbuilt, 0, "Connected controllers", "Which controller is which player", ""},
            {K::Unbuilt, 0, "Add a controller", "The same pairing screen as first run", ""},
            {K::Unbuilt, 0, "Button mapping",
             "For controllers the console does not recognise", ""},
        }});

        std::string netValue = "Checking\xE2\x80\xA6", netDetail;
        {
            std::lock_guard<std::mutex> lk(settingsNet.m);
            if (settingsNet.have) {
                const net::Status& st = settingsNet.st;
                if (st.managerMissing) netValue = "Unknown";
                else if (!st.online) netValue = "Not connected";
                else if (st.link == net::Link::Ethernet) netValue = "Connected over Ethernet";
                else if (st.link == net::Link::WiFi)
                    netValue = "Connected to " + (st.connection.empty() ? std::string("Wi-Fi")
                                                                        : st.connection);
                else netValue = "Connected";
                if (!st.ipv4.empty()) {
                    // "192.168.1.212/24" -> the address a person would type.
                    netDetail = "Address " + st.ipv4.substr(0, st.ipv4.find('/'));
                }
            }
        }
        cats.push_back({"Network", {
            {K::Info, 0, "Status", netDetail, netValue},
            {K::Action, SetServer, "RomM server", "", rommAddress ? rommAddress : ""},
        }});
        // ONE WI-FI ROW, showing the network in use. The networks themselves
        // are in a panel it opens: as rows here they flooded the page in a
        // crowded building. MMagTech, 2026-09-24.
        {
            bool radioPresent = true, radioOn = true;
            std::string onWifi;
            {
                std::lock_guard<std::mutex> lk(settingsNet.m);
                if (settingsNet.have) {
                    radioPresent = settingsNet.st.wifiPresent;
                    radioOn = settingsNet.st.wifiEnabled;
                }
            }
            {
                std::lock_guard<std::mutex> lk(settingsWifi.m);
                for (const net::Network& n : settingsWifi.list)
                    if (n.active) onWifi = n.ssid;
            }
            if (radioPresent)
                cats.back().rows.push_back(
                    {K::Action, SetWifi, "Wi-Fi", "",
                     !radioOn ? "Off" : (onWifi.empty() ? "Not connected" : onWifi)});
        }

        cats.push_back({"Display and Sound", {
            {K::Unbuilt, 0, "Picture quality",
             "Performance, Balanced or Quality, for the whole console", ""},
            [] {
                Row r{K::Choice, SetInterfaceSounds, "Interface sounds",
                      "The clicks when you move around the menus", ""};
                for (int i = 0; i < sound::kLevelCount; ++i)
                    r.choices.push_back(sound::levelName(static_cast<sound::Level>(i)));
                r.choice = static_cast<int>(sound::level());
                return r;
            }(),
            // Here, not under System: it is about the screen, and it is where a
            // person looks for it. MMagTech, 2026-09-24.
            [&] {
                // A FACT THE ROW CANNOT OTHERWISE SHOW, not an explanation:
                // the dim is fixed and nothing else on screen says when.
                Row r{K::Choice, SetScreenOff, "Turn off screen after", "Dims after 5 minutes",
                      ""};
                for (int i = 0; i < kScreenOffCount; ++i) r.choices.push_back(kScreenOff[i].name);
                r.choice = screenOffIndex;
                return r;
            }(),
        }});

        // THE DRIVES, by MMagTech's names: the main drive is "CabinetOS", any
        // other internal disk "Internal", one that can be unplugged
        // "External". With two of one kind, counting drives that cannot be
        // used, the drive's own name tells them apart: its label for one in
        // use, its model for one that is not. THE DRIVE'S OWN ROW IS THE
        // BUTTON: an External drive in use offers Eject, a blank drive offers
        // Format, and there are no separate action rows, so there is never a
        // question of which drive a row means. MMagTech on the TV, 2026-09-25:
        // a Format row under the drive "can make me feel like I'm formatting
        // something that isn't the unformatted drive".
        std::vector<Row> store;
        const std::vector<std::string> locs = storage::locations();
        const std::vector<drives::Unusable> unusableDrives = drives::unusable();
        int internals = 0, externals = 0;
        for (size_t i = 1; i < locs.size(); ++i)
            (storage::isExternal(locs[i]) ? externals : internals)++;
        for (const drives::Unusable& u : unusableDrives) (u.external ? externals : internals)++;
        const std::map<std::string, std::string> locNames = locationNames(locs, unusableDrives);
        for (size_t i = 0; i < locs.size(); ++i) {
            const std::string name = locNames.at(locs[i]);
            const bool external = i > 0 && storage::isExternal(locs[i]);
            // THE SPACE ON THE SECOND LINE AND THE ACTION AS THE VALUE, so
            // the row says what pressing it does: a chevron alone only says
            // "more". MMagTech on the TV, 2026-09-25.
            const storage::Space sp = storage::spaceOf(locs[i]);
            const std::string space = sp.ok ? gb(sp.freeBytes) + " free of " + gb(sp.totalBytes)
                                            : std::string("Unknown");
            const std::string value =
                !external ? "" : drives::ejecting() ? "Ejecting\xE2\x80\xA6" : "Eject";
            store.push_back({external ? K::Action : K::Info,
                             external ? kSetEject + static_cast<int>(i) : 0, name, space, value});
            if (external) driveNames[kSetEject + static_cast<int>(i)] = name;
        }
        // DRIVES FOUND AND NOT USABLE, greyed, with the reason. A new internal
        // SSD arrives blank; without this row it would be invisible, because
        // internal drives get no notices. A blank one has Format under it.
        formatable.clear();
        for (const drives::Unusable& u : unusableDrives) {
            std::string name = u.external ? "External" : "Internal";
            if ((u.external ? externals : internals) > 1 && !u.model.empty())
                name += " (" + u.model + ")";
            const std::string size = u.sizeBytes ? gb(static_cast<int64_t>(u.sizeBytes)) : "";
            if (u.blank) {
                store.push_back({K::Action, kSetFormat + static_cast<int>(formatable.size()),
                                 name, size.empty() ? "Blank" : "Blank \xC2\xB7 " + size,
                                 drives::formatting() ? "Formatting\xE2\x80\xA6" : "Format"});
                formatable.push_back(u);
            } else {
                std::string why = u.wrongFormat ? "Isn't exFAT or NTFS" : "Couldn't use this drive";
                if (!size.empty()) why += " \xC2\xB7 " + size;
                store.push_back({K::Disabled, 0, name, why, ""});
            }
        }
        // DOWNLOADS, #68: everyone's downloaded games, behind the PIN. The
        // cache is not shown. The count and size on the second line, like a
        // drive's space; greyed with nothing downloaded. docs/SETTINGS.md,
        // Storage.
        {
            const std::vector<screens::DownloadItem> dl = downloadItems();
            int64_t bytes = 0;
            for (const screens::DownloadItem& d : dl) bytes += d.bytes;
            if (dl.empty())
                store.push_back({K::Disabled, 0, "Downloads", "None", ""});
            else
                store.push_back({K::Action, SetDownloads, "Downloads",
                                 screens::countText(static_cast<int>(dl.size()), bytes), ""});
        }
        // FILE ACCESS: ONE ROW. What a computer needs to reach it (address,
        // user name, the password in plain text) is in a panel the row opens,
        // as Wi-Fi's networks are: as rows under Storage they ran off the
        // bottom of the screen. MMagTech on the TV, 2026-09-25. SFTP, port
        // 22, user `cabinet`, the saves and games folders only.
        {
            std::string value = filesState.on ? "On" : "Off";
            std::string detail = "SFTP";
            if (filesSaidAt != 0) value = filesWant ? "Turning on\xE2\x80\xA6" : "Turning off\xE2\x80\xA6";
            else if (filesState.failed) detail = filesState.reason;
            store.push_back({K::Toggle, SetFiles, "File access", detail, value});
        }
        cats.push_back({"Storage", std::move(store)});

        cats.push_back({"System", {
            updateRow(),
            [&] {
                // Weekly only ever checks, never downloads.
                Row r{K::Choice, SetUpdateCheck, "Check for updates", "", ""};
                r.choices = {"Manual", "Weekly"};
                r.choice = updWeekly ? 1 : 0;
                return r;
            }(),
        }});

        // VERSION: the date version, with Bazzite's under it. A console that
        // does not follow `latest` says which tag it does follow, which is how
        // a test console is told from the rest at a glance; nothing here
        // changes it. MMagTech, 2026-09-25.
        {
            std::string version = update::bootedVersion();
            if (version.empty()) version = "Unknown";
            const std::string ch = update::channel();
            if (ch == "testing") version += " \xC2\xB7 Testing";
            else if (!ch.empty() && ch != "latest") version += " \xC2\xB7 " + ch;
            const std::string base = update::baseVersion();
            cats.push_back({"About", {
                {K::Info, 0, "Version", base.empty() ? "" : "Bazzite " + base, version},
                {K::Action, SetCredits, "Credits and licences", "", ""},
            }});
        }


        settingsScreen.setCategories(std::move(cats));
    };
    // Start root's check or download, and say so at once rather than when
    // root's first write lands. A refusal is said in the row: the unit is not
    // in this image, or polkit said no.
    auto startUpdate = [&](bool download, bool automatic) {
        std::string why;
        update::Status said;
        said.download = download;
        said.at = std::time(nullptr);
        if (update::start(download, &why)) {
            said.state = update::State::Checking;
        } else {
            said.state = update::State::Failed;
            updAskAfterCheck = false;   // no check is coming to answer it
            said.reason = why.find("not found") != std::string::npos ? "Not in this image" : why;
            if (said.reason.size() > 60) said.reason.resize(60);
        }
        upd = said;
        updSaidAt = said.at;
        updAuto = automatic;
        if (here() == Screen::Settings) buildSettings();
    };
    // "UPDATE AVAILABLE": DOWNLOAD OR LATER. A check that finds something
    // used to stop at the row, and the second press that downloads was
    // nowhere on screen. MMagTech on the TV, 2026-09-25: *"i have to click
    // again even though it doesnt indicate it."* Now the check a person
    // pressed opens this, the row opens it again, and Download goes to the
    // PIN. The same shape as "Update ready", so an update is: check,
    // Download, Restart now.
    auto askUpdateDownload = [&]() {
        std::string version = upd.version, detail;
        int64_t size = upd.size;
        if (upd.state != update::State::Available) {
            version = prefs::get("update_found", "");
            size = prefNum("update_size");
        }
        detail = version + " \xC2\xB7 " + updBytes(size);
        askChoice("Update available", detail, {"Download", "Later"}, 0, [&](int k) {
            if (k != 0) return;
            askPin("Enter the PIN", "To update",
                   [&]() { startUpdate(/*download=*/true, /*automatic=*/false); });
        });
    };
    // "Update ready", once per staged version. Restart now is the Power
    // menu's Restart: logind holds the machine while saves upload (PR #52),
    // which is the "waits for saves" Sign out has. Later leaves it staged for
    // the next Restart or Power off, and the row says "Restart to update".
    auto askUpdateReady = [&]() {
        askChoice("Update ready", upd.version, {"Restart now", "Later"}, 0, [&](int k) {
            if (k != 0) return;
            std::fprintf(stderr, "[update] restart now, into %s\n", upd.version.c_str());
            power::act(power::Action::Restart);
        });
    };
    // File access on or off. A refusal is said in the row, as the update
    // row does; otherwise "Turning on…" until root answers.
    auto setFiles = [&](bool on) {
        std::string why;
        const bool ok = on ? files::turnOn(&why) : files::turnOff(&why);
        if (!ok) {
            filesState.on = false;
            filesState.failed = true;
            filesState.reason = why.find("not found") != std::string::npos ? "Not in this image" : why;
            if (filesState.reason.size() > 60) filesState.reason.resize(60);
            filesSaidAt = 0;
            filesPanelWhenOn = false;
        } else {
            filesSaidAt = std::time(nullptr);
        }
        filesWant = on;
        prefs::set("file_access", on ? "on" : "off");
        buildSettings();
    };
    // THE PANEL: everything a computer needs, the password in plain text
    // (anyone at the television can already turn this on or make a new
    // one), then Done, New password, Turn off. Focus on Done.
    std::function<void()> openFilesPanel;
    openFilesPanel = [&]() {
        std::string ip;
        {
            std::lock_guard<std::mutex> lk(settingsNet.m);
            if (settingsNet.have && !settingsNet.st.ipv4.empty())
                ip = settingsNet.st.ipv4.substr(0, settingsNet.st.ipv4.find('/'));
        }
        char host[256] = {0};
        ::gethostname(host, sizeof host - 1);
        std::string where = ip;
        if (host[0]) where += (where.empty() ? "" : " \xC2\xB7 ") + std::string(host) + ".local";
        const std::string pass = files::password();
        askChoice("File access",
                  where + "\nUser name  cabinet\nPassword  " + (pass.empty() ? "\xE2\x80\xA6" : pass),
                  {"Done", "New password", "Turn off"}, 0, [&](int k) {
                      if (k == 2) {
                          setFiles(false);
                      } else if (k == 1) {
                          // The old one stops working on every computer that
                          // saved it: asked once, focus on Cancel. Then the
                          // panel comes back with the new one.
                          askPin("Enter the PIN", "To make a new password", [&]() {
                              askChoice("New password?", "", {"New password", "Cancel"}, 1,
                                        [&](int c) {
                                            if (c != 0) { openFilesPanel(); return; }
                                            std::string why;
                                            if (!files::newPassword(&why)) {
                                                menuNotice.say("Couldn't make a new password",
                                                               Tone::Problem);
                                                return;
                                            }
                                            filesPanelWhenOn = true;
                                            filesSaidAt = std::time(nullptr);
                                        });
                          });
                      }
                  });
    };
    askWifiPassword = [&](const std::string& ssid, bool forgetFirst, const std::string& why) {
        ui::Keyboard::Config cfg;
        cfg.title = ssid;
        cfg.placeholder = "Password";
        cfg.conceal = true;   // masked, last character shown; keyboard.h
        // Opened over itself on a retry, in the same frame: no close, no cut.
        // WHY GOES IN THE FIELD ("Wrong password", shaken), not in a line
        // under the title that made the panel grow. Keyboard::sayInField.
        keyboard.open(cfg);
        if (!why.empty()) keyboard.sayInField(why, /*problem=*/true);
        keyboardThen = [&, ssid, forgetFirst, cfg](ui::KeyboardResult r) {
            if (r != ui::KeyboardResult::Committed) return;
            const std::string pass = keyboard.value();
            // THE KEYBOARD STAYS UP WHILE IT JOINS, saying so, and takes no
            // typing (Keyboard::setBusy). The job's answer closes it or turns
            // it back into a retry; B leaves and lets the join finish alone.
            keyboard.open(cfg);
            keyboard.sayInField("Joining\xE2\x80\xA6", /*problem=*/false);
            keyboard.setBusy(true);
            wifiKeyboardFor = ssid;
            keyboardThen = [&](ui::KeyboardResult) { wifiKeyboardFor.clear(); };
            startWifiJoin(ssid, pass, forgetFirst);
            buildSettings();
        };
    };

    // THE CURTAIN COMES DOWN ON THE STARTUP SCREEN, and pumpLeave does the
    // rest once it is down.
    auto startLeaving = [&](Leave what, const std::string& label) {
        leaving = what;
        leaveLabel = label;
        leaveDownAt = 0;
        accountsOpen = false;
        barFocused = false;
        curtain.retarget(1.0f, kCurtainDown);
        sound::play(sound::Cue::Activate);
        std::fprintf(stderr, "[server] %s\n", label.c_str());
    };

    askServerAddress = [&](const std::string& typed, const std::string& why) {
        const ui::Keyboard::Config cfg = serverKeyboardConfig(typed);
        keyboard.open(cfg);
        if (!why.empty()) keyboard.sayInField(why, /*problem=*/true, /*keep=*/true);
        keyboardThen = [&, cfg](ui::KeyboardResult r) {
            if (r != ui::KeyboardResult::Committed) return;
            const std::string addr = trimmedAddress(keyboard.value());
            // The same address, or none: nothing to change.
            if (addr.empty() || addr == (rommAddress ? rommAddress : "")) return;
            ui::Keyboard::Config again = cfg;
            again.initial = addr;
            keyboard.open(again);
            keyboard.sayInField("Checking\xE2\x80\xA6", /*problem=*/false, /*keep=*/true);
            keyboard.setBusy(true);
            serverKeyboardFor = addr;
            keyboardThen = [&](ui::KeyboardResult) { serverKeyboardFor.clear(); };
            startServerCheck(addr);
        };
    };

    auto apply = [&](const screens::Result& res) {
        switch (res.action) {
            case screens::Action::None:
                break;
            case screens::Action::Back:
                if (accountsOpen) {
                    // Closes the panel and puts focus back ON THE CHIP, which
                    // is where it came from. Dropping focus into the screen
                    // underneath would lose the place the person was at.
                    accountsOpen = false;
                    barFocused = true;
                    barSlot = BarAccount;
                    sound::play(sound::Cue::Back);
                } else if (stack.size() > 1) {
                    stack.pop_back(); sound::play(sound::Cue::Back);
                } else sound::play(sound::Cue::Edge);
                break;
            case screens::Action::OpenTile: {
                const auto& tiles = libraryScreen.visible();
                if (res.value < 0 || res.value >= static_cast<int>(tiles.size())) break;
                // THE GAMES ARE FETCHED HERE AND NOWHERE EARLIER — open
                // question 28. Boot draws this screen having fetched no games
                // at all; a grid costs one request at the moment somebody
                // walks into it, and nothing at all for the systems they never
                // open. The second visit costs nothing: loadTileGames sees a
                // membership it already filled and returns.
                const bool isCollection = libraryScreen.tab() != 0;
                const int tileId = tiles[res.value].id;
                auto& own = isCollection ? collectionTiles : platformTiles;
                screens::Tile* t = nullptr;
                for (auto& x : own) if (x.id == tileId) { t = &x; break; }
                if (!t) break;
                bool more = false;
                if (t->cards.empty()) {
                    // ONE PAGE, AND IT BLOCKS FOR THAT. The person has chosen
                    // a system and is waiting for one request rather than for
                    // the whole platform — 0.19 s for 141 games here, and the
                    // same 0.19 s for a platform of thirty thousand.
                    loadTileGames(liveClient, lib, *t, isCollection, &more);
                    libraryScreen.learnedTile(tileId, t->detail, t->cover);
                }
                if (t->cards.empty()) {
                    // Nothing came back, or nothing in it can be played here.
                    // Refusing is better than opening onto an empty grid with
                    // no reason on it.
                    sound::play(sound::Cue::Edge);
                    break;
                }
                gridScreen.open(t->title, t->cards, cards);
                gridScreen.setLoadingMore(more);
                // ONE FILLER AT A TIME. Walking into a second platform stops
                // the first: nobody is looking at it any more, and two of
                // these would be appending into two different tiles at once.
                gridFill.stop();
                if (more) {
                    gridFill.tileId = tileId;
                    gridFill.running.store(true);
                    const std::string filter =
                        (isCollection ? "collection_id=" : "platform_ids=") +
                        std::to_string(tileId);
                    const size_t from = t->cards.size();
                    gridFill.th = std::thread([&gridFill, filter, from]() {
                        constexpr int kPage = 500;
                        size_t offset = from;
                        for (;;) {
                            if (gridFill.quit.load()) break;
                            std::vector<romm::Game> page;
                            std::string err;
                            int total = 0;
                            if (!liveClient.fetchRoms(
                                    filter + "&offset=" + std::to_string(offset),
                                    kPage, &page, &err, &total))
                                break;
                            if (page.empty()) break;
                            offset += page.size();
                            {
                                std::lock_guard<std::mutex> lk(gridFill.m);
                                for (auto& g : page)
                                    gridFill.arrived.push_back(std::move(g));
                            }
                            if (total > 0 && offset >= static_cast<size_t>(total)) break;
                        }
                        gridFill.running.store(false);
                    });
                }
                stack.push_back(Screen::Grid);
                sound::play(sound::Cue::Activate);
                break;
            }
            case screens::Action::SwitchAccount: {
                // THE REFUSALS BELONG TO THE APP AND SO DO THEIR WORDS. The
                // screen does not know whether a game is running or a save is
                // still going up; it asked to become somebody and this decides.
                const int id = res.value;
                // NOT SWITCHED HERE. The switch asks the server who this is and
                // reloads the library, which blocks the frame loop; done here,
                // the screen froze mid-panel for as long as that took (eleven
                // seconds once, on the A9). It is handed to pumpSwitch, which
                // brings the curtain down with the person's name on it first.
                auto go = [&, id]() {
                    const std::vector<accounts::Account> list = accounts::all();
                    const accounts::Account* a = accounts::find(list, id);
                    switchPendingId = id;
                    switchCurtain = true;
                    switchLabel = "Switching to " + (a ? a->name : std::string("them"));
                    switchCurtainFrames = 0;
                    accountsOpen = false;
                    barFocused = false;
                    accountScreen.setNotice("");
                    curtain.retarget(1.0f, kCurtainDown);
                    sound::play(sound::Cue::Activate);
                };
                // THE SAME PIN STOPS ANYBODY SWITCHING INTO THE OWNER.
                // docs/SETTINGS.md, Accounts. With no PIN, askPin just goes.
                if (id == accounts::ownerId() && accounts::activeId() != id) {
                    const std::vector<accounts::Account> list = accounts::all();
                    const accounts::Account* a = accounts::find(list, id);
                    askPin("Enter the PIN",
                           "To switch to " + (a ? a->name : std::string("them")), go,
                           /*always=*/true);
                } else {
                    go();
                }
                break;
            }
            case screens::Action::AddAccount:
                // WITH A PIN SET, ADDING SOMEBODY ASKS FOR IT, from here and
                // from Settings alike. MMagTech, 2026-09-24: without it,
                // anyone holding the controller could add themselves; with no
                // PIN, anyone can, which is the no-PIN rule everywhere.
                askPin("Enter the PIN", "To add an account", [&]() {
                    // THE PANEL CLOSES AND A SCREEN OPENS. Leaving the panel
                    // up behind a pairing code would put the list somebody is
                    // about to change underneath the thing changing it.
                    accountsOpen = false;
                    barFocused = false;
                    addAccountScreen.open();
                    stack.push_back(Screen::AddAccount);
                    startAddAccount();
                });
                sound::play(sound::Cue::Activate);
                break;
            case screens::Action::Setting:
                if (res.value >= kSetFormat) {
                    const size_t i = static_cast<size_t>(res.value - kSetFormat);
                    if (drives::formatting() || i >= formatable.size()) {
                        sound::play(sound::Cue::Edge);
                        break;
                    }
                    // THREE STEPS, MMagTech 2026-09-25: the PIN if set; the
                    // drive by name and size with Cancel focused, so a drive
                    // wrongly read as blank is recognised before anything
                    // happens; then a code the console chose. The only thing
                    // on the console that erases anything.
                    const drives::Unusable u = formatable[i];
                    const std::string what =
                        std::string(u.external ? "External" : "Internal") +
                        (u.model.empty() ? "" : ", " + u.model) +
                        (u.sizeBytes ? ", " + driveSize(static_cast<int64_t>(u.sizeBytes)) : "");
                    askPin("Enter the PIN", "To format a drive", [&, u, what]() {
                        askChoice("Format this drive?", what, {"Cancel", "Format"}, 0,
                                  [&, u](int k) {
                            if (k != 1) return;
                            char code[8];
                            std::snprintf(code, sizeof code, "%04u",
                                          static_cast<unsigned>(SDL_rand(10000)));
                            askCode(code, std::string("Enter ") + code, "To format it as exFAT",
                                    [&, u]() {
                                std::fprintf(stderr, "[drives] format asked for %s\n",
                                             u.id.c_str());
                                drives::format(u.id);
                                buildSettings();
                            });
                        });
                    });
                    sound::play(sound::Cue::Activate);
                } else if (res.value >= kSetEject) {
                    const std::vector<std::string> locs = storage::locations();
                    const size_t i = static_cast<size_t>(res.value - kSetEject);
                    if (drives::ejecting() || i >= locs.size() || !storage::isExternal(locs[i])) {
                        sound::play(sound::Cue::Edge);
                        break;
                    }
                    // The drive's row was pressed: Eject is asked, with the
                    // drive named, and Cancel beside it.
                    const std::string loc = locs[i];
                    const std::string ejName =
                        driveNames.count(res.value) ? driveNames[res.value] : "External";
                    askChoice(ejName, "", {"Eject", "Cancel"}, 0, [&, loc](int k) {
                    if (k != 0) return;
                    // FINISHES OR STOPS ANYTHING WRITING TO IT. The only thing
                    // that writes to a drive is a download being kept there;
                    // it is stopped and waited for, so nothing holds a file
                    // open on the drive. Its stage is left as the worker set
                    // it, so the frame loop undoes the keep as for any failed
                    // download.
                    if (launchJob.busy() && launchJob.entryPath.rfind(loc + "/", 0) == 0) {
                        std::fprintf(stderr, "[drives] eject: stopping the download of %s\n",
                                     launchJob.title.c_str());
                        launchJob.stop();
                    }
                    drives::eject(loc);
                    buildSettings();
                    });
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetAddAccount) {
                    // The same route as the chip's Add user, PIN included.
                    // Back from the pairing screen returns here, because it
                    // is pushed.
                    askPin("Enter the PIN", "To add an account", [&]() {
                        addAccountScreen.open();
                        stack.push_back(Screen::AddAccount);
                        startAddAccount();
                    });
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetWifi) {
                    // PIN FIRST, then either the radio on or the list of
                    // networks in the question panel, scrolling past six.
                    askPin("Enter the PIN", "To change Wi-Fi", [&]() {
                        bool radioOn = true;
                        {
                            std::lock_guard<std::mutex> lk(settingsNet.m);
                            if (settingsNet.have) radioOn = settingsNet.st.wifiEnabled;
                        }
                        if (!radioOn) {
                            std::string err;
                            if (!net::setRadio(true, &err))
                                menuNotice.say("Couldn't turn on Wi-Fi", Tone::Problem);
                            askNetwork();
                            askWifi();
                            buildSettings();
                            return;
                        }
                        // A password, typed on the on-screen keyboard, then the
                        // join on a worker. `forgetFirst` is Change password.
                        auto typeAndJoin = [&](const net::Network& net, bool forgetFirst) {
                            askWifiPassword(net.ssid, forgetFirst, "");
                        };
                        auto join = [&, typeAndJoin](const net::Network& net) {
                            if (net.secured && !net.known) { typeAndJoin(net, false); return; }
                            menuNotice.say("Joining " + net.ssid, Tone::Busy);
                            startWifiJoin(net.ssid, "", false);
                            buildSettings();
                        };
                        auto forget = [&](const net::Network& net) {
                            askChoice("Forget " + net.ssid + "?", "", {"Forget", "Cancel"}, 1,
                                      [&, net](int i) {
                                          if (i != 0) return;
                                          std::string err;
                                          if (net::forget(net.ssid, &err)) {
                                              menuNotice.say("Forgot " + net.ssid, Tone::Done);
                                              // AT ONCE, not when the next scan
                                              // lands: joining a stale "saved"
                                              // network sent no password.
                                              std::lock_guard<std::mutex> lk(settingsWifi.m);
                                              for (net::Network& w : settingsWifi.list)
                                                  if (w.ssid == net.ssid)
                                                      w.known = w.active = false;
                                          }
                                          else
                                              menuNotice.say("Couldn't forget " + net.ssid,
                                                             Tone::Problem);
                                          std::fprintf(stderr, "[wifi] forget %s: %s\n",
                                                       net.ssid.c_str(),
                                                       err.empty() ? "done" : err.c_str());
                                          askNetwork();
                                          askWifi();
                                          buildSettings();
                                      });
                        };
                        sortWifi();
                        std::vector<std::string> names, values;
                        wifiPanelRows(&names, &values);
                        askChoice("Wi-Fi",
                                  names.empty() ? "Looking for networks\xE2\x80\xA6" : "",
                                  names, 0, [&, typeAndJoin, join, forget](int i) {
                                      wifiPanelOpen = false;
                                      if (i < 0 || i >= static_cast<int>(wifiShown.size()))
                                          return;
                                      const net::Network n = wifiShown[i];
                                      if (wifiJob.running.load()) {
                                          sound::play(sound::Cue::Edge);
                                          return;
                                      }
                                      if (n.active) {
                                          askChoice(n.ssid, "",
                                                    {"Change password", "Forget", "Cancel"}, 0,
                                                    [&, n, typeAndJoin, forget](int k) {
                                                        if (k == 0) typeAndJoin(n, true);
                                                        else if (k == 1) forget(n);
                                                    });
                                      } else if (n.known) {
                                          askChoice(n.ssid, "",
                                                    {"Join", "Change password", "Forget",
                                                     "Cancel"},
                                                    0, [&, n, typeAndJoin, join, forget](int k) {
                                                        if (k == 0) join(n);
                                                        else if (k == 1) typeAndJoin(n, true);
                                                        else if (k == 2) forget(n);
                                                    });
                                      } else {
                                          join(n);
                                      }
                                  });
                        choiceScreen.setValues(values);
                        wifiPanelOpen = true;
                        askWifi();
                    });
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetFiles) {
                    if (filesSaidAt != 0) {
                        sound::play(sound::Cue::Edge);
                    } else if (filesState.on) {
                        openFilesPanel();
                        sound::play(sound::Cue::Activate);
                    } else {
                        // On behind the PIN, then the panel opens by itself
                        // once root says it is on.
                        askPin("Enter the PIN", "To turn on file access", [&]() {
                            filesPanelWhenOn = true;
                            setFiles(true);
                        });
                        sound::play(sound::Cue::Activate);
                    }
                } else if (res.value == SetDownloads) {
                    // AN ADMIN SCREEN: the PIN to open it, once per Settings
                    // visit, so clearing several games is one PIN. MMagTech,
                    // 2026-09-25. Removing your own needs none, from the
                    // game's own screen.
                    askPin("Enter the PIN", "To open Downloads", [&]() {
                        std::vector<screens::DownloadItem> dl = downloadItems();
                        if (dl.empty()) { buildSettings(); return; }
                        downloadsPanel.open(std::move(dl));
                    });
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetCredits) {
                    // A list, one line per project: what it does, and its
                    // licence. Nothing to choose; A or B closes it.
                    std::vector<std::string> names, values;
                    for (const Credit& cr : kCredits) {
                        names.push_back(cr.name);
                        values.push_back(cr.what);
                    }
                    askChoice("Credits and licences", "", names, 0, [](int) {});
                    choiceScreen.setValues(values);
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetUpdate) {
                    // ONE ROW, AND WHAT IT DOES IS WHAT IT SAYS: check, fetch
                    // (behind the PIN: an update can be gigabytes and replaces
                    // the system, so the owner decides when), restart, or
                    // retry. Busy, it does nothing. docs/SETTINGS.md, System.
                    const bool available = upd.state == update::State::Available ||
                                           (upd.state == update::State::None && updFoundNewer());
                    const bool retryDownload = upd.state == update::State::Failed && upd.download;
                    if (upd.busy()) {
                        sound::play(sound::Cue::Edge);
                    } else if (upd.state == update::State::Ready) {
                        askUpdateReady();
                        sound::play(sound::Cue::Activate);
                    } else if (available) {
                        askUpdateDownload();
                        sound::play(sound::Cue::Activate);
                    } else if (retryDownload) {
                        askPin("Enter the PIN", "To update",
                               [&]() { startUpdate(/*download=*/true, /*automatic=*/false); });
                        sound::play(sound::Cue::Activate);
                    } else {
                        updAskAfterCheck = true;
                        startUpdate(/*download=*/false, /*automatic=*/false);
                        sound::play(sound::Cue::Activate);
                    }
                } else if (res.value == SetServer) {
                    // PIN FIRST, then what to do: the Wi-Fi row's shape.
                    // docs/SETTINGS.md, Network; issues #60 and #61.
                    askPin("Enter the PIN", "To change the RomM server", [&]() {
                        const std::string addr = rommAddress ? rommAddress : "";
                        askChoice(addr, "", {"Change address", "Sign out", "Cancel"}, 0,
                                  [&, addr](int i) {
                            if (i == 0) {
                                // Root's address outranks ours (firstrun.h).
                                // Only a console set up by hand has one.
                                if (!server::addressIsOurs()) {
                                    menuNotice.say("Set in /etc/cabinetos/session.env",
                                                   Tone::Problem);
                                    return;
                                }
                                askServerAddress(addr, "");
                            } else if (i == 1) {
                                // ONE CONFIRMATION, focus on Cancel. Unsent
                                // saves are said, not counted (MMagTech,
                                // 2026-09-25); by design there are none.
                                const bool unsent = server::unsentSaves() > 0;
                                askChoice("Sign out?",
                                          std::string("Removes every game and account from "
                                                      "this console.\n") +
                                              (unsent ? "Saves waiting to upload will be lost."
                                                      : "Saves stay on the server."),
                                          {"Sign out", "Cancel"}, 1, [&](int k) {
                                              if (k == 0)
                                                  startLeaving(Leave::SignOut, "Signing out");
                                          });
                            }
                        });
                    });
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetRemoveAccount) {
                    // PIN (if set), then who, then are you sure. With one
                    // person to remove, "who" is skipped.
                    askPin("Enter the PIN", "To remove an account", [&]() {
                        auto confirm = [&](accounts::Account who) {
                            // NO EXPLANATION. MMagTech, 2026-09-24: the people
                            // using this are technical, "we don't need to spoon
                            // feed them everything with an explanation".
                            askChoice("Remove " + who.name + "?", "",
                                      {"Remove", "Cancel"}, 1, [&, who](int i) {
                                          if (i != 0) return;
                                          std::string err;
                                          if (accounts::remove(who.id, &err))
                                              std::fprintf(stderr,
                                                           "[accounts] removed %d - %s, "
                                                           "now %zu accounts\n",
                                                           who.id, who.name.c_str(),
                                                           accounts::all().size());
                                          else
                                              std::fprintf(stderr,
                                                           "[accounts] could not remove "
                                                           "%d: %s\n", who.id, err.c_str());
                                          refreshAccountRows();
                                          buildSettings();
                                      });
                        };
                        const std::vector<accounts::Account> people = removableAccounts();
                        if (people.empty()) return;
                        if (people.size() == 1) { confirm(people[0]); return; }
                        std::vector<std::string> names;
                        for (const auto& p : people) names.push_back(p.name);
                        names.push_back("Cancel");
                        askChoice("Remove an account", "", names, 0,
                                  [&, people, confirm](int i) {
                                      if (i >= 0 && i < static_cast<int>(people.size()))
                                          confirm(people[i]);
                                  });
                    });
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetPinSet) {
                    choosePin("Choose a PIN", "",
                              [&]() { buildSettings(); });
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetPinChange) {
                    askPin("Enter your current PIN", "", [&]() {
                        choosePin("Choose a new PIN", "",
                                  [&]() { buildSettings(); });
                    });
                    sound::play(sound::Cue::Activate);
                } else if (res.value == SetPinOff) {
                    askPin("Enter the PIN", "To turn it off", [&]() {
                        std::string err;
                        if (accounts::setPin("", &err))
                            std::fprintf(stderr, "[pin] turned off\n");
                        buildSettings();
                    });
                    sound::play(sound::Cue::Activate);
                }
                break;
            case screens::Action::SettingChoice:
                if (res.value == SetUpdateCheck) {
                    updWeekly = settingsScreen.choiceOf(SetUpdateCheck) == 1;
                    prefs::set("update_check", updWeekly ? "weekly" : "manual");
                    sound::play(sound::Cue::Move);
                }
                if (res.value == SetScreenOff) {
                    const int i = settingsScreen.choiceOf(SetScreenOff);
                    if (i >= 0 && i < kScreenOffCount) {
                        screenOffIndex = i;
                        prefs::set("screen_off_after", kScreenOff[i].word);
                    }
                    sound::play(sound::Cue::Move);
                }
                if (res.value == SetInterfaceSounds) {
                    const int i = settingsScreen.choiceOf(SetInterfaceSounds);
                    if (i >= 0 && i < sound::kLevelCount) {
                        const auto lv = static_cast<sound::Level>(i);
                        sound::setLevel(lv);
                        prefs::set("interface_sounds", sound::levelWord(lv));
                    }
                    // AFTER the change, so each step is heard at the level it
                    // just chose, which is how a person picks one. Off is
                    // silent, which says it too.
                    sound::play(sound::Cue::Move);
                }
                break;
            case screens::Action::FocusKeyboard:
                // Back into the keyboard under the results. Focus does not
                // leave the screen, it moves down within it.
                searchScreen.setFocused(false);
                sound::play(sound::Cue::Move);
                break;
            case screens::Action::FocusBar:
                // Where the cursor lands in the bar: on the destination you are
                // standing in, so walking up and straight back down is a no-op
                // rather than a silent change of where you would go.
                barSlot = (here() == Screen::Library || here() == Screen::Grid)
                              ? BarLibrary
                              : (here() == Screen::Settings ? BarSettings : 0);
                barFocused = true;
                sound::play(sound::Cue::Move);
                break;
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
                refreshKeeps();
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
        // THE FIRST CARD ON RECENT IS RESUME, and it goes straight into the
        // game. This used to be a pill on the hero, and the objection recorded
        // against a shelf card launching directly was that it would be "a
        // second Resume that nothing on the screen says is one". That was
        // right, and it is answered rather than ignored: the shelf header says
        // Resume over this card and the card carries a play mark. It is the
        // only card on Home that behaves this way.
        //
        // Home promises one action from cold to playing, and this is it.
        if (haveResume() && focusRow == RowRecent && focusSlot == 0) {
            launchById(cards[heroIndex].id);
            return;
        }
        // Every other cover on Home opens the launch screen, the same as a
        // cover anywhere else.
        if (const Card* c = cardAt(focusRow, focusSlot)) {
            for (size_t i = 0; i < cards.size(); ++i) {
                if (cards[i].id == c->id) { openDetail(static_cast<int>(i)); return; }
            }
        }
    };

    // THE SHOULDERS SWITCH DESTINATION — 2026-09-21.
    //
    // MMagTech: *"i also think that r1 and l1 should allow moving between the
    // top rows."* They do now, and they do it product-wide rather than only on
    // Home, which is the point of them: the top bar was a row of links a person
    // had to TRAVEL to — up out of the shelves, across, press A — and the
    // bumpers make it a tab strip reachable from wherever you are. Every
    // console does this and the shoulders were unmapped in the whole product.
    //
    // ONLY AT THE TOP LEVEL. Home and Library are destinations; the grid of a
    // platform and a game's launch screen are places you went INSIDE one, and a
    // bumper that teleported out of them would lose somebody's place rather
    // than move them. Back is what leaves those, and it already does.
    //
    // NO WRAPPING. With two destinations built, wrapping would make L1 and R1
    // do the same thing, which teaches nobody anything. When Search and
    // Settings exist this walks four and the question can be asked again.
    // GOING TO A DESTINATION RESETS TO ITS ROOT, which is what makes the bar a
    // tab strip rather than a stack of pushes. Walking into a platform's grid
    // and then pressing R1 twice must not leave that grid buried under two more
    // screens: each destination is entered fresh and Back from any of them
    // returns to Home.
    //
    // 0 Home, 1 Library, 2 Search, 3 Settings.
    auto goToDestination = [&](int d) {
        if (keyboard.isOpen()) keyboard.cancel();
        stack.clear();
        stack.push_back(Screen::Home);
        if (d == 1) {
            libraryScreen.enter();
            stack.push_back(Screen::Library);
        } else if (d == 2) {
            searchScreen.open();
            searchTyped.clear();
            stack.push_back(Screen::Search);
            ui::Keyboard::Config cfg;
            cfg.title = "Search";
            cfg.placeholder = "Game name";
            cfg.dockedBottom = true;
            keyboard.open(cfg);
        } else if (d == 3) {
            // Rebuilt on every visit: the drives, the network and the account
            // are read fresh, never remembered.
            buildSettings();
            askNetwork();
            askWifi();
            settingsScreen.enter();
            stack.push_back(Screen::Settings);
        }
    };

    auto destinationHere = [&]() {
        switch (here()) {
            case Screen::Home: return 0;
            case Screen::Library:
            case Screen::Grid: return 1;
            case Screen::Search: return 2;
            case Screen::Settings: return 3;
            default: return -1;      // the launch screen is inside a destination
        }
    };

    // SWITCHING TOP-BAR DESTINATIONS IS A DISSOLVE — 2026-09-24.
    //
    // MMagTech: *"the switch between top tabs is too quick and visually
    // snappy"*, most of all Search to Settings. It was a cut out and a fade in.
    // Tried the same afternoon and dropped on the television: fading the old
    // screen out and the new one in, with the background in between, then with
    // the background alongside. What he asked for in the end was the new
    // screen arriving see-through, with the old one showing through it.
    //
    // So the last frame of the old screen is copied at the moment of the
    // switch and drawn over the new one, fading away (Renderer::
    // captureSnapshot). Everything dissolves together: content, keyboard,
    // background and the bar's highlight. A press mid-dissolve copies the
    // frame as it is and starts again from there.
    //
    // THE OLD SCREEN GOES FIRST, THEN THE NEW ONE'S CONTENT ARRIVES. MMagTech,
    // same afternoon: "can the search dissolve more before it goes to
    // settings", because a solid keyboard dissolving straight into a page of
    // text read as one busy frame. So the copy fades out quickly, and the new
    // screen's content waits until it is mostly gone before fading in. The
    // new background comes with the dissolve, under the copy.
    //
    // Starting values, to be judged on the television.
    constexpr float kTabDissolve = 0.300f;      // the old frame, away
    constexpr float kTabContentDelay = 0.200f;  // then the new content starts
    constexpr float kTabContentIn = 0.300f;     // and takes this long
    constexpr float kTabArrive = kTabDissolve;  // the new background's fade
    float tabSince = -1.0f;      // seconds since the switch; negative when none
    // THE NEW CONTENT RISES THIS FAR as it fades in, and SEARCH'S KEYBOARD
    // SLIDES rather than dissolving: down and away as Search is left, up as it
    // arrives. Opacity alone read as one picture turning into another; a
    // little movement makes one screen leave and the next arrive.
    constexpr float kTabRise = 16.0f;
    constexpr float kKeyboardSlide = 0.300f;
    Animated keyboardSlide;
    keyboardSlide.smooth = true;
    bool snapTaken = false;
    int pendingDest = -1;
    Animated tabDissolve;
    tabDissolve.smooth = true;
    tabDissolve.from = tabDissolve.to = 0.0f;
    auto transitionTo = [&](int d) {
        if (shotMode) { goToDestination(d); return; }
        // Taken at the end of the next frame, which still shows the old screen.
        pendingDest = d;
    };
    // Where the console is, or is about to be.
    auto destinationGoing = [&]() {
        return pendingDest >= 0 ? pendingDest : destinationHere();
    };

    auto switchDestination = [&](int delta) {
        const int at = destinationGoing();
        if (at < 0) return;
        const int want = at + delta;
        // No wrapping: the ends make the edge sound, so a bumper that goes
        // nowhere says so rather than reading as broken.
        if (want < 0 || want > 3 || want == at) {
            sound::play(sound::Cue::Edge);
            return;
        }
        transitionTo(want);
        sound::play(want > at ? sound::Cue::Activate : sound::Cue::Back);
    };

    // THE BAR'S OWN KEYS. Offered every key first whenever it holds focus, from
    // whichever screen is underneath — which is the whole point of its cursor
    // living here rather than in a screen.
    auto barKey = [&](screens::Nav n) -> bool {
        switch (n) {
            case screens::Nav::Left:
            case screens::Nav::Right: {
                const int d = (n == screens::Nav::Right) ? 1 : -1;
                const int next = std::clamp(barSlot + d, 0, BarCount - 1);
                if (next == barSlot) { sound::play(sound::Cue::Edge); return true; }
                barSlot = next;
                // MOVING ACROSS THE BAR SWITCHES THE SCREEN UNDER IT — MMagTech,
                // 2026-09-24: the shoulders already did, and the d-pad made
                // you press A on each one. The Apple TV's top bar works this
                // way too: A only drops you into the screen.
                //
                // Only a MOVE switches. Arriving in the bar with Up does not,
                // or looking at the bar from Home would throw you off Home.
                // And the account chip opens a panel rather than going
                // anywhere, so sliding onto it opens nothing.
                if (barSlot != BarAccount) {
                    const int want = (barSlot == BarLibrary) ? 1
                                   : (barSlot == BarSearch ? 2 : 3);
                    if (destinationGoing() != want) {
                        transitionTo(want);
                        barFocused = true;
                    }
                }
                sound::play(sound::Cue::Move);
                return true;
            }
            // DOWN AND BACK BOTH LEAVE IT, and neither leaves the screen. Back
            // out of the bar going to the previous screen would mean a person
            // who walked up to look at it could not simply come back down.
            case screens::Nav::Down:
            case screens::Nav::Back:
                barFocused = false;
                sound::play(n == screens::Nav::Down ? sound::Cue::Move
                                                    : sound::Cue::Back);
                return true;
            case screens::Nav::Up:
                sound::play(sound::Cue::Edge);
                return true;
            case screens::Nav::Activate:
                if (barSlot == BarAccount) {
                    // WHO IS PLAYING. Rebuilt from the store every time it
                    // opens rather than cached: the list is three lines of
                    // JSON and a stale switcher is a switcher that signs the
                    // console in as somebody who has been removed.
                    //
                    // **barFocused STAYS TRUE, and setting it false was a bug.**
                    // MMagTech, 2026-09-22: *"when i click the user image the
                    // recent game expands like its being selected."* It was —
                    // dropping bar focus told everything underneath that focus
                    // had come back to it, so Home lit its card while the panel
                    // was open, and the chip that had just been pressed lost
                    // its own rim. Focus is on the chip; the panel is what the
                    // chip opened.
                    refreshAccountRows();
                    accountScreen.open();
                    accountsOpen = true;
                    sound::play(sound::Cue::Activate);
                    return true;
                }
                if (barSlot == BarLibrary || barSlot == BarSearch ||
                    barSlot == BarSettings) {
                    const int d = (barSlot == BarLibrary) ? 1
                                : (barSlot == BarSearch ? 2 : 3);
                    barFocused = false;
                    // Already standing in it: drop back into the screen rather
                    // than rebuilding it under the person's feet.
                    if (destinationGoing() == d) {
                        sound::play(sound::Cue::Move);
                        return true;
                    }
                    transitionTo(d);
                    sound::play(sound::Cue::Activate);
                    return true;
                }
                sound::play(sound::Cue::Edge);
                std::fprintf(stderr, "[nav] %s is not built yet\n", kBarLabels[barSlot]);
                return true;
        }
        return false;
    };

    // DOWNLOADS' REMOVE: one question naming how many and how much, with
    // Cancel focused, then everybody's keep released for each. The pill says
    // how many went; the panel comes back with what is left, and goes when
    // nothing is. docs/SETTINGS.md, Storage.
    auto removeDownloads = [&]() {
        const std::vector<int> ids = downloadsPanel.toRemove();
        int removed = 0, failed = 0, demoted = 0;
        int64_t freed = 0;
        for (int romId : ids) {
            const bool nowPlaying = playing && session.romId == romId;
            cache::Release r;
            cache::unkeepForEveryone(romId, /*keepTheBytes=*/nowPlaying, &r);
            switch (r.what) {
                case cache::Release::What::DeleteFailed: ++failed; break;
                case cache::Release::What::Demoted: ++demoted; ++removed; break;
                case cache::Release::What::Deleted: freed += r.bytesFreed; ++removed; break;
                default: ++removed; break;
            }
        }
        std::fprintf(stderr, "[downloads] removed %d, %lld bytes back, %d failed\n", removed,
                     static_cast<long long>(freed), failed);
        refreshKeeps();
        if (!downloadsPanel.replace(downloadItems())) downloadsPanel.close();
        buildSettings();
        if (failed > 0)
            menuNotice.say("Couldn't delete some of the files", Tone::Problem);
        else if (demoted > 0)
            menuNotice.say("Removed. The space comes back when you stop playing it", Tone::Info);
        else
            menuNotice.say(removed == 1 ? "Download removed"
                                        : std::to_string(removed) + " downloads removed",
                           Tone::Done);
    };
    downloadsOutcome = [&](screens::DownloadsPanel::Outcome o) {
        using O = screens::DownloadsPanel::Outcome;
        if (o == O::Closed) {
            downloadsPanel.close();
            buildSettings();
        } else if (o == O::Remove) {
            askChoice(downloadsPanel.removeTitle(), downloadsPanel.removeDetail(),
                      {"Cancel", downloadsPanel.removingAll() ? "Remove all" : "Remove"}, 0,
                      [&](int answer) {
                          if (answer == 1) removeDownloads();
                      });
        }
    };

    // The first answer, before anything is drawn. Everything after this is a
    // refresh triggered by the thing that changed it.
    refreshKeeps();

    navigate = [&](screens::Nav n) -> bool {
        // THE PIN PAD BEFORE ANYTHING. It covers the screen, so nothing under
        // it may take a press.
        if (pinScreen.isOpen()) { pinOutcome(pinScreen.key(n)); return true; }
        if (choiceScreen.isOpen()) { choiceOutcome(choiceScreen.key(n)); return true; }
        if (downloadsPanel.isOpen()) { downloadsOutcome(downloadsPanel.key(n)); return true; }
        // Nothing takes a press while an account switch is behind the curtain.
        if (switchPendingId || leaving != Leave::None) return true;
        // The bar first, wherever it is focused. One place, one behaviour.
        if (accountsOpen) { apply(accountScreen.key(n)); return true; }
        if (barFocused && barKey(n)) return true;
        switch (here()) {
            case Screen::Home: return homeKey ? homeKey(n) : false;
            case Screen::Library: apply(libraryScreen.key(n)); return true;
            case Screen::Grid: apply(gridScreen.key(n)); return true;
            case Screen::Detail: apply(detailScreen.key(n)); return true;
            case Screen::Search: apply(searchScreen.key(n)); return true;
            case Screen::AddAccount: apply(addAccountScreen.key(n)); return true;
            case Screen::Settings: apply(settingsScreen.key(n)); return true;
        }
        return false;
    };

    // Opening straight onto a screen, for a capture. This walks the SAME route
    // a person would: the Library is entered, a tile is opened, the launch
    // screen is opened from a card. A capture that built a screen some other
    // way would be photographing something the product cannot reach.
    // SEARCH IS ITS OWN ROUTE, because walking to it is not walking to the
    // Library. `--screen search --query metal` types the query the way a person
    // would type it and leaves focus in the results, which is the state worth
    // photographing — an empty Search is a picture of a keyboard.
    // THE SWITCHER IS REACHED THE WAY A PERSON REACHES IT: focus the bar, walk
    // to the chip, press it. A capture that called open() directly would be
    // photographing a panel the product might not be able to get to.
    if (focusBarSlot >= 0) {
        barFocused = true;
        barSlot = std::clamp(focusBarSlot, 0, BarCount - 1);
    }
    if (initialScreen && (SDL_strcmp(initialScreen, "accounts") == 0 ||
                          SDL_strcmp(initialScreen, "add-account") == 0)) {
        barFocused = true;
        barSlot = BarAccount;
        barKey(screens::Nav::Activate);
        if (SDL_strcmp(initialScreen, "add-account") == 0) {
            // WALKS DOWN TO THE ADD ROW FIRST, and not doing so switched the
            // console to somebody else. This pressed Activate straight away,
            // which was right while the panel held only the Add row and wrong
            // the moment a second account existed — row 0 became a person, so
            // the capture route signed the console in as them.
            //
            // The lesson is the one this project keeps paying for: a route
            // that walks the way a person walks has to keep walking when the
            // screen gains a row. Down until the focus stops moving, then
            // press, which is what a person does.
            for (int guard = 0; guard < 16; ++guard)
                accountScreen.key(screens::Nav::Down);
            // Presses the Add row the way a person would. It starts a REAL
            // pairing against the real server — a device code that expires in
            // minutes and creates nothing unless somebody approves it.
            apply(accountScreen.key(screens::Nav::Activate));
            // AND THE CAPTURE HAS TO WAIT FOR THE SERVER. This loop is not
            // paced, so on the A9 four hundred frames go by in well under a
            // second and the shot comes out with no code on it — the same trap
            // `--launch-after` fell into and the same one that cost the setup
            // pairing capture. Gate the shot on the fact rather than on a
            // frame count.
            waitForPairCode = true;
        }
    } else if (initialScreen && SDL_strcmp(initialScreen, "settings") == 0) {
        // `--screen settings --tile N` opens on category N; `--focus-row 1`
        // puts focus in its rows. The network is waited for here, because a
        // capture has no wall clock and would otherwise always say Checking.
        goToDestination(3);
        if (settingsNet.th.joinable()) settingsNet.th.join();
        if (settingsWifi.th.joinable()) settingsWifi.th.join();
        buildSettings();
        settingsScreen.focusCategory(initialTile, initialRow > 0);
    } else if (initialScreen && SDL_strcmp(initialScreen, "search") == 0) {
        goToDestination(2);
        if (searchQuery) {
            keyboard.typeText(searchQuery);
            searchTyped = keyboard.value();
            searchScreen.setQuery(searchTyped);
            // Asked straight away rather than waiting out the debounce: a
            // capture has no typing to wait for, and a screenshot of the
            // Searching… state is not what anybody asked for.
            runSearch(searchTyped);
            if (searchScreen.resultCount() > 0) searchScreen.setFocused(true);
        }
    } else if (initialScreen) {
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
    // ---- An account switch, behind the curtain ---------------------------
    //
    // The request (the account panel's SwitchAccount) only sets these. Each
    // frame this brings the curtain down; once it is fully down and a frame
    // with the name on it has been shown, it runs the blocking switch, then
    // lifts the curtain on the new person's Home. MMagTech, 2026-09-24: the UI
    // stalled on a switch with nothing to say why.
    // (switchPendingId, switchLabel and switchCurtainFrames are declared by
    // the curtain, further up, because the account panel's apply sets them.)
    auto pumpSwitch = [&]() {
        if (!switchPendingId) return;
        curtain.retarget(1.0f, kCurtainDown);
        if (curtain.value() < 0.995f) return;
        if (switchShownAt == 0) {
            switchShownAt = SDL_GetTicks();
            switchText.smooth = true;
            switchText.retarget(1.0f, kSwitchTextFade);
        }
        if (!switchDone) {
            // Two frames at full curtain, so the name is on the television
            // before the frame loop stops for the network.
            if (++switchCurtainFrames < 3) return;
            const Uint64 t0 = SDL_GetTicks();
            switchWhy.clear();
            switchOk = switchAccount(switchPendingId, &switchWhy);
            std::fprintf(stderr, "[accounts] switch took %llu ms\n",
                         static_cast<unsigned long long>(SDL_GetTicks() - t0));
            switchDone = true;
            // Straight to Home, because everything behind it belonged to the
            // last account. Behind the curtain, so it is ready when it lifts.
            if (switchOk) {
                stack.clear();
                stack.push_back(Screen::Home);
            }
        }
        // THE NAME STAYS UP LONG ENOUGH TO READ. A switch usually takes a
        // quarter of a second, and the curtain dropping and lifting around
        // that read as a glitch: MMagTech, 2026-09-24, *"the account switch
        // text flash to fast that it just seems like a glitch"*. A slow
        // switch is not held any longer than it took. Starting value.
        constexpr Uint64 kSwitchHoldMs = 700;
        if (SDL_GetTicks() - switchShownAt < kSwitchHoldMs) return;
        // The name goes before the curtain does.
        switchText.retarget(0.0f, kSwitchTextFade);
        if (switchText.value() > 0.01f) return;
        const bool ok = switchOk;
        const std::string why = switchWhy;
        switchPendingId = 0;
        switchDone = false;
        switchShownAt = 0;
        if (!ok) {
            // Back to the panel, saying why, with the old person still in.
            accountsOpen = true;
            barFocused = true;
            barSlot = BarAccount;
            refreshAccountRows();
            accountScreen.open();
            accountScreen.setNotice(why);
            sound::play(sound::Cue::Edge);
        }
        curtain.retarget(0.0f, kCurtainUp);
    };

    // ---- Leaving, behind the startup screen ------------------------------
    //
    // Sign out and a new server address both end with this program starting
    // again: the address is read once at startup, and first run is a loop of
    // its own that runs before this one (setup.h). The curtain is the startup
    // screen, so the picture the app leaves on is the one it comes back on.
    // MMagTech, 2026-09-25: not a black screen.
    auto pumpLeave = [&]() {
        if (leaving == Leave::None) return;
        curtain.retarget(1.0f, kCurtainDown);
        if (curtain.value() < 0.995f) return;
        const Uint64 now = SDL_GetTicks();
        if (leaveDownAt == 0) leaveDownAt = now;
        // UPLOADS FIRST, at most twenty seconds, as for sleep. A save that
        // is still going up would otherwise be cleared by Sign out, or cut
        // off by the restart. By design this is nothing: leaving a game
        // uploads, and Settings is a walk away from any game.
        if (uploader.pending() > 0 && now - leaveDownAt < 20000) return;
        // The line stays up long enough to read: the switch's reason.
        constexpr Uint64 kLeaveHoldMs = 700;
        if (now - leaveDownAt < kLeaveHoldMs) return;
        if (uploader.pending() > 0)
            std::fprintf(stderr, "[server] leaving with %d upload(s) still owed\n",
                         uploader.pending());
        if (leaving == Leave::SignOut) {
            std::string err;
            if (!server::signOut(&err)) {
                std::fprintf(stderr, "[sign out] could not: %s\n", err.c_str());
                leaving = Leave::None;
                curtain.retarget(0.0f, kCurtainUp);
                menuNotice.say("Couldn't sign out", Tone::Problem);
                return;
            }
        }
        restartSelf = true;
        running = false;
    };

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
            refreshKeeps();
            launchJob.stop();
            launchJob.stage = LaunchJob::Stage::Idle;
            return;
        }
        if (st != LaunchJob::Stage::Ready) return;

        // THE CURTAIN COMES DOWN BEFORE THE BLOCKING WORK, not after it. See
        // design::kCurtainDown. The stage is deliberately NOT cleared here, so
        // this runs again next frame and the frames in between are spent
        // animating something a person can see rather than waiting inside
        // Core::loadGame with nothing on the screen.
        //
        // Only for a launch. A download that was merely asked for must not
        // black the screen out — the person is still browsing, and its progress
        // is on the row they pressed.
        if (launchJob.playWhenReady) {
            curtain.retarget(1.0f, kCurtainDown);
            if (curtain.value() < 0.995f) return;
        }

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
            refreshKeeps();
            // THE ONE EARLY WARNING. Only a Download can fill the drives (the
            // cache clears itself, saves are small), so this is the one moment
            // to say it: kept games are close to leaving no room to play
            // something new. MMagTech, 2026-09-25.
            if (launchJob.keepWhenReady && cache::almostFull())
                menuNotice.say("Storage almost full", Tone::Info);
            return;
        }

        // AND IT ONLY LAUNCHES ITSELF IF NOBODY WALKED AWAY — 2026-09-21.
        //
        // MMagTech, on the download no longer blocking the screen: *"wait
        // should it auto launch after complete."* It is a question that could
        // not be asked while a modal panel was up, because there was nowhere to
        // walk to. Now there is: press Play on a 1.78 GB arcade set, get bored,
        // go and look at something else, and four minutes later the console
        // would drop you into a game you had stopped waiting for.
        //
        // So: still on the screen you pressed it from, you are visibly waiting
        // and it launches — "one action from cold to playing" holds. Moved on,
        // and it finishes quietly. The game is on the disk, prepared exactly as
        // a launch would have prepared it, and its row says Play.
        if (launchJob.startedOn >= 0 &&
            launchJob.startedOn != static_cast<int>(here())) {
            std::fprintf(stderr,
                         "[launch] %s is ready, and not starting: the screen moved on\n",
                         launchJob.title.c_str());
            if (here() == Screen::Detail &&
                detailScreen.game().romId == launchJob.romId)
                detailScreen.setKept(
                    cache::isKeptBy(storage::currentUser(), launchJob.romId));
            refreshKeeps();
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
        // Before the restore, because the restore puts the card at the path
        // this names and the core reads both on startup.
        if (launchJob.platformSlug == "ngc") writeDolphinConfig(saveDir);

        std::vector<cab::FileSaveState> restored =
            restoreFileSaves(saveSpecs, launchJob.fsStem, saveDir, launchJob.romId,
                             launchTag, liveClient);

        if (!core.loadGame(launchJob.romPath, storage::biosDir(), saveDir)) {
            // AND SAY IT ON THE SCREEN, NOT ONLY TO STDERR — 2026-09-21.
            //
            // MMagTech: *"dreamcast game downloaded and didnt auto launch and
            // clicking play didnt launch it either."* The console knew exactly
            // why and had said so, to a log nobody on a sofa can read:
            //
            //   [launch] this core wants Vulkan and this device will not export
            //   memory as a file descriptor, so nothing it draws could reach
            //   the screen
            //
            // A refusal the person cannot see is indistinguishable from a
            // console that has stopped responding — and it is worse than a
            // crash, because pressing the button again does the same nothing
            // forever. The launch screen already has somewhere to put this:
            // `setNotice` is the same place a full disk reports itself.
            std::fprintf(stderr, "[launch] %s\n", core.error().c_str());
            // If the press came from somewhere with nowhere to put a message —
            // Home's Resume card, for instance — the console goes to the
            // game's own screen and says it there, rather than inventing a
            // second place for refusals to live.
            if (here() != Screen::Detail ||
                detailScreen.game().romId != launchJob.romId) {
                for (size_t i = 0; i < cards.size(); ++i) {
                    if (cards[i].id == launchJob.romId) {
                        openDetail(static_cast<int>(i));
                        break;
                    }
                }
            }
            detailScreen.setNotice(core.error());
            sound::play(sound::Cue::Edge);
            // The curtain came down for a game that is not going to start, so
            // it goes straight back up onto the screen that says why.
            curtain.retarget(0.0f, kCurtainUp);
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
        // The game is loaded and the next frame is its first. Up it goes.
        curtain.retarget(0.0f, kCurtainUp);

        session = GameSession{};
        session.romId = launchJob.romId;
        session.title = launchJob.title;
        session.saveDir = saveDir;
        session.fsStem = launchJob.fsStem;
        session.stateDir = storage::statesDir(
            user, launchJob.platformFsSlug, launchJob.romId, launchJob.coreName);
        session.fileSaves = std::move(restored);
        if (launchTag) session.saveTag = launchTag;
        session.snapshots = catalog::snapshotsAllowed(launchJob.coreName.c_str());
        if (!session.snapshots)
            std::fprintf(stderr, "[state] no save states on this system, true to the console\n");
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
            // AND THE WHOLE TABLE, when asked for. --core-options-detail
            // cannot see these at all: a core that declares nothing until it
            // knows what it is running is invisible to an audit taken at core
            // load, and that is Dolphin, FBNeo and MAME — three of the most
            // configurable things this console ships.
            if (coreOptionsDetail) {
                for (const auto& o : opts) {
                    std::fprintf(stderr, "  %s\n", o.key.c_str());
                    if (!o.desc.empty())
                        std::fprintf(stderr, "      what   %s\n", o.desc.c_str());
                    std::fprintf(stderr, "      now    %s%s\n", o.chosen.c_str(),
                                 o.overridden ? "   (ours)" : "");
                    if (!o.values.empty()) {
                        std::fprintf(stderr, "      takes ");
                        for (size_t i = 0; i < o.values.size(); ++i)
                            std::fprintf(stderr, "%s%s", i ? " | " : " ",
                                         o.values[i].c_str());
                        std::fprintf(stderr, "\n");
                    }
                }
            }
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


    auto leaveFocus = [&]() {
        if (Card* c = cardAt(focusRow, focusSlot)) c->focus.retarget(0.0f, kFocusDuration);
    };
    auto enterFocus = [&]() {
        if (Card* c = cardAt(focusRow, focusSlot)) c->focus.retarget(1.0f, kFocusDuration);
    };

    auto moveFocus = [&](int delta) {
        const int slots = static_cast<int>(rowSlots(focusRow));
        if (slots <= 0) return;
        const int next = std::clamp(focusSlot + delta, 0, slots - 1);
        // The end of a row is a sound too. Silence here reads as the console
        // having stopped listening rather than as there being nothing there.
        if (next == focusSlot) { sound::play(sound::Cue::Edge); return; }
        leaveFocus();
        sound::play(sound::Cue::Move);
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
            if (candidate < RowRecent || candidate > RowFavorites) return;
            row = candidate;
            if (rowExists(row)) break;
            if (row == RowFavorites) return;
        }
        if (row == focusRow || !rowExists(row)) { sound::play(sound::Cue::Edge); return; }
        leaveFocus();
        sound::play(sound::Cue::Move);
        focusRow = row;
        focusSlot = std::clamp(rememberedSlot[row], 0,
                               static_cast<int>(rowSlots(row)) - 1);
        enterFocus();
    };

    // Home's keys, routed through the same door as every other screen's so the
    // bar can be offered them first. See `navigate`.
    homeKey = [&](screens::Nav n) -> bool {
        // AN EMPTY HOME SAYS "Press A to open the Library", and A does.
        // Up is still the bar.
        if (homeEmpty()) {
            switch (n) {
                case screens::Nav::Up:
                    barSlot = 0;
                    barFocused = true;
                    sound::play(sound::Cue::Move);
                    return true;
                case screens::Nav::Activate:
                    transitionTo(1);
                    sound::play(sound::Cue::Activate);
                    return true;
                default:
                    sound::play(sound::Cue::Edge);
                    return true;
            }
        }
        switch (n) {
            case screens::Nav::Left:  moveFocus(-1); return true;
            case screens::Nav::Right: moveFocus(+1); return true;
            case screens::Nav::Down:  moveRow(+1); return true;
            case screens::Nav::Up:
                // Up out of the top shelf is the bar, the same as it is on the
                // Library and in a grid. This is the one that used to be a row
                // move into RowBar.
                if (focusRow == RowRecent) {
                    leaveFocus();
                    barSlot = 0;
                    barFocused = true;
                    sound::play(sound::Cue::Move);
                } else {
                    moveRow(-1);
                }
                return true;
            case screens::Nav::Activate: activateHome(); return true;
            // Home is the root. Back has nowhere to go and says so.
            case screens::Nav::Back: sound::play(sound::Cue::Edge); return true;
        }
        return false;
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
        // THE DOCKED KEYBOARD DOES NOT OWN THE SCREEN. Everywhere else an open
        // keyboard is modal and takes every key, which is right for a question
        // with one answer. On Search it shares the screen with the results, and
        // when focus is up in those results the keyboard is just something that
        // is still visible.
        // The overlay takes the pad FROM the core while it is open, which is
        // the whole rule: never both. tvOS does this by turning the focus
        // engine off during play; here it is this one line. FIRST, ahead of the
        // keyboard, since the Power menu can open over a screen that has the
        // keyboard up, and a menu nobody can steer is a trap.
        if (overlayOpen) return InputOwner::Overlay;
        // NOR WHEN THE BAR HAS FOCUS ABOVE IT: walking across the bar onto
        // Search opens its keyboard, and the next press still belongs to the
        // bar until Down leaves it.
        if (keyboard.isOpen() &&
            !(here() == Screen::Search && (searchScreen.focused() || barFocused)))
            return InputOwner::Keyboard;
        if (playing) return InputOwner::Game;
        return InputOwner::UI;
    };

    // UP OUT OF THE KEYBOARD'S TOP ROW. It means nothing anywhere else — this
    // keyboard deliberately does not wrap — and on Search it is how a person
    // gets from what they typed to what it found.
    auto keyboardUp = [&]() {
        if (here() == Screen::Search && keyboard.atTopRow() &&
            searchScreen.resultCount() > 0) {
            searchScreen.setFocused(true);
            sound::play(sound::Cue::Move);
            return;
        }
        keyboard.moveFocus(0, -1);
    };

    // What a commit or a cancel MEANS, which is not the same on every screen.
    // On Search, done goes up into the results and cancel leaves the screen —
    // and cancel closing the keyboard while leaving an empty Search behind it
    // would be a dead end with no way out but the bar.
    auto keyboardResult = [&](ui::KeyboardResult res) {
        if (here() != Screen::Search) {
            if (res == ui::KeyboardResult::Typing || !keyboardThen) return;
            auto then = std::move(keyboardThen);
            keyboardThen = nullptr;
            then(res);
            return;
        }
        if (res == ui::KeyboardResult::Committed) {
            if (searchScreen.resultCount() > 0) searchScreen.setFocused(true);
            else if (!keyboard.isOpen()) goToDestination(2);   // nothing found: type again
        } else if (res == ui::KeyboardResult::Cancelled) {
            goToDestination(0);
            sound::play(sound::Cue::Back);
        }
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
        // And the same curtain on the way out — `unloadGame` blocks too, and a
        // game vanishing into Home mid-frame is the same cut in the other
        // direction.
        curtain.from = curtain.to = 1.0f;
        curtain.elapsed = curtain.duration;
        curtain.retarget(0.0f, kCurtainUp);
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
        overlayFade.retarget(0.0f, overlayFadeSeconds);
        std::fprintf(stderr, "[overlay] exited to Home\n");
    };

    auto exitToHome = [&]() {
        if (!cab::Core::shared().running()) {
            // Still building the machine. Close the overlay so the quit looks
            // like it was accepted — it has been — and let the loop finish it.
            exitPending = true;
            exitWaitStart = SDL_GetTicksNS();
            overlayOpen = false;
            overlayFade.retarget(0.0f, overlayFadeSeconds);
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

    auto closeOverlay = [&]() {
        overlayOpen = false;
        overlayFade.retarget(0.0f, overlayFadeSeconds);
    };

    auto powerActivate = [&](PowerItem item) {
        if (item == PwResume) {
            closeOverlay();
            return;
        }
        const power::Action act = item == PwRest      ? power::Action::Rest
                                  : item == PwRestart ? power::Action::Restart
                                                      : power::Action::PowerOff;
        std::fprintf(stderr, "[power] %s chosen%s\n", power::name(act),
                     playing ? " from a game; leaving it first" : "");
        if (playing) finishExit();
        closeOverlay();
        if (act == power::Action::Rest) {
            restPending = true;
            restWaitStart = SDL_GetTicksNS();
        } else {
            // A restart or a power-off stops this process on its way down, and
            // the uploads just queued are drained then — PR #50.
            power::act(act);
        }
    };

    auto overlayActivate = [&]() {
        if (powerMenu) {
            if (overlaySlot >= 0 && overlaySlot < static_cast<int>(powerItems.size()))
                powerActivate(powerItems[overlaySlot]);
            return;
        }
        if (overlaySlot < 0 || overlaySlot >= static_cast<int>(pauseItems.size())) return;
        switch (pauseItems[overlaySlot]) {
            case OvResume:
                overlayOpen = false;
                overlayFade.retarget(0.0f, overlayFadeSeconds);
                break;
            case OvSaveState: saveStateNow(session, uploader, menuNotice); break;
            case OvLoadState: beginLoadLatestState(stateLoad, session, liveClient, menuNotice); break;
            case OvExit: exitToHome(); break;
            default: break;
        }
    };

    auto toggleOverlay = [&]() {
        if (!playing) return;
        overlayOpen = !overlayOpen;
        // Only on the way IN: the list must not change under a panel that is
        // still fading out.
        if (overlayOpen) {
            powerMenu = false;
            pauseItems = {OvResume};
            if (session.snapshots) {
                pauseItems.push_back(OvSaveState);
                pauseItems.push_back(OvLoadState);
            }
            pauseItems.push_back(OvExit);
        }
        overlaySlot = 0;
        overlayFade.retarget(overlayOpen ? 1.0f : 0.0f, overlayFadeSeconds);
        overlayFocus.retarget(1.0f, kOverlayFocusDuration);
        overlayFocus.elapsed = kOverlayFocusDuration;
    };

    // Pressed while it is already open, it closes, like Resume.
    auto openPowerMenu = [&]() {
        if (overlayOpen && powerMenu) {
            closeOverlay();
            return;
        }
        powerItems.clear();
        if (playing) powerItems.push_back(PwResume);
        if (restAvailable) powerItems.push_back(PwRest);
        powerItems.push_back(PwRestart);
        powerItems.push_back(PwPowerOff);
        powerMenu = true;
        overlayOpen = true;
        overlaySlot = 0;
        overlayFade.retarget(1.0f, overlayFadeSeconds);
        overlayFocus.retarget(1.0f, kOverlayFocusDuration);
        overlayFocus.elapsed = kOverlayFocusDuration;
        std::fprintf(stderr, "[power] menu open%s\n", playing ? " over a game" : "");
    };

    // The button is borrowed for the life of this process — power.h. Not for a
    // capture, which is not the console and must not take its button.
    if (!shotMode) {
        power::takeButtons();
        power::takeShutdownDelay();
        restAvailable = power::canRest();
    }
    // Set while logind is waiting on us to leave a game before it proceeds.
    bool releasePending = false;
    uint64_t releaseWaitStart = 0;
    if (powerMenuDemo) {
        if (shotMode) restAvailable = power::canRest();
        openPowerMenu();
    }

    // Here rather than with --screen, because Home's keys are only wired up
    // just above; walking before that pressed nothing.
    if (navScript) {
        std::string word;
        for (const char* p = navScript;; ++p) {
            if (*p && *p != ',') { word += *p; continue; }
            screens::Nav n;
            bool ok = true;
            if (word == "up") n = screens::Nav::Up;
            else if (word == "down") n = screens::Nav::Down;
            else if (word == "left") n = screens::Nav::Left;
            else if (word == "right") n = screens::Nav::Right;
            else if (word == "a") n = screens::Nav::Activate;
            else if (word == "b") n = screens::Nav::Back;
            else ok = false;
            // A digit is a PIN pad key typed on a keyboard, so a whole PIN
            // can be walked headless: "down,a,4,8,2,1,4,8,2,1".
            if (!ok && word.size() == 1 && word[0] >= '0' && word[0] <= '9') {
                pinOutcome(pinScreen.typeDigit(word[0]));
                word.clear();
                if (!*p) break;
                continue;
            }
            if (ok) navigate(n);
            else if (!word.empty()) std::fprintf(stderr, "[nav] unknown press '%s'\n", word.c_str());
            word.clear();
            if (!*p) break;
        }
        std::fprintf(stderr, "[nav] after '%s': screen %d, bar %s slot %d%s\n", navScript,
                     static_cast<int>(here()), barFocused ? "focused" : "not focused",
                     barSlot, pinScreen.isOpen() ? ", PIN pad open" : "");
        // A capture shows the pad at rest, not part-way through its fade.
        if (pinScreen.isOpen()) pinScreen.settle();
        if (choiceScreen.isOpen()) choiceScreen.settle();
    }


    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            const InputOwner owner = inputOwner();
            // ANY TOUCH WAKES THE SCREEN, AND ON A DARK SCREEN THAT IS ALL IT
            // DOES. The press that lights a dimmed or blank Home is swallowed,
            // so it cannot also launch whatever had focus, which nobody could
            // see. A running game is the exception: its pad is read by state
            // rather than by these events, so nothing is lost from play.
            bool touched = false;
            switch (e.type) {
                case SDL_EVENT_KEY_DOWN:
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                case SDL_EVENT_MOUSE_BUTTON_DOWN:
                    touched = true;
                    break;
                case SDL_EVENT_GAMEPAD_AXIS_MOTION:
                    touched = std::abs(static_cast<int>(e.gaxis.value)) > idle::kAxisDeadzone;
                    break;
                default:
                    break;
            }
            // The power and Sleep keys are never swallowed: on a dark screen
            // they light it AND open the Power menu, which is what the person
            // reaching for that button wants.
            const bool powerKey = e.type == SDL_EVENT_KEY_DOWN &&
                                  (e.key.key == SDLK_POWER || e.key.key == SDLK_SLEEP);
            if (touched && idleWatch.input(clockSeconds()) && owner != InputOwner::Game &&
                !powerKey)
                continue;
            if (powerKey) {
                // The press that woke the machine from Rest arrives here too,
                // and must not wake it straight into this menu.
                if (power::justWoke())
                    std::fprintf(stderr, "[power] ignoring the press that woke the machine\n");
                else
                    openPowerMenu();
                continue;
            }
            switch (e.type) {
                case SDL_EVENT_QUIT:
                    running = false;
                    askedToStop = true;
                    std::fprintf(stderr, "[shutdown] asked to stop%s\n",
                                 playing ? " with a game running" : "");
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
                    // A physical keyboard's digits go into the PIN pad, the
                    // same field the controller fills.
                    if (pinScreen.isOpen() && owner == InputOwner::UI) {
                        const SDL_Keycode k = e.key.key;
                        char d = 0;
                        if (k >= SDLK_0 && k <= SDLK_9) d = static_cast<char>('0' + (k - SDLK_0));
                        else if (k >= SDLK_KP_1 && k <= SDLK_KP_9)
                            d = static_cast<char>('1' + (k - SDLK_KP_1));
                        else if (k == SDLK_KP_0) d = '0';
                        if (d) { pinOutcome(pinScreen.typeDigit(d)); break; }
                        if (k == SDLK_BACKSPACE) { pinScreen.deleteDigit(); break; }
                        if (k == SDLK_ESCAPE) { navigate(screens::Nav::Back); break; }
                    }
                    if (owner == InputOwner::Keyboard) {
                        // While it owns input it takes every key, the same way
                        // the core owns the pad while a game runs. A control
                        // that means two things at once is the bug.
                        switch (e.key.key) {
                            case SDLK_LEFT: keyboard.moveFocus(-1, 0); break;
                            case SDLK_RIGHT: keyboard.moveFocus(+1, 0); break;
                            case SDLK_UP: keyboardUp(); break;
                            case SDLK_DOWN: keyboard.moveFocus(0, +1); break;
                            case SDLK_BACKSPACE: keyboard.backspace(); break;
                            case SDLK_RETURN:
                                std::fprintf(stderr, "[keyboard] committed: %s\n",
                                             keyboard.value().c_str());
                                keyboardResult(keyboard.commit());
                                break;
                            case SDLK_ESCAPE: keyboardResult(keyboard.cancel()); break;
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
                        else if (overlayOpen) closeOverlay();
                        else if (stack.size() > 1) navigate(screens::Nav::Back);
                        // At the root, Escape is Start: the Power menu.
                        else openPowerMenu();
                    }
                    // The shoulders, for a machine with no pad attached. `[`
                    // and `]` sit where L1 and R1 do on a controller and the
                    // capture tooling can drive them.
                    if (owner == InputOwner::UI) {
                        if (e.key.key == SDLK_LEFTBRACKET) switchDestination(-1);
                        if (e.key.key == SDLK_RIGHTBRACKET) switchDestination(+1);
                        // The triggers' equivalent, where a pad is not attached.
                        if (here() == Screen::Grid) {
                            if (e.key.key == SDLK_COMMA) gridScreen.jumpLetter(-1);
                            if (e.key.key == SDLK_PERIOD) gridScreen.jumpLetter(+1);
                        }
                    }
                    if (owner == InputOwner::Overlay) {
                        if (e.key.key == SDLK_UP || e.key.key == SDLK_DOWN) {
                            const int delta = (e.key.key == SDLK_DOWN) ? 1 : -1;
                            overlaySlot = std::clamp(overlaySlot + delta, 0, ovCount() - 1);
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
                    if (playing && session.snapshots && e.key.key == SDLK_F5) saveStateNow(session, uploader, menuNotice);
                    if (playing && session.snapshots && e.key.key == SDLK_F8) beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
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
                    // THROUGH `navigate`, NOT STRAIGHT AT HOME. Every key on
                    // every screen arrives at one place now, so the bar can be
                    // offered it first wherever it holds focus.
                    // `e.key.repeat` is the system's own key repeat, at the
                    // system's own rate. Ignore it and run the same accelerating
                    // repeat the pad gets, so the two input devices behave
                    // identically — which is the rule the whole input model is
                    // built on.
                    if (!e.key.repeat) {
                        if (e.key.key == SDLK_LEFT) {
                            navigate(screens::Nav::Left); holdNav(screens::Nav::Left);
                        }
                        if (e.key.key == SDLK_RIGHT) {
                            navigate(screens::Nav::Right); holdNav(screens::Nav::Right);
                        }
                        if (e.key.key == SDLK_UP) {
                            navigate(screens::Nav::Up); holdNav(screens::Nav::Up);
                        }
                        if (e.key.key == SDLK_DOWN) {
                            navigate(screens::Nav::Down); holdNav(screens::Nav::Down);
                        }
                    }
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
                        if (!playing) navigate(screens::Nav::Activate);
                    }
                    break;
                case SDL_EVENT_KEY_UP:
                    if (e.key.key == SDLK_LEFT) releaseNav(screens::Nav::Left);
                    if (e.key.key == SDLK_RIGHT) releaseNav(screens::Nav::Right);
                    if (e.key.key == SDLK_UP) releaseNav(screens::Nav::Up);
                    if (e.key.key == SDLK_DOWN) releaseNav(screens::Nav::Down);
                    if (e.key.key == SDLK_RETURN || e.key.key == SDLK_SPACE) pressing = false;
                    break;
                case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
                    // THE SHOULDERS LEAVE SEARCH, keyboard or not. MMagTech,
                    // 2026-09-24: R1 landed on Search and then neither shoulder
                    // got you off it, because the docked keyboard took every
                    // button. Only Search's docked keyboard: a keyboard asking
                    // a question (a Wi-Fi password) is modal and keeps them.
                    // Leaving clears the search, as entering any destination
                    // starts it fresh.
                    if (owner == InputOwner::Keyboard && here() == Screen::Search &&
                        (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ||
                         e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER)) {
                        switchDestination(
                            e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER ? -1 : +1);
                        break;
                    }
                    // X TAKES A DIGIT OFF THE PIN, as it takes a letter off
                    // the on-screen keyboard. B leaves the pad.
                    if (pinScreen.isOpen() && owner == InputOwner::UI &&
                        e.gbutton.button == SDL_GAMEPAD_BUTTON_WEST) {
                        pinScreen.deleteDigit();
                        break;
                    }
                    if (owner == InputOwner::Keyboard) {
                        switch (e.gbutton.button) {
                            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: keyboard.moveFocus(-1, 0); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: keyboard.moveFocus(+1, 0); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_UP: keyboardUp(); break;
                            case SDL_GAMEPAD_BUTTON_DPAD_DOWN: keyboard.moveFocus(0, +1); break;
                            case SDL_GAMEPAD_BUTTON_SOUTH: keyboardResult(keyboard.pressKey()); break;
                            case SDL_GAMEPAD_BUTTON_WEST: keyboard.backspace(); break;
                            case SDL_GAMEPAD_BUTTON_NORTH: keyboard.toggleShift(); break;
                            case SDL_GAMEPAD_BUTTON_START:
                                std::fprintf(stderr, "[keyboard] committed: %s\n",
                                             keyboard.value().c_str());
                                keyboardResult(keyboard.commit());
                                break;
                            case SDL_GAMEPAD_BUTTON_EAST:
                                keyboardResult(keyboard.cancel()); break;
                            default: break;
                        }
                        break;
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
                            overlaySlot = std::min(ovCount() - 1, overlaySlot + 1);
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) overlayActivate();
                        // East is Back, and Back from the overlay is Resume —
                        // or, from the Power menu on Home, just closing it.
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) closeOverlay();
                        // Start closes the Power menu it opened on Home.
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_START && powerMenu &&
                            !playing)
                            closeOverlay();
                        break;
                    }
                    if (owner != InputOwner::UI) break;
                    // The shoulders first, because they are the one input that
                    // means the same thing on every screen that has them.
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_LEFT_SHOULDER) {
                        switchDestination(-1);
                        break;
                    }
                    if (e.gbutton.button == SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER) {
                        switchDestination(+1);
                        break;
                    }
                    // ONE DIRECTION HANDLER FOR EVERY SCREEN — 2026-09-21.
                    //
                    // There were three: Left and Right were handled early and
                    // split by screen, Up and Down were handled once for the
                    // pushed screens and again for Home, and only Home's copy
                    // ever started the hold. So `holdNav` existed and MMagTech
                    // was right that *"holding down isnt working"* — it worked
                    // on Home and nowhere else, which is the worst kind of
                    // working. Everything below is one switch, and there is now
                    // no way to add a direction to one screen and forget it on
                    // another.
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                            navigate(screens::Nav::Left);
                            holdNav(screens::Nav::Left);
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                            navigate(screens::Nav::Right);
                            holdNav(screens::Nav::Right);
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_UP:
                            navigate(screens::Nav::Up);
                            holdNav(screens::Nav::Up);
                            break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                            navigate(screens::Nav::Down);
                            holdNav(screens::Nav::Down);
                            break;
                        case SDL_GAMEPAD_BUTTON_SOUTH:
                            navigate(screens::Nav::Activate);
                            pressing = true;
                            break;
                        // East is Back everywhere in this product, which is the
                        // rule the in-game overlay already follows. On Home it
                        // is the root and `homeKey` answers with the edge cue.
                        case SDL_GAMEPAD_BUTTON_EAST:
                            navigate(screens::Nav::Back);
                            break;
                        case SDL_GAMEPAD_BUTTON_START:
                            // START ON HOME IS THE POWER MENU — decided
                            // 2026-09-22. It used to quit the frontend, a
                            // development leftover that just got the session
                            // restarted under whoever was watching.
                            if (here() == Screen::Home) openPowerMenu();
                            break;
                        default: break;
                    }
                    break;
                // THE TRIGGERS ARE FAST NAVIGATION BY LETTER, and they are an
                // AXIS rather than a button: a trigger reports how far it is
                // pulled, from 0 to 32767, so it has to be edge-detected here
                // or one pull would fire a hundred jumps on its way down.
                //
                // The threshold is deliberately high and the release threshold
                // deliberately lower. A trigger at rest on a worn pad does not
                // read zero, and a single threshold turns that into a jump
                // every time the pad is picked up.
                case SDL_EVENT_GAMEPAD_AXIS_MOTION: {
                    if (owner != InputOwner::UI) break;
                    const bool left = e.gaxis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
                    const bool right = e.gaxis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
                    if (!left && !right) break;
                    bool& held = left ? l2Held : r2Held;
                    const int v = e.gaxis.value;
                    if (!held && v > 20000) {
                        held = true;
                        if (here() == Screen::Grid) gridScreen.jumpLetter(left ? -1 : +1);
                    } else if (held && v < 8000) {
                        held = false;
                    }
                    break;
                }
                case SDL_EVENT_GAMEPAD_BUTTON_UP:
                    switch (e.gbutton.button) {
                        case SDL_GAMEPAD_BUTTON_DPAD_LEFT:
                            releaseNav(screens::Nav::Left); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT:
                            releaseNav(screens::Nav::Right); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_UP:
                            releaseNav(screens::Nav::Up); break;
                        case SDL_GAMEPAD_BUTTON_DPAD_DOWN:
                            releaseNav(screens::Nav::Down); break;
                        default: break;
                    }
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

        // Kept current every frame, so it can tell a wake the moment one
        // happens rather than only when a key asks.
        power::justWoke();

        // THE MACHINE IS ABOUT TO GO DOWN OR SLEEP, and logind is holding it
        // for us. Leave the game the one way games are left — finishExit, every
        // save class for every emulator — then let it go once the uploads are
        // through, or at 25 s, inside the image's 30 s allowance.
        switch (power::poll()) {
            case power::Event::GoingDown:
            case power::Event::GoingToSleep:
                std::fprintf(stderr, "[shutdown] the machine is going down%s\n",
                             playing ? "; leaving the game first" : "");
                if (playing && cab::Core::shared().running()) finishExit();
                releasePending = true;
                releaseWaitStart = SDL_GetTicksNS();
                break;
            case power::Event::Woke:
            case power::Event::None:
                break;
        }
        // USB DRIVES coming, going and ejected. The pill and nothing else: no
        // sound, because a chime would play over a game. MMagTech, 2026-09-25.
        if (const drives::Notice dn = drives::poll(); dn.event != drives::Event::None) {
            const std::string drive = dn.external ? "External drive" : "Internal drive";
            switch (dn.event) {
                case drives::Event::Connected:
                    menuNotice.say(drive + " connected", Tone::Done);
                    break;
                case drives::Event::WrongFormat:
                    menuNotice.say(drive + " isn't exFAT or NTFS", Tone::Problem);
                    break;
                case drives::Event::CouldNotUse:
                    menuNotice.say("Couldn't use the " + std::string(dn.external ? "external" : "internal") +
                                       " drive", Tone::Problem);
                    break;
                case drives::Event::Removed:
                    menuNotice.say(drive + " removed", Tone::Info);
                    break;
                case drives::Event::SafeToUnplug:
                    menuNotice.say("Safe to unplug", Tone::Done);
                    break;
                case drives::Event::EjectFailed:
                    menuNotice.say("Couldn't eject the external drive", Tone::Problem);
                    break;
                case drives::Event::FormatFailed:
                    menuNotice.say("Couldn't format the drive", Tone::Problem);
                    break;
                case drives::Event::None:
                case drives::Event::Changed:
                    break;
            }
            // File access shows each drive as a folder; a drive that came or
            // went changes the list. Root rebuilds it (cabinetos-files refresh).
            // Changed covers an internal drive arriving or leaving, which has
            // no notice of its own.
            if (filesState.on && (dn.event == drives::Event::Connected ||
                                  dn.event == drives::Event::Removed ||
                                  dn.event == drives::Event::SafeToUnplug ||
                                  dn.event == drives::Event::Changed))
                files::refreshDrives(nullptr);
            if (here() == Screen::Settings) buildSettings();
        }
        if (releasePending) {
            const double waited = (SDL_GetTicksNS() - releaseWaitStart) / 1e9;
            if (uploader.pending() == 0 || waited > 25.0) {
                if (uploader.pending() > 0)
                    std::fprintf(stderr, "[shutdown] going down with %d upload(s) still owed\n",
                                 uploader.pending());
                releasePending = false;
                power::releaseDelay();
            }
        }
        if (restPending) {
            const double waited = (SDL_GetTicksNS() - restWaitStart) / 1e9;
            if (uploader.pending() == 0 || waited > 20.0) {
                if (uploader.pending() > 0)
                    std::fprintf(stderr, "[power] resting with %d upload(s) still owed\n",
                                 uploader.pending());
                restPending = false;
                power::act(power::Action::Rest);
            }
        }

        // Idle, once a frame. The timers only ever deepen here; input() is
        // what lifts them, above.
        idleFrame(dt, playing && !overlayOpen, kScreenOff[screenOffIndex].seconds);

        // The held direction, repeating and speeding up. Driven from the frame
        // loop rather than from the event queue, because the pad sends nothing
        // at all while a button is held down.
        if (navHeld && inputOwner() == InputOwner::UI && !keyboard.isOpen()) {
            heldFor += dt;
            if (heldFor >= nextRepeat) {
                navigate(heldNav);
                const float t = std::clamp(heldFor / kRepeatRamp, 0.0f, 1.0f);
                nextRepeat = heldFor + (kRepeatStart + (kRepeatFast - kRepeatStart) * t);
            }
        } else {
            navHeld = false;
        }

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
                        const int lt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_LEFT_TRIGGER);
                        const int rt = SDL_GetGamepadAxis(gp, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER);
                        if (lt > kTrigger) pad.buttons |= bit(cab::L2);
                        if (rt > kTrigger) pad.buttons |= bit(cab::R2);
                        // AND HOW FAR, not just whether. A Dreamcast reads its
                        // triggers as a continuous value — they are the
                        // accelerator and the brake in a driving game — and
                        // Flycast asks for that through the analogue channel
                        // rather than the button. The digital bits above still
                        // go out for every core that wants a shoulder.
                        //
                        // A pad with switches instead of springs, which is what
                        // a Switch Pro Controller's ZL and ZR are, hands SDL a
                        // clean 0 or 32767 and arrives here as 0 or 1.
                        pad.leftTrigger = std::clamp(lt / 32767.0f, 0.0f, 1.0f);
                        pad.rightTrigger = std::clamp(rt / 32767.0f, 0.0f, 1.0f);

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
            // AND FOR PLAYSTATION 2 THAT IS NOT ENOUGH, because PCSX2 is not
            // stepped by this loop at all: it runs its own machine on a thread
            // of its own and would carry on playing behind the menu exactly as
            // every core did before 2026-09-19. It has to be TOLD. A no-op for
            // all twenty-one libretro cores, so it is stated unconditionally
            // rather than behind a test somebody has to remember.
            core.setPaused(overlayOpen);

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
        // The pairing worker's answer, picked up on the frame thread. Nothing
        // here touches the network — it reads what the thread published.
        if (here() == Screen::AddAccount) {
            bool code = false, fin = false, ok = false;
            std::string url, user, err;
            accounts::Paired who;
            {
                std::lock_guard<std::mutex> lk(addJob->m);
                code = addJob->haveCode; fin = addJob->finished; ok = addJob->ok;
                url = addJob->pairing.verificationUrl;
                user = addJob->pairing.userCode;
                err = addJob->err;
                who = addJob->who;
            }
            if (fin) {
                { std::lock_guard<std::mutex> lk(addJob->m); addJob->finished = false; }
                if (ok && who.isNew) {
                    // Added, NOT switched to. Back to the panel with the new
                    // person in it, which is where the switch is.
                    refreshAccountRows();
                    // AND SETTINGS UNDER IT, which it may have been opened
                    // from: Remove an account stayed greyed out until Settings
                    // was left and re-entered. MMagTech, on the A9.
                    buildSettings();
                    if (stack.size() > 1) stack.pop_back();
                    accountsOpen = true;
                    barFocused = true;
                    barSlot = BarAccount;
                    // NO NOTE. "<name> was added. Choose them to switch." used
                    // to sit under the list; MMagTech, 2026-09-24: it looked
                    // bad and said what scanning the code had just done. Their
                    // name appearing in the list is the confirmation.
                    accountScreen.setNotice("");
                    std::fprintf(stderr, "[accounts] added %d - %s, now %zu accounts, "
                                         "still acting as %d\n",
                                 who.id, who.name.c_str(), accounts::all().size(),
                                 accounts::activeId());
                } else if (ok) {
                    // **THE CASE THAT LIED.** The pairing worked and wrote a
                    // valid token, and it added nobody: whoever approved it
                    // already has an account here. Staying on this screen and
                    // saying so is right — going back to a panel that looks
                    // exactly as it did is what made this look broken.
                    addAccountScreen.setError(
                        who.name + " is already on this console. "
                        "Sign in to RomM as the person you are adding (a private "
                        "window is easiest) and try again.");
                    std::fprintf(stderr, "[accounts] NOT ADDED: approved as %d - %s, "
                                         "who is already here. %zu accounts.\n",
                                 who.id, who.name.c_str(), accounts::all().size());
                } else {
                    addAccountScreen.setError(err.empty() ? "That did not pair." : err);
                    std::fprintf(stderr, "[accounts] add failed: %s\n",
                                 err.empty() ? "no reason given" : err.c_str());
                }
            } else if (code && !shownPairCode) {
                shownPairCode = true;
                addAccountScreen.setPairing(url, user);
                // SAID IN THE JOURNAL AS WELL AS ON THE TELEVISION. A code
                // that exists only as pixels cannot be read back by anybody
                // helping from a shell, and this is the one screen whose whole
                // content is a string somebody has to act on within minutes.
                // It is not a secret: the device code is, and that is not this.
                std::fprintf(stderr, "[accounts] pair at %s (code %s)\n",
                             url.c_str(), user.c_str());
            }
        }
        if (autoSwitchAccountId > 0) {
            const int id = autoSwitchAccountId;
            autoSwitchAccountId = 0;
            std::string why;
            if (!switchAccount(id, &why))
                std::fprintf(stderr, "[accounts] refused: %s\n", why.c_str());
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
                // NOT ON HOME, SO NOT IN THE LIBRARY, since 2026-09-22: boot
                // stopped fetching the whole catalogue, and `--launch` quietly
                // lost every game that is not Recent or a Favorite. It said
                // "no game with id" and idled, which is how the save work of
                // 2026-09-23 found it. Asked for by id instead, the way a
                // platform grid or Search would have fetched it.
                if (lib.byRomId.find(id) == lib.byRomId.end()) {
                    romm::Game g;
                    std::string err;
                    if (liveClient.fetchGame(id, &g, &err)) {
                        appendGame(lib, g);
                        std::fprintf(stderr, "[launch] fetched %s by id\n",
                                     g.name.c_str());
                    } else {
                        std::fprintf(stderr, "[launch] could not fetch rom %d: %s\n",
                                     id, err.c_str());
                    }
                }
                launchById(id);
            }
        }

        pumpLaunch();
        pumpSwitch();
        pumpLeave();
        pumpExit();
        pumpStateLoad(stateLoad, session, menuNotice);
        if (stateLoad.loaded) {
            stateLoad.loaded = false;
            closeOverlay();
        }
        if (overlayDemo && playing && !overlayOpen &&
            cab::Core::shared().framesRun() >= static_cast<uint64_t>(overlayDemoAfter)) {
            overlayDemo = false;
            toggleOverlay();
        }

        // --overlay-test: hold the pause menu open over a game this console did
        // not draw. `playing` is asserted so the whole world — backdrop, hero,
        // shelves, screens — is skipped; there is no core, so no game picture is
        // drawn either, and what reaches the screen is the scrim and the panel
        // over transparency. That is the thing being looked at.
        if (overlayTest) {
            playing = true;
            if (!overlayOpen) {
                overlayOpen = true;
                overlayFade.retarget(1.0f, overlayFadeSeconds);
            }
        }
        if (overlayExitDemo && playing) {
            static int t = 0;
            // How long the game is left running before the overlay quits it.
            // Was a fixed 120 frames, which is two seconds — long enough to
            // watch the transition and far too short for anything else. A PSP
            // game is still BOOTING at that point, and a save cannot be tested
            // at all because the game has not had time to write one.
            if (++t == overlayExitAfter) {
                toggleOverlay();
                overlaySlot = static_cast<int>(pauseItems.size()) - 1;   // Exit to Home
            }
            if (t == overlayExitAfter + 60) { overlayExitDemo = false; overlayActivate(); }
        }

        if (stateCheckAfter > 0 && playing) {
            static std::vector<uint8_t> first;
            static uint64_t savedAt = 0;
            cab::Core& core = cab::Core::shared();
            if (first.empty() && core.framesRun() >= static_cast<uint64_t>(stateCheckAfter)) {
                if (!core.saveState(first) || first.empty()) {
                    std::fprintf(stderr, "[state-check] %s cannot make a state\n",
                                 launchJob.coreName.c_str());
                    stateCheckAfter = 0;
                } else {
                    savedAt = core.framesRun();
                }
            } else if (!first.empty() && core.framesRun() >= savedAt + 120) {
                const bool loaded = core.loadState(first);
                std::vector<uint8_t> again;
                core.saveState(again);
                size_t same = 0;
                for (size_t k = 0; k < std::min(first.size(), again.size()); ++k)
                    if (first[k] == again[k]) ++same;
                std::fprintf(stderr,
                             "[state-check] %s: %zu bytes, load %s, re-save %s "
                             "(%zu of %zu bytes equal)\n",
                             launchJob.coreName.c_str(), first.size(),
                             loaded ? "ACCEPTED" : "REFUSED",
                             again == first ? "IDENTICAL" : "DIFFERS",
                             same, first.size());
                stateCheckAfter = 0;
            }
        }

        if (loadStateAfter > 0 && playing &&
            cab::Core::shared().framesRun() >= static_cast<uint64_t>(loadStateAfter)) {
            loadStateAfter = 0;
            std::fprintf(stderr, "[load-state] loading the newest state\n");
            beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
        }

        // The round trip, once, a couple of seconds into the game so there is
        // something in memory worth snapshotting.
        if (syncTest && playing) {
            static int sinceStart = 0;
            if (++sinceStart == 150) {
                std::fprintf(stderr, "[sync-test] --- saving ---\n");
                const uint64_t t0 = SDL_GetTicksNS();
                saveStateNow(session, uploader, menuNotice);
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
                beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
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
        scrollY.tick(dt);
        backdropMix.tick(dt);
        curtain.tick(dt);
        switchText.tick(dt);
        menuNotice.tick(dt);
        // ---- System update: root's answers, the weekly check, the panel ----
        updPoll -= dt;
        updAutoWait -= dt;
        if (updPoll <= 0.0f) {
            updPoll = 0.5f;
            {
                // File access: root's answer. A state said on screen waits
                // for an answer written after it; ten seconds without one
                // and it gives up waiting and shows what root last said.
                const files::State f = files::read();
                const bool answered = filesSaidAt == 0 || f.at >= filesSaidAt ||
                                      std::time(nullptr) - filesSaidAt > 10;
                if (answered && (f.on != filesState.on || f.failed != filesState.failed ||
                                 f.reason != filesState.reason || f.at != filesState.at ||
                                 filesSaidAt != 0)) {
                    filesState = f;
                    filesSaidAt = 0;
                    std::fprintf(stderr, "[files] %s%s\n",
                                 f.on ? "on" : (f.failed ? "failed: " : "off"), f.reason.c_str());
                    if (here() == Screen::Settings) buildSettings();
                    if (filesPanelWhenOn) {
                        filesPanelWhenOn = false;
                        if (f.on && here() == Screen::Settings && !choiceScreen.isOpen() &&
                            !pinScreen.isOpen())
                            openFilesPanel();
                    }
                }
            }
            const update::Status s = update::read();
            const bool mine = updSaidAt == 0 || s.at >= updSaidAt;
            const bool changed = s.state != upd.state || s.at != upd.at || s.done != upd.done ||
                                 s.written != upd.written || s.version != upd.version ||
                                 s.reason != upd.reason;
            if (mine && changed) {
                upd = s;
                updSaidAt = 0;
                std::fprintf(stderr, "[update] %s%s%s%s\n",
                             s.state == update::State::Checking      ? "checking"
                             : s.state == update::State::UpToDate    ? "up to date"
                             : s.state == update::State::Available   ? "available "
                             : s.state == update::State::Downloading ? "downloading "
                             : s.state == update::State::Installing  ? "installing "
                             : s.state == update::State::Ready       ? "ready "
                             : s.state == update::State::Failed      ? "failed: "
                                                                     : "none",
                             s.version.c_str(),
                             !s.version.empty() && !s.reason.empty() ? ", " : "",
                             s.reason.c_str());
                // A check that finished: remember when, and what it found.
                // Both kinds of check write the "Checked" line.
                if ((s.state == update::State::UpToDate || s.state == update::State::Available) &&
                    s.at > prefNum("update_checked")) {
                    prefs::set("update_checked", std::to_string(s.at));
                    prefs::set("update_found",
                               s.state == update::State::Available ? s.version : "");
                    prefs::set("update_size", std::to_string(s.size));
                    if (s.state == update::State::Available &&
                        prefs::get("update_announced", "") != s.version) {
                        // Once per new version, and only when nobody asked:
                        // a check somebody pressed answers in its own row.
                        if (updAuto) menuNotice.say("Update available", Tone::Info);
                        prefs::set("update_announced", s.version);
                    }
                }
                // Staged: ask once per version, remembering the boot it was
                // staged in so the next boot can tell whether it applied.
                if (s.state == update::State::Ready &&
                    (prefs::get("update_pending", "") != s.version ||
                     prefs::get("update_pending_boot", "") != update::bootId())) {
                    prefs::set("update_pending", s.version);
                    prefs::set("update_pending_boot", update::bootId());
                    updReadyPanel = true;
                    updReadyAfterGame = playing;
                }
                // The weekly check says nothing when it fails; it tries again
                // later, which is how "offline skips it" and "reconnecting
                // catches up" both happen with no separate trigger.
                // The answer to a check somebody pressed: an update opens the
                // panel, if they are still in Settings and nothing else is up.
                if (updAskAfterCheck && !s.busy()) {
                    updAskAfterCheck = false;
                    if (s.state == update::State::Available && here() == Screen::Settings &&
                        !choiceScreen.isOpen() && !pinScreen.isOpen() && !keyboard.isOpen())
                        askUpdateDownload();
                }
                if (!s.busy()) updAuto = false;
                if (here() == Screen::Settings) buildSettings();
            } else if (upd.state == update::State::Installing && here() == Screen::Settings) {
                buildSettings();   // the clock on "Installing…" moves by itself
            }
            // WEEKLY: on Home, no game, nothing else under way, more than seven
            // days since the last check that worked, and not more than once
            // every half hour while it keeps failing.
            if (updWeekly && !playing && here() == Screen::Home && !upd.busy() &&
                upd.state != update::State::Ready && updAutoWait <= 0.0f &&
                std::time(nullptr) - prefNum("update_checked") > 7 * 86400) {
                updAutoWait = 1800.0f;
                std::fprintf(stderr, "[update] weekly check\n");
                startUpdate(/*download=*/false, /*automatic=*/true);
            }
        }
        // NEVER DURING A GAME. Ready while playing waits for the game to be
        // closed, then asks once on Home; ready anywhere else asks at once,
        // unless something else is already asking.
        if (updReadyPanel && !playing && !overlayOpen && !choiceScreen.isOpen() &&
            !pinScreen.isOpen() && !keyboard.isOpen() && !accountsOpen &&
            leaving == Leave::None && switchPendingId == 0 && curtain.value() < 0.01f &&
            (!updReadyAfterGame || here() == Screen::Home)) {
            updReadyPanel = false;
            askUpdateReady();
        }
        if (!updStartNotice.empty() && !playing && here() == Screen::Home &&
            curtain.value() < 0.01f) {
            menuNotice.say(updStartNotice, updStartTone);
            updStartNotice.clear();
        }
        if (noticeGallery) {
            // Four seconds each, the first after two so the screen has settled.
            static float galleryClock = -2.0f;
            static int galleryNext = 0;
            galleryClock += dt;
            constexpr int kN = static_cast<int>(sizeof kNoticeGallery / sizeof kNoticeGallery[0]);
            if (galleryClock >= 0.0f && galleryNext < kN * 100) {
                const GalleryNotice& g = kNoticeGallery[galleryNext % kN];
                std::fprintf(stderr, "[notice] gallery %d/%d: %s\n", galleryNext % kN + 1, kN,
                             g.text);
                menuNotice.say(g.text, g.tone);
                // Hold it for the whole slot, including a Busy one.
                menuNotice.life = 3.8f;
                ++galleryNext;
                galleryClock = -4.0f;
            }
        }
        // The other half of the sentence Save started. Cabinet's own words,
        // because they are better than anything invented here: it either
        // reached the server or it is waiting for signal.
        if (const int outcome = uploader.stateOutcome.exchange(0); outcome != 0)
            menuNotice.say(outcome == 1 ? "Saved to RomM"
                                        : "Saved. Will upload when RomM is back",
                           outcome == 1 ? Tone::Done : Tone::Info);
        // Reset the moment it stops, so the next launch starts its own clock
        // and a second press cannot inherit the first one's patience.
        launchJob.busyFor = launchJob.busy() ? launchJob.busyFor + dt : 0.0f;
        overlayFade.tick(dt);
        overlayFocus.tick(dt);
        {
            // Every screen ticks, not only the one in front: a screen that is
            // pushed over keeps its focus animation settled rather than
            // resuming mid-transition when the person comes back to it.
            screens::Ctx ctx{renderer, text, images, renderer.scale(), &cards};
            libraryScreen.tick(dt);
            accountScreen.tick(dt);
            addAccountScreen.tick(dt);
            settingsScreen.setHasFocus(!barFocused && !accountsOpen);
            settingsScreen.tick(dt);
            pinScreen.tick(dt);
            choiceScreen.tick(dt);
            downloadsPanel.tick(dt);
            tabDissolve.tick(dt);
            keyboardSlide.tick(dt);
            if (keyboard.sliding() || keyboard.isOpen()) {
                keyboard.setSlide(keyboardSlide.value());
                if (keyboard.sliding() && keyboardSlide.elapsed >= keyboardSlide.duration) {
                    keyboard.endSlide();
                    // Back to "in place", so a keyboard opened any other way
                    // later is not left waiting below the screen.
                    keyboardSlide.settle(0.0f);
                }
            }
            if (tabSince >= 0.0f) {
                tabSince += dt;
                if (tabSince > kTabContentDelay + kTabContentIn) tabSince = -1.0f;
            }
            gridScreen.tick(dt, ctx);
            // The network's answer, when it lands. Rebuilt only if Settings is
            // still what is on screen; the next visit asks again anyway.
            {
                bool fresh = false;
                {
                    std::lock_guard<std::mutex> lk(settingsNet.m);
                    fresh = settingsNet.fresh;
                    settingsNet.fresh = false;
                }
                if (fresh && here() == Screen::Settings) buildSettings();
            }
            {
                bool fresh = false;
                {
                    std::lock_guard<std::mutex> lk(settingsWifi.m);
                    fresh = settingsWifi.fresh;
                    settingsWifi.fresh = false;
                }
                if (!choiceScreen.isOpen()) wifiPanelOpen = false;
                // Leaving Settings locks the PIN again.
                if (here() != Screen::Settings && !pinScreen.isOpen()) pinUnlocked = false;
                if (here() != Screen::Settings) downloadsPanel.close();
                if (fresh && wifiPanelOpen) {
                    // The list is open: refresh it in place, focus kept.
                    sortWifi();
                    std::vector<std::string> names, values;
                    wifiPanelRows(&names, &values);
                    choiceScreen.replace(names, values,
                                         names.empty() ? (settingsWifi.running.load()
                                                              ? "Looking for networks\xE2\x80\xA6"
                                                              : "No networks found")
                                                       : "");
                }
                if (fresh && here() == Screen::Settings) buildSettings();
            }
            {
                // A join finished: say how it went, then ask again, so the
                // rows and the Status line say what is true now.
                bool done = false, ok = false;
                std::string ssid, err;
                {
                    std::lock_guard<std::mutex> lk(wifiJob.m);
                    if (wifiJob.done) {
                        done = true;
                        wifiJob.done = false;
                        ok = wifiJob.ok;
                        ssid = wifiJob.ssid;
                        err = wifiJob.err;
                    }
                }
                if (done) {
                    // "Secrets were required" is nmcli's wrong password.
                    const bool badPass = err.find("ecrets") != std::string::npos ||
                                         err.find("psk") != std::string::npos;
                    // Is the keyboard still up, waiting on this very join?
                    const bool waiting = keyboard.isOpen() && keyboard.busy() &&
                                         wifiKeyboardFor == ssid;
                    if (waiting && badPass) {
                        // ANOTHER GO, IN PLACE: the same panel, emptied, saying
                        // why. MMagTech tested a wrong password on the TV:
                        // "kicked out and hard to re-enter", then a strobe.
                        askWifiPassword(ssid, false, "Wrong password");
                    } else {
                        if (waiting) {
                            keyboard.cancel();
                            keyboardThen = nullptr;
                        }
                        if (ok) menuNotice.say("Connected to " + ssid, Tone::Done);
                        else if (badPass) menuNotice.say("Wrong password", Tone::Problem);
                        else menuNotice.say("Couldn't join " + ssid, Tone::Problem);
                    }
                    wifiKeyboardFor.clear();
                    askNetwork();
                    askWifi();
                    if (here() == Screen::Settings) buildSettings();
                }
            }
            {
                // An address check finished. Same server: saved, and the app
                // starts again on it. Anything else: said in the field, the
                // address kept behind it to correct.
                bool done = false;
                server::Check r = server::Check::Failed;
                std::string addr;
                {
                    // "CHECKING…" STAYS UP 700 MS AT LEAST. A refused port
                    // answers in 10 ms and the word flashed for one frame,
                    // which reads as a glitch: the account switch's lesson.
                    std::lock_guard<std::mutex> lk(serverJob.m);
                    if (serverJob.done && SDL_GetTicks() - serverJob.startedAt >= 700) {
                        done = true;
                        serverJob.done = false;
                        r = serverJob.result;
                        addr = serverJob.address;
                    }
                }
                // B while it checked means leave it alone, whatever it found.
                if (done && keyboard.isOpen() && keyboard.busy() && serverKeyboardFor == addr) {
                    serverKeyboardFor.clear();
                    std::string err;
                    const std::string from = rommAddress ? rommAddress : "";
                    if (r == server::Check::Same && server::changeAddress(from, addr, &err)) {
                        keyboard.cancel();
                        keyboardThen = nullptr;
                        startLeaving(Leave::NewAddress, "Connecting to " + addr);
                    } else if (r == server::Check::Same) {
                        std::fprintf(stderr, "[server] could not save %s: %s\n",
                                     addr.c_str(), err.c_str());
                        askServerAddress(addr, "Couldn't save it");
                    } else {
                        askServerAddress(addr,
                                         r == server::Check::Different ? "A different server: use Sign out"
                                         : r == server::Check::NoServer
                                             ? "No RomM server there"
                                             : "Couldn't check it");
                    }
                }
            }
            searchScreen.tick(dt, ctx);

            // The launch screen is told what its own game is doing. Only its
            // own: a background download of something else belongs in the bar's
            // corner, not on this game's Play button.
            screens::DetailScreen::Progress prog;
            if (launchJob.busy() && launchJob.busyFor >= kProgressDelay &&
                launchJob.romId == detailScreen.game().romId) {
                prog.active = true;
                // WHICH ROW LIT UP IS DECIDED BY WHAT WAS PRESSED. A download
                // started by "Download and keep" must not make Play look busy,
                // and a Play that joined a running download must.
                prog.action = launchJob.playWhenReady ? screens::Action::Play
                                                      : screens::Action::Download;
                prog.got = launchJob.got.load();
                prog.total = launchJob.total.load();
                prog.unpacking =
                    launchJob.stage.load() == LaunchJob::Stage::Unpacking;
            }
            detailScreen.setProgress(prog);

            // THE SEARCH LOOP. It used to be three lines because the whole
            // library was in memory and a substring match was free. It asks
            // the server now — open question 28 — so it is in two halves:
            // notice what was typed, and some time after the typing stops, go
            // and ask.
            if (here() == Screen::Search && keyboard.isOpen() &&
                keyboard.value() != searchTyped) {
                searchTyped = keyboard.value();
                searchScreen.setQuery(searchTyped);
                searchPending = searchTyped;
                searchDebounce = kSearchDebounce;
                // Said immediately, not when the request goes out: the gap
                // between the last keypress and the answer is exactly the
                // stretch where a screen that says nothing looks broken.
                searchScreen.setWaiting();
            }
            if (!searchPending.empty()) {
                searchDebounce -= dt;
                if (searchDebounce <= 0.0f) {
                    const std::string q = searchPending;
                    searchPending.clear();
                    // BLOCKING, ON THE FRAME THREAD, AND MEASURED RATHER THAN
                    // ASSUMED: 40-80 ms against the reference server, which is
                    // a few dropped frames once the typing has already
                    // stopped. A worker thread would hide it and is what this
                    // wants if the wait is ever felt on a slower server; the
                    // shape to copy is ImageCache's, not a second event loop.
                    runSearch(q);
                }
            }
            // Pages of a big grid that arrived behind the first one. Applied
            // here because this thread owns the store and the screen.
            if (here() == Screen::Grid) {
                std::vector<romm::Game> got;
                {
                    std::lock_guard<std::mutex> lk(gridFill.m);
                    got.swap(gridFill.arrived);
                }
                if (!got.empty()) {
                    std::vector<int> added;
                    added.reserve(got.size());
                    screens::Tile* t = nullptr;
                    for (auto& v : {&platformTiles, &collectionTiles})
                        for (screens::Tile& x : *v)
                            if (x.id == gridFill.tileId) { t = &x; break; }
                    for (const auto& g : got) {
                        if (!catalog::playable(g)) continue;
                        const int i = appendGame(lib, g);
                        added.push_back(i);
                        if (t) t->cards.push_back(i);
                    }
                    gridScreen.append(added, cards);
                }
                gridScreen.setLoadingMore(gridFill.running.load());
            }

            // Covers that arrived behind Home. Applied here because this is
            // the thread that owns the tiles and the screen drawing them.
            {
                std::vector<std::pair<int, std::string>> arrived;
                {
                    std::lock_guard<std::mutex> lk(coverFill.m);
                    arrived.swap(coverFill.done);
                }
                for (const auto& [id, cover] : arrived) {
                    for (screens::Tile& t : platformTiles)
                        if (t.id == id && t.cover.empty()) { t.cover = cover; break; }
                    libraryScreen.learnedTile(id, "", cover);
                    auto it = lib.tileCache.find(id);
                    if (it != lib.tileCache.end()) it->second.cover = cover;
                }
                // Written when the last one lands rather than per cover: this
                // is thirty-six short lines and the next boot is what reads it.
                if (!arrived.empty() && coverFill.next.load() >= coverFill.want.size())
                    covercache::saveTiles(lib.tileCache);
            }

            // The results are sized to the room the keyboard leaves, and the
            // keyboard's height depends on its own layout — so it is asked
            // rather than assumed.
            if (here() == Screen::Search)
                searchScreen.setResultsBottom(keyboard.isOpen() ? keyboard.panelTop()
                                                                : ui::kCanvasHeight);
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
        // The top bar belongs to HOME. It used to be drawn inside the hero's
        // block, which meant a console with nothing playable in its recent
        // history had no way to reach Library at all.
        const float sc = renderer.scale();
        if (playing) {
            // BLACK behind a running game, not the menu's backdrop. The
            // reference implementation's player clears to black, and it is
            // right: a gradient around a game picture is decoration competing
            // with the thing you are looking at, and the letterbox glow is
            // bias lighting, which means light against black. On a purple
            // backdrop it is neither.
            //
            // EXCEPT WHEN THE GAME IS NOT OURS TO DRAW. On the composited path
            // the emulator owns its own window and gamescope puts our frame on
            // top, so this fill would black the game out. Drawing nothing is
            // what lets it through — the scrim below still dims it, because a
            // translucent black rectangle composites exactly as it should.
            if (!overlayTest)
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(1.0f)});
        } else {
            renderer.drawBackdrop(ui::Gradient{ui::palette::kBackdropTop,
                                               ui::palette::kBackdropMid,
                                               ui::palette::kBackdropBottom, 0.55f});

        // ---- THE BACKDROP FOLLOWS FOCUS, ON EVERY BROWSING SCREEN ----------
        //
        // The design system's kHomeBackdrop* block has the reasoning and the
        // three numbers to argue about. What happens here is the mechanism:
        //
        //  1. Ask what focus is pointing at. On Home that is a card; on the
        //     Library it is the focused tile; in a platform's grid it is the
        //     focused game. The TOP BAR is not a game and neither is a switcher
        //     pill, so walking onto one changes nothing — the room stays lit by
        //     whatever you were on rather than blanking on the way past.
        //  2. Do nothing until that answer has held still for kHomeBackdropDelay.
        //     A controller crosses a shelf far faster than the 180ms focus
        //     tempo and a backdrop chasing it frame for frame is a strobe.
        //  3. Cross-fade, old under new. One texture replacing another with a
        //     cut is the failure this is built to avoid.
        //
        // IT RUNS OUTSIDE ANY ONE SCREEN, and that is the second reason it is
        // here rather than in Home. MMagTech, on the panel: *"after this
        // transition to the library it isnt smooth."* It was a cut in two ways
        // at once — the Library's content appeared between one frame and the
        // next, AND the lit room vanished with Home, so the whole screen
        // changed colour instantly. The content now fades (Renderer::
        // setContentAlpha) and the room does not go anywhere: it simply
        // re-lights from the tile you land on. The ground staying put while the
        // content changes is most of what makes a move read as a move.
        //
        // THE LARGE COVER, NOT THE SHELF'S THUMBNAIL. Filling 1920x1080 from a
        // 162x216 PNG is a 12x upscale of an image that was already a
        // thumbnail; the 810x1080 original costs about 1.5x the bytes and is
        // the only reason this looks like anything. romm.h has the measurement.
        if (!playing && here() != Screen::Detail) {
            // The launch screen is excluded because it already IS this idea,
            // at full strength: a game's cover filled, blurred and scrimmed
            // across the whole screen. Two of them would fight.
            std::string want = backdropWant;
            if (here() == Screen::Home) {
                const Card* lit = cardAt(focusRow, focusSlot);
                if (lit) want = lit->coverLarge.empty() ? lit->cover : lit->coverLarge;
            } else if (here() == Screen::Library) {
                const std::string tile = libraryScreen.focusedCover();
                if (!tile.empty()) want = tile;
            } else if (here() == Screen::Grid) {
                const int ci = gridScreen.focusedCard();
                if (ci >= 0 && ci < static_cast<int>(cards.size()))
                    want = cards[ci].coverLarge.empty() ? cards[ci].cover
                                                        : cards[ci].coverLarge;
            } else if (here() == Screen::Search) {
                // The room follows the results here too. With nothing found,
                // or nothing typed yet, it is lit by the last game art you
                // were browsing, NOT by whatever the previous screen left: that
                // was the plain purple when you came from Settings, so Search
                // was sometimes plain and sometimes blurred. It is always
                // blurred now.
                const int ci = searchScreen.focusedCard();
                if (ci >= 0 && ci < static_cast<int>(cards.size()))
                    want = cards[ci].coverLarge.empty() ? cards[ci].cover
                                                        : cards[ci].coverLarge;
                else if (!lastLitArt.empty())
                    want = lastLitArt;
            } else if (here() == Screen::AddAccount) {
                // **A TEXT SCREEN GETS THE PLAIN GRADIENT.** MMagTech,
                // 2026-09-22: *"i preferred the purple background that went
                // with the first run setup better. The current background
                // isn't ideal for a text heavy screen."*
                //
                // Every other screen here is a wall of covers, and a colour
                // field lifted from the focused one sits under them. This one
                // is prose, an address and a code — things that have to be
                // READ, and read off a television — and a blurred game cover
                // behind them is contrast nobody chose, different on every
                // visit depending on what happened to be lit on Home.
                //
                // Clearing it rather than excluding this screen from the block
                // above: leaving `want` alone would keep whatever Home was lit
                // by, which is exactly the bleed-through being complained
                // about. It fades out on the same cross-fade everything else
                // uses, so it goes as deliberately as it arrives.
                //
                // It also puts this screen where first run already is, which
                // is the point — the two do the same job minutes apart.
                want.clear();
            } else if (here() == Screen::Settings) {
                // SETTINGS IS A TEXT SCREEN TOO, and gets the plain gradient
                // for the reason above: rows to read, not covers to browse.
                want.clear();
            }
            // Which screen asked, so a change of screen can be told apart
            // from focus moving within one.
            if (want == backdropWant) backdropScreen = here();
            if (!want.empty() && (here() == Screen::Home || here() == Screen::Library ||
                                  here() == Screen::Grid || here() == Screen::Search))
                lastLitArt = want;
            if (want != backdropWant) {
                backdropWant = want;
                backdropSettle = 0.0f;
                // A NEW SCREEN CHANGES THE ROOM AT ONCE, AND AT THE SCREEN'S
                // OWN SPEED. The wait exists for focus running along a shelf;
                // applied to a screen change it left the old art showing for a
                // moment after the new screen had arrived. MMagTech,
                // 2026-09-24, Search to Settings: "you can see the colors
                // before it goes all purple so it looks like a visual bug".
                backdropForScreen = (here() != backdropScreen);
                if (backdropForScreen) backdropSettle = backdropDelay;
            } else if (backdropWant != backdropKey &&
                       backdropMix.elapsed >= backdropMix.duration) {
                // AND NEVER INTERRUPT A FADE THAT IS STILL RUNNING. Committing
                // mid-flight drops the outgoing layer at whatever alpha it had
                // reached — at a third of the way through, a picture showing at
                // 70% vanishes in one frame, which is a pop in the middle of
                // the very transition that exists to avoid one. Two layers can
                // cross-fade; three cannot, so the third waits its turn.
                backdropSettle += dt;
                // A CAPTURE HAS NO WALL CLOCK TO WAIT ON. `--frames N` on the
                // A9's Radeon goes by in well under the 220ms this delay
                // wants — the same trap `--launch-after` fell into, recorded in
                // docs/NEXT-SESSION.md — so a screenshot would show the screen
                // with no backdrop at all and look like the feature was not
                // built. Every other animation here already settles for a
                // capture; this one joins them.
                if (shotMode) backdropSettle = backdropDelay;
                if (backdropSettle >= backdropDelay) {
                    backdropPrevKey = backdropKey;
                    backdropKey = backdropWant;
                    backdropMix.from = 0.0f;
                    backdropMix.to = 0.0f;
                    backdropMix.elapsed = 0.0f;
                    // A new screen's background arrives with the screen, at its
                    // speed, so the two read as one change.
                    backdropMix.retarget(1.0f, backdropForScreen ? kTabArrive
                                                                 : backdropFade);
                    // And arrive, rather than being caught half way in.
                    if (shotMode) backdropMix.elapsed = backdropMix.duration;
                }
            }

            // One cover, cropped to the screen rather than stretched across it.
            // A 3:4 cover mapped 0..1 over 16:9 is a smear that is no longer
            // the art's colours, which is the only thing it is here to be.
            auto drawLit = [&](const std::string& key, float alpha) {
                if (key.empty() || alpha <= 0.001f) return;
                const ui::Image& art = images.get(key);
                if (!art.ready) return;
                const float boxAspect = ui::kCanvasWidth / ui::kCanvasHeight;
                const float imgAspect = art.aspect();
                float u0 = 0, v0 = 0, u1 = 1, v1 = 1;
                if (imgAspect < boxAspect) {
                    const float span = imgAspect / boxAspect;
                    v0 = (1.0f - span) * 0.5f;
                    v1 = v0 + span;
                } else {
                    const float span = boxAspect / imgAspect;
                    u0 = (1.0f - span) * 0.5f;
                    u1 = u0 + span;
                }
                renderer.drawTextured(
                    0, 0, ui::kCanvasWidth, ui::kCanvasHeight, art.texture, u0, v0, u1, v1,
                    ui::Color{1, 1, 1, alpha * art.fade * backdropFill}, false,
                    blurToTexels(static_cast<float>(art.width) * (u1 - u0),
                                 backdropTexels));
            };
            const float mix = backdropMix.value();
            drawLit(backdropPrevKey, 1.0f - mix);
            drawLit(backdropKey, mix);
            // The scrim goes on whether or not there is art, so the content
            // sits on the same ground either way and a game with no cover is
            // not a brighter screen than one with.
            if (!backdropKey.empty())
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(backdropScrim)});
        }
        }

        // The arriving screen's content, held back until the old frame has
        // mostly dissolved. Under the bar, which does not fade; reset below.
        auto tabContent = [&]() {
            if (tabSince < 0.0f || playing) return 1.0f;
            const float t = std::clamp((tabSince - kTabContentDelay) / kTabContentIn,
                                       0.0f, 1.0f);
            return design::easeInOut(t);
        };
        renderer.setContentFade(tabContent());
        renderer.setContentOffsetY(playing ? 0.0f : (1.0f - tabContent()) * kTabRise);

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
                // EVERY SYSTEM FILLS THE HEIGHT NOW — changed 2026-09-21.
                //
                // MMagTech: *"all systems should go to top and bottom of the
                // screen."* Integer scaling was the default and it cost real
                // screen: a 320x240 core on a 1080 canvas scaled by 4 rather
                // than 4.5, so a 960-point picture sat inside 120 points of
                // black bar for no reason a person watching it would accept.
                //
                // WHAT IS GIVEN UP, because it is not nothing. An integer scale
                // makes every source pixel exactly the same size; 4.5 makes
                // some of them five rows tall and some four. On a Game Boy at
                // 160x144 that is a 7.5x scale and the unevenness is visible if
                // you go looking for it. The argument for integer scaling is
                // that those pixels were each a deliberate choice; the argument
                // against is that a third of the screen is a bigger price, and
                // it is the one MMagTech is paying.
                //
                // THE VERTICAL ARCADE BOARDS ALREADY WORKED THIS WAY and the
                // reasoning recorded for them is the same one, reached first:
                // "the dot grid argument that earns integer scaling a Game Boy
                // is worth less here than the 33% of the screen it costs". This
                // is that decision applied everywhere rather than in one place.
                //
                // `--integer-scale` puts it back, for comparing the two on a
                // television, which is the only place the question can be
                // settled.
                const bool integerScale =
                    integerScaling && !core.hardwareRendered() && !quarterTurn;
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
                // Said once per game. The picture's geometry is four numbers
                // that have to agree — what the core hands back, what it says
                // the shape is, the quad the layout builds, and the corner of
                // the texture that is sampled — and when the picture comes out
                // the wrong shape there is no way from outside to tell which
                // of the four is lying.
                {
                    static int saidFor = -1;
                    if (saidFor != session.romId) {
                        saidFor = session.romId;
                        std::fprintf(stderr,
                                     "[picture] core %gx%g aspect %.4f%s -> quad %.0fx%.0f "
                                     "(%.4f) at %.0f,%.0f  uv %.4f,%.4f..%.4f,%.4f\n",
                                     srcW, srcH, core.avInfo().aspectRatio,
                                     quarterTurn ? " TURNED" : "", dw, dh, dw / dh, px, py,
                                     u0, v0, u1, v1);
                    }
                }
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
                case Screen::Search: searchScreen.draw(ctx); break;
                case Screen::AddAccount: addAccountScreen.draw(ctx); break;
                case Screen::Settings: settingsScreen.draw(ctx); break;
                case Screen::Home: break;   // unreachable, and the compiler asks
            }
            // A screen sets the transition alpha and its own scroll window for
            // its drawing, and owns neither for the rest of the frame. Reset
            // both here rather than trusting each screen to, so a new screen
            // cannot silently fade or clip the bar drawn on top of it.
            renderer.setContentAlpha(1.0f);
            renderer.clearScissor();

            // The top vignette, over the clipped content and under the bar that
            // is drawn later. See design::kScrollFade*.
            if (here() == Screen::Library || here() == Screen::Grid) {
                ui::Rect fade{0, 0, ui::kCanvasWidth, kScrollFadeHeight, 0,
                              ui::Color::black(kScrollFadeAlpha)};
                fade.gradient = true;
                fade.fillBottom = ui::Color::black(0.0f);
                renderer.draw(fade);
            }
        } else {

        // The height a shelf will occupy, known before it is drawn so the
        // scroll target can be computed this frame rather than one frame late.
        // No caption row. The focused card's title rides in the shelf header
        // instead, which is what makes Home fit in one screen — see the
        // budget in design.h.
        const float shelfBlockHeight =
            text.lineHeight(ui::TextStyle::Title3, sc) + kShelfHeaderGap + kShelfHeadroom +
            kShelfCoverHeight + kShelfHeadroom;

        const float recentTop = barTop + barHeight + barGapBelow;
        const float favoritesTop = recentTop + shelfBlockHeight;

        // Scroll only as far as the focused row needs. On the bar or Recent
        // that is not at all — Home should not drift under the cursor. With
        // the hero gone it does not scroll at all at 1080, and the machinery
        // stays because a shelf's cover height is a number people change.
        //
        // And never past the end of the content. Pinning the last row to the
        // top of the screen leaves half a screen of nothing under it, which is
        // not what a scroll view does and reads as the layout having broken.
        const float contentHeight =
            (haveFavorites() ? favoritesTop + shelfBlockHeight : recentTop + shelfBlockHeight) +
            kHomeBottomPad;
        const float maxScroll = std::max(0.0f, contentHeight - ui::kCanvasHeight);
        float wantScroll = 0.0f;
        if (focusRow == RowFavorites)
            wantScroll = std::min(favoritesTop - recentTop, maxScroll);
        if (std::fabs(wantScroll - scrollY.to) > 0.5f)
            scrollY.retarget(wantScroll, kFocusDuration);
        const float scroll = scrollY.value();

        // ---- The hero -------------------------------------------------------
        //
        float shelfHeaderY = recentTop - scroll;

        // One shelf, drawn twice: Recent and Favorites are the same component in
        // different arrangements, which is what the design system says every
        // row on this screen is. Returns the height it used, so the caller can
        // stack the next one under it without either knowing the other's size.
        auto drawShelf = [&](const char* label, const std::vector<int>& indices,
                             int rowId, float top) -> float {
            // FOCUS IS IN ONE PLACE AT A TIME. When it is up in the bar — or
            // in the panel the chip opened — Home must stop drawing a focused
            // card, or two things look selected at once and pressing A appears
            // to do something to the wrong one.
            const bool rowFocused = (focusRow == rowId) && !barFocused && !accountsOpen;
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
                const Card& fc = cards[at(slot)];
                float titleX = kContentInset + headerWidth + 10.0f +
                               text.measure("\xE2\x80\xBA", ui::TextStyle::Callout, sc) +
                               24.0f;
                // RESUME IS SAID, NOT IMPLIED. This one card launches straight
                // into the game where every other cover on Home opens a launch
                // screen, and the objection recorded against that was exactly
                // that nothing on the screen would say so. Now something does.
                if (rowId == RowRecent && slot == 0 && haveResume()) {
                    const char* kResume = "\xE2\x96\xB6  Resume";
                    text.draw(renderer, kResume, titleX, headerBaseline,
                              ui::TextStyle::Callout, ui::Color::white(0.95f), sc);
                    titleX += text.measure(kResume, ui::TextStyle::Callout, sc) + 20.0f;
                }
                const float room = ui::kCanvasWidth - kContentInset - titleX;
                std::string line = fc.title;
                // The platform after the name, which the hero's band used to
                // carry and nothing has carried since. A library with a Game
                // Boy and a Genesis "Altered Beast" in it needs this said.
                if (!fc.platform.empty()) line += "   \xC2\xB7   " + fc.platform;
                text.draw(renderer, text.truncate(line, ui::TextStyle::Callout, sc, room),
                          titleX, headerBaseline, ui::TextStyle::Callout,
                          ui::Color::white(0.60f), sc);
            }

            const float shelfTop =
                top + text.lineHeight(ui::TextStyle::Title3, sc) + kShelfHeaderGap + kShelfHeadroom;

            // CULL TO WHAT IS ON SCREEN, and do it before touching the cover
            // cache. This loop used to run over every card: invisible with a
            // six-entry stand-in library, and against a real 1232 it queued a
            // download for every cover in the collection and overran the
            // texture budget. A shelf is a window onto a list, not a drawing of
            // the list. The margin keeps a card's art loading just before it
            // slides in, so the fade has somewhere to start.
            const float kCullMargin = (kShelfCoverWidth + kShelfSpacing) * 2.0f;
            // THE ROW FOLLOWS FOCUS, and only as far as it has to. Moving
            // right, the row slides once the focused card would cross the
            // right edge, and it stops where the NEXT card still peeks in, so
            // there is visibly more to come. Moving back left, it slides once
            // focus would cross the left inset. The last card rests against
            // the right edge; the first against the left inset.
            const float pitch = kShelfCoverWidth + kShelfSpacing;
            if (rowId == RowRecent || rowId == RowFavorites) {
                Animated& rs = shelfScroll[rowId];
                if (rowFocused) {
                    const float left = kContentInset + static_cast<float>(focusSlot) * pitch;
                    const bool last = focusSlot >= static_cast<int>(count) - 1;
                    const float peek = last ? 0.0f : kShelfSpacing + kShelfCoverWidth * 0.35f;
                    const float rightEdge = ui::kCanvasWidth - kContentInset - peek;
                    float target = rs.to;
                    if (left + kShelfCoverWidth - target > rightEdge)
                        target = left + kShelfCoverWidth - rightEdge;
                    if (left - target < kContentInset) target = left - kContentInset;
                    target = std::max(0.0f, target);
                    rs.retarget(target, kFocusDuration * 1.6f);
                }
                rs.tick(dt);
            }
            const float scroll =
                (rowId == RowRecent || rowId == RowFavorites) ? shelfScroll[rowId].value() : 0.0f;
            auto cardBaseX = [&](size_t slot) {
                return kContentInset + static_cast<float>(slot) * pitch - scroll;
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
                    // Unfocused artwork sits back. See design::kRestArtDim.
                    if (f < 1.0f)
                        renderer.draw(ui::Rect{x, y, w, h, kCoverRadius * scale,
                                               ui::Color::black(kRestArtDim * (1.0f - f))});
                    // The kept mark, the same as the grid draws. A mark that
                    // appeared on a game in one screen and not in another is
                    // exactly the drift the shared design system exists to
                    // prevent.
                    if (card.kept) {
                        const float d = kKeptMarkSize * scale;
                        const float mx = x + w - kKeptMarkInset * scale - d;
                        const float my = y + kKeptMarkInset * scale;
                        const float r = kKeptMarkRing * scale;
                        renderer.draw(ui::Rect{mx - r, my - r, d + r * 2.0f, d + r * 2.0f,
                                               (d + r * 2.0f) * 0.5f,
                                               ui::Color::black(0.55f)});
                        renderer.draw(ui::Rect{mx, my, d, d, d * 0.5f,
                                               ui::palette::kScreenCyan});
                    }
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
        if (haveFavorites()) drawShelf("Favorites", favorites, RowFavorites, rowY);
        // SAID, NOT LEFT BLANK, with the way out under it. The first version
        // lit Library in the bar instead, which read as already being on the
        // Library: MMagTech pressed A on what he took for a loaded Library and
        // it only then opened. Home stays Home; the button is the suggestion.
        if (homeEmpty()) {
            const char* title = "Nothing played yet";
            // One line. A second, pointing at the Library, was dropped on
            // MMagTech's word: Library is already lit in the bar.
            const char* detail = "Games you play or favourite will show up here.";
            const float tw = text.measure(title, ui::TextStyle::Title2, sc);
            const float dw = text.measure(detail, ui::TextStyle::Callout, sc);
            const float ty = ui::kCanvasHeight * 0.45f;
            text.draw(renderer, title, (ui::kCanvasWidth - tw) * 0.5f, ty,
                      ui::TextStyle::Title2, ui::Color::white(0.95f), sc);
            text.draw(renderer, detail, (ui::kCanvasWidth - dw) * 0.5f,
                      ty + text.lineHeight(ui::TextStyle::Title2, sc) * 0.9f,
                      ui::TextStyle::Callout, ui::Color::white(0.60f), sc);
            // AN INSTRUCTION, NOT A BUTTON. A focused Open Library button was
            // tried twice, quiet and then white, and neither read as selected,
            // because nobody had moved focus onto it. MMagTech, 2026-09-24:
            // *"since one didn't navigate to end up there it doesn't read as
            // already being selected... it should Press A to Open Library."*
            // The A is drawn as a button badge, the way consoles prompt. It
            // dims while focus is up in the bar, where A means something else.
            const bool on = !barFocused && !accountsOpen;
            const ui::TextStyle st = ui::TextStyle::Title3;
            const char* before = "Press";
            const char* after = "to open the Library";
            const float gap = 16.0f;
            const float badge = text.lineHeight(st, sc) * 0.95f;
            const float w1 = text.measure(before, st, sc);
            const float w2 = text.measure(after, st, sc);
            const float total = w1 + gap + badge + gap + w2;
            const float base = ty + text.lineHeight(ui::TextStyle::Title2, sc) * 0.9f + 96.0f;
            const float alpha = on ? 1.0f : 0.45f;
            float x = (ui::kCanvasWidth - total) * 0.5f;
            text.draw(renderer, before, x, base, st, ui::Color::white(0.92f * alpha), sc);
            x += w1 + gap;
            // The badge: a white disc with a dark A, centred on the text's
            // x-height rather than its baseline.
            const float capMid = base - text.ascent(st, sc) * 0.36f;
            renderer.draw(ui::Rect{x, capMid - badge * 0.5f, badge, badge, badge * 0.5f,
                                   ui::Color::white(0.95f * alpha)});
            const float aw = text.measure("A", st, sc);
            text.draw(renderer, "A", x + (badge - aw) * 0.5f,
                      capMid + text.ascent(st, sc) * 0.36f, st,
                      ui::Color{0.07f, 0.05f, 0.12f, alpha}, sc);
            x += badge + gap;
            text.draw(renderer, after, x, base, st, ui::Color::white(0.92f * alpha), sc);
        }
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
                // Search has no glass: the keyboard docked under it is the only
                // material on the screen and the app draws that itself.
                case Screen::Search: break;
                // The QR draws its own white card; there is nothing behind it
                // on this screen for glass to blur.
                case Screen::AddAccount: break;
                case Screen::Settings: settingsScreen.drawGlass(ctx); break;
                case Screen::Home: break;
            }
            renderer.setContentAlpha(1.0f);
            renderer.clearScissor();
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
            // The 64 is the padding above and below the buttons. There used to
            // be 44 more, reserved for the line the menu answered with; that
            // answer is the notification pill now, so the panel is just its
            // buttons and has no empty band at the foot.
            const int ovN = ovCount();
            const float panelH = static_cast<float>(ovN) * kOverlayButtonHeight +
                                 static_cast<float>(ovN - 1) * kOverlayButtonGap + 64.0f;
            const float px = (ui::kCanvasWidth - kOverlayPanelWidth) * 0.5f;
            // Rises slightly as it arrives rather than only fading: a panel that
            // just materialises reads as a glitch.
            const float py = (ui::kCanvasHeight - panelH) * 0.5f + (1.0f - ovl) * overlayRise;
            // SOLID, NOT GLASS. See design.h, kOverlayPanelFill, for why — in
            // short, glass blurs the console's own scene texture, and on the
            // composited path the game is not in it. A panel that is frosted on
            // one path and flat on the other is the exact difference this
            // console cannot afford between a Mega Drive and a PlayStation 2.
            //
            // The shadow is what separates it from the game now that the blur
            // does not, and it costs nothing on either path.
            ui::Color panelFill = kOverlayPanelSurface;
            panelFill.a = kOverlayPanelFill * ovl;
            ui::Rect panel{px, py, kOverlayPanelWidth, panelH,
                           kOverlayPanelRadius, panelFill};
            // Lit from above: a little lighter at the head, denser and darker
            // at the foot, with a highlight on the top edge only.
            panel.gradient = true;
            panel.fillBottom =
                ui::Color{panelFill.r * kOverlayPanelBottomDarken,
                          panelFill.g * kOverlayPanelBottomDarken,
                          panelFill.b * kOverlayPanelBottomDarken,
                          kOverlayPanelFillBottom * ovl};
            panel.edgeLight = ui::Color::white(kOverlayPanelEdgeLight * ovl);
            panel.border = kOverlayPanelBorder;
            panel.borderColor = ui::Color::white(kOverlayPanelBorderAlpha * ovl);
            panel.shadowBlur = kOverlayPanelShadowBlur;
            panel.shadowOffsetY = kOverlayPanelShadowY;
            panel.shadowColor = ui::Color::black(kOverlayPanelShadowAlpha * ovl);
            renderer.draw(panel);

            for (int i = 0; i < ovN; ++i) {
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

                // FOCUS IS A RIM, the same one every card and pill in this
                // console uses. See design.h — a light-filled bar was tried and
                // it both shouted and invented a second focus idiom for one
                // screen.
                ui::Rect btn{bx, by, bw, bh, kOverlayButtonRadius,
                             ui::Color::white(
                                 (kOverlayButtonRestFill +
                                  f * (kOverlayButtonFocusFill - kOverlayButtonRestFill)) * ovl)};
                if (f > 0.0f) {
                    btn.border = f * kFocusRimWidth;
                    // FADED WITH THE PANEL. It was the one part of the menu
                    // that did not follow `ovl`, so the focus rim arrived at
                    // full strength on the first frame, floating over a panel
                    // that had not faded in yet — MMagTech on the Power menu,
                    // 2026-09-22: *"it opens in pieces."*
                    btn.borderColor = ui::palette::kFocusRim;
                    btn.borderColor.a *= ovl;
                    btn.shadowBlur = kOverlayButtonFocusShadowBlur;
                    btn.shadowOffsetY = kOverlayButtonFocusShadowY;
                    btn.shadowColor =
                        ui::Color::black(kOverlayButtonFocusShadowAlpha * f * ovl);
                }
                renderer.draw(btn);

                const char* label = ovLabel(i);
                const float tw = text.measure(label, ui::TextStyle::Title3, sc);
                // Dark on the light row, light on the dark ones. Crossfaded by
                // the focus animation so the text does not snap between them.
                // Note Color::white(a) is WHITE AT ALPHA a, not a grey. A
                // build that wanted dark text asked for white(0.06) and got a
                // six-per-cent white, which was invisible. It reads like a
                // brightness and is not one.
                const ui::Color labelColor = ui::Color::white(
                    (kOverlayButtonRestText + f * (1.0f - kOverlayButtonRestText)) * ovl);
                text.draw(renderer, label, bx + (bw - tw) * 0.5f,
                          by + (bh - text.lineHeight(ui::TextStyle::Title3, sc)) * 0.5f +
                              text.ascent(ui::TextStyle::Title3, sc),
                          ui::TextStyle::Title3, labelColor, sc);
            }

        }

        // DOWNLOADS, under the pill that says what a removal did (the pill
        // sits where the panel's foot is) and under the question it asks
        // ("Remove 3 games?"), which is drawn with the other layers below.
        if (downloadsPanel.isOpen() && !playing) {
            screens::Ctx dctx{renderer, text, images, sc, &cards};
            downloadsPanel.draw(dctx);
            renderer.setContentAlpha(1.0f);
        }

        // ---- The notification pill — see MenuNotice ------------------------
        //
        // Over the menus and over a game, under the curtain and the dim. Near
        // the foot of the screen, inside the safe area, centred: where a
        // console's confirmation is looked for, and clear of every menu's
        // buttons. It rises a little as it arrives, like the panels do.
        {
            const float na = menuNotice.alpha();
            const float keepAlpha = renderer.contentAlpha();
            renderer.setContentAlpha(1.0f);   // not part of a screen's transition
            if (na > 0.01f && !menuNotice.text.empty()) {
                const ui::TextStyle st = ui::TextStyle::Callout;
                const float tw = text.measure(menuNotice.text, st, sc);
                constexpr float kPillH = 64.0f, kPad = 30.0f, kDot = 14.0f, kGap = 16.0f;
                const float w = kPad + kDot + kGap + tw + kPad;
                const float x = (ui::kCanvasWidth - w) * 0.5f;
                const float y = ui::kCanvasHeight - ui::kSafeInset - kPillH - 24.0f +
                                (1.0f - std::min(1.0f, menuNotice.age / 0.25f)) * 12.0f;
                ui::Color fill = kOverlayPanelSurface;
                fill.a = 0.96f * na;
                ui::Rect pill{x, y, w, kPillH, kPillH * 0.5f, fill};
                pill.border = 1.5f;
                pill.borderColor = ui::Color::white(0.14f * na);
                pill.edgeLight = ui::Color::white(0.18f * na);
                pill.shadowBlur = 26.0f;
                pill.shadowOffsetY = 10.0f;
                pill.shadowColor = ui::Color::black(0.45f * na);
                renderer.draw(pill);

                // The dot says which kind of answer it is before the words do.
                ui::Color dot = ui::Color::white(0.70f);
                switch (menuNotice.tone) {
                    case Tone::Done: dot = ui::palette::kScreenCyan; break;
                    case Tone::Busy: {
                        dot = ui::palette::kScreenCyan;
                        // A slow breath, so "working" never reads as "done".
                        const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
                        dot.a = 0.35f + 0.65f * (0.5f + 0.5f * std::sin(t * 4.2f));
                        break;
                    }
                    case Tone::Info: break;
                    case Tone::Problem: dot = ui::palette::kMarqueeAmber; break;
                }
                dot.a *= na;
                renderer.draw(ui::Rect{x + kPad, y + (kPillH - kDot) * 0.5f, kDot, kDot,
                                       kDot * 0.5f, dot});
                text.draw(renderer, menuNotice.text, x + kPad + kDot + kGap,
                          y + (kPillH - text.lineHeight(st, sc)) * 0.5f + text.ascent(st, sc),
                          st, ui::Color::white(0.94f * na), sc);
            }
            renderer.setContentAlpha(keepAlpha);
        }

        // THE DOWNLOAD PANEL IS GONE — 2026-09-21.
        //
        // It was a glass panel over a 45% scrim in the middle of the screen,
        // and the thing that settled it is that IT NEVER BLOCKED ANYTHING:
        // `inputOwner` knows about the keyboard, the overlay and a running
        // game and has never known about a download, so every key still
        // reached the screen underneath. It was obstruction with no behaviour
        // behind it, which is the worst of both.
        //
        // MMagTech, asked whether it should be its own window or shown on the
        // normal screen, chose the screen. Progress is on the ROW that started
        // it — see DetailScreen::setProgress — and, for somebody who walked
        // away from it, in the corner of the top bar below.

        renderer.setContentFade(1.0f);
        renderer.setContentOffsetY(0.0f);

        // ---- The top bar, which is its own strip ----------------------------
        //
        // MMagTech, 2026-09-21: *"the top bar with library settings and search
        // should not be on the hero and be its own separate thing at the top."*
        //
        // It used to be drawn over the hero's top edge on a black scrim, and
        // the scrim was the tell: a strip of black laid across a piece of
        // artwork to make text readable is a header bar that has been put
        // somewhere it does not belong. It has its own room now, at the safe
        // inset, and NO SURFACE UNDER IT — which is the one thing worth taking
        // from how Valve handle chrome. The destinations sit on the backdrop
        // like the shelf headings do, and the backdrop is already blurred and
        // scrimmed for exactly that reason.
        //
        // IT DRAWS ON EVERY BROWSING SCREEN, not only on Home — 2026-09-21.
        // MMagTech: *"when you switch from home to library the bar text at the
        // top should be the same."* It is chrome, and chrome that appears and
        // disappears as you move between destinations is not chrome, it is
        // decoration on one screen. Keeping it fixed also does half the work of
        // making the move to the Library read as smooth: the frame stays still
        // and only what is inside it changes.
        //
        // NOT ON THE LAUNCH SCREEN. docs/PROJECT.md is explicit that game
        // detail "is a full-screen cover, not a push — it replaces the screen
        // entirely, with the artwork as its own backdrop". A bar across the top
        // of it would make it a page, which is the thing it is deliberately not.
        //
        // A console whose recent history holds nothing this machine can play
        // once drew no hero, and the bar vanished with it — which left Library
        // unreachable on the one screen that is meant to reach everything.
        if (!playing && here() != Screen::Detail) {
            // SELECTION IS NOT FOCUS, and the design system has had both since
            // the switcher pills: a SELECTED destination is where you are, a
            // FOCUSED one is what you would open. Standing in the Library, the
            // bar says Library without pretending the cursor is up there.
            const int selected = (here() == Screen::Library || here() == Screen::Grid)
                                     ? BarLibrary
                                     : here() == Screen::Search   ? BarSearch
                                     : here() == Screen::Settings ? BarSettings
                                                                  : -1;
            const float barX = kContentInset;
            const float barW = ui::kCanvasWidth - kContentInset * 2.0f;
            const float barBaseline =
                barTop + (barHeight - text.lineHeight(ui::TextStyle::Callout, sc)) * 0.5f +
                text.ascent(ui::TextStyle::Callout, sc);
            float bx = barX;
            for (int i = 0; i < kBarCapsules; ++i) {
                const bool on = barFocused && barSlot == i;
                const bool sel = (i == selected);
                const float w = text.measure(kBarLabels[i], ui::TextStyle::Callout, sc);
                // SELECTED AND FOCUSED ARE BOTH A CAPSULE, at the design
                // system's two tints. A cyan underline under the selected one
                // was tried on 2026-09-21 and withdrawn the same minute —
                // MMagTech, looking at it: *"nevermind drop that looks bad."*
                //
                // The objection it was answering is still open and written down
                // here rather than lost: 0.35 against 0.25 is a difference you
                // can measure and can barely see from a sofa, so which
                // destination you are standing in is weakly said. Whatever
                // answers it next, it is not a second colour on the bar.
                if (on || sel) {
                    // Treatment 2, the text-control one, the same as Resume: a
                    // tinted pill rather than a scale, because a destination
                    // growing would shove its neighbours along.
                    //
                    // **kBarPillPadX AND kBarItemGap MOVE TOGETHER.** MMagTech,
                    // 2026-09-22: *"can it have a bit more padding so the
                    // letters aren't right to its edge."* They were — 14pt,
                    // which at Callout is about half a character.
                    //
                    // Widening the pill alone would have closed the gap
                    // between neighbours to nothing: the items are spaced
                    // `w + kBarItemGap`, so the space BETWEEN two pills is
                    // `kBarItemGap - 2 * kBarPillPadX`. At 14 and 44 that was
                    // 16 points; at 22 and 44 it would have been zero and the
                    // pills would have met. The gap is what keeps them reading
                    // as separate destinations rather than one segmented
                    // control, so the spacing goes up with the padding.
                    constexpr float kBarPillPadX = 22.0f;
                    constexpr float kBarPillInsetY = 6.0f;
                    const float ph = barHeight - kBarPillInsetY * 2.0f;
                    renderer.draw(ui::Rect{bx - kBarPillPadX, barTop + kBarPillInsetY,
                                           w + kBarPillPadX * 2.0f, ph, ph * 0.5f,
                                           ui::Color::white(on ? kFocusedTint
                                                               : kSelectedTint)});
                }
                text.draw(renderer, kBarLabels[i], bx, barBaseline, ui::TextStyle::Callout,
                          ui::Color::white(on || sel ? 1.0f : 0.65f), sc);
                bx += w + 60.0f;
            }

            // A DOWNLOAD IN FLIGHT, IN THE CORNER. For the person who started
            // one and walked away: without it a background fetch of a 1.78 GB
            // arcade set is completely invisible the moment you leave the
            // screen that started it, which is exactly when you want to know.
            //
            // It is a readout and not a control — nothing is reached by
            // pointing at it — so it is small, quiet, and says only the two
            // things worth knowing: that something is coming, and how far.
            float rightEdge = barX + barW;
            if (launchJob.busy() && launchJob.busyFor >= kProgressDelay) {
                const int64_t got = launchJob.got.load();
                const int64_t total = launchJob.total.load();
                char pct[64];
                if (launchJob.stage.load() == LaunchJob::Stage::Unpacking)
                    std::snprintf(pct, sizeof pct, "Unpacking\xE2\x80\xA6");
                else if (total > 0)
                    std::snprintf(pct, sizeof pct, "%.0f%%",
                                  100.0 * static_cast<double>(got) /
                                      static_cast<double>(total));
                else
                    std::snprintf(pct, sizeof pct, "%.0f MB",
                                  static_cast<double>(got) / 1e6);
                const float pw = text.measure(pct, ui::TextStyle::Callout, sc);
                const float trackW = 140.0f;
                const float trackY = barTop + barHeight * 0.5f - 3.0f;
                float px = rightEdge - pw;
                text.draw(renderer, pct, px, barBaseline, ui::TextStyle::Callout,
                          ui::Color::white(0.75f), sc);
                px -= 14.0f + trackW;
                renderer.draw(ui::Rect{px, trackY, trackW, 6.0f, 3.0f,
                                       ui::Color::white(0.16f)});
                if (total > 0) {
                    const float frac = std::clamp(
                        static_cast<float>(got) / static_cast<float>(total), 0.0f, 1.0f);
                    renderer.draw(ui::Rect{px, trackY, trackW * frac, 6.0f, 3.0f,
                                           ui::palette::kScreenCyan});
                }
                rightEdge = px - 32.0f;
            }

            // The account, at the far right — the corner the reference
            // implementation reserves for it. `TVAccountChip`: "the signed-in
            // RomM username beside a small circular avatar, in Home's top-right
            // corner". Name and avatar, as MMagTech asked for.
            //
            // A lettered disc stands in until the real avatar is fetched, which
            // is Cabinet's fallback too — it uses a person glyph. Account
            // switching is its own topic and nothing here is focusable yet.
            const storage::User me = storage::currentUser();
            const std::string who = me.valid() ? me.name : std::string("Not signed in");
            // A STEP DOWN THE RAMP FROM THE DESTINATIONS, and that is the
            // whole of the sizing rule. MMagTech, looking at the first
            // capture: *"chip seems a bit too big."* It was, and the reason
            // was measurable rather than a matter of taste — the name was
            // Callout, which is exactly what Library, Search and Settings
            // are, so ambient state was typeset at destination weight and
            // competed with the navigation it sits opposite.
            //
            // Caption1 against the bar's Callout, and a disc sized to the
            // smaller text. The reference calls it "a SMALL circular avatar"
            // and that word was doing work nobody had read.
            // THE NAME MATCHES THE DESTINATIONS; THE DISC DOES NOT NEED TO.
            // MMagTech, 2026-09-22: *"the username is sized different to
            // library, settings and search."* It was, because "the chip seems
            // a bit too big" the day before had been read as the TEXT when it
            // was the disc — a 30pt avatar beside 31pt labels is a heavy
            // object in the corner, and a 26pt one is not.
            //
            // So the name goes back to Callout, level with the bar it sits in,
            // and the disc stays small. The chip is quiet because the picture
            // is small, not because the name is shrunken.
            const ui::TextStyle chipStyle = ui::TextStyle::Callout;
            const float discD = barHeight - 30.0f;
            const float discX = rightEdge - discD;
            const float discY = barTop + (barHeight - discD) * 0.5f;
            const float nameW = text.measure(who, chipStyle, sc);
            // THE PERSON'S OWN PICTURE, when RomM has one — new 2026-09-21.
            // MMagTech: *"i also noticed my user login isnt showing its image
            // from romm."* It never did: the comment below promised a lettered
            // disc "until the real avatar is fetched" and nothing ever fetched
            // one. It comes from `/api/users/<id>/avatar`, through the same
            // cache and the same authenticated client as every cover.
            //
            // The disc is drawn either way, as the ground under a picture with
            // transparency and as the fallback when there is none.
            // FOCUSED, THE CHIP GETS THE BAR'S OWN PILL, behind the name and
            // the picture together, at the focused tint the destinations use.
            // It used to take only a rim on its small disc, and MMagTech found
            // it hard to tell when focus had arrived on it, 2026-09-24:
            // *"can we make the text or some way grab your attention more
            // beside just the little highlight of the icon"*.
            const bool chipOn = barFocused && barSlot == BarAccount;
            if (chipOn) {
                constexpr float kChipPillPadX = 22.0f;   // the bar pill's
                constexpr float kChipPillInsetY = 6.0f;
                const float ph = barHeight - kChipPillInsetY * 2.0f;
                const float left = discX - 10.0f - nameW - kChipPillPadX;
                const float right = discX + discD + 12.0f;
                renderer.draw(ui::Rect{left, barTop + kChipPillInsetY, right - left, ph,
                                       ph * 0.5f, ui::Color::white(kFocusedTint)});
            }
            renderer.draw(ui::Rect{discX, discY, discD, discD, discD * 0.5f,
                                   ui::Color::white(chipOn ? 0.34f : 0.22f)});
            // WHERE THE PANEL HANGS FROM. The app knows where the chip is; the
            // screen must not guess, or the panel drifts the day the bar moves.
            accountScreen.setAnchor(discX + discD, barTop + barHeight + 12.0f);
            const ui::Image* face = nullptr;
            if (!me.avatar.empty()) {
                const ui::Image& img = images.get(me.avatar);
                if (img.ready) face = &img;
            }
            if (face) {
                // FILLED AND ROUND. A profile picture is not always square —
                // the one on the live server is 1200x1200 but nothing promises
                // that — and a contained fit inside a circle leaves bars that
                // the circle then slices into crescents.
                ui::drawImage(renderer, *face, discX, discY, discD, discD,
                              ui::Fit::Fill, face->fade, discD * 0.5f);
            } else if (!who.empty()) {
                const std::string initial(1, static_cast<char>(std::toupper(
                    static_cast<unsigned char>(who[0]))));
                const float iw = text.measure(initial, chipStyle, sc);
                text.draw(renderer, initial, discX + (discD - iw) * 0.5f,
                          discY + (discD - text.lineHeight(chipStyle, sc)) * 0.5f +
                              text.ascent(chipStyle, sc),
                          chipStyle, ui::Color::white(0.90f), sc);
            }
            // Its own baseline, because it is no longer the bar's size and
            // sharing `barBaseline` would sit it a few points low.
            const float chipBaseline =
                barTop + (barHeight - text.lineHeight(chipStyle, sc)) * 0.5f +
                text.ascent(chipStyle, sc);
            // Brighter when focused, because it just got smaller and a focus
            // target you cannot find is worse than one that is too loud.
            text.draw(renderer, who, discX - 10.0f - nameW, chipBaseline,
                      chipStyle, ui::Color::white(chipOn ? 1.0f : 0.62f), sc);
        }

        // ---- The account switcher, over the screen and over the bar -------
        //
        // AFTER THE BAR, because it hangs from the chip the bar draws and has
        // to sit on top of it rather than under. Before the curtain, because a
        // curtain covers everything including this.
        if (accountsOpen) {
            screens::Ctx actx{renderer, text, images, sc, &cards};
            accountScreen.draw(actx);
        }
        // The PIN pad over all of it, the panel included: it can be opened
        // from the panel, and it covers the screen.
        if (choiceScreen.isOpen() && !playing) {
            screens::Ctx cctx{renderer, text, images, sc, &cards};
            choiceScreen.draw(cctx);
            renderer.setContentAlpha(1.0f);
        }
        if (pinScreen.isOpen() && !playing) {
            screens::Ctx pctx{renderer, text, images, sc, &cards};
            pinScreen.draw(pctx);
            renderer.setContentAlpha(1.0f);
        }

        // ---- The curtain, over everything --------------------------------
        //
        // Last, and over the keyboard and the in-game overlay as well: it is
        // not part of any screen, it is the screen going away. See design.h.
        {
            const float c = curtain.value();
            if (c > 0.001f && leaving != Leave::None) {
                // Leaving: the startup screen, which is what comes back.
                const float keep = renderer.contentAlpha();
                renderer.setContentAlpha(1.0f);
                setup::drawStartup(renderer, text, leaveLabel.c_str(), c);
                renderer.setContentAlpha(keep);
            } else if (c > 0.001f && switchCurtain) {
                // The console's own backdrop, in one piece, faded. Two bands
                // were drawn here first and left a line under the name where
                // they met; MMagTech saw it on the TV.
                renderer.drawBackdrop({ui::palette::kBackdropTop, ui::palette::kBackdropMid,
                                       ui::palette::kBackdropBottom, 0.55f}, c);
            } else if (c > 0.001f) {
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(c)});
            }
            if (c <= 0.001f && switchPendingId == 0) switchCurtain = false;
            // WHO IT IS BECOMING, on the curtain, only while the curtain is
            // fully down (see switchText).
            const float ta = switchText.value();
            if (ta > 0.001f && !switchLabel.empty()) {
                const float lw = text.measure(switchLabel, ui::TextStyle::Title2, sc);
                text.draw(renderer, switchLabel, (ui::kCanvasWidth - lw) * 0.5f,
                          ui::kCanvasHeight * 0.5f + text.ascent(ui::TextStyle::Title2, sc) * 0.5f,
                          ui::TextStyle::Title2, ui::Color::white(0.9f * ta), sc);
            } else if (ta <= 0.001f && switchPendingId == 0) {
                switchLabel.clear();
            }
        }

        // A switch asked for this frame: copy it now, before the keyboard, so
        // the keyboard can leave by sliding rather than dissolve with the copy.
        if (pendingDest >= 0 && !playing && renderer.sceneCaptured()) {
            renderer.captureSnapshot();
            snapTaken = true;
        }

        // The keyboard of the screen that is here, then the old screen's copy
        // dissolving over everything, then a keyboard that is leaving: over
        // the copy, or the copy would hide it at the start of its slide.
        if (!keyboard.sliding()) keyboard.draw(renderer, text, renderer.scale());
        if (!playing) renderer.drawSnapshot(tabDissolve.value());
        if (keyboard.sliding()) keyboard.draw(renderer, text, renderer.scale());
        if (safeGuides) renderer.drawSafeAreaGuides();

        // ---- The dim, over absolutely everything --------------------------
        //
        // After the keyboard and the guides, because it is not part of the
        // interface; it is the television resting. At full depth it is also
        // the blank wherever gamescope cannot put the output to sleep.
        {
            const float d = dimLayer.value();
            if (d > 0.001f) {
                const float keep = renderer.contentAlpha();
                renderer.setContentAlpha(1.0f);
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(d)});
                renderer.setContentAlpha(keep);
            }
        }

        ++frame;
        // Capture before the swap. After a swap the back buffer's contents are
        // undefined, so a readback taken there is whatever the driver left.
        if (shotMode && frame >= shotAfterFrames &&
            (!waitForPairCode || shownPairCode)) {
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

        // A top-bar switch asked for: this frame still shows the old screen, so
        // copy it, THEN switch. The next frame draws the new screen with this
        // copy dissolving over it.
        if (pendingDest >= 0) {
            if (snapTaken) {
                snapTaken = false;
                tabDissolve.from = tabDissolve.to = 1.0f;
                tabDissolve.elapsed = 0.0f;
                tabDissolve.retarget(0.0f, kTabDissolve);
                tabSince = 0.0f;
            }
            // Leaving Search: its keyboard goes on drawing, sliding down.
            if (here() == Screen::Search && keyboard.isOpen()) {
                keyboard.keepForSlide();
                keyboardSlide.from = keyboardSlide.to = 0.0f;
                keyboardSlide.elapsed = 0.0f;
                keyboardSlide.retarget(1.0f, kKeyboardSlide);
            }
            goToDestination(pendingDest);
            // Arriving at Search: its keyboard comes up from below.
            if (here() == Screen::Search && keyboard.isOpen()) {
                keyboardSlide.from = keyboardSlide.to = 1.0f;
                keyboardSlide.elapsed = 0.0f;
                keyboardSlide.retarget(0.0f, kKeyboardSlide);
            }
            if (tabDissolve.value() > 0.0f) {
                libraryScreen.settleArrival();
                settingsScreen.settleArrival();
            }
            pendingDest = -1;
        }

        if (offscreen) {
            renderer.endOffscreen();
        } else {
            SDL_GL_SwapWindow(window);
        }

        // A BLANK SCREEN DOES NOT NEED SIXTY PICTURES A SECOND. Ten keeps the
        // background jobs polled and a wake within a tenth of a second, and
        // lets the GPU stop redrawing 4K for nobody.
        if (idleShown == idle::Level::Blank && dimLayer.value() >= 0.999f) SDL_Delay(100);
    }

    // Never leave the television asleep behind us: the next thing on it —
    // this program restarting, or a person at a console — must be seen.
    if (idleShown == idle::Level::Blank) idle::setDisplayAsleep(false, /*wait=*/true);

    // A SHUTDOWN MID-GAME LEAVES THE WAY EXIT TO HOME DOES — docs/PROJECT.md,
    // open question 10b. Until 2026-09-22 it did not: the loop ended, the code
    // below unloaded the core, and nothing uploaded, so pressing the power
    // button during a game threw away whatever the game had saved since it
    // started. finishExit is the one way out of a game and it carries every
    // save class there is — battery, directory, file — so going through it is
    // what makes this true for every emulator, present and future, rather
    // than for the ones somebody remembered. The uploads it queues are
    // drained below, before the process ends.
    //
    // Not while the machine is still being built: nothing has been played, and
    // finishExit's own caller waits for exactly that case rather than calling
    // it. And never for a capture, which is not a person's evening.
    if (playing && askedToStop && !shotMode && cab::Core::shared().running()) {
        std::fprintf(stderr, "[shutdown] leaving the game the way Exit to Home does\n");
        finishExit();
    }

    if (playing) {
        cab::Core& core = cab::Core::shared();
        const double realtime = core.audioFramesTotal() / core.avInfo().sampleRate;
        std::fprintf(stderr,
                     "[core] %llu frames, %llu audio frames = %.2fs of emulated time\n",
                     static_cast<unsigned long long>(core.framesRun()),
                     static_cast<unsigned long long>(core.audioFramesTotal()), realtime);
        // What the bridge between Vulkan and GL cost, when there was one. The
        // whole question "does handing a Vulkan picture to a GLES UI hurt"
        // gets an answer here rather than an opinion. See vkhost.h.
        {
            uint64_t vframes = 0;
            double vseconds = 0.0;
            cab::vk::presentCost(vframes, vseconds);
            if (vframes > 0)
                std::fprintf(stderr,
                             "[vulkan] %llu pictures crossed into GL in %.3fs "
                             "= %.3f ms each\n",
                             static_cast<unsigned long long>(vframes), vseconds,
                             1000.0 * vseconds / static_cast<double>(vframes));
            if (vframes > 0) {
                double rec = 0.0, wait = 0.0;
                cab::vk::presentCostSplit(rec, wait);
                std::fprintf(stderr,
                             "[vulkan]   of which %.3f ms recording the copy and "
                             "%.3f ms waiting for the GPU\n",
                             1000.0 * rec / static_cast<double>(vframes),
                             1000.0 * wait / static_cast<double>(vframes));
            }
        }
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
    {
        const covercache::Stats cs = covercache::stats();
        if (cs.fromDisk || cs.fetched)
            std::fprintf(stderr, "[covers] %d from disk, %d fetched, %d stored\n",
                         cs.fromDisk, cs.fetched, cs.stored);
    }
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

    // STARTING AGAIN IN PLACE, for Sign out and a new server address. Not by
    // exiting: gamescope ends with its child, and the session coming back up
    // is seconds of black. exec keeps the process, so gamescope stays and the
    // startup screen follows the one this left on. Every descriptor is closed
    // first so nothing this run held (the power delay lock above all) is
    // carried into the next one.
    if (restartSelf) {
        std::fprintf(stderr, "[frontend] starting again\n");
        std::fflush(stderr);
        ::close_range(3, ~0U, 0);
        ::execv("/proc/self/exe", argv);
        std::fprintf(stderr, "[frontend] could not start again: %s\n", std::strerror(errno));
        return 1;
    }
    return 0;
}
