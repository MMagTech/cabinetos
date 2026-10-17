// Who the console is acting as, and every person with a folder on it
// (users.h): the current person, asked of RomM once and cached for a console
// that cannot reach its server, and each person's folder, found by their
// RomM id. Moved out of storage.cpp in the restructure (#398).

#include "users.h"

#include <dirent.h>

#include <algorithm>
#include <cstdio>
#include <map>
#include <mutex>

#include <json-c/json.h>

#include "romm.h"
#include "storage.h"
#include "storage_internal.h"

namespace storage {
namespace {

User gUser;

std::string userCachePath() { return configDir() + "/user.json"; }

}  // namespace

// FOUND BY THE ROMM ID, NAMED ONCE (#194). The id is the key; the name is for
// a person reading the folder. A person renamed on RomM kept getting a new,
// empty folder, and anything owed to RomM stayed stranded in the old one. The
// folder is not renamed to follow: what is owed is recorded with its full path
// (cache::Owed::localPath), so a rename would strand it all the same. An
// existing `<id> - <any name>` folder is used as it is, and only a person with
// none gets one under their current name. Looked up once per id.
std::string User::dirName() const {
    const std::string fresh = std::to_string(id) + " - " + safeSegment(name);
    if (id <= 0) return fresh;
    static std::mutex m;
    static std::map<int, std::string> chosen;
    std::lock_guard<std::mutex> lock(m);
    if (auto it = chosen.find(id); it != chosen.end()) return it->second;
    std::string pick = fresh;
    const std::string dir = root() + "/users";
    if (!isDir(dir + "/" + fresh)) {
        const std::string prefix = std::to_string(id) + " - ";
        if (DIR* d = ::opendir(dir.c_str())) {
            while (struct dirent* e = ::readdir(d)) {
                const std::string n = e->d_name;
                if (n.compare(0, prefix.size(), prefix) == 0 && isDir(dir + "/" + n)) {
                    pick = n;
                    std::fprintf(stderr, "[storage] %s is %s on RomM now; keeping the folder\n",
                                 n.c_str(), name.c_str());
                    break;
                }
            }
            ::closedir(d);
        }
    }
    chosen[id] = pick;
    return pick;
}

const User& currentUser() { return gUser; }

void setCurrentUser(const User& u) {
    gUser = u;
    if (!u.valid()) return;
    makeDirs(configDir());
    // Cached so a console with no network still knows whose saves it holds.
    // "We could not reach the server" is not a reason to start writing into
    // somebody else's directory.
    json_object* o = json_object_new_object();
    json_object_object_add(o, "id", json_object_new_int(u.id));
    json_object_object_add(o, "username", json_object_new_string(u.name.c_str()));
    // Cached with the rest of them, so a console that cannot reach its server
    // still draws the right face rather than falling back to a letter.
    if (!u.avatar.empty())
        json_object_object_add(o, "avatar", json_object_new_string(u.avatar.c_str()));
    const char* text = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PRETTY);
    const std::string tmp = userCachePath() + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(text, f);
        std::fputc('\n', f);
        std::fclose(f);
        ::rename(tmp.c_str(), userCachePath().c_str());
    }
    json_object_put(o);
    makeDirs(userDir(u));
}

bool resolveCurrentUser(romm::Client& client, std::string* err) {
    romm::User me;
    std::string e;
    if (client.haveToken() && client.fetchCurrentUser(&me, &e) && me.id > 0) {
        User u;
        u.id = me.id;
        u.name = me.username;
        u.avatar = me.avatarPath;
        setCurrentUser(u);
        return true;
    }
    // The cached answer. A console that cannot reach its server still has to
    // put a save somewhere, and the right somewhere is where the last one went.
    if (FILE* f = std::fopen(userCachePath().c_str(), "rb")) {
        std::string body;
        char buf[512];
        size_t n;
        while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
        std::fclose(f);
        json_object* o = json_tokener_parse(body.c_str());
        if (o) {
            json_object* v = nullptr;
            User u;
            if (json_object_object_get_ex(o, "id", &v) && v)
                u.id = json_object_get_int(v);
            if (json_object_object_get_ex(o, "username", &v) && v)
                u.name = json_object_get_string(v);
            if (json_object_object_get_ex(o, "avatar", &v) && v)
                u.avatar = json_object_get_string(v);
            json_object_put(o);
            if (u.valid()) {
                gUser = u;
                std::fprintf(stderr, "[storage] server did not answer; using the "
                                     "last known user %s\n", u.dirName().c_str());
                return true;
            }
        }
    }
    if (err) *err = e.empty() ? "no RomM user, and none cached" : e;
    return false;
}

std::vector<User> knownUsers() {
    std::vector<User> out;
    const std::string dir = root() + "/users";
    DIR* d = ::opendir(dir.c_str());
    if (!d) return out;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        if (!isDir(dir + "/" + e->d_name)) continue;
        const std::string name = e->d_name;
        const int id = romIdFromEntry(name);   // the same convention, both times
        if (id <= 0) continue;
        User u;
        u.id = id;
        const size_t dash = name.find(" - ");
        u.name = dash == std::string::npos ? std::string() : name.substr(dash + 3);
        out.push_back(std::move(u));
    }
    ::closedir(d);
    std::sort(out.begin(), out.end(), [](const User& a, const User& b) { return a.id < b.id; });
    return out;
}

}  // namespace storage
