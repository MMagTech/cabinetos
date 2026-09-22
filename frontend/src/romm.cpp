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

// What the console asks for, and where the line is drawn.
//
// READ: everything needed to show a library and start a game — the library
// itself, artwork, collections, and firmware. firmware.read is not optional:
// about half the systems here cannot start a game without a BIOS, and without
// it they fail pointing at a file the person has no way to supply.
//
// WRITE: only the person's own play data. assets.write uploads saves, memory
// cards and save states, which is the whole point of the server holding them —
// a console that could only download would lose progress the moment it
// reclaimed a game. roms.user.write is favourites and play state.
//
// NOT ASKED FOR, deliberately: roms.write, platforms.write and firmware.write
// (the console must never alter the library it is reading), users.*, tasks.run
// and logs.read. The line is that this can change YOUR data and never THE
// library.
//
// Two rounds of under-scoping got here. "Ask again when something needs it" is
// right for genuinely occasional access and wrong for a requirement, and each
// mistake costs a re-pairing that a person has to walk across a room for.
const char* kScopes[] = {
    "me.read", "platforms.read", "roms.read", "assets.read",
    "roms.user.read", "collections.read", "firmware.read",
    "assets.write", "roms.user.write",
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


// RomM hands back cover paths with a cache-busting query appended:
//
//   /assets/.../cover/small.png?ts=2026-02-08 21:13:30
//                                              ^ a space, inside a URL
//
// curl rejects that outright — "Malformed input to a URL function" — so
// without this EVERY cover fails, fetchBytes returns empty, ImageCache reads
// empty as failure, and the library renders with no art and no error anywhere.
// A silent total failure is worth more care than a loud partial one.
//
// Encoding conservatively: anything already legal in a URL is left alone,
// including a % that begins a valid escape, so a path that is already encoded
// is not encoded twice. Everything else — the space, and any non-ASCII byte —
// becomes %XX.
std::string encodeUrl(const std::string& in) {
    static const char* kHex = "0123456789ABCDEF";
    auto isHex = [](unsigned char c) {
        return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
    };
    std::string out;
    out.reserve(in.size() + 16);
    for (size_t i = 0; i < in.size(); ++i) {
        const unsigned char c = static_cast<unsigned char>(in[i]);
        const bool unreserved = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                                (c >= '0' && c <= '9') ||
                                c == '-' || c == '.' || c == '_' || c == '~';
        // Delimiters that are meaningful where they sit and must survive.
        const bool delimiter = strchr("/?=&:@+$,;#!*'()[]", c) != nullptr;
        const bool liveEscape = c == '%' && i + 2 < in.size() &&
                                isHex(static_cast<unsigned char>(in[i + 1])) &&
                                isHex(static_cast<unsigned char>(in[i + 2]));
        if (unreserved || delimiter || liveEscape) {
            out.push_back(static_cast<char>(c));
        } else {
            out.push_back('%');
            out.push_back(kHex[c >> 4]);
            out.push_back(kHex[c & 0x0F]);
        }
    }
    return out;
}

bool parseGame(json_object* o, Game* g) {
    g->id = static_cast<int>(jint(o, "id"));
    if (g->id == 0) return false;
    g->platformId = static_cast<int>(jint(o, "platform_id"));
    g->platformSlug = jstr(o, "platform_slug");
    g->platformFsSlug = jstr(o, "platform_fs_slug");
    g->platformName = jstr(o, "platform_display_name");
    if (g->platformName.empty()) g->platformName = jstr(o, "platform_name");
    g->name = jstr(o, "name");
    g->fsName = jstr(o, "fs_name");
    g->sizeBytes = jint(o, "fs_size_bytes");
    // BOTH SIZES, because they are for different jobs. See romm.h: `small` is
    // a 162x216 thumbnail and `big` is 810x1080. Either may be absent — a game
    // the server never matched has neither — so each falls back to the other
    // and a caller can use one field without checking two.
    g->coverPath = jstr(o, "path_cover_small");
    g->coverLargePath = jstr(o, "path_cover_large");
    if (g->coverPath.empty()) g->coverPath = g->coverLargePath;
    if (g->coverLargePath.empty()) g->coverLargePath = g->coverPath;
    return true;
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

    // THE DIRECTORY HAS TO BE MADE, AND NOT MAKING IT COST A PAIRING ON THE
    // FIRST CONSOLE EVER INSTALLED. `~/.config/cabinetos/` does not exist on a
    // machine nobody has configured, which is precisely the machine that is
    // pairing. The open below then fails with ENOENT, the pairing that the
    // person has just approved in a browser is thrown away, and the console
    // goes on showing the stand-in library. Found 2026-09-19, on the A9 Pro,
    // ten minutes after it first booted; the test VM never showed it because
    // that directory had been created there by hand weeks earlier.
    //
    // 0700, because what goes in it is a credential.
    if (const size_t slash = path.rfind('/'); slash != std::string::npos && slash > 0) {
        const std::string dir = path.substr(0, slash);
        std::string built;
        size_t at = 0;
        while (at < dir.size()) {
            const size_t next = dir.find('/', at + 1);
            built = dir.substr(0, next == std::string::npos ? dir.size() : next);
            if (!built.empty()) ::mkdir(built.c_str(), S_IRWXU);
            if (next == std::string::npos) break;
            at = next;
        }
    }

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

bool Client::fetchCurrentUser(User* out, std::string* err) {
    std::string body;
    if (!get("/api/users/me", &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_object) {
        if (root) json_object_put(root);
        if (err) *err = "users/me response was not an object";
        return false;
    }
    // Hand-written and deliberately partial, the same rule as every other
    // response here: decode the fields used and ignore the rest, because
    // generating from openapi.json is what makes a client break across RomM
    // releases. This one carries an email, a role, an avatar and a page of UI
    // preferences, and none of them are this console's business.
    out->id = static_cast<int>(jint(root, "id"));
    out->username = jstr(root, "username");
    // `avatar_path` is used only as a FLAG — it says whether this person has a
    // picture at all. Its value is a path into RomM's asset tree that nothing
    // serves; see the note on User::avatarPath. The endpoint is built from the
    // id instead, which is the only thing that answers.
    if (!jstr(root, "avatar_path").empty() && jint(root, "id") > 0)
        out->avatarPath = "/api/users/" + std::to_string(jint(root, "id")) + "/avatar";
    json_object_put(root);
    if (out->id <= 0) {
        if (err) *err = "users/me carried no id";
        return false;
    }
    return true;
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

bool Client::fetchCollections(std::vector<Collection>* out, std::string* err) {
    std::string body;
    if (!get("/api/collections", &body, err)) return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        if (err) *err = "collections response was not an array";
        return false;
    }
    const size_t n = json_object_array_length(root);
    out->clear();
    out->reserve(n);
    for (size_t i = 0; i < n; ++i) {
        json_object* o = json_object_array_get_idx(root, i);
        Collection c;
        c.id = static_cast<int>(jint(o, "id"));
        c.name = jstr(o, "name");
        c.romCount = static_cast<int>(jint(o, "rom_count"));
        json_object* fav = nullptr;
        if (json_object_object_get_ex(o, "is_favorite", &fav) && fav)
            c.isFavorite = json_object_get_boolean(fav);

        // The membership. This is the whole reason a collection is cheap: the
        // ids come down with the list, so opening one is a lookup rather than
        // another request.
        json_object* ids = nullptr;
        if (json_object_object_get_ex(o, "rom_ids", &ids) &&
            json_object_get_type(ids) == json_type_array) {
            const size_t m = json_object_array_length(ids);
            c.romIds.reserve(m);
            for (size_t j = 0; j < m; ++j) {
                const int id = static_cast<int>(
                    json_object_get_int64(json_object_array_get_idx(ids, j)));
                if (id != 0) c.romIds.push_back(id);
            }
        }

        // `path_cover_small` is null for a collection the person never gave
        // art to, which is the common case; `path_covers_small` is RomM's
        // mosaic of member covers and its first entry is a real cover from a
        // real game. Either way the tile gets a picture rather than a hole.
        c.coverPath = jstr(o, "path_cover_small");
        if (c.coverPath.empty()) {
            json_object* covers = nullptr;
            if (json_object_object_get_ex(o, "path_covers_small", &covers) &&
                json_object_get_type(covers) == json_type_array &&
                json_object_array_length(covers) > 0) {
                const char* s =
                    json_object_get_string(json_object_array_get_idx(covers, 0));
                if (s) c.coverPath = s;
            }
        }
        if (c.id != 0) out->push_back(std::move(c));
    }
    json_object_put(root);
    return true;
}

bool Client::fetchGames(int platformId, std::vector<Game>* out, std::string* err,
                        const std::function<void(int)>& onPage) {
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
            Game g;
            if (parseGame(json_object_array_get_idx(items, i), &g))
                out->push_back(std::move(g));
        }
        json_object_put(root);

        // Said after each page rather than at the end: a library of sixteen
        // hundred arrives in four of these and the screen waiting on it has
        // to show something moving in between.
        if (onPage) onPage(static_cast<int>(out->size()));
        if (n < static_cast<size_t>(kPage)) break;
        offset += kPage;
    }
    return true;
}

namespace {
// One multipart/form-data body with a single file part. Hand-built because the
// whole client is: no HTTP library, and this is thirty lines.
std::string multipartBody(const std::string& boundary, const char* partName,
                          const std::string& fileName, const std::vector<uint8_t>& data) {
    std::string b;
    b.reserve(data.size() + fileName.size() + 256);
    b += "--" + boundary + "\r\n";
    b += "Content-Disposition: form-data; name=\"";
    b += partName;
    b += "\"; filename=\"" + fileName + "\"\r\n";
    b += "Content-Type: application/octet-stream\r\n\r\n";
    b.append(reinterpret_cast<const char*>(data.data()), data.size());
    b += "\r\n--" + boundary + "--\r\n";
    return b;
}

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
        a.fileName = jstr(o, "file_name");
        a.sizeBytes = jint(o, "file_size_bytes");
        a.emulator = jstr(o, "emulator");
        a.updatedAt = jstr(o, "updated_at");
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
                         const std::vector<uint8_t>& data, std::string* err) const {
    // Deliberately NOT overwrite: a history of states is the point of states.
    const std::string path = "/api/states?rom_id=" + std::to_string(romId) +
                             "&emulator=" + emulator;
    return postMultipart(path, "stateFile", fileName, data, err);
}

bool Client::postMultipart(const std::string& path, const char* partName,
                           const std::string& fileName, const std::vector<uint8_t>& data,
                           std::string* err) const {
    CURL* c = curl_easy_init();
    if (!c) { if (err) *err = "curl init failed"; return false; }

    const std::string boundary = "CabinetOSBoundary7f3a91c4";
    const std::string body = multipartBody(boundary, partName, fileName, data);

    curl_slist* hdrs = curl_slist_append(
        nullptr, ("Content-Type: multipart/form-data; boundary=" + boundary).c_str());
    if (!token_.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + token_).c_str());

    std::string reply;
    const std::string url = encodeUrl(base_ + path);
    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_POST, 1L);
    curl_easy_setopt(c, CURLOPT_POSTFIELDS, body.data());
    curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE_LARGE,
                     static_cast<curl_off_t>(body.size()));
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, sink);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &reply);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    curl_easy_setopt(c, CURLOPT_TIMEOUT, kTimeoutSec);
    curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (rc != CURLE_OK) { if (err) *err = curl_easy_strerror(rc); return false; }
    if (status >= 400) {
        if (err) *err = "HTTP " + std::to_string(status) + ": " + reply.substr(0, 180);
        return false;
    }
    return true;
}

bool Client::fetchFirmware(int platformId, std::vector<Firmware>* out, std::string* err) {
    out->clear();
    std::string body;
    if (!get("/api/firmware?platform_id=" + std::to_string(platformId), &body, err))
        return false;
    json_object* root = json_tokener_parse(body.c_str());
    if (!root || json_object_get_type(root) != json_type_array) {
        if (root) json_object_put(root);
        if (err) *err = "firmware response was not an array";
        return false;
    }
    const size_t n = json_object_array_length(root);
    for (size_t i = 0; i < n; ++i) {
        json_object* o = json_object_array_get_idx(root, i);
        Firmware f;
        f.id = static_cast<int>(jint(o, "id"));
        f.fileName = jstr(o, "file_name");
        f.sizeBytes = jint(o, "file_size_bytes");
        f.md5 = jstr(o, "md5_hash");
        json_object* v = nullptr;
        if (json_object_object_get_ex(o, "is_verified", &v))
            f.verified = json_object_get_boolean(v);
        if (f.id != 0 && !f.fileName.empty()) out->push_back(std::move(f));
    }
    json_object_put(root);
    return true;
}

bool Client::fetchFavorites(int limit, std::vector<Game>* out, std::string* err) {
    return fetchFiltered("&favorite=true", limit, out, err);
}

bool Client::fetchRecent(int limit, std::vector<Game>* out, std::string* err) {
    return fetchFiltered("&order_by=last_played&order_dir=desc&last_played=true",
                         limit, out, err);
}

bool Client::fetchFiltered(const char* filter, int limit, std::vector<Game>* out,
                           std::string* err) {
    out->clear();
    const std::string path = "/api/roms?limit=" + std::to_string(limit) + filter;
    std::string body;
    if (!get(path, &body, err)) return false;

    json_object* root = json_tokener_parse(body.c_str());
    if (!root) { if (err) *err = "response was not JSON"; return false; }
    json_object* items = nullptr;
    if (!json_object_object_get_ex(root, "items", &items) ||
        json_object_get_type(items) != json_type_array) {
        json_object_put(root);
        if (err) *err = "response had no items array";
        return false;
    }
    const size_t n = json_object_array_length(items);
    for (size_t i = 0; i < n; ++i) {
        Game g;
        if (parseGame(json_object_array_get_idx(items, i), &g))
            out->push_back(std::move(g));
    }
    json_object_put(root);
    return true;
}

namespace {
struct FileSink {
    FILE* f = nullptr;
    const Client::ProgressFn* progress = nullptr;
    int64_t got = 0;
    int64_t total = 0;
    bool aborted = false;
};

size_t writeToFile(char* p, size_t sz, size_t n, void* user) {
    auto* s = static_cast<FileSink*>(user);
    const size_t bytes = sz * n;
    if (std::fwrite(p, 1, bytes, s->f) != bytes) return 0;   // short write aborts curl
    s->got += static_cast<int64_t>(bytes);
    return bytes;
}

int reportProgress(void* user, curl_off_t dlTotal, curl_off_t dlNow, curl_off_t, curl_off_t) {
    auto* s = static_cast<FileSink*>(user);
    s->total = dlTotal;
    if (s->progress && *s->progress) {
        // A non-zero return aborts the transfer, which is what makes cancelling
        // a three gigabyte download possible at all.
        if (!(*s->progress)(dlNow, dlTotal)) { s->aborted = true; return 1; }
    }
    return 0;
}
}  // namespace

bool Client::fetchToFile(const std::string& path, const std::string& destPath,
                         const ProgressFn& onProgress, std::string* err) const {
    CURL* c = curl_easy_init();
    if (!c) { if (err) *err = "curl init failed"; return false; }

    // Written to a neighbouring .part and renamed only on success, so an
    // interrupted download can never be mistaken for a complete ROM by
    // whatever looks in this directory next.
    const std::string partPath = destPath + ".part";
    FileSink sink;
    sink.f = std::fopen(partPath.c_str(), "wb");
    if (!sink.f) {
        if (err) *err = "cannot write " + partPath;
        curl_easy_cleanup(c);
        return false;
    }
    sink.progress = &onProgress;

    const std::string url = encodeUrl(base_ + path);
    curl_slist* hdrs = nullptr;
    if (!token_.empty())
        hdrs = curl_slist_append(hdrs, ("Authorization: Bearer " + token_).c_str());

    curl_easy_setopt(c, CURLOPT_URL, url.c_str());
    curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, writeToFile);
    curl_easy_setopt(c, CURLOPT_WRITEDATA, &sink);
    curl_easy_setopt(c, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(c, CURLOPT_XFERINFOFUNCTION, reportProgress);
    curl_easy_setopt(c, CURLOPT_XFERINFODATA, &sink);
    curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, kConnectTimeoutSec);
    // NO CURLOPT_TIMEOUT. A whole-transfer deadline is wrong for a file that
    // can legitimately take twenty minutes on a slow link; a stall is caught by
    // the low-speed limit below instead.
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_LIMIT, 1024L);
    curl_easy_setopt(c, CURLOPT_LOW_SPEED_TIME, 60L);
    curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
    if (hdrs) curl_easy_setopt(c, CURLOPT_HTTPHEADER, hdrs);

    const CURLcode rc = curl_easy_perform(c);
    long status = 0;
    curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &status);
    std::fclose(sink.f);
    if (hdrs) curl_slist_free_all(hdrs);
    curl_easy_cleanup(c);

    if (sink.aborted) {
        std::remove(partPath.c_str());
        if (err) *err = "cancelled";
        return false;
    }
    if (rc != CURLE_OK || status >= 400) {
        std::remove(partPath.c_str());
        if (err) {
            *err = rc != CURLE_OK ? curl_easy_strerror(rc)
                                  : "HTTP " + std::to_string(status);
        }
        return false;
    }
    if (std::rename(partPath.c_str(), destPath.c_str()) != 0) {
        std::remove(partPath.c_str());
        if (err) *err = "cannot rename into place: " + destPath;
        return false;
    }
    return true;
}

std::vector<uint8_t> Client::fetchBytes(const std::string& path) const {
    std::vector<uint8_t> data;
    CURL* c = curl_easy_init();
    if (!c) return data;
    // The path comes from the server and may carry anything; see encodeUrl.
    const std::string raw = (path.compare(0, 4, "http") == 0) ? path : base_ + path;
    const std::string url = encodeUrl(raw);
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
