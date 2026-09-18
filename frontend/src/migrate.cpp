#include "migrate.h"

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <map>

#include <json-c/json.h>

#include "cache.h"
#include "catalog.h"
#include "storage.h"

namespace migrate {
namespace {

// A digest of what a file holds, so a move can be proved rather than assumed.
//
// FNV-1a, 64 bit. Not a cryptographic hash and it does not need to be: nothing
// here is defending against an adversary choosing the bytes, it is answering
// "are these the same bytes that were there a second ago". It reads the file,
// which is the point — the alternative is trusting a return value.
uint64_t hashFile(const std::string& path, bool* ok) {
    uint64_t h = 1469598103934665603ULL;
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) { if (ok) *ok = false; return 0; }
    unsigned char buf[1 << 16];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0)
        for (size_t i = 0; i < n; ++i) h = (h ^ buf[i]) * 1099511628211ULL;
    const bool bad = std::ferror(f) != 0;
    std::fclose(f);
    if (ok) *ok = !bad;
    return h;
}

void walk(const std::string& dir, const std::string& prefix,
          std::vector<std::string>* out) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return;
    while (struct dirent* e = ::readdir(d)) {
        if (std::strcmp(e->d_name, ".") == 0 || std::strcmp(e->d_name, "..") == 0) continue;
        const std::string rel = prefix.empty() ? e->d_name : prefix + "/" + e->d_name;
        const std::string full = dir + "/" + e->d_name;
        struct stat st;
        if (::lstat(full.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) walk(full, rel, out);
        else out->push_back(rel);
    }
    ::closedir(d);
}

// The same question asked of a whole tree: every file's relative path, size and
// contents rolled into one number. A directory that comes out the other end
// with a file missing produces a different digest, which is exactly the failure
// a size comparison would not catch.
uint64_t hashTree(const std::string& path, bool* ok) {
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0) { if (ok) *ok = false; return 0; }
    if (!S_ISDIR(st.st_mode)) return hashFile(path, ok);

    std::vector<std::string> files;
    walk(path, "", &files);
    std::sort(files.begin(), files.end());
    uint64_t h = 1469598103934665603ULL;
    bool all = true;
    for (const std::string& rel : files) {
        for (unsigned char c : rel) h = (h ^ c) * 1099511628211ULL;
        bool one = true;
        const uint64_t fh = hashFile(path + "/" + rel, &one);
        all = all && one;
        for (int i = 0; i < 8; ++i)
            h = (h ^ static_cast<unsigned char>(fh >> (i * 8))) * 1099511628211ULL;
    }
    if (ok) *ok = all;
    return h;
}

bool isDir(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool exists(const std::string& p) {
    struct stat st;
    return ::lstat(p.c_str(), &st) == 0;
}

std::vector<std::string> entriesOf(const std::string& dir) {
    std::vector<std::string> out;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        out.push_back(e->d_name);
    }
    ::closedir(d);
    std::sort(out.begin(), out.end());
    return out;
}

bool endsWith(const std::string& name, const char* suffix) {
    const size_t n = std::strlen(suffix);
    return name.size() >= n && name.compare(name.size() - n, n, suffix) == 0;
}

// THE OLD LAYOUT WROTE EXACTLY THREE KINDS OF FILE into a game's directory, and
// listing them is safer than listing extensions that look save-shaped:
//
//   <title>.srm               the battery snapshot this frontend takes
//   <title> [timestamp].state a save state
//   <title>.zip               PSP's save FOLDER, zipped — and PSP only
//
// Everything else in there is the game: the payload RomM sent, and whatever it
// unpacked into.
//
// THE `.zip` RULE IS THE ONE THAT MATTERS AND THE FIRST VERSION GOT IT WRONG.
// An arcade ROM is a `.zip` — `lethalen.zip` is the whole of a MAME game — and
// a Game Boy ROM arrives as one too. Classifying every `.zip` as a save filed
// two real games as save data and reported their directories as "empty", which
// the dry run showed before anything moved. The payload is the file RomM named,
// so that name is what decides, and a rom the server no longer lists keeps its
// zip rather than risking the same mistake with no way to check it.
bool looksLikeSaveData(const std::string& name, const std::string& fsName) {
    if (!fsName.empty() && name == fsName) return false;   // the game itself
    if (endsWith(name, ".srm") || endsWith(name, ".state")) return true;
    return endsWith(name, ".zip") && !fsName.empty();
}

bool isState(const std::string& name) { return endsWith(name, ".state"); }

std::string timestamp() {
    char stamp[32];
    const std::time_t now = std::time(nullptr);
    std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H-%M-%S", std::localtime(&now));
    return stamp;
}

struct Resolved {
    std::string platformSlug;    // for roms/ and cache/
    std::string platformFsSlug;  // for saves/ and states/, mirroring RomM
    std::string title;
    std::string fsName;          // what the server called the payload
    std::string core;
    bool known = false;
};

// What the server knows about a rom id, with the old kept record as a fallback
// — a kept game embeds its whole library entry precisely so it can be
// identified with no network, and a migration is exactly that case.
Resolved resolveRom(int romId, const std::map<int, romm::Game>& library,
                    const std::string& oldRoot) {
    Resolved r;
    if (auto it = library.find(romId); it != library.end()) {
        r.platformSlug = it->second.platformSlug;
        r.platformFsSlug = it->second.platformFsSlug;
        r.title = it->second.name.empty() ? it->second.fsName : it->second.name;
        r.fsName = it->second.fsName;
        if (const catalog::Coverage cov = catalog::coverageFor(it->second); cov.core)
            r.core = cov.core;
        r.known = true;
        return r;
    }
    const std::string rec = oldRoot + "/romcache/kept/" + std::to_string(romId) + ".json";
    if (FILE* f = std::fopen(rec.c_str(), "rb")) {
        std::string body;
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
        std::fclose(f);
        if (json_object* o = json_tokener_parse(body.c_str()); o) {
            json_object* v = nullptr;
            auto s = [&](const char* k) -> std::string {
                return json_object_object_get_ex(o, k, &v) && v ? json_object_get_string(v)
                                                                : std::string();
            };
            r.platformSlug = s("platform_slug");
            r.platformFsSlug = s("platform_fs_slug");
            r.title = s("name");
            r.fsName = s("fs_name");
            r.known = !r.platformSlug.empty();
            json_object_put(o);
        }
    }
    if (r.title.empty()) r.title = "rom " + std::to_string(romId);
    return r;
}

const char* kUnknownCore = "unknown-core";

}  // namespace

Plan build(const std::string& oldRoot, const std::vector<romm::Game>& library) {
    Plan plan;
    std::map<int, romm::Game> byId;
    for (const romm::Game& g : library) byId[g.id] = g;

    const storage::User& user = storage::currentUser();
    // WHERE THE UNATTRIBUTABLE SAVES GO, and it is inside the saves tree
    // rather than beside it. They are saves; putting them in `config/` would be
    // the same category error this layout exists to remove, and somebody
    // hunting for a lost memory card looks under `saves/`. The segment where a
    // platform would be says plainly that it is not one.
    //
    // It goes under the CURRENT user, which is an assumption and is true today:
    // this console has had one person on it. A machine with a play history for
    // two people would need somebody to sort them, and there is no machine like
    // that yet — which is exactly why open question 18 says to do this now.
    const std::string setAside =
        (user.valid() ? storage::userDir(user) + "/saves"
                      : storage::configDir()) +
        "/unattributed " + timestamp();

    const std::string romcache = oldRoot + "/romcache";

    // --- Which games were kept, before anything moves -----------------------
    //
    // Read first because it decides whether a game lands in `roms/` or in
    // `cache/`, and because the record itself becomes this person's keep.
    std::vector<int> keptIds;
    for (const std::string& name : entriesOf(romcache + "/kept")) {
        const int id = storage::romIdFromEntry(name);
        if (id > 0) keptIds.push_back(id);
    }

    // --- The games themselves ------------------------------------------------
    for (const std::string& name : entriesOf(romcache)) {
        if (name == "kept" || name == "pending" || name == "saves") continue;
        const int romId = storage::romIdFromEntry(name);
        if (romId <= 0 || name != std::to_string(romId)) {
            plan.leftAlone.push_back(romcache + "/" + name +
                                     " — not a game directory, left where it is");
            continue;
        }
        const std::string dir = romcache + "/" + name;
        if (!isDir(dir)) continue;
        const std::vector<std::string> files = entriesOf(dir);
        if (files.empty()) {
            plan.leftAlone.push_back(dir + " — empty, left to be removed by hand");
            continue;
        }

        const Resolved res = resolveRom(romId, byId, oldRoot);
        const bool kept =
            std::find(keptIds.begin(), keptIds.end(), romId) != keptIds.end();
        const std::string location = storage::locations().front();

        // Save data first, because it is the half that has to be right.
        std::vector<std::string> payloads;
        for (const std::string& f : files) {
            if (!looksLikeSaveData(f, res.fsName)) { payloads.push_back(f); continue; }
            const std::string core = res.core.empty() ? kUnknownCore : res.core;
            const std::string dest =
                (isState(f) ? storage::statesDir(user, res.platformFsSlug, romId, core)
                            : storage::savesDir(user, res.platformFsSlug, romId, core)) +
                "/" + f;
            plan.moves.push_back({dir + "/" + f, dest, /*verify=*/true,
                                  isState(f) ? "save state" : "save"});
        }

        if (payloads.empty()) {
            plan.leftAlone.push_back(dir + " — held only save data, now empty");
            continue;
        }

        // ONE FILE STAYS ONE FILE. A game that is a single payload becomes
        // `cache/psx/321 - Crash Bandicoot.chd`, which is the shape open
        // question 18 wrote down and the shape somebody browsing over SFTP
        // wants. A game whose archive unpacked into several files cannot be
        // that, so it becomes a directory of the same name — both are renamed
        // identically, so nothing downstream has to know which it is.
        const std::string entry =
            cache::entryPathFor(location, res.platformSlug, romId, res.title, kept);
        if (payloads.size() == 1) {
            const std::string& only = payloads.front();
            const size_t dot = only.find_last_of('.');
            const std::string ext = dot == std::string::npos ? "" : only.substr(dot);
            plan.moves.push_back({dir + "/" + only, entry + ext, /*verify=*/false,
                                  kept ? "kept game" : "cached game"});
        } else {
            for (const std::string& f : payloads)
                plan.moves.push_back({dir + "/" + f, entry + "/" + storage::safeSegment(f),
                                      /*verify=*/false,
                                      std::string(kept ? "kept game" : "cached game") +
                                          ", part of a set"});
        }
        if (!res.known)
            plan.leftAlone.push_back(
                "rom " + std::to_string(romId) +
                " — the server does not list it, so it lands under platform "
                "'unknown' with its id, which keeps it findable");
    }

    // --- The keeps themselves, and the upload queue --------------------------
    //
    // A keep was a marker file per rom in one shared directory. It is now a
    // record per rom under the person who made the decision, which is the
    // change that stops one person's release taking a game from another.
    if (user.valid()) {
        for (const std::string& name : entriesOf(romcache + "/kept")) {
            const int id = storage::romIdFromEntry(name);
            if (id <= 0) continue;
            plan.moves.push_back({romcache + "/kept/" + name,
                                  storage::keepsDir(user) + "/" + std::to_string(id) + ".json",
                                  /*verify=*/true, "keep record"});
        }
        for (const std::string& name : entriesOf(romcache + "/pending")) {
            plan.moves.push_back({romcache + "/pending/" + name,
                                  storage::pendingDir(user) + "/" + name,
                                  /*verify=*/true, "unsent upload marker"});
        }
    } else if (exists(romcache + "/kept")) {
        plan.leftAlone.push_back(
            romcache + "/kept — there is no current user, so there is nobody to "
                       "attribute these keeps to. Pair with RomM and run this again.");
    }

    // --- The system directory ------------------------------------------------
    //
    // BIOS fetched from RomM, plus whatever the cores wrote in there. It moves
    // wholesale to `bios/`, which is the console's libretro system directory.
    // It is not a clean split and storage.h says so: libretro gives a core one
    // system directory and Flycast keeps the Dreamcast's own flash in it.
    if (isDir(oldRoot + "/system")) {
        for (const std::string& name : entriesOf(oldRoot + "/system")) {
            plan.moves.push_back({oldRoot + "/system/" + name,
                                  storage::biosDir() + "/" + name,
                                  // dc/ holds a Dreamcast's saved flash, which
                                  // is the one thing in here that cannot be
                                  // fetched again. Verify the lot; it is 4 MB.
                                  /*verify=*/true, "system directory"});
        }
    }

    // --- The two flat save piles ---------------------------------------------
    //
    // `romcache/saves/` and `saves/` — the two directories that started this
    // whole question, because a save written one way was not seen the other
    // way. Nothing in them says which game wrote it: `scd_U.brm` is A Sega CD
    // cartridge save and `pcsx-card2.mcd` is somebody's second memory card, and
    // the old layout recorded no more than that.
    //
    // SO THEY ARE NOT ATTRIBUTED. They move, whole and unaltered, to one place
    // with a name that says what they are, and every file in them is listed in
    // the report. A save filed against the wrong game is worse than one filed
    // nowhere, and the person reading the report is the only thing on this
    // machine that knows which game they were playing.
    for (const char* rel : {"romcache/saves", "saves"}) {
        const std::string from = oldRoot + "/" + rel;
        if (!isDir(from)) continue;
        std::vector<std::string> files;
        walk(from, "", &files);
        if (files.empty()) continue;
        const std::string tag = std::strcmp(rel, "saves") == 0 ? "saves" : "romcache-saves";
        plan.moves.push_back({from, setAside + "/" + tag, /*verify=*/true,
                              "the old shared save pile — NOT attributed to any game"});
        for (const std::string& f : files)
            plan.leftAlone.push_back("set aside: " + std::string(rel) + "/" + f);
    }

    // --- Things this deliberately does not touch -----------------------------
    if (isDir(oldRoot + "/roms")) {
        for (const std::string& name : entriesOf(oldRoot + "/roms")) {
            if (isDir(oldRoot + "/roms/" + name)) continue;   // already a platform folder
            plan.leftAlone.push_back(
                "roms/" + name +
                " — a loose file somebody put there by hand, probably for --core. "
                "The new roms/ holds platform folders; this is left alone rather "
                "than moved out from under a command that expects it.");
        }
    }
    return plan;
}

// Removes the old directories once they hold nothing. rmdir only succeeds on an
// empty directory, so this cannot take anything with it — which is why it is
// safe to do without recording it in the manifest.
static void removeEmptyRemains(const std::string& oldRoot) {
    const std::string romcache = oldRoot + "/romcache";
    for (const std::string& name : entriesOf(romcache))
        ::rmdir((romcache + "/" + name).c_str());
    for (const char* d : {"/romcache", "/saves", "/system"})
        if (::rmdir((oldRoot + d).c_str()) == 0)
            std::fprintf(stderr, "[migrate] removed the empty %s%s\n", oldRoot.c_str(), d);
}

void print(const Plan& p) {
    std::printf("storage root   %s\n", storage::root().c_str());
    const storage::User& u = storage::currentUser();
    std::printf("user           %s\n",
                u.valid() ? u.dirName().c_str() : "NONE — pair with RomM first");
    std::printf("\n%zu move(s):\n", p.moves.size());
    for (const Move& m : p.moves)
        std::printf("  %-28s %s\n        -> %s%s\n", m.note.c_str(), m.from.c_str(),
                    m.to.c_str(), m.verify ? "   [verified by reading it back]" : "");
    if (!p.leftAlone.empty()) {
        std::printf("\n%zu thing(s) left alone:\n", p.leftAlone.size());
        for (const std::string& s : p.leftAlone) std::printf("  %s\n", s.c_str());
    }
}

bool run(const Plan& p, std::string* manifestPath, std::string* err) {
    if (p.moves.empty()) {
        if (manifestPath) manifestPath->clear();
        return true;
    }
    storage::makeDirs(storage::configDir() + "/migrations");
    const std::string path =
        storage::configDir() + "/migrations/" + timestamp() + ".jsonl";
    FILE* log = std::fopen(path.c_str(), "wb");
    if (!log) {
        if (err) *err = "could not open the migration manifest at " + path;
        return false;
    }
    if (manifestPath) *manifestPath = path;

    int done = 0;
    for (const Move& m : p.moves) {
        if (!exists(m.from)) continue;   // an interrupted run, resumed
        if (exists(m.to)) {
            std::fclose(log);
            if (err) *err = m.to + " is already there; refusing to overwrite it";
            return false;
        }
        bool readable = true;
        const uint64_t before = m.verify ? hashTree(m.from, &readable) : 0;
        if (m.verify && !readable) {
            std::fclose(log);
            if (err) *err = "could not read " + m.from + " before moving it";
            return false;
        }
        const storage::MoveResult r = storage::moveEntry(m.from, m.to);
        if (!r.ok) {
            std::fclose(log);
            if (err) *err = r.error;
            return false;
        }
        if (m.verify) {
            const uint64_t after = hashTree(m.to, &readable);
            if (!readable || after != before) {
                // Put it straight back and stop. There is no version of this
                // where a save is half-moved and the run carries on.
                storage::moveEntry(m.to, m.from);
                std::fclose(log);
                if (err)
                    *err = m.to + " did not read back the same as " + m.from +
                           "; it has been put back and nothing else was moved";
                return false;
            }
        }
        // RECORDED AND FLUSHED BEFORE THE NEXT MOVE STARTS. A power cut between
        // two moves leaves a manifest that describes exactly what happened.
        json_object* o = json_object_new_object();
        json_object_object_add(o, "from", json_object_new_string(m.from.c_str()));
        json_object_object_add(o, "to", json_object_new_string(m.to.c_str()));
        json_object_object_add(o, "verify", json_object_new_boolean(m.verify));
        std::fprintf(log, "%s\n", json_object_to_json_string(o));
        json_object_put(o);
        std::fflush(log);
        ++done;
        if (r.crossedFilesystem)
            std::fprintf(stderr, "[migrate] %s crossed a filesystem and was copied\n",
                         m.from.c_str());
    }
    std::fclose(log);
    removeEmptyRemains(".");
    std::fprintf(stderr, "[migrate] %d move(s), all verified where it mattered\n", done);
    std::fprintf(stderr, "[migrate] manifest: %s\n", path.c_str());
    return true;
}

bool undo(const std::string& manifestPath, std::string* err) {
    FILE* f = std::fopen(manifestPath.c_str(), "rb");
    if (!f) {
        if (err) *err = "cannot read " + manifestPath;
        return false;
    }
    std::vector<std::pair<std::string, std::string>> moves;   // from, to
    std::vector<bool> verify;
    char line[4096];
    while (std::fgets(line, sizeof line, f)) {
        json_object* o = json_tokener_parse(line);
        if (!o) continue;
        json_object* v = nullptr;
        std::string from, to;
        bool ver = false;
        if (json_object_object_get_ex(o, "from", &v) && v) from = json_object_get_string(v);
        if (json_object_object_get_ex(o, "to", &v) && v) to = json_object_get_string(v);
        if (json_object_object_get_ex(o, "verify", &v) && v) ver = json_object_get_boolean(v);
        json_object_put(o);
        if (!from.empty() && !to.empty()) {
            moves.push_back({from, to});
            verify.push_back(ver);
        }
    }
    std::fclose(f);

    // Backwards, so a directory that was emptied is refilled before anything
    // expects it to exist.
    int undone = 0;
    for (size_t i = moves.size(); i-- > 0;) {
        const std::string& from = moves[i].first;
        const std::string& to = moves[i].second;
        if (!exists(to)) continue;
        bool readable = true;
        const uint64_t before = verify[i] ? hashTree(to, &readable) : 0;
        const storage::MoveResult r = storage::moveEntry(to, from);
        if (!r.ok) {
            if (err) *err = r.error;
            return false;
        }
        if (verify[i]) {
            const uint64_t after = hashTree(from, &readable);
            if (!readable || after != before) {
                if (err) *err = from + " did not read back the same after being restored";
                return false;
            }
        }
        ++undone;
    }
    std::fprintf(stderr, "[migrate] %d move(s) undone\n", undone);
    return true;
}

}  // namespace migrate
