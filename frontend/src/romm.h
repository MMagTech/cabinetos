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
    // THE TWO FIELDS A CACHE VALIDATES AGAINST, and they cost nothing because
    // the platform list is fetched at every boot anyway. `updatedAt` moves when
    // the platform changes; `romCount` catches the case where it does not.
    // Neither is used for anything else — see covercache and PROJECT.md 30.
    std::string updatedAt;
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
    //
    // TWO SIZES, AND THE SMALL ONE IS A THUMBNAIL. RomM keeps `small` at
    // 162x216 and `big` at 810x1080, for about double the bytes — the small
    // ones are inefficiently encoded PNGs, so 5x the pixels costs 1.5x the
    // transfer. Measured against the live server 2026-09-21.
    //
    // 162 points wide is not enough for anything on a 4K television: a shelf
    // cover is 158 design points, which is 316 real pixels, so even the
    // SMALLEST place art appears is a 2x upscale. Everywhere else is worse.
    // Ask for the size the drawing needs rather than the one that arrives
    // first — `coverLargePath` where a cover is big or full-screen, and the
    // thumbnail where it is a thumbnail.
    std::string coverPath;
    std::string coverLargePath;
    int64_t sizeBytes = 0;
    // RomM's `title_id`, as it sends it: hex, read off the file by RomM. Only
    // Wii uses it so far, to look the game up in GameTDB's controller list
    // (wii.h); empty for the many platforms RomM reads no ID for.
    std::string titleId;
    // A Wii U game's product code, `BWPE`, read off its file by the console:
    // GameTDB's key for its controllers (wiiu.h). Empty for everything else.
    std::string productCode;
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

// One file of a game, as RomM lists it under `files`. A game RomM holds as a
// folder (a PS3 PKG and its licence) is several; a one-file game is one.
//
// WHY A GAME WOULD BE FETCHED FILE BY FILE rather than as the one zip RomM
// makes of a folder: a PS3 PKG has to be installed after it arrives, and the
// zip route would hold the zip, the PKG unpacked from it and the installed
// game at once, three times a 20 GB game. File by file, it is twice, and only
// while the install runs. `?file_ids=` on the content endpoint serves one
// file as it is (RomM 5.3.1, checked 2026-09-28).
struct RomFile {
    int id = 0;
    std::string fileName;
    int64_t sizeBytes = 0;
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
    // A state's picture on RomM (its `screenshot.download_path`), for the
    // launch screen. Empty for a save, or a state sent without one.
    std::string picturePath;
};

// Who the token belongs to.
//
// The console needs this before it writes a single save, because the on-disk
// layout is namespaced per user — `users/<id> - <name>/` — and RomM's own asset
// tree is namespaced the same way. The ID is the identity and the name is
// decoration: usernames change and ids do not, so a console keyed on the name
// would quietly start a new empty directory the day somebody renamed
// themselves, with every save still on the disk and nothing looking for it.
//
// The honest limit, recorded rather than solved: two RomM instances both have a
// User:1, and RomM exposes no instance identity to tell them apart. See
// docs/PROJECT.md, open questions 14 and 18.
struct User {
    int id = 0;
    std::string username;
    // The avatar, as a path this client can fetch, or empty when the person
    // never set one. NOT the `avatar_path` the server reports — that is a
    // location inside RomM's own asset tree ("users/<hash>/profile/<name>.png")
    // and it is not served anywhere underneath /assets, which is where every
    // other picture in this product comes from. Measured against the live
    // server 2026-09-21: all four spellings of that path are 404 and the file
    // is only reachable through `/api/users/<id>/avatar`, which answers a
    // 1200x1200 PNG. So the endpoint is what gets stored.
    std::string avatarPath;
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
    // For `accounts::recordPairing`, which files this under the id the server
    // says it belongs to. A credential: it is never printed and never logged.
    const std::string& token() const { return token_; }

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

    // `/api/users/me` — the id and name behind the token. Needs the `me.read`
    // scope, which pairing already asks for.
    bool fetchCurrentUser(User* out, std::string* err);

    bool fetchPlatforms(std::vector<Platform>* out, std::string* err);

    // The person's collections, which the Library shows beside the platforms.
    // Empty is a normal answer — plenty of libraries have none — and the
    // switcher says so rather than showing a blank grid.
    bool fetchCollections(std::vector<Collection>* out, std::string* err);

    // platformId <= 0 fetches across every platform. Pages internally: RomM
    // caps a response and a library of thousands would otherwise arrive
    // truncated, silently.
    // `onPage` is called after each page arrives, with how many games are in
    // hand so far. It exists so the startup screen can show a number that
    // moves: this call takes several seconds on a real library and a screen
    // that says the same thing throughout is indistinguishable from a hang.
    // Optional, and the pages are fetched on the calling thread either way.
    bool fetchGames(int platformId, std::vector<Game>* out, std::string* err,
                    const std::function<void(int)>& onPage = {});

    // ONE page of roms under whatever filter the caller needs, given as a raw
    // query fragment: "platform_ids=5", "collection_id=2", "search_term=mario".
    //
    // WHY A FRAGMENT RATHER THAN A TYPED QUERY. Three callers want three
    // different filters, each of them a single key the server already
    // understands. A struct of optional fields would be the same string with
    // more ceremony, and a fourth filter would have to edit it.
    //
    // AN UNKNOWN KEY IS IGNORED, NOT REFUSED, and that is the trap in this
    // whole area. `?search=mario` returns the ENTIRE library with a 200 and is
    // indistinguishable from a filter that worked if you count rows — it was
    // very nearly reported as working. Anything added here is checked against
    // `total` and against the names, never against the row count. See
    // docs/PROJECT.md open question 28.
    //
    // `total` is what the server says matched, which is not what came back
    // when `limit` cut it short. Search shows it so a person who typed three
    // letters knows there are more than the twenty on screen.
    bool fetchRoms(const std::string& filter, int limit,
                   std::vector<Game>* out, std::string* err, int* total = nullptr);

    // ONE game, by its id, whatever list it is or is not on. For `--launch`,
    // which since 2026-09-22 can no longer find a game in the library unless
    // it is on Home: boot stopped fetching the whole catalogue.
    bool fetchGame(int romId, Game* out, std::string* err);

    // A few facts about a game from RomM's merged metadata (`metadatum`), for
    // the launch screen: the year, the first company named, how many can
    // play. Any can be missing; a game RomM never matched has none.
    struct Facts {
        int year = 0;
        std::string maker;
        std::string players;   // as RomM gives it: "1", "1-2", "2"
    };
    bool fetchFacts(int romId, Facts* out, std::string* err);

    // TIME PLAYED (#128), playtime.h. `POST /api/play-sessions` takes up to a
    // hundred at once; `outcome` says, per entry, 1 RomM has it (new, or a
    // duplicate of one already sent), -1 RomM refused it for good (its end is
    // in the future), 0 not known. RomM spots a duplicate by game and start
    // time, so sending one twice after a lost answer counts it once.
    struct PlaySession {
        int romId = 0;
        std::string start, end;   // ISO 8601, UTC
        int64_t durationMs = 0;
    };
    bool postPlaySessions(const std::vector<PlaySession>& list, std::vector<int>* outcome,
                          std::string* err) const;
    // The sum of this person's sessions for a game, as this console's device
    // sees them (playtime.h, "whose time RomM returns").
    bool fetchPlayedMs(int romId, int64_t* ms, std::string* err) const;

    // A value going INTO a query fragment, percent-encoded strictly. A search
    // term is whatever somebody typed on a television keyboard, so it can hold
    // a space, an ampersand or an apostrophe — all of which would otherwise
    // end the value or start another parameter.
    static std::string encodeQueryValue(const std::string& in);

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

    // A game's own files (`GET /api/roms/{id}`), and the server path that
    // fetches one of them as it is, for fetchToFile.
    bool fetchRomFiles(int romId, std::vector<RomFile>* out, std::string* err);
    static std::string romFilePath(int romId, const RomFile& f);

    bool fetchSaves(int romId, std::vector<Asset>* out, std::string* err);
    bool fetchStates(int romId, std::vector<Asset>* out, std::string* err);

    // `/api/saves/{id}/content` or `/api/states/{id}/content`. Small enough to
    // hold: a state is hundreds of kilobytes, a memory card is 128.
    std::vector<uint8_t> fetchAsset(const char* kind, int assetId) const;

    // Both post multipart. `emulator` is the tag that decides whether a state
    // is offered later, so it must identify the BUILD and not just the core.
    bool uploadSave(int romId, const std::string& emulator, const std::string& fileName,
                    const std::vector<uint8_t>& data, std::string* err) const;
    // A state may carry its picture, as Cabinet's do (#99): a second part,
    // `screenshotFile`, image/png, named like the state. Empty for none.
    bool uploadState(int romId, const std::string& emulator, const std::string& fileName,
                     const std::vector<uint8_t>& data, std::string* err,
                     const std::string& shotName = "",
                     const std::vector<uint8_t>& shot = {}) const;

    // Removes states, and each one's picture with it (RomM 5.1,
    // `POST /api/states/delete`). Only the signed-in user's; RomM refuses the
    // whole request if one id is not theirs.
    bool deleteStates(const std::vector<int>& ids, std::string* err) const;

    // A screenshot into this person's gallery on RomM 5.1 (`POST
    // /api/screenshots?rom_id=`: stored under the user, private until shared),
    // not among the pictures RomM's metadata sources give a game (#79).
    bool uploadScreenshot(int romId, const std::string& fileName,
                          const std::vector<uint8_t>& png, std::string* err) const;

    // For ImageCache::Loader. Returns empty on any failure, because a cover
    // that will not load is not an error the frame loop can do anything about.
    //
    // For COVERS, not for ROMs: it holds the whole body in memory, which is
    // right for 50 KB of PNG and catastrophic for the 1.78 GB arcade set in the
    // reference library. Anything that might be a game goes through
    // fetchToFile.
    std::vector<uint8_t> fetchBytes(const std::string& path) const;

    // The first `bytes` of a game's file, by an HTTP range: how the console
    // reads a Wii game's code itself when RomM has none (wii.h). Empty on any
    // failure, which the caller must not take for an answer.
    std::vector<uint8_t> fetchHead(const Game& g, size_t bytes) const;
    // `bytes` from `offset`, the same way: how the console reads a Wii U
    // game's product code off the end of its file (wiiu.h). A server that
    // ignores the range gets no answer here unless `offset` is 0.
    std::vector<uint8_t> fetchRange(const Game& g, uint64_t offset, size_t bytes) const;

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
    // Gives every Wii game in `games` from `first` on a code: RomM's, else one
    // this console read before, else read now off the file (wii.h). And every
    // Wii U game its product code, which RomM does not have (fillWiiUCode).
    void fillWiiCodes(std::vector<Game>& games, size_t first = 0) const;
    void fillWiiUCode(Game& g) const;
    bool fetchFiltered(const char* filter, int limit, std::vector<Game>* out,
                       std::string* err);
    bool postMultipart(const std::string& path, const char* partName,
                       const std::string& fileName, const std::vector<uint8_t>& data,
                       std::string* err, const std::string& shotName = "",
                       const std::vector<uint8_t>& shot = {}) const;
    bool get(const std::string& path, std::string* body, std::string* err) const;
    bool postJson(const std::string& path, const std::string& json,
                  std::string* body, long* status, std::string* err) const;
    bool probe(const std::string& candidate);

    std::string base_;
    std::string token_;
    std::string serverVersion_;
};

// WHETHER THE SERVER IS AWAY (#88, offline play). Every request reports what
// it met: one that could not reach the server at all (no connection, no name,
// or a deadline before any connection) sets it, and any answer clears it.
// Read from any thread. `serverAwaySince` is when it last went away, in
// seconds since the epoch. `noteTransport` is the requests' own hook, taking
// a CURL handle and a CURLcode so this header needs no curl.
bool serverAway();
void setServerAway(bool away);
long long serverAwaySince();
void noteTransport(void* curlHandle, int curlCode);

// A game read back from a record written in RomM's own field names: a keep
// record, or the console's note of a game it has on its drive. False when it
// is not one.
bool gameFromJson(const std::string& json, Game* out);

// True for addresses that are obviously on a local network, which decides
// whether http or https is tried first. A hostname with no dots is local by
// construction, and so is anything in the private ranges or .local/.lan.
bool looksLocal(const std::string& host);

}  // namespace romm
