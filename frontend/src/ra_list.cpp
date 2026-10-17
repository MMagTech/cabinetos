// The game's page's achievements (fetchList): the set and this person's
// unlocks, asked of RetroAchievements in turn, and the last answer kept per
// person and game, so the page still has it offline. Moved out of
// achievements.cpp in the restructure (#398).

#include "ra_state.h"

#include "covercache.h"
#include "storage.h"

#include <json-c/json.h>

#include "rc_api_runtime.h"
#include "rc_api_user.h"

#include <algorithm>
#include <cstdio>
#include <map>
#include <memory>

namespace ra {
namespace {

// --- The game's page -------------------------------------------------------

std::map<std::string, GameList> gLists;   // "<account>:<hash>"

std::string listKey(const std::string& hash) { return std::to_string(gAccount) + ":" + hash; }

std::string listPath(const std::string& hash) {
    return covercache::dir() + "/achievements/" +
           storage::safeSegment(std::to_string(gAccount) + "-" + hash) + ".json";
}

void keepList(const std::string& hash, const GameList& l) {
    json_object* o = json_object_new_object();
    json_object_object_add(o, "none", json_object_new_boolean(l.none));
    json_object* arr = json_object_new_array();
    for (const Achievement& a : l.items) {
        json_object* x = json_object_new_object();
        json_object_object_add(x, "id", json_object_new_int64(a.id));
        json_object_object_add(x, "title", json_object_new_string(a.title.c_str()));
        json_object_object_add(x, "description", json_object_new_string(a.description.c_str()));
        json_object_object_add(x, "badge", json_object_new_string(a.badgeUrl.c_str()));
        json_object_object_add(x, "badge_locked",
                               json_object_new_string(a.lockedBadgeUrl.c_str()));
        json_object_object_add(x, "points", json_object_new_int64(a.points));
        json_object_object_add(x, "unlocked", json_object_new_boolean(a.unlocked));
        json_object_array_add(arr, x);
    }
    json_object_object_add(o, "items", arr);
    const std::string body = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
    json_object_put(o);
    storage::makeDirs(covercache::dir() + "/achievements");
    const std::string path = listPath(hash);
    if (FILE* f = std::fopen((path + ".part").c_str(), "wb")) {
        std::fwrite(body.data(), 1, body.size(), f);
        std::fclose(f);
        std::rename((path + ".part").c_str(), path.c_str());
    }
}

void count(GameList& l) {
    l.total = static_cast<int>(l.items.size());
    l.unlocked = static_cast<int>(
        std::count_if(l.items.begin(), l.items.end(), [](const Achievement& a) { return a.unlocked; }));
}

bool keptList(const std::string& hash, GameList* out) {
    FILE* f = std::fopen(listPath(hash).c_str(), "rb");
    if (!f) return false;
    std::string body;
    char buf[8192];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    std::fclose(f);
    json_object* o = json_tokener_parse(body.c_str());
    if (!o) return false;
    GameList l;
    l.known = true;
    json_object* v = nullptr;
    if (json_object_object_get_ex(o, "none", &v)) l.none = json_object_get_boolean(v);
    json_object* arr = nullptr;
    if (json_object_object_get_ex(o, "items", &arr) && json_object_get_type(arr) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(arr); ++i) {
            json_object* x = json_object_array_get_idx(arr, i);
            auto s = [x](const char* k) {
                json_object* y = nullptr;
                return (json_object_object_get_ex(x, k, &y) && y) ? str(json_object_get_string(y))
                                                                 : std::string();
            };
            auto i64 = [x](const char* k) {
                json_object* y = nullptr;
                return json_object_object_get_ex(x, k, &y) ? json_object_get_int64(y) : 0;
            };
            Achievement a;
            a.id = static_cast<uint32_t>(i64("id"));
            a.title = s("title");
            a.description = s("description");
            a.badgeUrl = s("badge");
            a.lockedBadgeUrl = s("badge_locked");
            a.points = static_cast<uint32_t>(i64("points"));
            json_object* u = nullptr;
            a.unlocked = json_object_object_get_ex(x, "unlocked", &u) && json_object_get_boolean(u);
            l.items.push_back(std::move(a));
        }
    }
    json_object_put(o);
    count(l);
    *out = std::move(l);
    return true;
}

}  // namespace

// Unlocked first, then locked, each keeping the set's own order.
void order(GameList& l) {
    std::stable_partition(l.items.begin(), l.items.end(),
                          [](const Achievement& a) { return a.unlocked; });
    count(l);
}

// The list kept in memory for a game, forgotten (endGame), so its page asks
// again.
void forgetList(const std::string& hash) { gLists.erase(listKey(hash)); }

// --- The game's page --------------------------------------------------------------

void fetchList(const std::string& raHash, std::function<void(const GameList&)> done) {
    if (gSample) {
        done(sampleList());
        return;
    }
    if (!signedIn() || raHash.empty()) {
        done(GameList{});
        return;
    }
    const std::string key = listKey(raHash);
    if (const auto it = gLists.find(key); it != gLists.end()) {
        done(it->second);
        return;
    }
    // THREE QUESTIONS IN TURN: the set (by hash), then this person's unlocks
    // in softcore and in hardcore, because a hardcore unlock made elsewhere is
    // unlocked here too. None of them starts a session: looking at a page is
    // not playing.
    const int account = gAccount;
    const std::string user = gUser, token = gToken;
    auto list = std::make_shared<GameList>();
    auto failed = [raHash, done, account]() {
        GameList kept;
        if (account == gAccount && keptList(raHash, &kept)) {
            done(kept);
            return;
        }
        done(GameList{});
    };

    rc_api_fetch_game_sets_request_t sets{};
    sets.username = user.c_str();
    sets.api_token = token.c_str();
    sets.game_hash = raHash.c_str();
    rc_api_request_t req{};
    if (rc_api_init_fetch_game_sets_request(&req, &sets) != RC_OK) { failed(); return; }
    send(req, [=](int status, const std::string& body) {
        if (account != gAccount) return;
        // The same answer a game's start asks for: kept, so a game whose page
        // was opened online can be played offline.
        if (status >= 200 && status < 300 && body.find("\"Success\":true") != std::string::npos)
            keepAnswer("sets-" + raHash + ".json", body);
        rc_api_server_response_t sr{body.c_str(), body.size(), status};
        rc_api_fetch_game_sets_response_t r{};
        const int rc = rc_api_process_fetch_game_sets_server_response(&r, &sr);
        // "NO SET" ONLY WHEN THE SERVER SAID SO. A 502 page, a refused token or
        // a garbled answer is a failure, and keeps what was kept before.
        const bool answered = status >= 200 && status < 300 && rc == RC_OK && r.response.succeeded;
        const bool unknown = rc == RC_NOT_FOUND || (answered && r.id == 0);
        if (!answered && !unknown) {
            rc_api_destroy_fetch_game_sets_response(&r);
            failed();
            return;
        }
        if (unknown) {
            // Answered, and the answer is "no set": kept, so the page says it
            // offline too.
            rc_api_destroy_fetch_game_sets_response(&r);
            list->known = true;
            list->none = true;
            gLists[listKey(raHash)] = *list;
            keepList(raHash, *list);
            done(*list);
            return;
        }
        const uint32_t gameId = r.id;
        for (uint32_t s = 0; s < r.num_sets; ++s) {
            const rc_api_achievement_set_definition_t& set = r.sets[s];
            if (set.type != RC_ACHIEVEMENT_SET_TYPE_CORE) continue;
            for (uint32_t i = 0; i < set.num_achievements; ++i) {
                const rc_api_achievement_definition_t& d = set.achievements[i];
                if (d.category != RC_ACHIEVEMENT_CATEGORY_CORE || d.id >= kWarningId) continue;
                Achievement a;
                a.id = d.id;
                a.title = str(d.title);
                a.description = str(d.description);
                a.badgeUrl = str(d.badge_url);
                a.lockedBadgeUrl = str(d.badge_locked_url);
                a.points = d.points;
                list->items.push_back(std::move(a));
            }
        }
        rc_api_destroy_fetch_game_sets_response(&r);
        if (list->items.empty()) {
            list->known = true;
            list->none = true;
            gLists[listKey(raHash)] = *list;
            keepList(raHash, *list);
            done(*list);
            return;
        }

        auto unlocks = std::make_shared<std::function<void(uint32_t)>>();
        *unlocks = [=](uint32_t hardcore) {
            rc_api_fetch_user_unlocks_request_t u{};
            u.username = user.c_str();
            u.api_token = token.c_str();
            u.game_id = gameId;
            u.hardcore = hardcore;
            rc_api_request_t ureq{};
            if (rc_api_init_fetch_user_unlocks_request(&ureq, &u) != RC_OK) {
                *unlocks = nullptr;
                failed();
                return;
            }
            send(ureq, [=](int st, const std::string& b) {
                if (account != gAccount) { *unlocks = nullptr; return; }
                rc_api_server_response_t usr{b.c_str(), b.size(), st};
                rc_api_fetch_user_unlocks_response_t ur{};
                const int urc = rc_api_process_fetch_user_unlocks_server_response(&ur, &usr);
                if (st < 0 || urc != RC_OK || !ur.response.succeeded) {
                    rc_api_destroy_fetch_user_unlocks_response(&ur);
                    *unlocks = nullptr;
                    failed();
                    return;
                }
                for (uint32_t k = 0; k < ur.num_achievement_ids; ++k)
                    for (Achievement& a : list->items)
                        if (a.id == ur.achievement_ids[k]) a.unlocked = true;
                rc_api_destroy_fetch_user_unlocks_response(&ur);
                if (hardcore == 0) {
                    (*unlocks)(1);
                    return;
                }
                list->known = true;
                order(*list);
                gLists[listKey(raHash)] = *list;
                keepList(raHash, *list);
                done(*list);
                // Breaks the shared_ptr's hold on itself, now nothing follows.
                *unlocks = nullptr;
            });
            rc_api_destroy_request(&ureq);
        };
        (*unlocks)(0);
    });
    rc_api_destroy_request(&req);
}

}  // namespace ra
