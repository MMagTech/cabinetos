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
#include "firstrun.h"
#include "gpu.h"
#include "vkhost.h"
#include "net.h"
#include "qr.h"
#include "romfile.h"
#include "romm.h"
#include "screens.h"
#include "sound.h"
#include "setup.h"
#include "accounts.h"
#include "storage.h"
#include "text.h"
#include "overlaywin.h"
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
struct MenuNotice {
    std::string text;
    float life = 0.0f;
    void say(std::string t) { text = std::move(t); life = 3.2f; }
    void tick(float dt) { if (life > 0.0f) life -= dt; }
    float alpha() const {
        if (life <= 0.0f) return 0.0f;
        return life < 0.5f ? life / 0.5f : 1.0f;   // the last half second fades
    }
};

static void saveStateNow(GameSession& sess, Uploader& up, MenuNotice& notice) {
    cab::Core& core = cab::Core::shared();
    std::vector<uint8_t> st;
    if (!core.saveState(st) || st.empty()) {
        std::fprintf(stderr, "[state] this core cannot serialize\n");
        notice.say("This system cannot save a state");
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
        notice.say("Could not write the state to this machine");
        return;
    }
    std::fprintf(stderr, "[state] %zu bytes saved locally\n", st.size());

    if (sess.stateTag.empty()) {
        std::fprintf(stderr, "[state] no settled tag for this core — not uploaded\n");
        // SAVED, and honest about the half that did not happen. A state this
        // console cannot tag is one no other device will be offered, which is
        // worth knowing before somebody relies on it being there.
        notice.say("State saved here — not to the server");
        return;
    }
    // NOT "saved" YET. The bytes are on the disk, which is the guarantee that
    // matters, but the sentence a person reads should not claim the server has
    // it before the server has it. The frame loop finishes this sentence when
    // the uploader reports back — see Uploader::stateOutcome.
    notice.say("Saving\xE2\x80\xA6");
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
    std::vector<uint8_t> data;
    std::string note;
    std::thread worker;
    ~StateLoad() { if (worker.joinable()) worker.join(); }
};

static void beginLoadLatestState(StateLoad& load, GameSession& sess,
                                 romm::Client& client, MenuNotice& notice) {
    if (load.running.load()) return;

    notice.say("Looking for a state\xE2\x80\xA6");
    if (sess.stateTag.empty()) {
        std::fprintf(stderr, "[state] no settled tag for this core — refusing to load\n");
        // It used to return here in silence, which is half of why this looked
        // unwired: no local state, no tag, nothing said.
        notice.say("No state to load for this system");
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
            // The server could not be reached or would not answer, which is
            // Cabinet's offline case by another name. The newest state on this
            // machine is what is left, and it is better than a refusal.
            load.note = err;
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
                    notice.say(ok ? "State loaded from this machine"
                                  : "This state could not be loaded");
                    return;
                }
            }
        }
        // `note` is already a sentence written for a person — "none for
        // <core>", an error from the server — so it is shown rather than
        // replaced with a vaguer one.
        notice.say(load.note);
        return;
    }
    const bool ok = cab::Core::shared().loadState(load.data);
    std::fprintf(stderr, "[state] %s (%zu bytes) -> %s\n", load.note.c_str(),
                 load.data.size(), ok ? "restored" : "REFUSED");
    // A REFUSED state is the one that matters most. It means the bytes were
    // found and the core would not take them, which is a different problem from
    // there being none — and silently carrying on with the game running from
    // where it was is indistinguishable from nothing having happened.
    notice.say(ok ? "State loaded" : "This state could not be loaded");
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
                          bool isCollection) {
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
    for (;;) {
        std::vector<romm::Game> page;
        // fetchRoms writes the limit itself, so only the offset rides on the
        // filter.
        if (!client.fetchRoms(filter + "&offset=" + std::to_string(games.size()),
                              kPage, &page, &err, &total)) {
            std::fprintf(stderr, "[library] %s: %s\n", tile.title.c_str(), err.c_str());
            return false;
        }
        const size_t got = page.size();
        games.insert(games.end(), std::make_move_iterator(page.begin()),
                     std::make_move_iterator(page.end()));
        if (got < static_cast<size_t>(kPage)) break;
        if (total > 0 && games.size() >= static_cast<size_t>(total)) break;
    }
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
    // Alphabetical inside the grid, which is what the grid's letter-jump
    // expects. Sorted here rather than in the store — see appendGame.
    std::sort(tile.cards.begin(), tile.cards.end(), [&](int a, int b) {
        return lib.cards[a].title < lib.cards[b].title;
    });
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
    std::vector<romm::Game> recent;
    if (client.fetchRecent(16, &recent, &err)) {
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

    std::vector<romm::Game> favs;
    if (client.fetchFavorites(40, &favs, &err)) {
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
                !done.done               ? "NEEDED — this console is not set up"
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
        } else if (SDL_strcmp(argv[i], "--integer-scale") == 0) {
            integerScaling = true;
            std::fprintf(stderr, "[picture] integer scaling, with bars\n");
        } else if (SDL_strcmp(argv[i], "--query") == 0 && i + 1 < argc) {
            searchQuery = argv[++i];
        } else if (SDL_strcmp(argv[i], "--ui-sound") == 0 && i + 1 < argc) {
            const char* v = argv[++i];
            uiSound = !(SDL_strcmp(v, "off") == 0 || SDL_strcmp(v, "0") == 0);
            if (uiSound) uiSoundVolume = static_cast<float>(SDL_atof(v)) > 0
                                             ? static_cast<float>(SDL_atof(v))
                                             : uiSoundVolume;
            std::fprintf(stderr, "[sound] %s, volume %.2f\n",
                         uiSound ? "on" : "off", uiSoundVolume);
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
    if (keepersRomId > 0) return keepersProbe(keepersRomId);

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
    sound::setEnabled(uiSound);
    sound::setVolume(uiSoundVolume);

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
        const firstrun::Completion done = firstrun::completion();
        if (forceSetup || !done.done) {
            setup::Deps deps;
            deps.window = window;
            deps.renderer = &renderer;
            deps.text = &text;
            setup::Options opts;
            opts.startStep = setupStep;
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
        setup::showWaiting(waitDeps, "Starting up", "Loading your library — 640 games");
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
        // A BOUNDED WAIT, not an indefinite one, and not the offline console.
        // Ninety seconds covers a boot race and a router coming back after a
        // power cut. It is deliberately NOT the answer to "there is no server"
        // — a console that keeps its library, plays its kept games and fills
        // in when the server returns is open question 22's design and a
        // different piece of work. This is the difference between a machine
        // that recovers from a power cut on its own and one that does not.
        {
            constexpr double kWaitSeconds = 90.0;
            const uint64_t start = SDL_GetTicks();
            bool said = false;
            while (!liveClient.setAddress(rommAddress, &err)) {
                if ((SDL_GetTicks() - start) / 1000.0 >= kWaitSeconds) {
                    std::fprintf(stderr, "[romm] %s — gave up after %.0fs\n",
                                 err.c_str(), kWaitSeconds);
                    return 1;
                }
                // Once, not once per attempt: a line a second for a minute and
                // a half buries whatever else the boot had to say.
                if (!said) {
                    said = true;
                    std::fprintf(stderr,
                                 "[romm] %s — waiting up to %.0fs for it\n",
                                 err.c_str(), kWaitSeconds);
                }
                // A NUMBER THAT CHANGES, every two seconds, for as long as
                // ninety. This is the longest a person can be looking at the
                // startup screen and it used to say one unchanging sentence
                // throughout, which is what a hung console looks like.
                {
                    char line[96];
                    std::snprintf(line, sizeof line,
                                  "Waiting for your server — %ds",
                                  static_cast<int>((SDL_GetTicks() - start) / 1000));
                    setup::showWaiting(waitDeps, "Starting up", line);
                }
                SDL_Delay(2000);
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
    // THE CHIP IS A BAR SLOT NOW, and it is the only one that is not a
    // capsule: it is drawn as the avatar disc at the far right, so the label
    // loop below stops at BarSettings and the chip takes its own focus rim.
    // Everything else about it — L1/R1 walking onto it, Down leaving the bar —
    // it gets for free by being in this list.
    enum BarItem { BarLibrary = 0, BarSearch, BarSettings, BarAccount, BarCount };
    const char* kBarLabels[BarCount] = { "Library", "Search", "Settings", "Account" };
    // How many of those draw as labelled capsules. The chip draws itself.
    constexpr int kBarCapsules = BarAccount;

    const size_t shelfSlots = shelf.empty() ? cards.size() : shelf.size();
    // Whether there is a game to resume. It is shelf slot 0 when there is.
    const bool haveResume = heroIndex >= 0 && !shelf.empty();
    const bool haveFavorites = !favorites.empty();

    auto rowSlots = [&](int row) -> size_t {
        if (row == RowRecent) return shelfSlots;
        return favorites.size();
    };
    auto rowExists = [&](int row) { return rowSlots(row) > 0; };

    // The card a (row, slot) points at, or nullptr for the top bar, whose
    // items are destinations rather than cards.
    auto cardAt = [&](int row, int slot) -> Card* {
        if (row == RowRecent) {
            if (slot < 0 || static_cast<size_t>(slot) >= shelfSlots) return nullptr;
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
    enum class Screen { Home, Library, Grid, Detail, Search, AddAccount };
    std::vector<Screen> stack{Screen::Home};
    auto here = [&]() { return stack.back(); };

    screens::LibraryScreen libraryScreen;
    screens::GridScreen gridScreen;
    screens::DetailScreen detailScreen;
    screens::SearchScreen searchScreen;
    screens::AccountScreen accountScreen;
    screens::AddAccountScreen addAccountScreen;
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
    if (rommAddress) {
        for (const screens::Tile& t : platformTiles)
            if (t.enterable && t.cover.empty()) coverFill.want.push_back(t.id);
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
    }
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
        cache::Release r;
        cache::unkeep(storage::currentUser(), romId, /*keepTheBytes=*/nowPlaying, &r);
        detailScreen.setKept(false);

        // SILENCE IS THE RIGHT ANSWER ONLY WHEN THE ROW DID WHAT IT SAYS. The
        // badge going out is feedback enough for a game that is gone and a
        // disk that has the room back. The other three outcomes all leave the
        // bytes where they were, and saying nothing then is the row lying —
        // the same fault as a launch refusal reaching stderr and no further.
        switch (r.what) {
            case cache::Release::What::Deleted:
            case cache::Release::What::Nothing:
                detailScreen.setNotice("");
                break;
            case cache::Release::What::StillKept:
                // Unreachable until this console had more than one account,
                // and the first thing that account switching makes real.
                detailScreen.setNotice(
                    r.otherKeepers == 1
                        ? "Removed from your games. It stays on the console "
                          "because somebody else is keeping it, so no space "
                          "came back."
                        : "Removed from your games. It stays on the console "
                          "because other people are keeping it, so no space "
                          "came back.");
                break;
            case cache::Release::What::Demoted:
                detailScreen.setNotice("Removed from your games. The space "
                                       "comes back when you stop playing it.");
                break;
            case cache::Release::What::DeleteFailed:
                detailScreen.setNotice("Removed from your games, but the files "
                                       "could not be deleted.");
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
                if (t->cards.empty()) {
                    // IT BLOCKS, and that is the honest shape of it for now:
                    // the person has chosen a system and is waiting for one
                    // request rather than for the whole library. The biggest
                    // platform on the reference server is the one to watch —
                    // if this ever reads as a stall it wants the same waiting
                    // frame a launch already has, not a background thread.
                    loadTileGames(liveClient, lib, *t, isCollection);
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
                stack.push_back(Screen::Grid);
                sound::play(sound::Cue::Activate);
                break;
            }
            case screens::Action::SwitchAccount: {
                // THE REFUSALS BELONG TO THE APP AND SO DO THEIR WORDS. The
                // screen does not know whether a game is running or a save is
                // still going up; it asked to become somebody and this decides.
                std::string why;
                if (switchAccount(res.value, &why)) {
                    // Straight back to Home with the panel closed, because
                    // everything behind it belonged to the last account.
                    // Leaving the panel open over a Home that has just been
                    // rebuilt for somebody else is the stale-screen fault in
                    // miniature.
                    accountsOpen = false;
                    barFocused = false;
                    stack.clear();
                    stack.push_back(Screen::Home);
                    accountScreen.setNotice("");
                    sound::play(sound::Cue::Activate);
                } else {
                    accountScreen.setNotice(why);
                    sound::play(sound::Cue::Edge);
                }
                break;
            }
            case screens::Action::AddAccount:
                // THE PANEL CLOSES AND A SCREEN OPENS. Leaving the panel up
                // behind a pairing code would put the list somebody is about
                // to change underneath the thing changing it.
                accountsOpen = false;
                barFocused = false;
                addAccountScreen.open();
                stack.push_back(Screen::AddAccount);
                startAddAccount();
                sound::play(sound::Cue::Activate);
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
                              ? BarLibrary : 0;
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
        if (haveResume && focusRow == RowRecent && focusSlot == 0) {
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
    // 0 Home, 1 Library, 2 Search. Settings is drawn in the bar and does not
    // exist; see barKey.
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
        }
    };

    auto destinationHere = [&]() {
        switch (here()) {
            case Screen::Home: return 0;
            case Screen::Library:
            case Screen::Grid: return 1;
            case Screen::Search: return 2;
            default: return -1;      // the launch screen is inside a destination
        }
    };

    auto switchDestination = [&](int delta) {
        const int at = destinationHere();
        if (at < 0) return;
        const int want = at + delta;
        // The range ends at Search, because Settings is drawn in the bar and
        // does not exist. A bumper that went nowhere and said nothing would
        // read as the button being broken rather than the screen being unbuilt,
        // so it makes the edge sound instead.
        if (want < 0 || want > 2 || want == at) {
            sound::play(sound::Cue::Edge);
            return;
        }
        goToDestination(want);
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
                // Settings is drawn because the bar has to be laid out against
                // its real contents. It says nothing when pressed rather than
                // pretending — a destination that goes nowhere is a promise the
                // product does not keep, and that screen does not exist.
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
                if (barSlot == BarLibrary || barSlot == BarSearch) {
                    const int d = (barSlot == BarLibrary) ? 1 : 2;
                    barFocused = false;
                    // Already standing in it: drop back into the screen rather
                    // than rebuilding it under the person's feet.
                    if (destinationHere() == d) {
                        sound::play(sound::Cue::Move);
                        return true;
                    }
                    goToDestination(d);
                    sound::play(sound::Cue::Activate);
                    return true;
                }
                sound::play(sound::Cue::Edge);
                std::fprintf(stderr, "[nav] %s is not built yet\n", kBarLabels[barSlot]);
                return true;
        }
        return false;
    };

    // The first answer, before anything is drawn. Everything after this is a
    // refresh triggered by the thing that changed it.
    refreshKeeps();

    navigate = [&](screens::Nav n) -> bool {
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

    // Remembered focus per row, which is the behaviour tvOS gives free and the
    // one people notice missing: leaving Recent at the sixth cover and coming
    // back to the first is the kind of thing that feels broken without anyone
    // being able to say why.
    int rememberedSlot[2] = {0, 0};

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
        if (keyboard.isOpen() &&
            !(here() == Screen::Search && searchScreen.focused()))
            return InputOwner::Keyboard;
        // The overlay takes the pad FROM the core while it is open, which is
        // the whole rule: never both. tvOS does this by turning the focus
        // engine off during play; here it is this one line.
        if (overlayOpen) return InputOwner::Overlay;
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
        if (here() != Screen::Search) return;
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
            case OvSaveState: saveStateNow(session, uploader, menuNotice); break;
            case OvLoadState: beginLoadLatestState(stateLoad, session, liveClient, menuNotice); break;
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
                        else if (stack.size() > 1) navigate(screens::Nav::Back);
                        else running = false;
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
                    if (playing && e.key.key == SDLK_F5) saveStateNow(session, uploader, menuNotice);
                    if (playing && e.key.key == SDLK_F8) beginLoadLatestState(stateLoad, session, liveClient, menuNotice);
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
                            overlaySlot = std::min(OvCount - 1, overlaySlot + 1);
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_SOUTH) overlayActivate();
                        // East is Back, and Back from the overlay is Resume.
                        if (e.gbutton.button == SDL_GAMEPAD_BUTTON_EAST) toggleOverlay();
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
                            if (here() == Screen::Home) running = false;
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
                    if (stack.size() > 1) stack.pop_back();
                    accountsOpen = true;
                    barFocused = true;
                    barSlot = BarAccount;
                    accountScreen.setNotice(who.name + " was added. Choose them to switch.");
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
                        who.name + " is already on this console, so nobody was added. "
                        "Sign in to RomM as the person you are adding — a private "
                        "window is easiest — and try again.");
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
                launchById(id);
            }
        }

        pumpLaunch();
        pumpExit();
        pumpStateLoad(stateLoad, session, menuNotice);
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
                overlayFade.retarget(1.0f, kOverlayFade);
            }
        }
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
        menuNotice.tick(dt);
        // The other half of the sentence Save started. Cabinet's own words,
        // because they are better than anything invented here: it either
        // reached the server or it is waiting for signal.
        if (const int outcome = uploader.stateOutcome.exchange(0); outcome != 0)
            menuNotice.say(outcome == 1 ? "State saved to the server"
                                        : "State saved here — waiting for signal");
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
            gridScreen.tick(dt, ctx);
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
                }
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
                // The room follows the results here too. A search that found
                // nothing leaves it lit by whatever was there before rather
                // than blanking, the same rule the top bar gets.
                const int ci = searchScreen.focusedCard();
                if (ci >= 0 && ci < static_cast<int>(cards.size()))
                    want = cards[ci].coverLarge.empty() ? cards[ci].cover
                                                        : cards[ci].coverLarge;
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
            }
            if (want != backdropWant) {
                backdropWant = want;
                backdropSettle = 0.0f;
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
                    backdropMix.retarget(1.0f, backdropFade);
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
            (haveFavorites ? favoritesTop + shelfBlockHeight : recentTop + shelfBlockHeight) +
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
                if (rowId == RowRecent && slot == 0 && haveResume) {
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
                // Search has no glass: the keyboard docked under it is the only
                // material on the screen and the app draws that itself.
                case Screen::Search: break;
                // The QR draws its own white card; there is nothing behind it
                // on this screen for glass to blur.
                case Screen::AddAccount: break;
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
            // The 64 is the padding above and below the buttons; the extra 44
            // is the line the menu answers with. Reserved whether or not there
            // is one, so the panel does not change height as a message arrives
            // and leaves — a menu that resizes under a person's thumb is worse
            // than one with a little space at the bottom.
            const float panelH = static_cast<float>(OvCount) * kOverlayButtonHeight +
                                 static_cast<float>(OvCount - 1) * kOverlayButtonGap +
                                 64.0f + 44.0f;
            const float px = (ui::kCanvasWidth - kOverlayPanelWidth) * 0.5f;
            // Rises slightly as it arrives rather than only fading: a panel that
            // just materialises reads as a glitch.
            const float py = (ui::kCanvasHeight - panelH) * 0.5f + (1.0f - ovl) * 24.0f;
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
                    btn.borderColor = ui::palette::kFocusRim;
                    btn.shadowBlur = kOverlayButtonFocusShadowBlur;
                    btn.shadowOffsetY = kOverlayButtonFocusShadowY;
                    btn.shadowColor =
                        ui::Color::black(kOverlayButtonFocusShadowAlpha * f * ovl);
                }
                renderer.draw(btn);

                const char* label = kOverlayLabels[i];
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

            // WHAT THE MENU SAYS BACK. Under the buttons, inside the panel, so
            // it belongs to the thing that was pressed rather than floating
            // somewhere else on the screen. See MenuNotice.
            const float na = menuNotice.alpha() * ovl;
            if (na > 0.01f && !menuNotice.text.empty()) {
                const float nw = text.measure(menuNotice.text, ui::TextStyle::Callout, sc);
                text.draw(renderer, menuNotice.text,
                          px + (kOverlayPanelWidth - nw) * 0.5f,
                          py + panelH - 34.0f + text.ascent(ui::TextStyle::Callout, sc),
                          ui::TextStyle::Callout, ui::Color::white(0.75f * na), sc);
            }
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
                                     : (here() == Screen::Search ? BarSearch : -1);
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
            // THE CHIP IS FOCUSABLE NOW. It is not a capsule like the other
            // bar items, so it takes the focus treatment on its own disc — a
            // rim, which is this design system's focus idiom everywhere else.
            const bool chipOn = barFocused && barSlot == BarAccount;
            if (chipOn) {
                const float pad = 6.0f;
                renderer.draw(ui::Rect{discX - pad, discY - pad, discD + pad * 2.0f,
                                       discD + pad * 2.0f, (discD + pad * 2.0f) * 0.5f,
                                       ui::Color::white(0.55f)});
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
                      chipStyle, ui::Color::white(chipOn ? 0.95f : 0.62f), sc);
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

        // ---- The curtain, over everything --------------------------------
        //
        // Last, and over the keyboard and the in-game overlay as well: it is
        // not part of any screen, it is the screen going away. See design.h.
        {
            const float c = curtain.value();
            if (c > 0.001f)
                renderer.draw(ui::Rect{0, 0, ui::kCanvasWidth, ui::kCanvasHeight, 0,
                                       ui::Color::black(c)});
        }

        keyboard.draw(renderer, text, renderer.scale());
        if (safeGuides) renderer.drawSafeAreaGuides();

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
