// What each Lindbergh game and each Windows arcade game in RomM holds; the
// rule is in lindberghprograms.h.

#include "lindberghprograms.h"

#include <json-c/json.h>

#include <cstdio>
#include <map>
#include <mutex>

#include "arcadematch.h"
#include "cache.h"
#include "storage.h"

namespace lindberghprograms {
namespace {

std::mutex gLock;

// THE GAMES RomM LISTED NOTHING INSIDE, asked in this run and written
// nowhere (lindberghprograms.h): each one's rom id and the size RomM gave
// it, one map per kind, under gLock.
std::map<int, int64_t> gUnlisted[2];
std::map<int, int64_t>& unlistedOf(Kind kind) { return gUnlisted[kind == Kind::Windows ? 1 : 0]; }

// Each kind's file, the key its name is kept under, and its log's tag.
struct Store {
    const char* file;
    const char* key;
    const char* tag;
    const char* what;
};
const Store& storeOf(Kind kind) {
    static const Store kLindbergh{"lindbergh-programs.json", "program", "lindbergh", "program"};
    static const Store kWindows{"windows-arcade-games.json", "game", "windows",
                                ".windowsloader game"};
    return kind == Kind::Windows ? kWindows : kLindbergh;
}

std::string path(Kind kind) { return storage::configDir() + "/" + storeOf(kind).file; }

json_object* load(Kind kind) {
    std::string body;
    if (FILE* f = std::fopen(path(kind).c_str(), "rb")) {
        char buf[4096];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
        std::fclose(f);
    }
    json_object* o = body.empty() ? nullptr : json_tokener_parse(body.c_str());
    if (o && json_object_get_type(o) == json_type_object) return o;
    if (o) json_object_put(o);
    return json_object_new_object();
}

void keep(Kind kind, int romId, int64_t size, const std::string& name, int64_t unpacked) {
    std::lock_guard<std::mutex> hold(gLock);
    json_object* o = load(kind);
    json_object* e = json_object_new_object();
    json_object_object_add(e, "size", json_object_new_int64(size));
    json_object_object_add(e, storeOf(kind).key, json_object_new_string(name.c_str()));
    json_object_object_add(e, "unpacked", json_object_new_int64(unpacked));
    json_object_object_add(o, std::to_string(romId).c_str(), e);
    unlistedOf(kind).erase(romId);
    storage::makeDirs(storage::configDir());
    const std::string tmp = path(kind) + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY), f);
        std::fputc('\n', f);
        const bool ok = std::fflush(f) == 0;
        std::fclose(f);
        if (!ok || std::rename(tmp.c_str(), path(kind).c_str()) != 0)
            std::fprintf(stderr, "[%s] could not write %s\n", storeOf(kind).tag,
                         path(kind).c_str());
    }
    json_object_put(o);
}

// The name kept on the disk for the game at that size. Under gLock.
bool onDisk(Kind kind, int romId, int64_t size, std::string* name) {
    json_object* o = load(kind);
    json_object* e = nullptr;
    json_object* s = nullptr;
    json_object* p = nullptr;
    const bool have = json_object_object_get_ex(o, std::to_string(romId).c_str(), &e) &&
                      json_object_object_get_ex(e, "size", &s) &&
                      json_object_object_get_ex(e, storeOf(kind).key, &p) &&
                      json_object_get_int64(s) == size;
    if (have) *name = json_object_get_string(p);
    json_object_put(o);
    return have;
}

// What RomM's list of the game's files says, kept on the disk, or held in
// memory as none when RomM listed nothing inside it.
std::string take(Kind kind, int romId, int64_t size, const std::vector<romm::RomFile>& files) {
    const std::string name = namedIn(kind, files);
    if (!listed(files)) {
        {
            std::lock_guard<std::mutex> hold(gLock);
            unlistedOf(kind)[romId] = size;
        }
        std::fprintf(stderr,
                     "[%s] game %d's %s: none, RomM listed nothing inside %s; asked again at "
                     "the next library load\n",
                     storeOf(kind).tag, romId, storeOf(kind).what, files[0].fileName.c_str());
        return name;
    }
    const int64_t unpacked = unpackedIn(files);
    keep(kind, romId, size, name, unpacked);
    std::fprintf(stderr, "[%s] game %d's %s: %s (of %zu files, %lld bytes unpacked)\n",
                 storeOf(kind).tag, romId, storeOf(kind).what,
                 name.empty() ? "none in its list" : name.c_str(), files.size(),
                 static_cast<long long>(unpacked));
    return name;
}

bool learnWith(const Fetch& fetch, Kind kind, int romId, int64_t size, std::string* name,
               std::string* err) {
    std::vector<romm::RomFile> files;
    if (!fetch(romId, &files, err)) return false;
    *name = take(kind, romId, size, files);
    return true;
}

Fetch fromClient(romm::Client& client) {
    return [&client](int romId, std::vector<romm::RomFile>* files, std::string* err) {
        return client.fetchRomFiles(romId, files, err);
    };
}

}  // namespace

bool known(Kind kind, int romId, int64_t size, std::string* name) {
    std::lock_guard<std::mutex> hold(gLock);
    if (onDisk(kind, romId, size, name)) return true;
    const std::map<int, int64_t>& held = unlistedOf(kind);
    const auto it = held.find(romId);
    if (it == held.end() || it->second != size) return false;
    name->clear();
    return true;
}

bool kept(Kind kind, int romId, int64_t size, std::string* name) {
    std::lock_guard<std::mutex> hold(gLock);
    return onDisk(kind, romId, size, name);
}

std::string namedIn(Kind kind, const std::vector<romm::RomFile>& files) {
    std::vector<std::string> names;
    for (const romm::RomFile& f : files) {
        names.push_back(f.fileName);
        for (const romm::ArchiveMember& m : f.members)
            names.push_back(m.name.substr(m.name.find_last_of('/') + 1));
    }
    return kind == Kind::Windows ? arcadematch::windowsIdNamed(names)
                                 : arcadematch::lindberghProgramNamed(names);
}

bool listed(const std::vector<romm::RomFile>& files) {
    return !(files.size() == 1 && arcadematch::isArchive(files[0].fileName) &&
             files[0].members.empty());
}

int64_t unpackedIn(const std::vector<romm::RomFile>& files) {
    if (files.size() != 1 || !arcadematch::isArchive(files[0].fileName)) return 0;
    int64_t total = 0;
    for (const romm::ArchiveMember& m : files[0].members) total += m.size;
    return total;
}

int64_t unpackedKept(Kind kind, int romId, int64_t size) {
    std::lock_guard<std::mutex> hold(gLock);
    std::string name;
    if (!onDisk(kind, romId, size, &name)) return 0;
    json_object* o = load(kind);
    json_object* e = nullptr;
    json_object* u = nullptr;
    const int64_t unpacked =
        json_object_object_get_ex(o, std::to_string(romId).c_str(), &e) &&
                json_object_object_get_ex(e, "unpacked", &u)
            ? json_object_get_int64(u)
            : 0;
    json_object_put(o);
    return unpacked;
}

int64_t roomToUnpack(int64_t downloadBytes, int64_t unpackedBytes) {
    return downloadBytes + (unpackedBytes > 0 ? unpackedBytes : downloadBytes) +
           static_cast<int64_t>(static_cast<double>(downloadBytes) * cache::kOverheadFraction);
}

bool learn(romm::Client& client, Kind kind, int romId, int64_t size, std::string* name,
           std::string* err) {
    return learnWith(fromClient(client), kind, romId, size, name, err);
}

void learnAll(romm::Client& client, Kind kind, const std::vector<romm::Game>& games,
              const std::function<bool()>& over) {
    learnAll(fromClient(client), kind, games, over);
}

void learnAll(const Fetch& fetch, Kind kind, const std::vector<romm::Game>& games,
              const std::function<bool()>& over) {
    for (const romm::Game& g : games) {
        if (over()) return;
        std::string name, err;
        if (kept(kind, g.id, g.sizeBytes, &name)) continue;
        if (!learnWith(fetch, kind, g.id, g.sizeBytes, &name, &err))
            std::fprintf(stderr, "[%s] game %d's files: %s\n", storeOf(kind).tag, g.id,
                         err.c_str());
    }
}

}  // namespace lindberghprograms
