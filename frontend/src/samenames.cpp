// The diagnostic report's "Same-name games" (#383). See samenames.h.

#include "samenames.h"

#include "accounts.h"
#include "catalog.h"
#include "design.h"
#include "romm.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace samenames {

namespace {

std::string joined(const std::vector<std::string>& v) {
    if (v.empty()) return "none";
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) s += (i ? ", " : "") + v[i];
    return s;
}

}  // namespace

int probe(const char* serverAddress) {
    romm::Client client;
    std::string err;
    if (!serverAddress || !client.setAddress(serverAddress, &err)) {
        std::printf("No server to ask%s%s\n", err.empty() ? "" : ": ", err.c_str());
        return 1;
    }
    // The person signed in; the console's first login is the fallback, as
    // everywhere a token is looked for.
    const char* home = std::getenv("HOME");
    if (!client.loadToken(accounts::tokenPath(accounts::activeId())) &&
        !client.loadToken(std::string(home ? home : ".") + "/.config/cabinetos/romm.json")) {
        std::printf("Nobody is signed in on this console\n");
        return 1;
    }
    std::vector<romm::Game> all;
    if (!client.fetchGames(0, &all, &err)) {
        std::printf("The server did not answer: %s\n", err.c_str());
        return 1;
    }

    // The games a platform's grid shows: playable, or greyed with a reason.
    // Keyed by system name and id, so the list reads by system and two
    // systems with one name stay apart.
    using Key = std::pair<std::pair<std::string, int>, std::string>;
    std::map<Key, std::vector<const romm::Game*>> byName;
    for (const romm::Game& g : all) {
        if (!catalog::playable(g) && !catalog::shownGreyed(catalog::coverageFor(g).support))
            continue;
        const std::string title = g.name.empty() ? g.fsName : g.name;
        byName[{{g.platformName, g.platformId}, title}].push_back(&g);
    }

    int names = 0, games = 0;
    std::string out;
    for (const auto& [key, copies] : byName) {
        if (copies.size() < 2) continue;
        ++names;
        games += static_cast<int>(copies.size());
        const std::string& title = key.second;
        out += "\n" + key.first.first + ": " + title + "\n";
        for (const romm::Game* g : copies) {
            out += "  " + g->fsName + "\n";
            out += "    RomM: regions " + joined(g->regions) + "; languages " +
                   joined(g->languages) + "; revision " +
                   (g->revision.empty() ? "none" : g->revision) + "; tags " + joined(g->tags) +
                   "\n";
            out += "    Heading: " + title + (g->tagLabel.empty() ? "" : "  " + g->tagLabel) +
                   "\n";
        }
    }
    if (names == 0)
        std::printf("None: no two games on one system share a name.\n");
    else
        std::printf("%d name%s shared by %d games, on the systems this console shows.\n%s",
                    names, names == 1 ? "" : "s", games, out.c_str());
    return 0;
}

int test() {
    int failed = 0;
    auto check = [&](bool ok, const char* what) {
        std::printf("%s  %s\n", ok ? "pass" : "FAIL", what);
        if (!ok) ++failed;
    };

    // The wording: RomM's order, each group in its own brackets.
    romm::Game g;
    check(romm::tagLabel(g).empty(), "no fields, no label");
    g.regions = {"USA", "Europe"};
    check(romm::tagLabel(g) == "(USA, Europe)", "regions: (USA, Europe)");
    g.revision = "1";
    check(romm::tagLabel(g) == "(USA, Europe) (Rev 1)", "revision after regions: (Rev 1)");
    g.languages = {"English", "French"};
    g.tags = {"Beta", "1995"};
    check(romm::tagLabel(g) == "(USA, Europe) (English, French) (Rev 1) (Beta, 1995)",
          "all four in RomM's order");
    romm::Game t;
    t.tags = {"a1"};
    check(romm::tagLabel(t) == "(a1)", "tags alone");

    // When a twin counts: same name, same system, same list.
    std::vector<design::Card> pool(6);
    auto card = [&](int i, const char* title, int platform, const char* label) {
        pool[i].title = title;
        pool[i].platformId = platform;
        pool[i].tagLabel = label;
    };
    card(0, "Emerald", 2, "(USA, Europe)");
    card(1, "Emerald", 2, "");
    card(2, "Emerald", 7, "(Japan)");             // same name, another system
    card(3, "Hunters", 33, "(USA) (Rev 1)");
    card(4, "Hunters", 33, "");
    card(5, "Lone", 2, "(Europe)");
    check(design::hasTwin(pool, {0, 1}, 0), "twin in the list: label");
    check(!design::hasTwin(pool, {0, 1}, 1), "untagged twin: nothing to show");
    check(!design::hasTwin(pool, {0, 2}, 0), "REFUSED: same name on another system");
    check(!design::hasTwin(pool, {0, 3, 5}, 0), "REFUSED: twin not in this list");
    check(!design::hasTwin(pool, {0}, 0), "REFUSED: alone in the list");
    check(!design::hasTwin(pool, {0, 0}, 0), "REFUSED: the same card twice is not a twin");
    check(!design::hasTwin(pool, {5, 0}, 5), "REFUSED: tags but no twin");
    check(!design::hasTwin(pool, {0, 1}, 9), "REFUSED: a card outside the pool");
    check(design::hasTwin(pool, {3, 0, 4}, 3), "twin found anywhere in the list");
    design::Card unknown = pool[0];
    unknown.platformId = 0;
    pool.push_back(unknown);
    check(!design::hasTwin(pool, {6, 0}, 6), "REFUSED: no system known");

    std::printf("%s\n", failed ? "FAILED" : "all passed");
    return failed ? 1 : 0;
}

}  // namespace samenames
