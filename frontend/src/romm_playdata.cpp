// The person's play on RomM: saves and states (listed, fetched, uploaded,
// deleted), screenshots, favourites (setFavorite) and time played (play
// sessions). Moved out of romm.cpp in the restructure (#398).

#include "romm.h"

#include "romm_internal.h"

#include <curl/curl.h>
#include <json-c/json.h>

#include <algorithm>
#include <cstdio>
#include <set>

namespace romm {

namespace {
bool parseAssets(const std::string& body, std::vector<Asset>* out, std::string* err) {
    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "response was not JSON"; return false; }
    json_object* arr = root;
    if (json_object_get_type(root) != json_type_array) {
        if (!json_object_object_get_ex(root, "items", &arr) ||
            json_object_get_type(arr) != json_type_array) {
            json_object_put(root);
            if (err) *err = "response was not a list";
            return false;
        }
    }
    const size_t n = json_object_array_length(arr);
    for (size_t i = 0; i < n; ++i) {
        json_object* o = json_object_array_get_idx(arr, i);
        Asset a;
        a.id = static_cast<int>(jint(o, "id"));
        a.romId = static_cast<int>(jint(o, "rom_id"));
        a.fileName = jstr(o, "file_name");
        a.sizeBytes = jint(o, "file_size_bytes");
        a.emulator = jstr(o, "emulator");
        a.updatedAt = jstr(o, "updated_at");
        json_object* shot = nullptr;
        if (json_object_object_get_ex(o, "screenshot", &shot) &&
            json_object_get_type(shot) == json_type_object)
            a.picturePath = jstr(shot, "download_path");
        if (a.id != 0) out->push_back(std::move(a));
    }
    json_object_put(root);
    return true;
}
}  // namespace

bool Client::fetchSaves(int romId, std::vector<Asset>* out, std::string* err) {
    out->clear();
    std::string body;
    if (!get("/api/saves?rom_id=" + std::to_string(romId), &body, err)) return false;
    return parseAssets(body, out, err);
}

bool Client::fetchSavesFor(const std::vector<int>& romIds, std::vector<Asset>* out,
                           std::string* err) {
    out->clear();
    constexpr size_t kPerCall = 500;
    for (size_t at = 0; at < romIds.size(); at += kPerCall) {
        std::string q;
        for (size_t i = at; i < romIds.size() && i < at + kPerCall; ++i)
            q += (q.empty() ? "?rom_ids=" : "&rom_ids=") + std::to_string(romIds[i]);
        std::string body;
        std::vector<Asset> got;
        if (get("/api/saves" + q, &body, err) && parseAssets(body, &got, err)) {
            // Only rows for the games asked about count: a server that
            // ignored rom_ids answers with every save. A row with no game
            // cannot be placed, and then it is asked one game at a time.
            std::set<int> asked(romIds.begin() + static_cast<long>(at),
                                romIds.begin() + static_cast<long>(std::min(at + kPerCall,
                                                                            romIds.size())));
            bool placed = true;
            for (const Asset& a : got)
                if (a.romId <= 0) placed = false;
            if (placed) {
                for (Asset& a : got)
                    if (asked.count(a.romId)) out->push_back(std::move(a));
                continue;
            }
        }
        // One game at a time, as before RomM 5.3.
        for (size_t i = at; i < romIds.size() && i < at + kPerCall; ++i) {
            std::vector<Asset> one;
            if (!fetchSaves(romIds[i], &one, err)) return false;
            for (Asset& a : one) a.romId = romIds[i];
            out->insert(out->end(), one.begin(), one.end());
        }
    }
    return true;
}

bool Client::fetchStates(int romId, std::vector<Asset>* out, std::string* err) {
    out->clear();
    std::string body;
    if (!get("/api/states?rom_id=" + std::to_string(romId), &body, err)) return false;
    return parseAssets(body, out, err);
}

std::vector<uint8_t> Client::fetchAsset(const char* kind, int assetId) const {
    return fetchBytes(std::string("/api/") + kind + "/" + std::to_string(assetId) + "/content");
}

bool Client::uploadSave(int romId, const std::string& emulator, const std::string& fileName,
                        const std::vector<uint8_t>& data, std::string* err) const {
    // overwrite=true is load-bearing: it replaces the server's copy of the same
    // file name instead of stacking a row per upload, which is what keeps a PS1
    // game at ONE memory card rather than one per session. Cabinet's note.
    const std::string path = "/api/saves?rom_id=" + std::to_string(romId) +
                             "&emulator=" + emulator + "&overwrite=true";
    return postMultipart(path, "saveFile", fileName, data, err);
}

bool Client::uploadState(int romId, const std::string& emulator, const std::string& fileName,
                         const std::vector<uint8_t>& data, std::string* err,
                         const std::string& shotName, const std::vector<uint8_t>& shot) const {
    // Deliberately NOT overwrite: a history of states is the point of states.
    const std::string path = "/api/states?rom_id=" + std::to_string(romId) +
                             "&emulator=" + emulator;
    return postMultipart(path, "stateFile", fileName, data, err, shotName, shot);
}

bool Client::uploadScreenshot(int romId, const std::string& fileName,
                              const std::vector<uint8_t>& png, std::string* err) const {
    return postMultipart("/api/screenshots?rom_id=" + std::to_string(romId), "screenshotFile",
                         fileName, png, err);
}

bool Client::deleteStates(const std::vector<int>& ids, std::string* err) const {
    if (ids.empty()) return true;
    std::string json = "{\"states\":[";
    for (size_t i = 0; i < ids.size(); ++i) {
        if (i) json += ",";
        json += std::to_string(ids[i]);
    }
    json += "]}";
    std::string body;
    long status = 0;
    if (!postJson("/api/states/delete", json, &body, &status, err)) return false;
    if (status >= 400) {
        if (err) *err = "HTTP " + std::to_string(status) + " deleting states";
        return false;
    }
    return true;
}

bool Client::setFavorite(int romId, bool on, bool* forbidden, std::string* err) {
    *forbidden = false;
    std::vector<Collection> cols;
    if (!fetchCollections(&cols, err)) return false;
    // THEIR OWN Favorites. The list carries everyone's public collections
    // too (#352), so on a child's account a parent's public Favorites can
    // come first; RomM lets only the owner add to a collection (5.3.1,
    // add_collection_roms), so the child's favourite would be refused. Read
    // from RomM's source, not met on a console.
    int id = 0;
    bool any = false;
    for (const Collection& c : cols) any = any || c.isFavorite;
    if (any) {
        User me;
        if (!fetchCurrentUser(&me, err)) return false;
        for (const Collection& c : cols)
            if (c.isFavorite && c.userId == me.id) { id = c.id; break; }
    }
    if (!id && !on) return true;  // no favourites at all: nothing to take out of
    if (!id) {
        // THEIR FIRST FAVOURITE ANYWHERE: the collection RomM's own web page
        // would make, a form with its name, flagged by the query. RomM's own
        // name, "Favorites" (useFavoriteToggle.ts), so every account's reads
        // the same whichever side made it; this said "Favourites" until
        // 2026-10-12.
        const std::string boundary = "CabinetOSBoundary7f3a91c4";
        const std::string form = "--" + boundary +
                                 "\r\nContent-Disposition: form-data; name=\"name\"\r\n\r\n"
                                 "Favorites\r\n--" + boundary + "--\r\n";
        CURL* c = curl_easy_init();
        if (!c) { if (err) *err = "curl init failed"; return false; }
        curl_slist* hdrs = curl_slist_append(
            nullptr, ("Content-Type: multipart/form-data; boundary=" + boundary).c_str());
        const Where at = where();
        if (!at.token.empty())
            hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + at.token).c_str());
        std::string reply;
        const std::string url = at.base + "/api/collections?is_favorite=true";
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, form.data());
        curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(form.size()));
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &reply);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);
        const CURLcode rc = curl_easy_perform(c);
        noteTransport(c, rc);
        long status = 0;
        curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
        curl_slist_free_all(hdrs);
        curl_easy_cleanup(c);
        if (rc != CURLE_OK) { if (err) *err = curl_easy_strerror(rc); return false; }
        if (status == 401 || status == 403) *forbidden = true;
        if (status >= 400) {
            if (err) *err = "HTTP " + std::to_string(status) + " making the favourites";
            return false;
        }
        if (json_object* o = json_tokener_parse(reply.c_str())) {
            json_object* v = nullptr;
            if (json_object_object_get_ex(o, "id", &v)) id = json_object_get_int(v);
            json_object_put(o);
        }
        if (!id) { if (err) *err = "the new favourites collection had no id"; return false; }
    }
    std::string body;
    long status = 0;
    const std::string json = "{\"rom_ids\":[" + std::to_string(romId) + "]}";
    if (!sendJson(on ? "POST" : "DELETE", "/api/collections/" + std::to_string(id) + "/roms",
                  json, &body, &status, err))
        return false;
    if (status == 401 || status == 403) *forbidden = true;
    if (status >= 400) {
        if (err) *err = "HTTP " + std::to_string(status) + (on ? " adding" : " removing") +
                        " a favourite";
        return false;
    }
    return true;
}

bool Client::postPlaySessions(const std::vector<PlaySession>& list, std::vector<int>* outcome,
                              std::string* err) const {
    outcome->assign(list.size(), 0);
    if (list.empty()) return true;
    json_object* root = json_object_new_object();
    json_object* arr = json_object_new_array();
    for (const PlaySession& p : list) {
        json_object* o = json_object_new_object();
        json_object_object_add(o, "rom_id", json_object_new_int(p.romId));
        json_object_object_add(o, "start_time", json_object_new_string(p.start.c_str()));
        json_object_object_add(o, "end_time", json_object_new_string(p.end.c_str()));
        json_object_object_add(o, "duration_ms", json_object_new_int64(p.durationMs));
        json_object_array_add(arr, o);
    }
    json_object_object_add(root, "sessions", arr);
    const std::string payload = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PLAIN);
    json_object_put(root);

    std::string body;
    long status = 0;
    if (!postJson("/api/play-sessions", payload, &body, &status, err)) return false;
    if (status >= 400) {
        if (err) *err = "HTTP " + std::to_string(status) + " sending play sessions";
        return false;
    }
    json_object* resp = json_tokener_parse(body.c_str());
    if (!resp) { if (err) *err = "play session response was not JSON"; return false; }
    json_object* results = nullptr;
    if (json_object_object_get_ex(resp, "results", &results) &&
        json_object_get_type(results) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(results); ++i) {
            json_object* r = json_object_array_get_idx(results, i);
            const int64_t idx = jint(r, "index");
            if (idx < 0 || idx >= static_cast<int64_t>(list.size())) continue;
            const std::string st = jstr(r, "status");
            (*outcome)[idx] = st == "created" || st == "duplicate" ? 1 : st == "error" ? -1 : 0;
            if (st == "error")
                std::fprintf(stderr, "[playtime] RomM refused a session: %s\n",
                             jstr(r, "detail").c_str());
        }
    }
    json_object_put(resp);
    return true;
}

bool Client::fetchPlayedMs(int romId, int64_t* ms, std::string* err) const {
    *ms = 0;
    // Newest first, a page at a time. A game played every day for years is a
    // few thousand sessions; the cap is there so a wrong answer cannot loop.
    constexpr int kPage = 500;
    for (int page = 0; page < 40; ++page) {
        std::string body;
        if (!get("/api/play-sessions?rom_id=" + std::to_string(romId) +
                     "&limit=" + std::to_string(kPage) +
                     "&offset=" + std::to_string(page * kPage),
                 &body, err))
            return false;
        json_object* root = json_tokener_parse(body.c_str());
        if (!root || json_object_get_type(root) != json_type_array) {
            if (root) json_object_put(root);
            if (err) *err = "play session list was not a list";
            return false;
        }
        const size_t n = json_object_array_length(root);
        for (size_t i = 0; i < n; ++i)
            *ms += jint(json_object_array_get_idx(root, i), "duration_ms");
        json_object_put(root);
        if (n < static_cast<size_t>(kPage)) return true;
    }
    return true;
}

}  // namespace romm
