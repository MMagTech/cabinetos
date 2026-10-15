#include "offline_sync.h"

#include <SDL3/SDL.h>
#include <unistd.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <ctime>
#include <map>
#include <thread>
#include <vector>

#include "accounts.h"
#include "cache.h"
#include "catalog.h"
#include "covercache.h"
#include "library.h"
#include "romm.h"
#include "savemirror.h"
#include "storage.h"

// WHAT OFFLINE WILL NEED, FETCHED WHILE ONLINE (#88). A game on the drive
// with no record cannot be shown offline, and one whose covers were never
// drawn here would show as a colour. Both are fetched once per start, behind
// Home, on a client of their own: the records first (a game cached before
// records existed, or kept with an older one), then the two covers of every
// game on the drive that the cover cache does not hold. MMagTech, on #88:
// "fetch covers once for games already kept", and cached games the same.
void backfillDrive(const std::string& address) {
    static std::atomic<bool> running{false};
    if (running.exchange(true)) return;
    const storage::User who = storage::currentUser();
    std::thread([address, who]() {
        romm::Client c;
        std::string err;
        if (!who.valid() || !c.setAddress(address, &err) || !accounts::loadActiveToken(c)) {
            running = false;
            return;
        }
        int records = 0, covers = 0, missing = 0, saves = 0;
        std::map<int, std::string> saveTagOf;   // rom id -> the tag a launch restores
        for (const cache::OnDrive& d : cache::onDrive()) {
            const std::string note =
                storage::gamesDir() + "/" + std::to_string(d.romId) + ".json";
            romm::Game g;
            if (::access(note.c_str(), F_OK) != 0) {
                if (c.fetchGame(d.romId, &g, &err)) {
                    cache::rememberGame(g.id, gameRecordJson(g));
                    ++records;
                } else {
                    ++missing;   // gone from the server, or not this person's
                    continue;
                }
            } else if (!romm::gameFromJson(cache::gameRecord(d.romId), &g)) {
                continue;
            }
            for (const std::string& key : {g.coverPath, g.coverLargePath}) {
                if (key.empty() || !covercache::read(key).empty()) continue;
                std::vector<uint8_t> bytes = c.fetchBytes(key);
                if (bytes.empty()) continue;
                covercache::write(key, bytes);
                ++covers;
            }
            if (const catalog::Coverage cov = catalog::coverageFor(g); cov.core)
                if (const char* tag = catalog::saveTag(cov.core)) saveTagOf[g.id] = tag;
        }
        // THE NEWEST SAVES, one call for every game (savemirror.h): the rows
        // for each game, and the bytes of the newest row of each kind (by
        // extension: a card per region, a battery, a zip) under the tag a
        // launch restores from. Bytes already kept at that version are not
        // fetched again.
        std::vector<int> ids;
        for (const auto& kv : saveTagOf) ids.push_back(kv.first);
        std::vector<romm::Asset> rows;
        const uint64_t asked = savemirror::mark();
        if (!ids.empty() && c.fetchSavesFor(ids, &rows, &err)) {
            std::map<int, std::vector<romm::Asset>> byGame;
            for (int id : ids) byGame[id];   // a game with none is listed as none
            for (romm::Asset& a : rows) byGame[a.romId].push_back(std::move(a));
            for (const auto& [romId, list] : byGame) {
                if (savemirror::forgottenSince(romId, asked)) continue;   // an upload landed
                savemirror::putList(who, romId, list);
                std::map<std::string, const romm::Asset*> newest;   // extension -> row
                for (const romm::Asset& a : list) {
                    if (a.emulator != saveTagOf[romId]) continue;
                    const size_t dot = a.fileName.rfind('.');
                    const std::string ext = dot == std::string::npos ? "" : a.fileName.substr(dot);
                    const romm::Asset*& n = newest[ext];
                    if (!n || a.updatedAt > n->updatedAt) n = &a;
                }
                for (const auto& [ext, a] : newest) {
                    if (savemirror::hasBytes(who, romId, *a)) continue;
                    std::vector<uint8_t> bytes = c.fetchAsset("saves", a->id);
                    if (bytes.empty()) continue;
                    savemirror::putBytes(who, romId, *a, bytes);
                    ++saves;
                }
            }
        } else if (!ids.empty()) {
            std::fprintf(stderr, "[offline] saves not fetched: %s\n", err.c_str());
        }
        std::fprintf(stderr, "[offline] ready for offline: %d record(s), %d cover(s) and %d "
                             "save(s) fetched%s\n", records, covers, saves,
                     missing ? (", " + std::to_string(missing) +
                                " game(s) the server would not describe").c_str() : "");
        running = false;
    }).detach();
}

// WHETHER THIS MACHINE HAS A ROUTE OUT AT ALL: a default route in the kernel's
// table. Read only to notice the network coming back (startReconnect).
static bool haveDefaultRoute() {
    FILE* f = std::fopen("/proc/net/route", "r");
    if (!f) return false;
    char line[256];
    bool found = false;
    while (!found && std::fgets(line, sizeof line, f)) {
        char iface[64];
        unsigned long dest = 1;
        if (std::sscanf(line, "%63s %lx", iface, &dest) == 2 && dest == 0) found = true;
    }
    std::fclose(f);
    return found;
}

// THE SERVER, TRIED IN THE BACKGROUND WHILE IT IS AWAY (#88). Never on the
// frame thread. Quick tries first (every ten seconds for two minutes: a
// router coming back), then about once a minute (a server that is off, a
// hotel). A try that answers clears serverAway() by itself (noteTransport),
// and the frame loop does the rest. Its own client, so the live one is
// never touched from here. AND ONCE A MINUTE WHILE IT IS THERE, the same
// heartbeat, so a server that goes while the console sits on Home is
// noticed (the Offline chip, launches that stop asking it) without waiting
// for somebody to need it.
void startReconnect(const std::string& address) {
    std::thread([address]() {
        long long lastTry = 0;
        bool hadRoute = haveDefaultRoute();
        long long lastBeat = static_cast<long long>(std::time(nullptr));
        long long lastFill = lastBeat;
        for (;;) {
            if (!romm::serverAway()) {
                const long long now = static_cast<long long>(std::time(nullptr));
                // What offline needs, again every quarter of an hour: a save
                // made on another device since is the one a trip starts from.
                if (now - lastFill >= 900) {
                    lastFill = now;
                    backfillDrive(address);
                }
                if (now - lastBeat >= 60) {
                    lastBeat = now;
                    romm::Client c;
                    std::string err;
                    c.setAddress(address, &err);
                }
            } else {
                const long long now = static_cast<long long>(std::time(nullptr));
                // THE NETWORK CAME BACK (a cable, Wi-Fi switched on): try at
                // once rather than at the next minute's try.
                const bool route = haveDefaultRoute();
                if (route && !hadRoute) lastTry = 0;
                hadRoute = route;
                const long long awayFor = now - romm::serverAwaySince();
                const long long every = awayFor < 120 ? 10 : 60;
                if (now - lastTry >= every) {
                    lastTry = now;
                    romm::Client c;
                    std::string err;
                    c.setAddress(address, &err);
                }
            }
            SDL_Delay(250);
        }
    }).detach();
}
