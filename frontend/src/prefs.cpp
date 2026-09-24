#include "prefs.h"

#include <cstdio>

#include <json-c/json.h>

#include "storage.h"

namespace prefs {

namespace {

std::string path() { return storage::configDir() + "/settings.json"; }

// The whole file as an object, or a new empty one. The caller owns it.
json_object* load() {
    std::string body;
    if (FILE* f = std::fopen(path().c_str(), "rb")) {
        char buf[1024];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
        std::fclose(f);
    }
    json_object* o = body.empty() ? nullptr : json_tokener_parse(body.c_str());
    if (o && json_object_get_type(o) == json_type_object) return o;
    if (o) json_object_put(o);
    return json_object_new_object();
}

}  // namespace

std::string get(const std::string& key, const std::string& fallback) {
    json_object* o = load();
    std::string out = fallback;
    json_object* v = nullptr;
    if (json_object_object_get_ex(o, key.c_str(), &v) &&
        json_object_get_type(v) == json_type_string)
        out = json_object_get_string(v);
    json_object_put(o);
    return out;
}

void set(const std::string& key, const std::string& value) {
    // Read, change one key, write the lot: a file of a handful of words is
    // cheaper to rewrite than to reason about, and keys this build does not
    // know (written by a newer one) survive the round trip.
    json_object* o = load();
    json_object_object_add(o, key.c_str(), json_object_new_string(value.c_str()));
    storage::makeDirs(storage::configDir());
    const std::string tmp = path() + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY), f);
        std::fputc('\n', f);
        const bool ok = std::fflush(f) == 0;
        std::fclose(f);
        if (ok && std::rename(tmp.c_str(), path().c_str()) == 0)
            std::fprintf(stderr, "[prefs] %s = %s\n", key.c_str(), value.c_str());
        else
            std::fprintf(stderr, "[prefs] could not write %s\n", path().c_str());
    }
    json_object_put(o);
}

}  // namespace prefs
