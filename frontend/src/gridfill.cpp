// A platform's or collection's grid: opened on its first page, and the
// rest fetched behind it and added as it lands (#398, restructure batch e).

#include "app.h"

#include <cstdio>
#include <utility>

#include "catalog.h"

using namespace design;   // NOLINT, as main.cpp names them

// Pages of a big grid that arrived behind the first one. Applied
// here because this thread owns the store and the screen.
void App::drainGridPages() {
    if (here() == Screen::Grid) {
        std::vector<romm::Game> got;
        {
            std::lock_guard<std::mutex> lk(gridFill.m);
            got.swap(gridFill.arrived);
        }
        if (!got.empty()) {
            std::vector<int> added;
            added.reserve(got.size());
            screens::Tile* t = nullptr;
            for (auto& v : {&platformTiles, &collectionTiles})
                for (screens::Tile& x : *v)
                    if (x.query == gridFill.tileQuery) { t = &x; break; }
            for (const auto& g : got) {
                if (!catalog::playable(g) &&
                    !catalog::shownGreyed(catalog::coverageFor(g).support))
                    continue;
                const int i = appendGame(lib, g);
                added.push_back(i);
                // In the server's order, as the first page (gridOrder).
                if (t) t->cards.push_back(i);
            }
            gridScreen.append(added, cards);
        }
        gridScreen.setLoadingMore(gridFill.running.load());
    }
}

void App::openTile(const screens::Result& res) {
    const auto& tiles = libraryScreen.visible();
    if (res.value < 0 || res.value >= static_cast<int>(tiles.size())) return;
    // THE GAMES ARE FETCHED HERE AND NOWHERE EARLIER — open
    // question 28. Boot draws this screen having fetched no games
    // at all; a grid costs one request at the moment somebody
    // walks into it, and nothing at all for the systems they never
    // open. The second visit costs nothing: loadTileGames sees a
    // membership it already filled and returns. Whether each game is
    // greyed is asked again on every visit (gridOrder): pairing a Wii
    // Remote or starting a stream changes it.
    const bool isCollection = libraryScreen.tab() != 0;
    const std::string tileQuery = tiles[res.value].query;
    auto& own = isCollection ? collectionTiles : platformTiles;
    screens::Tile* t = nullptr;
    for (auto& x : own) if (x.query == tileQuery) { t = &x; break; }
    if (!t) return;
    bool more = false;
    // READ AGAIN WHEN REOPENED A MINUTE OR MORE LATER (#387). A cover fixed on
    // RomM showed only after a restart, because a grid was read once a run.
    // RomM moves a game's `updated_at` when it is matched but not its
    // platform's (checked on the reference server, 2026-10-18), so the
    // platform list cannot say a grid changed: time is the only signal. The
    // cost is the first open's, one page, and nothing while the server is
    // away. A read that fails keeps the list there was.
    constexpr unsigned long long kRereadAfterMs = 60 * 1000;
    if (!t->cards.empty() && !romm::serverAway() &&
        SDL_GetTicks() - t->loadedAtMs >= kRereadAfterMs) {
        gridFill.stop();
        std::vector<int> before;
        before.swap(t->cards);
        if (loadTileGames(liveClient, lib, *t, &more)) {
            libraryScreen.learnedTile(tileQuery, t->detail, t->cover);
            std::fprintf(stderr, "[library] %s: read again, %zu game(s)\n",
                         t->title.c_str(), t->cards.size());
        } else {
            t->cards.swap(before);
            more = false;
        }
    }
    if (t->cards.empty()) {
        // ONE PAGE, AND IT BLOCKS FOR THAT. The person has chosen
        // a system and is waiting for one request rather than for
        // the whole platform — 0.19 s for 141 games here, and the
        // same 0.19 s for a platform of thirty thousand.
        loadTileGames(liveClient, lib, *t, &more);
        libraryScreen.learnedTile(tileQuery, t->detail, t->cover);
    }
    if (t->cards.empty()) {
        // Nothing came back, or nothing in it can be played here.
        // Refusing is better than opening onto an empty grid with
        // no reason on it.
        sound::play(sound::Cue::Edge);
        return;
    }
    gridScreen.open(t->title, gridOrder(lib, *t), cards);
    gridScreen.setLoadingMore(more);
    // ONE FILLER AT A TIME. Walking into a second platform stops
    // the first: nobody is looking at it any more, and two of
    // these would be appending into two different tiles at once.
    gridFill.stop();
    if (more) {
        gridFill.tileQuery = tileQuery;
        gridFill.running.store(true);
        const std::string filter = tileQuery;
        const size_t from = t->cards.size();
        gridFill.th = std::thread([this, filter, from]() {
            constexpr int kPage = 500;
            size_t offset = from;
            for (;;) {
                if (gridFill.quit.load()) break;
                std::vector<romm::Game> page;
                std::string err;
                int total = 0;
                if (!liveClient.fetchRoms(
                        filter + "&offset=" + std::to_string(offset),
                        kPage, &page, &err, &total))
                    break;
                if (page.empty()) break;
                offset += page.size();
                {
                    std::lock_guard<std::mutex> lk(gridFill.m);
                    for (auto& g : page)
                        gridFill.arrived.push_back(std::move(g));
                }
                if (total > 0 && offset >= static_cast<size_t>(total)) break;
            }
            gridFill.running.store(false);
        });
    }
    stack.push_back(Screen::Grid);
    sound::play(sound::Cue::Activate);
}
