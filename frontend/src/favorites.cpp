#include "favorites.h"

#include <json-c/json.h>

#include <cstdio>
#include <cstdlib>
#include <mutex>

#include "fileio.h"

// FAVOURITES NOT YET ON THE SERVER (#267). The heart on a game's page answers
// at once; what the server still has to hear is kept per person in
// `favorites-owed.json`, rom id to wanted state, and sent by the uploader
// whenever it sends anything owed. The last press wins: pressing twice while
// the server is away leaves nothing to send. Locked: the frame thread writes
// it and the uploader's worker reads it.
static std::mutex gFavOwedMutex;

static std::map<int, bool> favOwedLocked(const storage::User& u) {
    std::map<int, bool> m;
    if (json_object* o = json_object_from_file((storage::userDir(u) + "/favorites-owed.json").c_str())) {
        if (json_object_is_type(o, json_type_object)) {
            json_object_object_foreach(o, k, v) m[std::atoi(k)] = json_object_get_boolean(v);
        }
        json_object_put(o);
    }
    return m;
}

static void putFavOwedLocked(const storage::User& u, const std::map<int, bool>& m) {
    const std::string path = storage::userDir(u) + "/favorites-owed.json";
    if (m.empty()) { cab::removeFile(path); return; }
    json_object* o = json_object_new_object();
    for (const auto& [id, on] : m)
        json_object_object_add(o, std::to_string(id).c_str(), json_object_new_boolean(on));
    const std::string tmp = path + ".part";
    if (json_object_to_file(tmp.c_str(), o) == 0) ::rename(tmp.c_str(), path.c_str());
    json_object_put(o);
}

std::map<int, bool> favOwed(const storage::User& u) {
    std::lock_guard<std::mutex> lock(gFavOwedMutex);
    return favOwedLocked(u);
}

void oweFavorite(const storage::User& u, int romId, bool on) {
    std::lock_guard<std::mutex> lock(gFavOwedMutex);
    std::map<int, bool> m = favOwedLocked(u);
    m[romId] = on;
    putFavOwedLocked(u, m);
}

// Every favourite `who` owes, sent; each stops being owed once the server has
// it, unless it was pressed again meanwhile.
bool sendFavoritesAs(romm::Client& c, const storage::User& who, std::string* err) {
    for (const auto& [romId, on] : favOwed(who)) {
        bool forbidden = false;
        if (!c.setFavorite(romId, on, &forbidden, err)) {
            if (forbidden)
                std::fprintf(stderr, "[favorite] RomM refused: this console was paired "
                                     "before favourites needed collections.write\n");
            return false;
        }
        std::lock_guard<std::mutex> lock(gFavOwedMutex);
        std::map<int, bool> m = favOwedLocked(who);
        if (auto it = m.find(romId); it != m.end() && it->second == on) m.erase(it);
        putFavOwedLocked(who, m);
        std::fprintf(stderr, "[favorite] %d %s on RomM\n", romId, on ? "added" : "removed");
    }
    return true;
}
