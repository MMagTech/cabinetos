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
#include <functional>
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

// A collection: the person's own grouping of games, held by RomM so it is the
// same on every device. It is not a platform in one way that decides the code
// here — its membership is a LIST OF ROM IDS rather than a property of each
// game, so a collection is resolved by looking its ids up in the library
// rather than by asking the server for a filtered page.
//
// RomM's own "Favorites" is a collection like any other, flagged `is_favorite`.
// Home already has a Favorites shelf fed by the favourites endpoint, so the
// Library deliberately shows it here too rather than hiding it: a person who
// opens Collections looking for the one they made is entitled to see the list
// their server actually holds.
struct Collection {
    int id = 0;
    std::string name;
    int romCount = 0;
    std::vector<int> romIds;
    // A cover for the tile. RomM keeps a mosaic of member covers for a
    // collection with no art of its own, and the first of them is a real cover
    // from a real game inside it, which is all a tile needs.
    std::string coverPath;
    bool isFavorite = false;
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

// One BIOS file a platform carries.
//
// Cabinet's rule, and it is the right one: fetch EVERY file the platform lists
// rather than working out which one a given game needs. A core looks BIOS up by
// name in the system directory and ignores what it does not want — Beetle
// Saturn takes one of two region BIOSes, some FBNeo boards need none — so extra
// files are harmless and a missing one is the only failure that matters.
struct Firmware {
    int id = 0;
    std::string fileName;
    int64_t sizeBytes = 0;
    std::string md5;
    bool verified = false;
};

// A save or a save state held by RomM.
//
// They are separate endpoints and separate ideas, and Cabinet keeps them apart
// on purpose. A SAVE is the game's own — a cartridge battery, a memory card —
// and it outlives everything; it is uploaded with overwrite so a PS1 game keeps
// one memory card rather than one per session. A STATE is a snapshot of the
// whole machine, only loadable by the build that wrote it, and a history of
// them is the point, so states never overwrite.
struct Asset {
    int id = 0;
    std::string fileName;
    int64_t sizeBytes = 0;
    // Which core wrote it. The reason states can be offered or greyed out
    // rather than failing in front of someone — see docs/CABINET.md.
    std::string emulator;
    std::string updatedAt;
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

    // The person's collections, which the Library shows beside the platforms.
    // Empty is a normal answer — plenty of libraries have none — and the
    // switcher says so rather than showing a blank grid.
    bool fetchCollections(std::vector<Collection>* out, std::string* err);

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

    // The games marked favourite, newest first. Home's second shelf, and only
    // drawn when there are any — an empty Favorites row is worse than none.
    bool fetchFavorites(int limit, std::vector<Game>* out, std::string* err);

    // Every firmware file a platform carries. Empty is a normal answer: most
    // platforms need none.
    bool fetchFirmware(int platformId, std::vector<Firmware>* out, std::string* err);

    bool fetchSaves(int romId, std::vector<Asset>* out, std::string* err);
    bool fetchStates(int romId, std::vector<Asset>* out, std::string* err);

    // `/api/saves/{id}/content` or `/api/states/{id}/content`. Small enough to
    // hold: a state is hundreds of kilobytes, a memory card is 128.
    std::vector<uint8_t> fetchAsset(const char* kind, int assetId) const;

    // Both post multipart. `emulator` is the tag that decides whether a state
    // is offered later, so it must identify the BUILD and not just the core.
    bool uploadSave(int romId, const std::string& emulator, const std::string& fileName,
                    const std::vector<uint8_t>& data, std::string* err) const;
    bool uploadState(int romId, const std::string& emulator, const std::string& fileName,
                     const std::vector<uint8_t>& data, std::string* err) const;

    // For ImageCache::Loader. Returns empty on any failure, because a cover
    // that will not load is not an error the frame loop can do anything about.
    //
    // For COVERS, not for ROMs: it holds the whole body in memory, which is
    // right for 50 KB of PNG and catastrophic for the 1.78 GB arcade set in the
    // reference library. Anything that might be a game goes through
    // fetchToFile.
    std::vector<uint8_t> fetchBytes(const std::string& path) const;

    // Streams a body straight to disk, never holding more than a buffer of it.
    //
    // `onProgress` is called from inside the transfer with bytes-so-far and the
    // total the server declared, which may be 0 when it declines to say. It
    // returns false to abort — that is how a cancel reaches a download that is
    // already running. It is called on whatever thread drove the request, so it
    // must not touch the UI directly.
    using ProgressFn = std::function<bool(int64_t got, int64_t total)>;
    bool fetchToFile(const std::string& path, const std::string& destPath,
                     const ProgressFn& onProgress, std::string* err) const;

private:
    bool fetchFiltered(const char* filter, int limit, std::vector<Game>* out,
                       std::string* err);
    bool postMultipart(const std::string& path, const char* partName,
                       const std::string& fileName, const std::vector<uint8_t>& data,
                       std::string* err) const;
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
