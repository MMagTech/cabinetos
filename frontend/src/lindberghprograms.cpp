// What each Lindbergh game and each Windows arcade game in RomM holds; the
// rule is in lindberghprograms.h.

#include "lindberghprograms.h"

#include <json-c/json.h>

#include <cstdio>
#include <mutex>

#include "arcadematch.h"
#include "storage.h"

namespace lindberghprograms {
namespace {

std::mutex gLock;

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

void keep(Kind kind, int romId, int64_t size, const std::string& name) {
    std::lock_guard<std::mutex> hold(gLock);
    json_object* o = load(kind);
    json_object* e = json_object_new_object();
    json_object_object_add(e, "size", json_object_new_int64(size));
    json_object_object_add(e, storeOf(kind).key, json_object_new_string(name.c_str()));
    json_object_object_add(o, std::to_string(romId).c_str(), e);
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

}  // namespace

bool known(Kind kind, int romId, int64_t size, std::string* name) {
    std::lock_guard<std::mutex> hold(gLock);
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

bool learn(romm::Client& client, Kind kind, int romId, int64_t size, std::string* name,
           std::string* err) {
    std::vector<romm::RomFile> files;
    if (!client.fetchRomFiles(romId, &files, err)) return false;
    *name = namedIn(kind, files);
    keep(kind, romId, size, *name);
    std::fprintf(stderr, "[%s] game %d's %s: %s (of %zu files)\n", storeOf(kind).tag, romId,
                 storeOf(kind).what, name->empty() ? "none" : name->c_str(), files.size());
    return true;
}

void learnAll(romm::Client& client, Kind kind, const std::vector<romm::Game>& games,
              const std::function<bool()>& over) {
    for (const romm::Game& g : games) {
        if (over()) return;
        std::string name, err;
        if (known(kind, g.id, g.sizeBytes, &name)) continue;
        if (!learn(client, kind, g.id, g.sizeBytes, &name, &err))
            std::fprintf(stderr, "[%s] game %d's files: %s\n", storeOf(kind).tag, g.id,
                         err.c_str());
    }
}

}  // namespace lindberghprograms
