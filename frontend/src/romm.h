// The RomM client for the CabinetOS frontend.
//
// See docs/PROJECT.md, Phase 4. What matters here, in the order it matters:
//
// NO PASSWORD EVER REACHES THIS PROGRAM. RomM has a device-authorisation flow:
// we ask the server to start a pairing, it returns a short code, and the person
// approves it in a browser they are already signed in to. We poll until it is
// approved and receive a scoped token we can be given and can have revoked.
// Cabinet solves first-run this way on tvOS for the same reason a console has:
// typing a password with a controller is miserable. So the whole typing burden
// on a television is ONE HOSTNAME.
//
// PLAIN HTTP MUST WORK. A self-hosted RomM on a home LAN very often speaks only
// http, and Apple's App Transport Security is the reason Cabinet had to fight
// this. Nothing on Linux forbids it, so the bug is simply avoidable — but only
// if it is not designed back in. setAddress() accepts a bare host, and probes
// rather than assumes.
//
// IT MUST NEVER BLOCK THE FRAME LOOP. Every call here is synchronous and
// blocking, which is deliberate: this class does not own a thread. Callers run
// it on workers, exactly as ImageCache does with its loader. Making it async
// internally would hide the cost and duplicate a threading model that already
// exists. Each call uses its own CURL handle, so concurrent calls are safe.
//
// A PLATFORM IS NOT ITS SLUG. RomM can hold two platforms with the same name
// AND the same slug — "Arcade" is FBNeo and MAME 2003-Plus in this library,
// deliberately. Platform carries `id` as identity and `fsSlug` as the thing
// that picks a core. Never key on `slug`.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace romm {

// A system, as RomM understands it. `id` is the ONLY unique field; see above.
struct Platform {
    int id = 0;
    std::string name;     // "Arcade" — not unique, and not always meaningful
    std::string slug;     // "arcade" — not unique either
    std::string fsSlug;   // "FBNEO" / "MAME2003" — what actually picks a core
    int romCount = 0;
};

struct Game {
    int id = 0;
    int platformId = 0;
    // The ROM payload carries its own platform, so a game knows what it runs on
    // without a second lookup. Home needs this: the hero is the most recently
    // played game THIS CONSOLE CAN PLAY, which cannot be decided without it.
    std::string platformSlug;    // "dc"
    std::string platformFsSlug;  // "Sega Dreamcast"
    std::string platformName;    // "Dreamcast" — for the hero's band
    std::string name;
    std::string fsName;
    // Path on the server, not a URL: the cover fetch goes through the same
    // authenticated client, so callers hand this straight to ImageCache.
    std::string coverPath;
    int64_t sizeBytes = 0;
};

// An in-flight pairing. Short-lived: RomM expires these in minutes.
struct Pairing {
    std::string userCode;          // shown to the person, e.g. "ZHVUCSF4"
    std::string verificationUrl;   // what the QR code encodes
    std::string deviceCode;        // secret; never shown, never logged
    int expiresIn = 0;
    int intervalSeconds = 5;
};

class Client {
public:
    Client();
    ~Client();

    // Accepts "romm.local:8080", "192.168.1.10:6005", "http://host", "host".
    // With no scheme it tries both and keeps whichever answers, preferring
    // http for an address that is obviously local. Returns false and fills
    // `err` if neither answers.
    bool setAddress(const std::string& address, std::string* err);
    const std::string& baseUrl() const { return base_; }

    // The server's version, filled in by setAddress from /api/heartbeat.
    const std::string& serverVersion() const { return serverVersion_; }

    bool haveToken() const { return !token_.empty(); }
    void setToken(std::string token) { token_ = std::move(token); }

    // Token persistence. The file is written 0600 and holds a credential, so
    // it does not belong anywhere near the repository.
    bool loadToken(const std::string& path);
    bool saveToken(const std::string& path) const;

    // Step one of pairing. Asks for read-only scopes only; anything that writes
    // is requested separately, when something actually needs to write.
    bool beginPairing(Pairing* out, std::string* err);

    // Step two, called on the pairing's own interval.
    //   1  approved — the token is now held by this client
    //   0  still pending
    //  -1  failed or expired; `err` says which
    int pollPairing(const Pairing& p, std::string* err);

    bool fetchPlatforms(std::vector<Platform>* out, std::string* err);

    // platformId <= 0 fetches across every platform. Pages internally: RomM
    // caps a response and a library of thousands would otherwise arrive
    // truncated, silently.
    bool fetchGames(int platformId, std::vector<Game>* out, std::string* err);

    // The games with play history, most recent first — the same query RomM's
    // own web home screen makes, so CabinetOS agrees with the web UI and with
    // Cabinet about what you were last playing.
    //
    // PLAY HISTORY LIVES ON THE SERVER, not on the console. A game played on an
    // Apple TV is recent here the moment this console is paired, which is what
    // makes a hero possible on a machine that has never launched anything.
    bool fetchRecent(int limit, std::vector<Game>* out, std::string* err);

    // For ImageCache::Loader. Returns empty on any failure, because a cover
    // that will not load is not an error the frame loop can do anything about.
    std::vector<uint8_t> fetchBytes(const std::string& path) const;

private:
    bool get(const std::string& path, std::string* body, std::string* err) const;
    bool postJson(const std::string& path, const std::string& json,
                  std::string* body, long* status, std::string* err) const;
    bool probe(const std::string& candidate);

    std::string base_;
    std::string token_;
    std::string serverVersion_;
};

// True for addresses that are obviously on a local network, which decides
// whether http or https is tried first. A hostname with no dots is local by
// construction, and so is anything in the private ranges or .local/.lan.
bool looksLocal(const std::string& host);

}  // namespace romm
