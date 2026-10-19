#include "remoteplay.h"

#include <curl/curl.h>
#include <json-c/json.h>

#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>

#include "sunshinelog.h"

namespace remoteplay {

namespace {

// Sunshine's API, on the console only (see the header).
constexpr const char* kApi = "https://localhost:47990";
constexpr const char* kUser = "cabinetos";
constexpr const char* kLoginFile = "/var/lib/cabinetos/remoteplay/web";
constexpr const char* kNamesFile = "/var/lib/cabinetos/remoteplay/names.json";
constexpr const char* kStateFile =
    "/var/lib/cabinetos/remoteplay/.config/sunshine/sunshine_state.json";

std::mutex gM;
std::vector<Device> gPaired;
std::vector<Request> gWaiting;
int gGeneration = 0;

// uuid -> the console's name for it (remoteplay.h, Device). Read once.
std::map<std::string, std::string> gNames;
bool gNamesRead = false;

void readNames() {
    if (gNamesRead) return;
    gNamesRead = true;
    json_object* o = json_object_from_file(kNamesFile);
    if (!o) return;
    json_object_object_foreach(o, k, v) {
        const char* n = json_object_get_string(v);
        if (n) gNames[k] = n;
    }
    json_object_put(o);
}

void writeNames() {
    json_object* o = json_object_new_object();
    for (const auto& [k, v] : gNames) json_object_object_add(o, k.c_str(), json_object_new_string(v.c_str()));
    const std::string tmp = std::string(kNamesFile) + ".new";
    if (json_object_to_file_ext(tmp.c_str(), o, JSON_C_TO_STRING_PRETTY) == 0)
        std::rename(tmp.c_str(), kNamesFile);
    json_object_put(o);
}

std::mutex gRunM;
std::condition_variable gRunCv;
std::thread gThread;
bool gWatching = false;
// later()'s work, under gRunM, and its answers, under gM.
struct Work {
    std::function<bool()> work;
    std::function<void(bool)> then;
};
std::deque<Work> gTodo;
std::vector<std::pair<std::function<void(bool)>, bool>> gDone;

void run(Work& w) {
    const bool ok = w.work();
    if (!w.then) return;
    std::lock_guard<std::mutex> lk(gM);
    gDone.emplace_back(std::move(w.then), ok);
}

std::string login() {
    std::ifstream f(kLoginFile);
    std::string pass;
    std::getline(f, pass);
    return pass;
}

size_t append(char* p, size_t size, size_t n, void* out) {
    static_cast<std::string*>(out)->append(p, size * n);
    return size * n;
}

// One call. `body` empty is a GET. The JSON answer, or null.
json_object* call(const char* path, const std::string& body) {
    const std::string pass = login();
    if (pass.empty()) return nullptr;
    CURL* c = curl_easy_init();
    if (!c) return nullptr;
    std::string out;
    const std::string url = std::string(kApi) + path;
    const std::string userpass = std::string(kUser) + ":" + pass;
    curl_slist* headers = nullptr;
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    // SUNSHINE'S CERTIFICATE IS ITS OWN, made on the console, and the call
    // never leaves it: localhost, so there is nobody in between to check for.
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(c, CURLOPT_USERPWD, userpass.c_str());
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 2L);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    if (!body.empty()) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    }
    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    if (rc != CURLE_OK || status != 200) return nullptr;
    return json_tokener_parse(out.c_str());
}

std::string str(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v)) return {};
    const char* s = json_object_get_string(v);
    return s ? s : "";
}

bool ok(json_object* o) {
    json_object* v = nullptr;
    return o && json_object_object_get_ex(o, "status", &v) && json_object_get_boolean(v);
}

// One look at Sunshine: who is paired, who is asking.
void refresh() {
    std::vector<Device> paired;
    std::vector<Request> waiting;
    if (json_object* o = call("/api/clients/list", "")) {
        json_object* list = nullptr;
        if (json_object_object_get_ex(o, "named_certs", &list))
            for (size_t i = 0; i < json_object_array_length(list); ++i) {
                json_object* d = json_object_array_get_idx(list, i);
                paired.push_back({str(d, "uuid"), str(d, "name")});
            }
        // The console's names over Sunshine's (remoteplay.h, Device).
        std::lock_guard<std::mutex> lk(gM);
        readNames();
        for (Device& d : paired)
            if (auto it = gNames.find(d.id); it != gNames.end()) d.name = it->second;
        json_object_put(o);
    }
    if (json_object* o = call("/api/pin", "")) {
        json_object* list = nullptr;
        if (json_object_object_get_ex(o, "pairings", &list))
            for (size_t i = 0; i < json_object_array_length(list); ++i) {
                json_object* r = json_object_array_get_idx(list, i);
                waiting.push_back({str(r, "id")});
            }
        json_object_put(o);
    }
    auto same = [](const auto& a, const auto& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].id != b[i].id) return false;
        return true;
    };
    auto sameNames = [](const std::vector<Device>& a, const std::vector<Device>& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].name != b[i].name) return false;
        return true;
    };
    std::lock_guard<std::mutex> lk(gM);
    if (same(paired, gPaired) && sameNames(paired, gPaired) && same(waiting, gWaiting)) return;
    gPaired = std::move(paired);
    gWaiting = std::move(waiting);
    ++gGeneration;
}

// The log every half second, so a handoff is quick; the API every two; the
// app's own calls as they come. What was asked before off is still done,
// and the lists are emptied here, after the last call, not by watch(false).
void loop() {
    std::unique_lock<std::mutex> lk(gRunM);
    for (int tick = 0;; ++tick) {
        while (!gTodo.empty()) {
            Work w = std::move(gTodo.front());
            gTodo.pop_front();
            lk.unlock();
            run(w);
            lk.lock();
        }
        if (!gWatching) break;
        lk.unlock();
        readLog();
        if (tick % 4 == 0) refresh();
        lk.lock();
        gRunCv.wait_for(lk, std::chrono::milliseconds(500),
                        [] { return !gWatching || !gTodo.empty(); });
    }
    lk.unlock();
    stopReadingLog();
    std::lock_guard<std::mutex> g(gM);
    gPaired.clear();
    gWaiting.clear();
    ++gGeneration;
}

}  // namespace

void watch(bool on, bool fresh) {
    {
        std::lock_guard<std::mutex> lk(gRunM);
        if (gWatching == on) return;
        if (!on) gWatching = false;
    }
    gRunCv.notify_all();
    if (!on) return;
    join();   // the last watch's thread, which may still be ending
    startReadingLog(fresh);
    {
        std::lock_guard<std::mutex> lk(gRunM);
        gWatching = true;
    }
    gThread = std::thread(loop);
}

void join() {
    if (gThread.joinable()) gThread.join();
}

void later(std::function<bool()> work, std::function<void(bool)> then) {
    Work w{std::move(work), std::move(then)};
    {
        std::lock_guard<std::mutex> lk(gRunM);
        if (gWatching) {
            gTodo.push_back(std::move(w));
            gRunCv.notify_all();
            return;
        }
    }
    run(w);
}

void answered() {
    std::vector<std::pair<std::function<void(bool)>, bool>> done;
    {
        std::lock_guard<std::mutex> lk(gM);
        if (gDone.empty()) return;
        done.swap(gDone);
    }
    for (auto& [then, ok] : done) then(ok);
}

bool sessionOpen() {
    const char* run = std::getenv("XDG_RUNTIME_DIR");
    return run && ::access((std::string(run) + "/cabinetos-remoteplay-session").c_str(), F_OK) == 0;
}

bool endSession() {
    // "{}" because call() takes an empty body for a GET; Sunshine reads none.
    json_object* o = call("/api/apps/close", "{}");
    const bool done = ok(o);
    if (o) json_object_put(o);
    std::fprintf(stderr, "[remoteplay] session ended: %s\n",
                 done ? "Sunshine took it" : "Sunshine did not answer");
    return done;
}

std::vector<Device> paired() {
    std::lock_guard<std::mutex> lk(gM);
    return gPaired;
}

std::vector<Request> waiting() {
    std::lock_guard<std::mutex> lk(gM);
    return gWaiting;
}

int generation() {
    std::lock_guard<std::mutex> lk(gM);
    return gGeneration;
}

namespace {

std::string json(std::initializer_list<std::pair<const char*, std::string>> fields) {
    json_object* o = json_object_new_object();
    for (const auto& [k, v] : fields) json_object_object_add(o, k, json_object_new_string(v.c_str()));
    std::string s = json_object_to_json_string_ext(o, JSON_C_TO_STRING_PLAIN);
    json_object_put(o);
    return s;
}

}  // namespace

bool pair(const std::string& requestId, const std::string& digits, const std::string& name,
          std::string* why) {
    json_object* o = call("/api/pin", json({{"pairing_id", requestId}, {"pin", digits}, {"name", name}}));
    const bool done = ok(o);
    if (o) json_object_put(o);
    if (!done && why) *why = o ? "Sunshine said no" : "Sunshine did not answer";
    std::fprintf(stderr, "[remoteplay] pair %s: %s\n", name.c_str(), done ? "taken" : "refused");
    refresh();
    return done;
}

bool decline(const std::string& requestId) {
    // Sunshine takes DELETE /api/pin for this; a POST with an empty PIN is
    // not it. Made by hand, since call() only GETs and POSTs.
    const std::string pass = login();
    CURL* c = curl_easy_init();
    if (!c || pass.empty()) {
        if (c) curl_easy_cleanup(c);
        return false;
    }
    const std::string url = std::string(kApi) + "/api/pin";
    const std::string userpass = std::string(kUser) + ":" + pass;
    const std::string body = json({{"pairing_id", requestId}});
    std::string out;
    curl_slist* headers = curl_slist_append(nullptr, "Content-Type: application/json");
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_CUSTOMREQUEST, "DELETE");
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(c, CURLOPT_SSL_VERIFYHOST, 0L);
    curl_easy_setopt(c, CURLOPT_HTTPAUTH, CURLAUTH_BASIC);
    curl_easy_setopt(c, CURLOPT_USERPWD, userpass.c_str());
    curl_easy_setopt(c, CURLOPT_TIMEOUT, 4L);
    curl_easy_setopt(c, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, append);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &out);
    const bool done = curl_easy_perform(c) == CURLE_OK;
    curl_slist_free_all(headers);
    curl_easy_cleanup(c);
    std::fprintf(stderr, "[remoteplay] declined a request\n");
    refresh();
    return done;
}

bool remove(const std::string& deviceId, std::string* why) {
    json_object* o = call("/api/clients/unpair", json({{"uuid", deviceId}}));
    const bool done = ok(o);
    if (o) json_object_put(o);
    if (!done && why) *why = "Sunshine did not remove it";
    std::fprintf(stderr, "[remoteplay] remove: %s\n", done ? "done" : "refused");
    if (done) {
        std::lock_guard<std::mutex> lk(gM);
        readNames();
        if (gNames.erase(deviceId)) writeNames();
    }
    refresh();
    return done;
}

std::vector<std::string> samePairedDevice(const std::string& deviceId) {
    std::vector<std::string> out;
    json_object* o = json_object_from_file(kStateFile);
    json_object *root = nullptr, *list = nullptr;
    if (!o || !json_object_object_get_ex(o, "root", &root) ||
        !json_object_object_get_ex(root, "named_devices", &list)) {
        if (o) json_object_put(o);
        return out;
    }
    std::string cert;
    for (size_t i = 0; i < json_object_array_length(list); ++i) {
        json_object* d = json_object_array_get_idx(list, i);
        if (str(d, "uuid") == deviceId) cert = str(d, "cert");
    }
    for (size_t i = 0; !cert.empty() && i < json_object_array_length(list); ++i) {
        json_object* d = json_object_array_get_idx(list, i);
        if (str(d, "uuid") != deviceId && str(d, "cert") == cert) out.push_back(str(d, "uuid"));
    }
    json_object_put(o);
    return out;
}

void rename(const std::string& deviceId, const std::string& name) {
    {
        std::lock_guard<std::mutex> lk(gM);
        readNames();
        gNames[deviceId] = name;
        writeNames();
    }
    std::fprintf(stderr, "[remoteplay] named a device\n");
    refresh();
}

std::string nextName() {
    const std::vector<Device> now = paired();
    for (int n = 1;; ++n) {
        const std::string name = "Device " + std::to_string(n);
        bool used = false;
        for (const Device& d : now) used = used || d.name == name;
        if (!used) return name;
    }
}

}  // namespace remoteplay
