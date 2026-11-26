// Which program each Lindbergh game in RomM holds; the rule is in
// lindberghprograms.h.

#include "lindberghprograms.h"

#include <json-c/json.h>

#include <cstdio>
#include <mutex>

#include "arcadematch.h"
#include "storage.h"

namespace lindberghprograms {
namespace {

std::mutex gLock;

std::string path() { return storage::configDir() + "/lindbergh-programs.json"; }

json_object* load() {
    std::string body;
    if (FILE* f = std::fopen(path().c_str(), "rb")) {
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

void keep(int romId, int64_t size, const std::string& program) {
    std::lock_guard<std::mutex> hold(gLock);
    json_object* o = load();
    json_object* e = json_object_new_object();
    json_object_object_add(e, "size", json_object_new_int64(size));
    json_object_object_add(e, "program", json_object_new_string(program.c_str()));
    json_object_object_add(o, std::to_string(romId).c_str(), e);
    storage::makeDirs(storage::configDir());
    const std::string tmp = path() + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY), f);
        std::fputc('\n', f);
        const bool ok = std::fflush(f) == 0;
        std::fclose(f);
        if (!ok || std::rename(tmp.c_str(), path().c_str()) != 0)
            std::fprintf(stderr, "[lindbergh] could not write %s\n", path().c_str());
    }
    json_object_put(o);
}

}  // namespace

bool known(int romId, int64_t size, std::string* program) {
    std::lock_guard<std::mutex> hold(gLock);
    json_object* o = load();
    json_object* e = nullptr;
    json_object* s = nullptr;
    json_object* p = nullptr;
    const bool have = json_object_object_get_ex(o, std::to_string(romId).c_str(), &e) &&
                      json_object_object_get_ex(e, "size", &s) &&
                      json_object_object_get_ex(e, "program", &p) &&
                      json_object_get_int64(s) == size;
    if (have) *program = json_object_get_string(p);
    json_object_put(o);
    return have;
}

std::string namedIn(const std::vector<romm::RomFile>& files) {
    std::vector<std::string> names;
    for (const romm::RomFile& f : files) {
        names.push_back(f.fileName);
        for (const romm::ArchiveMember& m : f.members)
            names.push_back(m.name.substr(m.name.find_last_of('/') + 1));
    }
    return arcadematch::lindberghProgramNamed(names);
}

bool learn(romm::Client& client, int romId, int64_t size, std::string* program,
           std::string* err) {
    std::vector<romm::RomFile> files;
    if (!client.fetchRomFiles(romId, &files, err)) return false;
    *program = namedIn(files);
    keep(romId, size, *program);
    std::fprintf(stderr, "[lindbergh] game %d's program: %s (of %zu files)\n", romId,
                 program->empty() ? "none of the loader's" : program->c_str(), files.size());
    return true;
}

void learnAll(romm::Client& client, const std::vector<romm::Game>& games,
              const std::function<bool()>& over) {
    for (const romm::Game& g : games) {
        if (over()) return;
        std::string program, err;
        if (known(g.id, g.sizeBytes, &program)) continue;
        if (!learn(client, g.id, g.sizeBytes, &program, &err))
            std::fprintf(stderr, "[lindbergh] game %d's files: %s\n", g.id, err.c_str());
    }
}

}  // namespace lindberghprograms
