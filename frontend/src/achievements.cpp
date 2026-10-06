#include "achievements.h"

#include "accounts.h"
#include "core.h"
#include "covercache.h"
#include "ps2.h"
#include "storage.h"

#include <curl/curl.h>
#include <strings.h>
#include <sys/stat.h>
#include <json-c/json.h>

#include "rc_client.h"
#include "rc_consoles.h"
#include "rc_api_runtime.h"
#include "rc_api_user.h"
#include "rc_libretro.h"
#include "libretro.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <thread>

namespace ra {
namespace {

rc_client_t* gClient = nullptr;

// --- The network ----------------------------------------------------------
//
// ONE WORKER, IN ORDER. rc_client sends little (a sign-in, a game's data, a
// session, an unlock now and then), and RetroAchievements asks clients not to
// fire requests in parallel. Answers come back to the frame thread through
// gDone, so rc_client only ever hears from the network there.

std::mutex gUaMutex;
std::string gUaBase;   // "CabinetOS/2026.10.06 (Linux)"
std::string gUaCore;   // " mGBA/0.10.5", the game's core, or empty
std::string gUaRc;     // " rcheevos/12.5.0"

std::string userAgent() {
    std::lock_guard<std::mutex> lk(gUaMutex);
    return gUaBase + gUaCore + gUaRc;
}

std::atomic<bool> gStopping{false};

// Shutting down: a request in flight stops at once rather than waiting out
// curl's timeouts, and comes back as "no answer" (shutdown queues it).
int abortTransfer(void*, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    return gStopping ? 1 : 0;
}

size_t appendBody(char* p, size_t size, size_t n, void* out) {
    static_cast<std::string*>(out)->append(p, size * n);
    return size * n;
}

// The HTTP status, or RetroAchievements' "could not reach it, try again" code
// when nothing came back at all: rc_client keeps an unlock and retries it on
// that one, which is what makes a dropped connection mid-game lose nothing.
int request(const std::string& url, const char* post, const char* contentType,
            std::string* body, long timeoutSeconds) {
    // CABINETOS_RA_OFFLINE=1: every request fails as with no network, for
    // trying the offline path without taking the console off its network.
    static const bool offline = std::getenv("CABINETOS_RA_OFFLINE") != nullptr;
    if (offline) return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    CURL* c = curl_easy_init();
    if (!c) return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    const std::string ua = userAgent();
    curl_slist* headers = nullptr;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_USERAGENT, ua.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 10L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, timeoutSeconds);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, appendBody);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, abortTransfer);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
    if (post) {
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, post);
        if (contentType) {
            headers = curl_slist_append(headers,
                                        (std::string("Content-Type: ") + contentType).c_str());
            curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
        }
    }
    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (headers) curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || status == 0) {
        std::fprintf(stderr, "[ra] no answer from %s: %s\n",
                     url.substr(0, url.find('?')).c_str(), curl_easy_strerror(rc));
        return RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR;
    }
    return static_cast<int>(status);
}

struct Job {
    std::string url;
    std::string post;
    bool isPost = false;
    std::string contentType;
    // Runs on the frame thread with the status and the body.
    std::function<void(int, const std::string&)> done;
};

std::mutex gJobMutex;
std::condition_variable gJobCv;
std::deque<Job> gJobs;
std::thread gWorker;

std::mutex gDoneMutex;
std::deque<std::function<void()>> gDone;

void workerLoop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lk(gJobMutex);
            gJobCv.wait(lk, [] { return gStopping || !gJobs.empty(); });
            if (gStopping) return;
            job = std::move(gJobs.front());
            gJobs.pop_front();
        }
        auto body = std::make_shared<std::string>();
        const int status = request(job.url, job.isPost ? job.post.c_str() : nullptr,
                                   job.contentType.empty() ? nullptr : job.contentType.c_str(),
                                   body.get(), 30L);
        std::lock_guard<std::mutex> lk(gDoneMutex);
        auto done = std::move(job.done);
        gDone.push_back([done, status, body] { done(status, *body); });
    }
}

void send(const rc_api_request_t& r, std::function<void(int, const std::string&)> done) {
    Job job;
    job.url = r.url ? r.url : "";
    job.isPost = r.post_data != nullptr;
    if (r.post_data) job.post = r.post_data;
    if (r.content_type) job.contentType = r.content_type;
    job.done = std::move(done);
    {
        std::lock_guard<std::mutex> lk(gJobMutex);
        gJobs.push_back(std::move(job));
    }
    gJobCv.notify_one();
}

// --- Offline (#74) ------------------------------------------------------------
//
// "ACHIEVEMENTS EARNED OFFLINE MUST WAIT AND SEND" (docs/ROADMAP.md). rcheevos
// keeps an unlock that could not be sent only in memory, retrying while the
// game runs, so one earned with the network down was lost if the game was
// quit first, and a game started offline could not be checked at all. So the
// console sits between rcheevos and the network:
//
//   ANSWERS ARE KEPT. Each sign-in, set and session answer RetroAchievements
//   gives is written to the person's private folder, so a game played online
//   once can be played offline after.
//
//   OFFLINE, THE KEPT ANSWER IS GIVEN instead of the failure, and an unlock
//   goes into a queue on disk (pending.json) and is answered "done".
//
//   THE QUEUE IS SENT when the network answers again, each with how long ago
//   it was earned, the field RetroAchievements has for a delayed unlock.
//
// Every one of these lives under accounts::raDir, 0700, removed with the
// account.

int gAccount = 0;
std::string gUser;            // stored username for gAccount; empty when none
std::string gToken;
bool gLoginBusy = false;      // a sign-in is in flight
bool gLoggedIn = false;       // rc_client accepted it

std::string param(const std::string& form, const std::string& key) {
    size_t at = 0;
    while (at < form.size()) {
        size_t amp = form.find('&', at);
        if (amp == std::string::npos) amp = form.size();
        const std::string kv = form.substr(at, amp - at);
        const size_t eq = kv.find('=');
        if (eq != std::string::npos && kv.compare(0, eq, key) == 0) {
            std::string v;
            const std::string raw = kv.substr(eq + 1);
            for (size_t i = 0; i < raw.size(); ++i) {
                if (raw[i] == '%' && i + 2 < raw.size()) {
                    v += static_cast<char>(std::strtol(raw.substr(i + 1, 2).c_str(), nullptr, 16));
                    i += 2;
                } else {
                    v += raw[i] == '+' ? ' ' : raw[i];
                }
            }
            return v;
        }
        at = amp + 1;
    }
    return {};
}

std::string keptPath(const std::string& name) {
    return accounts::raDir(gAccount) + "/" + storage::safeSegment(name);
}

void keepAnswer(const std::string& name, const std::string& body) {
    if (gAccount <= 0 || body.empty()) return;
    const std::string path = keptPath(name);
    const std::string part = path + ".part";
    FILE* f = std::fopen(part.c_str(), "wb");
    if (!f) return;
    ::fchmod(::fileno(f), 0600);
    std::fwrite(body.data(), 1, body.size(), f);
    std::fclose(f);
    std::rename(part.c_str(), path.c_str());
}

std::string keptAnswer(const std::string& name) {
    if (gAccount <= 0) return {};
    FILE* f = std::fopen(keptPath(name).c_str(), "rb");
    if (!f) return {};
    std::string body;
    char buf[16384];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    std::fclose(f);
    return body;
}

struct Owed {
    std::string user;
    uint32_t id = 0;
    std::string hash;
    long long when = 0;   // unix seconds it was earned
};

std::vector<Owed> loadOwed() {
    std::vector<Owed> out;
    json_object* arr = json_tokener_parse(keptAnswer("pending.json").c_str());
    if (!arr) return out;
    if (json_object_get_type(arr) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(arr); ++i) {
            json_object* o = json_object_array_get_idx(arr, i);
            json_object* v = nullptr;
            Owed w;
            if (json_object_object_get_ex(o, "user", &v)) w.user = json_object_get_string(v);
            if (json_object_object_get_ex(o, "id", &v)) w.id = static_cast<uint32_t>(json_object_get_int64(v));
            if (json_object_object_get_ex(o, "hash", &v)) w.hash = json_object_get_string(v);
            if (json_object_object_get_ex(o, "when", &v)) w.when = json_object_get_int64(v);
            if (w.id) out.push_back(std::move(w));
        }
    }
    json_object_put(arr);
    return out;
}

void saveOwed(const std::vector<Owed>& owed) {
    json_object* arr = json_object_new_array();
    for (const Owed& w : owed) {
        json_object* o = json_object_new_object();
        json_object_object_add(o, "user", json_object_new_string(w.user.c_str()));
        json_object_object_add(o, "id", json_object_new_int64(w.id));
        json_object_object_add(o, "hash", json_object_new_string(w.hash.c_str()));
        json_object_object_add(o, "when", json_object_new_int64(w.when));
        json_object_array_add(arr, o);
    }
    keepAnswer("pending.json", json_object_to_json_string_ext(arr, JSON_C_TO_STRING_PLAIN));
    json_object_put(arr);
}

long long nowSeconds() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}

// What rcheevos is given instead of a failure, when there is something to
// give. False leaves the failure as it was.
bool offlineAnswer(const std::string& api, const std::string& form, std::string* body) {
    const std::string hash = param(form, "m");
    if (api == "login2") {
        // Only the same person's kept sign-in, and only with their token.
        if (param(form, "t").empty()) return false;
        json_object* o = json_tokener_parse(keptAnswer("login.json").c_str());
        if (!o) return false;
        json_object* u = nullptr;
        const bool same = json_object_object_get_ex(o, "User", &u) &&
                          strcasecmp(json_object_get_string(u), param(form, "u").c_str()) == 0;
        json_object_put(o);
        if (!same) return false;
        *body = keptAnswer("login.json");
        std::fprintf(stderr, "[ra] offline: signed in from the kept answer\n");
        return true;
    }
    if (api == "achievementsets" && !hash.empty()) {
        *body = keptAnswer("sets-" + hash + ".json");
        if (!body->empty()) std::fprintf(stderr, "[ra] offline: the kept set for %s\n", hash.c_str());
        return !body->empty();
    }
    if (api == "startsession") {
        *body = keptAnswer("session-" + hash + ".json");
        // Never played online: no unlocks known, which only means one earned
        // elsewhere may be earned again here and sent twice; the server keeps
        // the first.
        if (body->empty()) *body = "{\"Success\":true}";
        return true;
    }
    if (api == "awardachievement") {
        Owed w;
        w.user = param(form, "u");
        w.id = static_cast<uint32_t>(std::strtoul(param(form, "a").c_str(), nullptr, 10));
        w.hash = hash;
        w.when = nowSeconds() - std::strtoll(param(form, "o").c_str(), nullptr, 10);
        if (w.id == 0) return false;
        std::vector<Owed> owed = loadOwed();
        bool have = false;
        for (const Owed& o : owed) have = have || (o.id == w.id && o.user == w.user);
        if (!have) owed.push_back(w);
        saveOwed(owed);
        std::fprintf(stderr, "[ra] offline: unlock %u kept to send later (%zu waiting)\n", w.id,
                     owed.size());
        *body = "{\"Success\":true,\"AchievementID\":" + std::to_string(w.id) + "}";
        return true;
    }
    if (api == "ping") {
        *body = "{\"Success\":true}";
        return true;
    }
    return false;
}

void rememberAnswer(const std::string& api, const std::string& form, const std::string& body) {
    // Only answers that say they succeeded: a refusal kept would be replayed.
    if (body.find("\"Success\":true") == std::string::npos) return;
    const std::string hash = param(form, "m");
    if (api == "login2") keepAnswer("login.json", body);
    else if (api == "achievementsets" && !hash.empty()) keepAnswer("sets-" + hash + ".json", body);
    else if (api == "startsession" && !hash.empty()) keepAnswer("session-" + hash + ".json", body);
}

bool gSending = false;
auto gLastSendTry = std::chrono::steady_clock::now() - std::chrono::hours(1);

// The queue, oldest first, one at a time; stops at the first that cannot
// reach the server and tries again later.
void sendOwed() {
    if (gSending || !gLoggedIn || gUser.empty() || gToken.empty()) return;
    std::vector<Owed> owed = loadOwed();
    auto it = std::find_if(owed.begin(), owed.end(), [](const Owed& w) {
        return strcasecmp(w.user.c_str(), gUser.c_str()) == 0;
    });
    if (it == owed.end()) return;
    const Owed w = *it;
    rc_api_award_achievement_request_t a{};
    a.username = gUser.c_str();
    a.api_token = gToken.c_str();
    a.achievement_id = w.id;
    a.hardcore = 0;
    a.game_hash = w.hash.c_str();
    a.seconds_since_unlock = static_cast<uint32_t>(std::max(1LL, nowSeconds() - w.when));
    rc_api_request_t req{};
    if (rc_api_init_award_achievement_request(&req, &a) != RC_OK) return;
    gSending = true;
    const int account = gAccount;
    send(req, [w, account](int status, const std::string& body) {
        gSending = false;
        if (account != gAccount) return;
        if (status == RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR || status >= 500) return;
        // Answered, either way: sent, or refused for good (already unlocked,
        // achievement gone). Off the queue, and on to the next.
        std::vector<Owed> owed = loadOwed();
        owed.erase(std::remove_if(owed.begin(), owed.end(),
                                  [&](const Owed& o) { return o.id == w.id && o.user == w.user; }),
                   owed.end());
        saveOwed(owed);
        std::fprintf(stderr, "[ra] sent the unlock of %u earned offline: %s (%zu waiting)\n", w.id,
                     body.find("\"Success\":true") != std::string::npos ? "accepted" : "refused",
                     owed.size());
        sendOwed();
    });
    rc_api_destroy_request(&req);
}

void RC_CCONV serverCall(const rc_api_request_t* r, rc_client_server_callback_t callback,
                         void* callbackData, rc_client_t*) {
    const std::string form = r->post_data ? r->post_data : "";
    const std::string api = param(form, "r");
    send(*r, [callback, callbackData, form, api](int status, const std::string& body) {
        std::string substitute;
        rc_api_server_response_t resp{};
        if (status == RC_API_SERVER_RESPONSE_RETRYABLE_CLIENT_ERROR &&
            offlineAnswer(api, form, &substitute)) {
            resp.body = substitute.c_str();
            resp.body_length = substitute.size();
            resp.http_status_code = 200;
            callback(&resp, callbackData);
            return;
        }
        if (status >= 200 && status < 300) rememberAnswer(api, form, body);
        resp.body = body.c_str();
        resp.body_length = body.size();
        resp.http_status_code = status;
        callback(&resp, callbackData);
    });
}

void RC_CCONV logMessage(const char* message, const rc_client_t*) {
    std::fprintf(stderr, "[ra] rcheevos: %s\n", message);
}

// --- Who is signed in ------------------------------------------------------

// ONE SIGN-IN AT A TIME. rcheevos takes whatever sign-in answer arrives as
// the current one's, so a second sign-in started while a first is in flight
// can be answered with the first person's account. So a new one waits for
// the one in flight (which a sign-out turns into "aborted"), and an answer
// that is not the latest sign-in's is thrown away.
uint32_t gLoginSeq = 0;
std::function<void()> gNextLogin;
// A game started before the sign-in finished: loaded when it does.
std::string gDeferredLoad;
void RC_CCONV onGameLoaded(int result, const char* message, rc_client_t* client, void*);

struct LoginCtx {
    uint32_t seq = 0;
    int account = 0;
    bool password = false;
    std::function<void(bool, const std::string&)> done;
};

std::string sayWhy(int result, const char* message) {
    switch (result) {
        case RC_INVALID_CREDENTIALS:
        case RC_EXPIRED_TOKEN:
        case RC_ACCESS_DENIED:
            return "Wrong username or password";
        case RC_NO_RESPONSE:
            return "RetroAchievements did not answer";
        default:
            break;
    }
    return (message && *message) ? message : "Sign-in failed";
}

void RC_CCONV onLogin(int result, const char* message, rc_client_t* client, void* userdata) {
    std::unique_ptr<LoginCtx> ctx(static_cast<LoginCtx*>(userdata));
    gLoginBusy = false;
    if (ctx->seq != gLoginSeq) {
        // Overtaken (another person, or another try): never kept, and the
        // sign-in waiting behind it starts now.
        if (result == RC_OK) rc_client_logout(client);
        std::fprintf(stderr, "[ra] an earlier sign-in's answer, dropped\n");
        auto next = std::move(gNextLogin);
        gNextLogin = nullptr;
        if (next) next();
        return;
    }
    if (result == RC_OK) {
        const rc_client_user_t* u = rc_client_get_user_info(client);
        gLoggedIn = true;
        if (ctx->password && u && u->username && u->token) {
            if (!accounts::setRaLogin(ctx->account, u->username, u->token))
                std::fprintf(stderr, "[ra] could not keep the sign-in for account %d\n",
                             ctx->account);
            gUser = u->username;
            gToken = u->token;
        }
        std::fprintf(stderr, "[ra] signed in as %s\n", gUser.c_str());
        sendOwed();
        if (!gDeferredLoad.empty()) {
            const std::string hash = std::move(gDeferredLoad);
            gDeferredLoad.clear();
            rc_client_begin_load_game(client, hash.c_str(), onGameLoaded, nullptr);
        }
        if (ctx->done) ctx->done(true, "");
        return;
    }
    gLoggedIn = false;
    if (!gDeferredLoad.empty())
        std::fprintf(stderr, "[ra] not signed in, so no achievements this game\n");
    gDeferredLoad.clear();
    std::fprintf(stderr, "[ra] sign-in refused (%d): %s\n", result, message ? message : "");
    // A STORED TOKEN THE SERVER NOW REFUSES is gone for good (the password
    // changed, or the account went), so it is forgotten and Settings offers
    // to sign in again. One that could not be checked is kept: that is the
    // server or the network, and the next game tries again.
    if (!ctx->password &&
        (result == RC_INVALID_CREDENTIALS || result == RC_EXPIRED_TOKEN ||
         result == RC_ACCESS_DENIED)) {
        accounts::clearRaLogin(ctx->account);
        gUser.clear();
        gToken.clear();
    }
    if (ctx->done) ctx->done(false, sayWhy(result, message));
}

void loginWithToken() {
    if (!gClient || gUser.empty() || gToken.empty() || gLoginBusy || gLoggedIn) return;
    gLoginBusy = true;
    auto* ctx = new LoginCtx;
    ctx->seq = ++gLoginSeq;
    ctx->account = gAccount;
    rc_client_begin_login_with_token(gClient, gUser.c_str(), gToken.c_str(), onLogin, ctx);
}

// Stops whatever sign-in there is, and runs `next` once rcheevos has let go of
// it: now when nothing is in flight, otherwise when the in-flight one answers.
void afterSignOut(std::function<void()> next) {
    if (gClient && (gLoggedIn || gLoginBusy)) rc_client_logout(gClient);
    gLoggedIn = false;
    if (gLoginBusy) {
        ++gLoginSeq;   // the answer in flight is no longer anybody's
        gNextLogin = std::move(next);
        return;
    }
    gNextLogin = nullptr;
    if (next) next();
}

// --- The game being played -------------------------------------------------

std::atomic<bool> gGameActive{false};
// CABINETOS_RA_PROBE=1: lay the game's memory out as for RetroAchievements,
// with nobody signed in and nothing sent, and log what was found two seconds
// in. For checking every core exposes its memory without an account.
bool gProbe = false;
uint64_t gProbeFrames = 0;
bool gPs2 = false;
uint32_t gConsole = 0;
std::string gHash;
rc_libretro_memory_regions_t gRegions{};
// Held for the length of a PS2 frame's check, so endGame can wait out one
// that is running on PCSX2's thread before the game goes.
std::mutex gFrameMutex;


uint32_t RC_CCONV readMemory(uint32_t address, uint8_t* buffer, uint32_t n, rc_client_t*) {
    return rc_libretro_memory_read(&gRegions, address, buffer, n);
}

void RC_CCONV coreMemoryInfo(uint32_t id, rc_libretro_core_memory_info_t* info) {
    size_t n = 0;
    info->data = cab::Core::shared().memoryPointer(id, &n);
    info->size = n;
}

// The libretro core's memory, as RetroArch hands it to rc_libretro: the map
// the core gave in retro_load_game if any, else retro_get_memory_data's
// system RAM laid over the console's known regions. Some cores fill theirs in
// only once the game is running, so frame() asks again until it has some.
unsigned gMapGeneration = 0;

bool mapLibretroMemory() {
    gMapGeneration = cab::Core::shared().memoryMapGeneration();
    rc_libretro_memory_destroy(&gRegions);
    std::memset(&gRegions, 0, sizeof gRegions);
    const bool ok = rc_libretro_memory_init(&gRegions, cab::Core::shared().memoryMap(),
                                            coreMemoryInfo, gConsole) != 0;
    return ok && gRegions.total_size > 0;
}

// PCSX2's: the EE's main RAM then its scratchpad, which is how
// RetroAchievements numbers a PS2's addresses (0x0 and 0x2000000). Null until
// the machine is running; the frame callback asks until it is.
bool mapPs2Memory() {
    size_t mainSize = 0, scratchSize = 0;
    uint8_t* main = ps2::memory(0, &mainSize);
    uint8_t* scratch = ps2::memory(1, &scratchSize);
    if (!main || mainSize == 0) return false;
    std::memset(&gRegions, 0, sizeof gRegions);
    gRegions.data[0] = main;
    gRegions.size[0] = mainSize;
    gRegions.count = 1;
    gRegions.total_size = mainSize;
    if (scratch && scratchSize) {
        gRegions.data[1] = scratch;
        gRegions.size[1] = scratchSize;
        gRegions.count = 2;
        gRegions.total_size += scratchSize;
    }
    std::fprintf(stderr, "[ra] PS2 memory: %zu + %zu bytes\n", mainSize, scratchSize);
    return true;
}

void probeReport() {
    if (++gProbeFrames != 120) return;
    std::fprintf(stderr, "[ra] probe: %u region(s), %zu bytes\n", gRegions.count,
                 gRegions.total_size);
    for (uint32_t i = 0; i < gRegions.count; ++i) {
        uint32_t nonzero = 0;
        if (gRegions.data[i])
            for (size_t k = 0; k < gRegions.size[i]; ++k) nonzero += gRegions.data[i][k] != 0;
        std::fprintf(stderr, "[ra] probe:   %u: %zu bytes, %s, %u non-zero\n", i,
                     gRegions.size[i], gRegions.data[i] ? "mapped" : "null filler", nonzero);
    }
}

void checkFrame() {
    if (gProbe) { probeReport(); return; }
    rc_client_do_frame(gClient);
}

// PCSX2's CPU thread, once per emulated frame, between frames: where PCSX2's
// own achievements code makes the same check.
void ps2Frame(void*) {
    std::lock_guard<std::mutex> lk(gFrameMutex);
    if (!gGameActive.load()) return;
    if (gRegions.total_size == 0 && !mapPs2Memory()) return;
    checkFrame();
}

// --- Pop-ups ---------------------------------------------------------------

std::mutex gPopupMutex;
std::deque<Popup> gPopups;

std::string str(const char* s) { return s ? s : ""; }

// rc_client's own number for the notices above (RC_CLIENT_ACHIEVEMENT_WARNING_ID,
// private to rc_client.c).
constexpr uint32_t kWarningId = 101000001;

void RC_CCONV onEvent(const rc_client_event_t* e, rc_client_t* client) {
    switch (e->type) {
        case RC_CLIENT_EVENT_ACHIEVEMENT_TRIGGERED: {
            const rc_client_achievement_t* a = e->achievement;
            // RETROACHIEVEMENTS' OWN NOTICE, NOT AN ACHIEVEMENT. A client it
            // has not registered gets an entry numbered from 101000001 that
            // "unlocks" at once to say hardcore is unavailable here. This
            // console is softcore only, so it is logged, not shown.
            if (a->id >= kWarningId) {
                std::fprintf(stderr, "[ra] server notice: %s\n", str(a->title).c_str());
                break;
            }
            Popup p;
            p.title = str(a->title);
            p.detail = str(a->description);
            p.points = a->points;
            p.badgeUrl = str(a->badge_url);
            std::fprintf(stderr, "[ra] unlocked %u \"%s\" (%u points)\n", a->id,
                         p.title.c_str(), a->points);
            std::lock_guard<std::mutex> lk(gPopupMutex);
            gPopups.push_back(std::move(p));
            break;
        }
        case RC_CLIENT_EVENT_GAME_COMPLETED: {
            const rc_client_game_t* g = rc_client_get_game_info(client);
            Popup p;
            p.complete = true;
            p.title = g ? str(g->title) : "";
            char url[512] = {0};
            if (g && rc_client_game_get_image_url(g, url, sizeof url) == RC_OK) p.badgeUrl = url;
            std::fprintf(stderr, "[ra] every achievement unlocked in \"%s\"\n", p.title.c_str());
            std::lock_guard<std::mutex> lk(gPopupMutex);
            gPopups.push_back(std::move(p));
            break;
        }
        case RC_CLIENT_EVENT_SERVER_ERROR:
            std::fprintf(stderr, "[ra] server error from %s: %s\n",
                         e->server_error ? str(e->server_error->api).c_str() : "?",
                         e->server_error ? str(e->server_error->error_message).c_str() : "");
            break;
        case RC_CLIENT_EVENT_DISCONNECTED:
            std::fprintf(stderr, "[ra] unlocks waiting for the network\n");
            break;
        case RC_CLIENT_EVENT_RECONNECTED:
            std::fprintf(stderr, "[ra] waiting unlocks sent\n");
            break;
        default:
            // Progress counters, challenge icons and leaderboards are not
            // shown: the unlock pop-up is the one thing over a game (decided
            // with MMagTech, 2026-10-06). Softcore has no leaderboards anyway.
            break;
    }
}

void RC_CCONV onGameLoaded(int result, const char* message, rc_client_t* client, void*) {
    if (result != RC_OK) {
        // Not a fault anybody is shown: a game RetroAchievements has no set
        // for is most of them on some platforms.
        std::fprintf(stderr, "[ra] no achievements for this game (%d): %s\n", result,
                     message ? message : "");
        return;
    }
    rc_client_user_game_summary_t s{};
    rc_client_get_user_game_summary(client, &s);
    const rc_client_game_t* g = rc_client_get_game_info(client);
    std::fprintf(stderr, "[ra] \"%s\" (game %u): %u of %u unlocked, %u unsupported\n",
                 g ? str(g->title).c_str() : "?", g ? g->id : 0, s.num_unlocked_achievements,
                 s.num_core_achievements, s.num_unsupported_achievements);
}

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

// Unlocked first, then locked, each keeping the set's own order.
void order(GameList& l) {
    std::stable_partition(l.items.begin(), l.items.end(),
                          [](const Achievement& a) { return a.unlocked; });
    count(l);
}

}  // namespace

// --- The client ---------------------------------------------------------------

void init(const std::string& consoleVersion) {
    if (gClient) return;
    gClient = rc_client_create(readMemory, serverCall);
    if (!gClient) {
        std::fprintf(stderr, "[ra] rcheevos would not start; no achievements this session\n");
        return;
    }
    // SOFTCORE. rc_client starts in hardcore; this console never uses it.
    rc_client_set_hardcore_enabled(gClient, 0);
    rc_client_set_unofficial_enabled(gClient, 0);
    rc_client_set_encore_mode_enabled(gClient, 0);
    // MEMORY IS READ ONLY INSIDE do_frame, never from a network answer on the
    // frame thread: for PS2 do_frame runs on PCSX2's thread, between frames,
    // and that is the only time its memory holds still.
    rc_client_set_allow_background_memory_reads(gClient, 0);
    rc_client_set_event_handler(gClient, onEvent);
    rc_client_enable_logging(gClient, RC_CLIENT_LOG_LEVEL_WARN, logMessage);

    char clause[64] = {0};
    rc_client_get_user_agent_clause(gClient, clause, sizeof clause);
    {
        std::lock_guard<std::mutex> lk(gUaMutex);
        gUaBase = "CabinetOS/" + (consoleVersion.empty() ? std::string("dev") : consoleVersion) +
                  " (Linux)";
        gUaRc = std::string(" ") + clause;
    }
    gStopping = false;
    gWorker = std::thread(workerLoop);
}

void shutdown() {
    if (!gClient) return;
    endGame();
    // NOTHING EARNED IS LOST TO A POWER-OFF. An unlock still waiting to go
    // out is written to the queue on disk, and the one on the wire is cut
    // short (abortTransfer) and comes back as a failure, which queues it too.
    std::deque<Job> left;
    {
        std::lock_guard<std::mutex> lk(gJobMutex);
        gStopping = true;
        left.swap(gJobs);
    }
    gJobCv.notify_all();
    if (gWorker.joinable()) gWorker.join();
    for (const Job& j : left) {
        std::string ignored;
        if (param(j.post, "r") == "awardachievement")
            offlineAnswer("awardachievement", j.post, &ignored);
    }
    pump();
    {
        std::lock_guard<std::mutex> lk(gDoneMutex);
        gDone.clear();
    }
    rc_client_destroy(gClient);
    gClient = nullptr;
}

void pump() {
    std::deque<std::function<void()>> ready;
    {
        std::lock_guard<std::mutex> lk(gDoneMutex);
        ready.swap(gDone);
    }
    for (auto& f : ready) f();

    // Unlocks earned offline: tried again every half a minute until sent.
    if (gLoggedIn && !gSending &&
        std::chrono::steady_clock::now() - gLastSendTry >= std::chrono::seconds(30)) {
        gLastSendTry = std::chrono::steady_clock::now();
        sendOwed();
    }
}


// --- Signing in -----------------------------------------------------------------

void useAccount(int accountId) {
    if (accountId == gAccount && (gLoggedIn || gLoginBusy) && !gNextLogin) return;
    gDeferredLoad.clear();
    gAccount = accountId;
    gUser.clear();
    gToken.clear();
    if (accountId > 0) accounts::raLogin(accountId, &gUser, &gToken);
    afterSignOut([] { loginWithToken(); });
}

bool gSample = false;
uint64_t gSampleFrames = 0;

GameList sampleList() {
    // Real badges from RetroAchievements' own server, so the pictures are the
    // shape and weight real ones are; the words are made up.
    static const char* kTitles[] = {"First Steps", "Getting Warmer", "Collector",
                                    "No Time to Lose", "Hidden Path", "Perfect Run"};
    GameList l;
    l.known = true;
    for (int i = 0; i < 40; ++i) {
        Achievement a;
        a.id = 1000 + i;
        a.title = std::string(kTitles[i % 6]) + (i >= 6 ? " " + std::to_string(i / 6 + 1) : "");
        a.description = "Reach the end of stage " + std::to_string(i + 1) + " without a continue";
        a.badgeUrl = "https://media.retroachievements.org/Badge/" + std::to_string(250000 + i * 7) + ".png";
        a.lockedBadgeUrl = "https://media.retroachievements.org/Badge/" +
                           std::to_string(250000 + i * 7) + "_lock.png";
        a.points = (i % 4 == 0) ? 25 : (i % 3 == 0) ? 10 : 5;
        a.unlocked = (i % 3 == 0) && i < 36;
        l.items.push_back(std::move(a));
    }
    order(l);
    return l;
}

bool signedIn() { return gSample || (gAccount > 0 && !gUser.empty()); }

std::string username() { return gSample ? std::string("sample") : signedIn() ? gUser : std::string(); }

void useSample() {
    gSample = true;
    std::fprintf(stderr, "[ra] sample mode: a made-up set, nothing sent\n");
}

void signIn(const std::string& user, const std::string& password,
            std::function<void(bool ok, const std::string& why)> done) {
    if (!gClient || gAccount <= 0) {
        if (done) done(false, "Sign-in failed");
        return;
    }
    gUser = user;
    gToken.clear();
    afterSignOut([user, password, done] {
        gLoginBusy = true;
        auto* ctx = new LoginCtx;
        ctx->seq = ++gLoginSeq;
        ctx->account = gAccount;
        ctx->password = true;
        ctx->done = [done](bool ok, const std::string& why) {
            // A refused password leaves nobody signed in, rather than the
            // name that was typed looking as though it worked.
            if (!ok) { gUser.clear(); gToken.clear(); }
            if (done) done(ok, why);
        };
        rc_client_begin_login_with_password(gClient, user.c_str(), password.c_str(), onLogin, ctx);
    });
}

void signOut() {
    gDeferredLoad.clear();
    afterSignOut(nullptr);
    if (gAccount > 0) accounts::clearRaLogin(gAccount);
    gUser.clear();
    gToken.clear();
    std::fprintf(stderr, "[ra] signed out\n");
}

// --- A game ---------------------------------------------------------------------

uint32_t consoleFor(const std::string& s) {
    static const std::map<std::string, uint32_t> kConsole = {
        {"3do", RC_CONSOLE_3DO},
        {"arcade", RC_CONSOLE_ARCADE},
        {"atari2600", RC_CONSOLE_ATARI_2600},
        {"atari7800", RC_CONSOLE_ATARI_7800},
        {"colecovision", RC_CONSOLE_COLECOVISION},
        {"dc", RC_CONSOLE_DREAMCAST},
        {"gamegear", RC_CONSOLE_GAME_GEAR},
        {"gb", RC_CONSOLE_GAMEBOY},
        {"gba", RC_CONSOLE_GAMEBOY_ADVANCE},
        {"gbc", RC_CONSOLE_GAMEBOY_COLOR},
        {"genesis", RC_CONSOLE_MEGA_DRIVE},
        {"jaguar", RC_CONSOLE_ATARI_JAGUAR},
        {"n64", RC_CONSOLE_NINTENDO_64},
        {"nds", RC_CONSOLE_NINTENDO_DS},
        {"neo-geo-pocket-color", RC_CONSOLE_NEOGEO_POCKET},
        {"nes", RC_CONSOLE_NINTENDO},
        {"ngc", RC_CONSOLE_GAMECUBE},
        {"ps2", RC_CONSOLE_PLAYSTATION_2},
        {"psp", RC_CONSOLE_PSP},
        {"psx", RC_CONSOLE_PLAYSTATION},
        {"saturn", RC_CONSOLE_SATURN},
        {"sega32", RC_CONSOLE_SEGA_32X},
        {"segacd", RC_CONSOLE_SEGA_CD},
        {"sms", RC_CONSOLE_MASTER_SYSTEM},
        {"snes", RC_CONSOLE_SUPER_NINTENDO},
        {"tg16", RC_CONSOLE_PC_ENGINE},
        {"turbografx-cd", RC_CONSOLE_PC_ENGINE_CD},
        {"vectrex", RC_CONSOLE_VECTREX},
        {"virtualboy", RC_CONSOLE_VIRTUAL_BOY},
        {"wii", RC_CONSOLE_WII},
    };
    const auto it = kConsole.find(s);
    return it == kConsole.end() ? 0 : it->second;
}

bool coreReadable(const std::string& manifestCore) { return manifestCore != "mame2003_plus"; }

void beginGame(const std::string& raHash, const std::string& platformSlug, bool ps2) {
    endGame();
    gSampleFrames = 0;
    const uint32_t console = consoleFor(platformSlug);
    if (console == 0) return;
    if (!signedIn() && std::getenv("CABINETOS_RA_PROBE")) {
        gProbe = true;
        gProbeFrames = 0;
        gConsole = console;
        gPs2 = ps2;
        std::memset(&gRegions, 0, sizeof gRegions);
        gGameActive = true;
        std::fprintf(stderr, "[ra] probe: %s, console %u, RomM hash %s\n", platformSlug.c_str(),
                     console, raHash.empty() ? "(none)" : raHash.c_str());
        if (ps2) ps2::setFrameCallback(ps2Frame, nullptr);
        return;
    }
    if (!gClient || !signedIn() || gSample) return;
    if (raHash.empty()) {
        std::fprintf(stderr, "[ra] RomM has no RetroAchievements hash for this game\n");
        return;
    }
    const cab::Core& core = cab::Core::shared();
    if (!ps2 && !rc_libretro_is_system_allowed(core.coreName().c_str(), console)) {
        std::fprintf(stderr, "[ra] RetroAchievements does not accept %s for this platform\n",
                     core.coreName().c_str());
        return;
    }
    {
        std::lock_guard<std::mutex> lk(gUaMutex);
        gUaCore = " " + (ps2 ? std::string("PCSX2/") + ps2::version()
                             : core.coreName() + "/" + core.coreVersion());
        std::replace(gUaCore.begin() + 1, gUaCore.end(), ' ', '_');
    }
    gConsole = console;
    gPs2 = ps2;
    gHash = raHash;
    std::memset(&gRegions, 0, sizeof gRegions);
    if (!ps2) mapLibretroMemory();
    gGameActive = true;
    if (gLoggedIn) {
        rc_client_begin_load_game(gClient, raHash.c_str(), onGameLoaded, nullptr);
    } else {
        // Loaded once the sign-in lands: one still in flight, or one that
        // failed for want of a network, tried again here.
        gDeferredLoad = raHash;
        loginWithToken();
    }
    if (ps2 && !ps2::setFrameCallback(ps2Frame, nullptr))
        std::fprintf(stderr, "[ra] this PS2 library has no frame hook; no achievements\n");
    std::fprintf(stderr, "[ra] checking %s for achievements (console %u)\n", raHash.c_str(),
                 console);
}

void endGame() {
    gDeferredLoad.clear();
    if (!gGameActive.exchange(false)) return;
    if (gPs2) {
        ps2::setFrameCallback(nullptr, nullptr);
        // Waits out a check running on PCSX2's thread right now.
        std::lock_guard<std::mutex> lk(gFrameMutex);
    }
    const bool probe = gProbe;
    gProbe = false;
    if (gClient && !probe) rc_client_unload_game(gClient);
    if (!gPs2) rc_libretro_memory_destroy(&gRegions);
    std::memset(&gRegions, 0, sizeof gRegions);
    // The page asks again next time: this session may have unlocked some.
    gLists.erase(listKey(gHash));
    gHash.clear();
    std::lock_guard<std::mutex> lk(gUaMutex);
    gUaCore.clear();
}

void stateLoaded() {
    if (!gClient || !gGameActive.load() || gProbe) return;
    std::lock_guard<std::mutex> lk(gFrameMutex);
    rc_client_reset(gClient);
}

void frame() {
    if ((!gClient && !gProbe) || !gGameActive.load() || gPs2) return;
    if ((gRegions.total_size == 0 ||
         gMapGeneration != cab::Core::shared().memoryMapGeneration()) &&
        !mapLibretroMemory() && !gProbe)
        return;
    checkFrame();
}

// NOT BEFORE THE MEMORY IS THERE. With background reads off, rcheevos switches
// a game's achievements on in the next do_frame OR idle, and checks every
// address as it does; memory not mapped yet reads as nothing and every
// achievement is switched off for the session. So while a game's memory is
// not laid out, idle waits. For PlayStation 2 it holds the frame lock, so
// nothing rcheevos does here overlaps a check on PCSX2's thread.
void idle() {
    if (!gClient) return;
    if (!gGameActive.load() || gProbe) {
        if (!gProbe) rc_client_idle(gClient);
        return;
    }
    std::lock_guard<std::mutex> lk(gFrameMutex);
    if (gRegions.total_size == 0) return;
    rc_client_idle(gClient);
}

// The sample unlock, counted in the frame loop's own calls (main.cpp calls
// frame() or idle() every frame of a game).
void sampleTick() {
    if (!gSample || ++gSampleFrames != 300) return;
    std::fprintf(stderr, "[ra] sample unlock\n");
    const GameList l = sampleList();
    Popup p;
    p.title = l.items[3].title;
    p.detail = l.items[3].description;
    p.points = l.items[3].points;
    p.badgeUrl = l.items[3].badgeUrl;
    std::lock_guard<std::mutex> lk(gPopupMutex);
    gPopups.push_back(std::move(p));
}

bool takePopup(Popup* out) {
    std::lock_guard<std::mutex> lk(gPopupMutex);
    if (gPopups.empty()) return false;
    *out = std::move(gPopups.front());
    gPopups.pop_front();
    return true;
}

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

bool isBadgeUrl(const std::string& key) {
    return key.rfind("https://media.retroachievements.org/", 0) == 0 ||
           key.rfind("https://retroachievements.org/", 0) == 0;
}

std::vector<uint8_t> fetchBytes(const std::string& url) {
    std::string body;
    const int status = request(url, nullptr, nullptr, &body, 20L);
    if (status != 200) return {};
    return std::vector<uint8_t>(body.begin(), body.end());
}

}  // namespace ra
