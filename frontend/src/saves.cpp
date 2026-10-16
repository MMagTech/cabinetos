#include "saves.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <set>
#include <sstream>

#include "cache.h"
#include "core.h"
#include "fileio.h"
#include "savemirror.h"
#include "storage.h"

// RomM'S SAVES FOR A GAME, OR THE COPY OF THEM (#88, savemirror.h). Online:
// asked, and the copy refreshed. Offline: the copy, as last seen, so a launch
// decides exactly as it would online. Every restore asks through these two.
bool savesFor(romm::Client& client, int romId, std::vector<romm::Asset>* rows,
              std::string* err) {
    const storage::User& u = storage::currentUser();
    if (!romm::serverAway()) {
        const uint64_t asked = savemirror::mark();
        if (client.fetchSaves(romId, rows, err)) {
            if (!savemirror::forgottenSince(romId, asked)) savemirror::putList(u, romId, *rows);
            return true;
        }
        if (!romm::serverAway()) return false;
    }
    if (savemirror::getList(u, romId, rows)) {
        std::fprintf(stderr, "[save] the server is away; using its saves as last seen\n");
        return true;
    }
    if (err) *err = "the server is away, and its saves were never seen here";
    return false;
}

std::vector<uint8_t> saveBytesFor(romm::Client& client, int romId,
                                  const romm::Asset& row) {
    const storage::User& u = storage::currentUser();
    if (!romm::serverAway()) {
        std::vector<uint8_t> b = client.fetchAsset("saves", row.id);
        if (!b.empty()) {
            savemirror::putBytes(u, romId, row, b);
            return b;
        }
        if (!romm::serverAway()) return b;
    }
    return savemirror::getBytes(u, romId, row);
}

// A DIRECTORY SAVE COMES DOWN before the game starts: the newest zip RomM
// holds under `tag` for this game, unpacked into `root`. PSP's memory stick
// and a Switch game's save folder both travel this way.
//
// GAMECUBE'S CARDS FROM BEFORE (#344): `oldCard` is where Dolphin reads a raw
// card into its `.gci` folder when the folder is missing (writeDolphinConfig).
// When RomM's newest save for the game is still such a card, it is put there
// and Dolphin does the rest at boot. Only when the folder does not exist yet,
// and never over a card already on this console: that one is at least as new,
// and Dolphin reads it in itself.
void restoreDirSave(romm::Client& client, int romId, const char* tag,
                    const std::string& root, const std::string& title,
                    const std::string& oldCard) {
    // NOT OVER A SAVE THIS CONSOLE HAS NOT SENT YET. The zip syncDirSave made
    // is newer than anything the server holds until its upload lands, and
    // unpacking the server's older one would write over it. Found 2026-09-29:
    // an Xbox save recovered after a power cut was queued, the game started
    // again at once, and the server's older zip came down over it. The same
    // rule the battery save already keeps (cache.h, isPending).
    const std::string name = sanitisedStem(title) + ".zip";
    if (cache::isPending(storage::currentUser(), romId, name)) {
        std::fprintf(stderr, "[save] %s is still on its way up; keeping this console's\n",
                     name.c_str());
        return;
    }
    std::vector<romm::Asset> saves;
    std::string serr;
    if (!savesFor(client, romId, &saves, &serr)) return;
    const romm::Asset* newest = nullptr;
    for (const auto& a : saves) {
        if (a.emulator != tag) continue;
        if (!newest || a.updatedAt > newest->updatedAt) newest = &a;
    }
    if (!newest) return;
    std::vector<uint8_t> data = saveBytesFor(client, romId, *newest);
    // SNIFFED, never taken from the name. The reference implementation's PSP
    // saves are an Apple directory archive wearing an `.srm` extension, so the
    // filename says nothing at all about what is inside. Anything that is not
    // a zip is left alone rather than guessed at — better a missing save than
    // a corrupted memory stick.
    if (cab::looksLikeZip(data)) {
        std::string uerr;
        if (cab::unzipTree(data, root, &uerr)) {
            std::fprintf(stderr, "[save] unpacked %s (%zu bytes) into %s\n",
                         newest->fileName.c_str(), data.size(), root.c_str());
        } else {
            std::fprintf(stderr, "[save] %s would not unpack: %s\n",
                         newest->fileName.c_str(), uerr.c_str());
        }
    } else if (!oldCard.empty() && !data.empty()) {
        if (storage::exists(root)) {
            std::fprintf(stderr, "[save] %s is a card from before; %s already holds this game's "
                         "saves, so it is left alone\n", newest->fileName.c_str(), root.c_str());
        } else if (storage::exists(oldCard)) {
            std::fprintf(stderr, "[save] %s is a card from before; this console's own %s is read "
                         "in instead\n", newest->fileName.c_str(), oldCard.c_str());
        } else if (!cab::holdsASave(data, catalog::Untouched::GameCubeDirectory)) {
            std::fprintf(stderr, "[save] %s is a card with no saves on it; left alone\n",
                         newest->fileName.c_str());
        } else if (cab::writeBytes(oldCard, data)) {
            std::fprintf(stderr, "[save] %s (%zu bytes) placed at %s; Dolphin reads it into "
                         "%s at boot\n", newest->fileName.c_str(), data.size(),
                         oldCard.c_str(), root.c_str());
        } else {
            std::fprintf(stderr, "[save] could not write %s\n", oldCard.c_str());
        }
    } else if (!data.empty()) {
        std::fprintf(stderr, "[save] %s is not a zip (%02x %02x %02x %02x) — left alone\n",
                     newest->fileName.c_str(), data[0], data[1],
                     data.size() > 2 ? data[2] : 0, data.size() > 3 ? data[3] : 0);
    }
}

// The save's files under a directory-save root, and only those.
std::vector<cab::DirEntry> listDirSave(const GameSession& sess) {
    std::vector<cab::DirEntry> all = cab::listTree(sess.dirSaveRoot);
    std::vector<cab::DirEntry> out;
    for (cab::DirEntry& e : all)
        if (catalog::inDirectorySave(sess.dirSaveCore.c_str(), sess.dirSavePlatform,
                                     sess.dirSaveTitleId, e.relPath))
            out.push_back(std::move(e));
    return out;
}

// A directory save, zipped and sent: PSP, Switch, PS3, Xbox and Xbox 360. See
// dirsave.h for why the archive is a zip and where it is rooted.
//
// THE WHOLE TREE, whenever anything in it changed. It used to be only the
// top-level folders created or touched since the game started, which was how
// one shared PSP save directory told its games apart. The directory is one
// game's now (GameSession), and RomM replaces the row by filename, so sending
// only the touched folders REPLACED the server's copy with part of the save:
// found 2026-09-30 when an Xbox 360 upload of Forza's profile folder dropped
// the game's shared `0000000000000000` folder from RomM, and a PSP game with
// two save folders would lose the one not written that session. Another
// console restoring that zip never got the rest. The baseline still decides
// whether anything was written at all.
void syncDirSave(GameSession& sess, Uploader& up) {
    std::map<std::string, cab::DirEntry> before;
    for (const cab::DirEntry& e : sess.dirAtLaunch) before[e.relPath] = e;

    const std::vector<cab::DirEntry> now = listDirSave(sess);
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
    for (const cab::DirEntry& e : now) paths.push_back(e.relPath);

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
    std::fprintf(stderr, "[save] %zu file(s), %zu folder(s) changed, %zu bytes zipped to %s\n",
                 paths.size(), touchedFolders.size(), zip.size(), path.c_str());
    sess.dirAtLaunch = now;   // this is the new baseline; do not send it twice

    if (sess.saveTag.empty()) return;
    Uploader::Job job{sess.romId, sess.saveTag, name, std::move(zip), false};
    job.localPath = path;
    up.push(std::move(job));
}

// A GAME WHOSE SAVES NEVER GOT ZIPPED: Xbox 360 and Wii U, where the emulator
// writes straight into the person's folder and a note says a game is running there
// (standalone::beforeStart). The console went down, or the frontend did,
// before the end of the game. The whole folder goes up now, as if the game
// had just ended with everything changed, and is owed until it lands, so the
// restore that follows keeps it rather than unpacking the server's older zip
// over it (restoreDirSave, isPending). The note names the game and the tag.
void finishInterruptedSave(const cab::standalone::Emulator& emu,
                           const std::string& saveDir, Uploader& up) {
    std::istringstream note(cab::standalone::playingNote(saveDir));
    std::string id, dir, title, tag;
    std::getline(note, id);
    std::getline(note, dir);
    std::getline(note, title);
    std::getline(note, tag);
    GameSession owed;
    owed.romId = std::atoi(id.c_str());
    owed.title = title;
    owed.saveDir = saveDir;
    owed.saveTag = tag;
    owed.dirSaveRoot = cab::standalone::saveRoot(emu, saveDir);   // no baseline: all of it
    std::fprintf(stderr, "[save] %s was never zipped after its last game; sending it all\n",
                 saveDir.c_str());
    if (owed.romId > 0 && !title.empty()) syncDirSave(owed, up);
    cab::standalone::finished(emu, saveDir);
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
std::string saveRowName(const std::string& fsStem, const std::string& coreRowName,
                        const std::string& region) {
    std::string base = sanitisedStem(fsStem) + " (Cabinet";
    if (!coreRowName.empty()) base += " " + coreRowName;
    return base + ")." + region;
}

// The same thing, spelled the way Cabinet for Mac spells it.
//
// `cabinet-604.ps2`. Only PlayStation 2 uses this, and only because its saves
// ALREADY EXISTED on the server written by the Mac: RomM matches a row for
// overwrite by filename alone, so a console that invented its own name would
// leave the person with two cards per game and restore neither. See
// catalog::SaveFile::macRowName.
static std::string macSaveRowName(int romId, const std::string& region) {
    return "cabinet-" + std::to_string(romId) + "." + region;
}

// Which region a row on the server belongs to, judged by the extension this
// console and the reference implementation both upload under. Anything else —
// a card somebody made in RomM's web player, a file from another emulator —
// reads as the main save, which is right: that is the only region a foreign
// row could ever be.
std::string regionOfRow(const std::string& fileName) {
    auto endsWith = [&](const char* ext) {
        const size_t n = std::strlen(ext);
        return fileName.size() > n && fileName.compare(fileName.size() - n, n, ext) == 0;
    };
    if (endsWith(".cart")) return "cart";
    if (endsWith(".rtc")) return "rtc";
    // PlayStation 2's card, named the Mac's way (`cabinet-604.ps2`), and a
    // GameCube raw card from before #344. Without these lines either reads as
    // "srm": the PS2 restore would look straight past the card, and the
    // battery restore could take a 16 MB card for a battery.
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
std::vector<cab::FileSaveState> restoreFileSaves(
        const std::vector<catalog::SaveFile>& specs, const std::string& fsStem,
        const std::string& saveDir, int romId, const char* tag,
        romm::Client& client) {
    std::vector<cab::FileSaveState> out;
    if (specs.empty()) return out;

    std::vector<romm::Asset> rows;
    if (tag && client.haveToken()) {
        std::string err;
        if (!savesFor(client, romId, &rows, &err))
            std::fprintf(stderr, "[save] could not ask the server: %s\n", err.c_str());
    }
    const storage::User& user = storage::currentUser();

    for (const catalog::SaveFile& spec : specs) {
        cab::FileSaveState f;
        f.spec = spec;
        f.path = (spec.inSystemDir ? storage::biosDir() : saveDir) + "/" + spec.path;
        const std::string name =
            spec.macRowName ? macSaveRowName(romId, spec.region)
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
                std::vector<uint8_t> data = saveBytesFor(client, romId, *newest);
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
void syncFileSaves(GameSession& sess, Uploader& up) {
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
        const std::string name =
            f.spec.macRowName
                ? macSaveRowName(sess.romId, f.spec.region)
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
        Uploader::Job job{sess.romId, sess.saveTag, name, std::move(data), false};
        job.localPath = f.spec.inSystemDir ? local : written;
        up.push(std::move(job));
    }
}

// Takes a snapshot of the game's own save and sends it, but only when it has
// actually changed. A cartridge with no battery returns nothing, which is a
// normal answer and not a failure.
void syncSave(GameSession& sess, Uploader& up) {
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
    Uploader::Job job{sess.romId, sess.saveTag, name, std::move(ram), false};
    job.localPath = path;
    up.push(std::move(job));
}

// THE BATTERY SAVE: RomM's newest, unless this console's own copy is
// still owed, and this console's copy whenever RomM has none to give
// (#259). Until 2026-10-05 the local `.srm` syncSave writes at every
// quit was never read back, so a game started with the server away
// began with an empty battery, and one started while its upload was
// still owed got RomM's OLDER copy over the newer one. The same rule
// restoreFileSaves and restoreDirSave keep (cache.h, isPending), and
// what RetroArch does: the frontend loads the local `.srm`.
// Stage 8 of a launch (launch.cpp), after the load and before the first frame.
void restoreBatterySave(cab::Core& core, GameSession& session, const std::string& saveDir,
                        const storage::User& user, romm::Client& client) {
    if (core.saveRamSize() > 0) {
        const std::string name = saveRowName(session.fsStem, std::string(), "srm");
        const std::vector<uint8_t> local = cab::readBytes(saveDir + "/" + name);
        const bool owed =
            !session.saveTag.empty() && cache::isPending(user, session.romId, name);
        bool fromServer = false;
        if (owed) {
            std::fprintf(stderr, "[save] %s has not reached the server yet - it wins\n",
                         name.c_str());
        } else if (!session.saveTag.empty()) {
            std::vector<romm::Asset> saves;
            std::string serr;
            if (savesFor(client, session.romId, &saves, &serr)) {
                const romm::Asset* newest = nullptr;
                for (const auto& a : saves) {
                    if (a.emulator != session.saveTag) continue;
                    // THE BATTERY ROW ONLY. Cabinet files a Game Boy
                    // clock as its own 8-byte `.rtc` row under the same
                    // tag; taken as the newest, its bytes went over the
                    // start of the save (#346, found in the audit).
                    if (regionOfRow(a.fileName) != "srm") continue;
                    if (!newest || a.updatedAt > newest->updatedAt) newest = &a;
                }
                if (newest) {
                    std::vector<uint8_t> data =
                        saveBytesFor(client, session.romId, *newest);
                    if (!data.empty() && core.writeSaveRam(data)) {
                        session.saveAtLaunch = data;
                        fromServer = true;
                        std::fprintf(stderr, "[save] restored %s (%zu bytes)\n",
                                     newest->fileName.c_str(), data.size());
                    }
                }
            } else {
                std::fprintf(stderr, "[save] could not ask the server: %s\n", serr.c_str());
            }
        }
        if (!fromServer && !local.empty() && core.writeSaveRam(local)) {
            session.saveAtLaunch = local;
            std::fprintf(stderr, "[save] restored this console's %s (%zu bytes)\n",
                         name.c_str(), local.size());
        }
    }
    if (session.saveAtLaunch.empty()) core.readSaveRam(session.saveAtLaunch);
    std::fprintf(stderr, "[save] battery is %zu bytes\n", core.saveRamSize());
}
