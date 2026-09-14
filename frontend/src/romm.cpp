#include "romm.h"

#include <curl/curl.h>
#include <json-c/json.h>

#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

namespace romm {
namespace {

constexpr long kConnectTimeoutSec = 5;
constexpr long kTimeoutSec = 30;

// Read-only, and no more than is needed. Anything that writes is a separate
// request made when something actually needs to write, so a token that leaks
// cannot modify the library.
const char* kScopes[] = {
    "me.read", "platforms.read", "roms.read",
    "assets.read", "roms.user.read", "collections.read",
};

size_t sink(char* p, size_t sz, size_t n, void* user) {
    static_cast<std::string*>(user)->append(p, sz * n);
    return sz * n;
}

size_t byteSink(char* p, size_t sz, size_t n, void* user) {
    auto* v = static_cast<std::vector<uint8_t>*>(user);
    v->insert(v->end(), p, p + sz * n);
    return sz * n;
}

// json-c returns borrowed pointers; these keep the call sites readable and
// make a missing field a default rather than a crash. RomM omits fields
// rather than nulling them in several places, so this is the common path and
// not an error case.
std::string jstr(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !v) return {};
    if (json_object_get_type(v) == json_type_null) return {};
    const char* s = json_object_get_string(v);
    return s ? std::string(s) : std::string();
}

int64_t jint(json_object* o, const char* key) {
    json_object* v = nullptr;
    if (!o || !json_object_object_get_ex(o, key, &v) || !v) return 0;
    if (json_object_get_type(v) == json_type_null) return 0;
    return json_object_get_int64(v);
}

}  // namespace

bool looksLocal(const std::string& host) {
    std::string h = host;
    auto colon = h.find(':');
    if (colon != std::string::npos) h = h.substr(0, colon);
    if (h.find('.') == std::string::npos) return true;         // bare hostname
    if (h.size() > 6 && h.compare(h.size() - 6, 6, ".local") == 0) return true;
    if (h.size() > 4 && h.compare(h.size() - 4, 4, ".lan") == 0) return true;
    if (h.compare(0, 4, "127.") == 0) return true;
    if (h.compare(0, 3, "10.") == 0) return true;
    if (h.compare(0, 8, "192.168.") == 0) return true;
    // 172.16.0.0/12
    if (h.compare(0, 4, "172.") == 0) {
        int second = atoi(h.c_str() + 4);
        if (second >= 16 && second <= 31) return true;
    }
    return false;
}

Client::Client() { curl_global_init(CURL_GLOBAL_DEFAULT); }
Client::~Client() = default;

bool Client::get(const std::string& path, std::string* body, std::string* err) const {
    CURL* c = curl_easy_init();
    if (!c) { if (err) *err = "curl init failed"; return false; }
    const std::string url = base_ + path;
    curl_slist* hdrs = nullptr;
    if (!token_.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + token_).c_str());

    body->clear();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) { if (err) *err = curl_easy_strerror(rc); return false; }
    if (status == 401 || status == 403) {
        if (err) *err = "not authorised — the token is missing, expired or lacks the scope";
        return false;
    }
    if (status >= 400) {
        if (err) *err = "HTTP " + std::to_string(status) + " for " + path;
        return false;
    }
    return true;
}

bool Client::postJson(const std::string& path, const std::string& json,
                      std::string* body, long* status, std::string* err) const {
    CURL* c = curl_easy_init();
    if (!c) { if (err) *err = "curl init failed"; return false; }
    const std::string url = base_ + path;
    curl_slist* hdrs = curl_slist_append(nullptr, "Content-Type: application/json");
    if (!token_.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + token_).c_str());

    body->clear();
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, json.c_str());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, static_cast<long>(json.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, body);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(c);
    long code = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
    if (status) *status = code;
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) { if (err) *err = curl_easy_strerror(rc); return false; }
    return true;
}

bool Client::probe(const std::string& candidate) {
    const std::string saved = base_;
    base_ = candidate;
    std::string body;
    if (!get("/api/heartbeat", &body, nullptr)) { base_ = saved; return false; }

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { base_ = saved; return false; }
    json_object* sys = nullptr;
    if (json_object_object_get_ex(root, "SYSTEM", &sys))
        serverVersion_ = jstr(sys, "VERSION");
    json_object_put(root);
    return true;
}

bool Client::setAddress(const std::string& address, std::string* err) {
    std::string a = address;
    while (!a.empty() && (a.back() == '/' || a.back() == ' ')) a.pop_back();
    if (a.empty()) { if (err) *err = "no address given"; return false; }

    // An explicit scheme is an instruction, not a hint. Honour it and do not
    // second-guess by trying the other one.
    if (a.compare(0, 7, "http://") == 0 || a.compare(0, 8, "https://") == 0) {
        if (probe(a)) return true;
        if (err) *err = "nothing answered at " + a;
        return false;
    }

    // No scheme. Try the likelier one first — http for a LAN address, because
    // that is what a self-hosted RomM usually speaks, and refusing it is the
    // exact bug Phase 4 exists to not have.
    const bool local = looksLocal(a);
    const std::string first  = (local ? "http://" : "https://") + a;
    const std::string second = (local ? "https://" : "http://") + a;
    if (probe(first)) return true;
    if (probe(second)) return true;
    if (err) *err = "nothing answered at " + a + " over http or https";
    return false;
}

bool Client::loadToken(const std::string& path) {
    FILE* f = fopen(path.c_str(), "rb");
    if (!f) return false;
    std::string body;
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    fclose(f);

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) return false;
    std::string t = jstr(root, "access_token");
    json_object_put(root);
    if (t.empty()) return false;
    token_ = std::move(t);
    return true;
}

bool Client::saveToken(const std::string& path) const {
    if (token_.empty()) return false;
    // 0600 from the moment it exists. Creating it readable and chmod-ing after
    // leaves a window where the credential is world-readable.
    int fd = open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, S_IRUSR | S_IWUSR);
    if (fd < 0) return false;
    json_object* root = json_object_new_object();
    json_object_object_add(root, "access_token", json_object_new_string(token_.c_str()));
    json_object_object_add(root, "base_url", json_object_new_string(base_.c_str()));
    const char* out = json_object_to_json_string(root);
    const size_t len = strlen(out);
    const bool ok = write(fd, out, len) == static_cast<ssize_t>(len);
    json_object_put(root);
    close(fd);
    return ok;
}

bool Client::beginPairing(Pairing* out, std::string* err) {
    json_object* req = json_object_new_object();
    json_object_object_add(req, "client_device_identifier",
                           json_object_new_string("cabinetos"));
    json_object_object_add(req, "name", json_object_new_string("CabinetOS"));
    json_object_object_add(req, "client", json_object_new_string("cabinetos"));
    json_object_object_add(req, "platform", json_object_new_string("linux"));
    json_object* scopes = json_object_new_array();
    for (const char* s : kScopes) json_object_array_add(scopes, json_object_new_string(s));
    json_object_object_add(req, "requested_scopes", scopes);
    const std::string payload = json_object_to_json_string(req);
    json_object_put(req);

    std::string body;
    long status = 0;
    if (!postJson("/api/auth/device/init", payload, &body, &status, err)) return false;
    if (status >= 400) {
        if (err) *err = "HTTP " + std::to_string(status) + " starting pairing";
        return false;
    }

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "pairing response was not JSON"; return false; }
    out->userCode = jstr(root, "user_code");
    out->deviceCode = jstr(root, "device_code");
    out->expiresIn = static_cast<int>(jint(root, "expires_in"));
    out->intervalSeconds = static_cast<int>(jint(root, "interval"));
    if (out->intervalSeconds <= 0) out->intervalSeconds = 5;
    out->verificationUrl = base_ + jstr(root, "verification_path_complete");
    json_object_put(root);

    if (out->userCode.empty() || out->deviceCode.empty()) {
        if (err) *err = "pairing response was missing its codes";
        return false;
    }
    return true;
}

int Client::pollPairing(const Pairing& p, std::string* err) {
    json_object* req = json_object_new_object();
    json_object_object_add(req, "device_code", json_object_new_string(p.deviceCode.c_str()));
    const std::string payload = json_object_to_json_string(req);
    json_object_put(req);

    std::string body;
    long status = 0;
    if (!postJson("/api/auth/device/token", payload, &body, &status, err)) return -1;

    // Still waiting for the person to approve. Not an error, and the common
    // case for as long as it takes them to pick up a phone.
    if (status == 400 || status == 428) return 0;
    if (status == 404 || status == 410) {
        if (err) *err = "the pairing expired before it was approved";
        return -1;
    }
    if (status >= 400) {
        if (err) *err = "HTTP " + std::to_string(status) + " while pairing";
        return -1;
    }

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "token response was not JSON"; return -1; }
    std::string t = jstr(root, "access_token");
    json_object_put(root);
    if (t.empty()) return 0;   // approved-but-empty should not happen; treat as pending
    token_ = std::move(t);
    return 1;
}

bool Client::fetchPlatforms(std::vector<Platform>* out, std::string* err) {
    std::string body;
    if (!get("/api/platforms", &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        if (err) *err = "platforms response was not an array";
        return false;
    }
    const size_t n = json_object_array_length(root);
    out->clear();
    out->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        json_object* o = json_object_array_get_idx(root, i);
        Platform p;
        p.id = static_cast<int>(jint(o, "id"));
        p.name = jstr(o, "name");
        p.slug = jstr(o, "slug");
        p.fsSlug = jstr(o, "fs_slug");
        p.romCount = static_cast<int>(jint(o, "rom_count"));
        if (p.id != 0) out->push_back(std::move(p));
    }
    json_object_put(root);
    return true;
}

bool Client::fetchGames(int platformId, std::vector<Game>* out, std::string* err) {
    out->clear();
    // RomM caps a page. A library of thousands arrives truncated unless this
    // pages, and truncated-but-successful is the worst possible failure: the
    // UI would simply show fewer games and say nothing.
    constexpr int kPage = 500;
    int offset = 0;
    for (;;) {
        std::string path = "/api/roms?limit=" + std::to_string(kPage) +
                           "&offset=" + std::to_string(offset);
        if (platformId > 0) path += "&platform_ids=" + std::to_string(platformId);

        std::string body;
        if (!get(path, &body, err)) return false;
        json_object* root = json_tokener_parse(body.c_str());
        if (!root) { if (err) *err = "games response was not JSON"; return false; }

        json_object* items = nullptr;
        if (!json_object_object_get_ex(root, "items", &items) ||
            json_object_get_type(items) != json_type_array) {
            json_object_put(root);
            if (err) *err = "games response had no items array";
            return false;
        }

        const size_t n = json_object_array_length(items);
        for (size_t i = 0; i < n; ++i) {
            json_object* o = json_object_array_get_idx(items, i);
            Game g;
            g.id = static_cast<int>(jint(o, "id"));
            g.platformId = static_cast<int>(jint(o, "platform_id"));
            g.name = jstr(o, "name");
            g.fsName = jstr(o, "fs_name");
            g.sizeBytes = jint(o, "fs_size_bytes");
            g.coverPath = jstr(o, "path_cover_small");
            if (g.coverPath.empty()) g.coverPath = jstr(o, "path_cover_large");
            if (g.id != 0) out->push_back(std::move(g));
        }
        json_object_put(root);

        if (n < static_cast<size_t>(kPage)) break;
        offset += kPage;
    }
    return true;
}

std::vector<uint8_t> Client::fetchBytes(const std::string& path) const {
    std::vector<uint8_t> data;
    CURL* c = curl_easy_init();
    if (!c) return data;
    const std::string url = (path.compare(0, 4, "http") == 0) ? path : base_ + "/" + path;
    curl_slist* hdrs = nullptr;
    if (!token_.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + token_).c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, byteSink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &data);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK || status >= 400) data.clear();
    return data;
}

}  // namespace romm
