// Search: the server asked (or the drive, offline) as somebody types, the
// wait after the last key, the results' room, and Search's half of the
// keyboard's answer (#398, restructure batch d).

#include "app.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

#include "cache.h"
#include "catalog.h"

using namespace design;   // NOLINT, as main.cpp names them

// Asks the server and hands the screen the answer. Shared by the live
// typing loop and by `--screen search --query`, so a capture exercises the
// same path a person does rather than a second implementation of it.
void App::runSearch(const std::string& q) {
    if (q.empty()) return;
    // OFFLINE (#88, MMagTech 2026-10-05): by name over the games on the
    // drive, the same games the offline Library shows. Results are simply
    // what is here; nothing says what might be on the server. Also while
    // the library is still the drive's, the server back but not yet
    // loaded (the frame loop loads it on Home).
    if (romm::serverAway() || libFromDrive) {
        auto lower = [](std::string x) {
            for (char& ch : x) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            return x;
        };
        const std::string want = lower(q);
        std::vector<int> idx;
        // THE DRIVE'S GAMES ARE READ ONCE, NOT PER SEARCH: a file per game,
        // which on a drive of a thousand would be a stutter per keystroke.
        // Read again at most every half minute (a drive plugged in).
        static std::vector<romm::Game> onDrive;
        static Uint64 readAt = 0;
        if (onDrive.empty() || SDL_GetTicks() - readAt > 30000) {
            readAt = SDL_GetTicks();
            onDrive.clear();
            for (const cache::OnDrive& d : cache::onDrive()) {
                romm::Game g;
                if (d.record.empty() || !romm::gameFromJson(d.record, &g)) continue;
                if (catalog::playable(g)) onDrive.push_back(std::move(g));
            }
        }
        for (const romm::Game& g : onDrive) {
            const std::string name = lower(g.name.empty() ? g.fsName : g.name);
            if (name.find(want) == std::string::npos) continue;
            idx.push_back(appendGame(lib, g));
        }
        std::sort(idx.begin(), idx.end(), [&](int a, int b) {
            return lower(cards[static_cast<size_t>(a)].title) <
                   lower(cards[static_cast<size_t>(b)].title);
        });
        const int n = static_cast<int>(idx.size());
        std::fprintf(stderr, "[search] %s: %d on the drive (offline)\n", q.c_str(), n);
        searchScreen.setResults(q, std::move(idx), n);
        return;
    }
    std::vector<romm::Game> found;
    std::string err;
    int total = 0;
    if (!liveClient.fetchRoms("search_term=" + romm::Client::encodeQueryValue(q),
                              kSearchLimit, &found, &err, &total)) {
        // NOT "nothing matches". The console could not go and look, and
        // saying the library holds nothing like it would be a lie told
        // confidently. See SearchScreen::State.
        std::fprintf(stderr, "[search] %s: %s\n", q.c_str(), err.c_str());
        searchScreen.setFailed(err);
        return;
    }
    std::vector<int> idx;
    idx.reserve(found.size());
    for (const auto& g : found) {
        // Same rule as Home's shelf and a collection's grid: a result this
        // console cannot start must not be offered. The heading's "12 of
        // 52" is what accounts for the difference.
        if (!catalog::playable(g)) continue;
        idx.push_back(appendGame(lib, g));
    }
    std::fprintf(stderr, "[search] %s: %zu of %d\n", q.c_str(), idx.size(), total);
    searchScreen.setResults(q, std::move(idx), total);
}

// UP OUT OF THE KEYBOARD'S TOP ROW. It means nothing anywhere else — this
// keyboard deliberately does not wrap — and on Search it is how a person
// gets from what they typed to what it found.
void App::keyboardUp() {
    if (here() == Screen::Search && keyboard.atTopRow() &&
        searchScreen.resultCount() > 0) {
        searchScreen.setFocused(true);
        sound::play(sound::Cue::Move);
        return;
    }
    keyboard.moveFocus(0, -1);
}

// Search's half of keyboardResult (main.cpp), which calls it last.
void App::searchKeyboardResult(ui::KeyboardResult res) {
    if (res == ui::KeyboardResult::Committed) {
        if (searchScreen.resultCount() > 0) searchScreen.setFocused(true);
        else if (!keyboard.isOpen()) goToDestination(2);   // nothing found: type again
    } else if (res == ui::KeyboardResult::Cancelled) {
        goToDestination(0);
        sound::play(sound::Cue::Back);
    }
}

void App::tickSearch(float dt, screens::Ctx& ctx) {
    searchScreen.tick(dt, ctx);
}

// THE SEARCH LOOP. It used to be three lines because the whole
// library was in memory and a substring match was free. It asks
// the server now — open question 28 — so it is in two halves:
// notice what was typed, and some time after the typing stops, go
// and ask.
void App::pumpSearch(float dt) {
    if (here() == Screen::Search && keyboard.isOpen() &&
        keyboard.value() != searchTyped) {
        searchTyped = keyboard.value();
        searchScreen.setQuery(searchTyped);
        searchPending = searchTyped;
        searchDebounce = kSearchDebounce;
        // Said immediately, not when the request goes out: the gap
        // between the last keypress and the answer is exactly the
        // stretch where a screen that says nothing looks broken.
        searchScreen.setWaiting();
    }
    if (!searchPending.empty()) {
        searchDebounce -= dt;
        if (searchDebounce <= 0.0f) {
            const std::string q = searchPending;
            searchPending.clear();
            // BLOCKING, ON THE FRAME THREAD, AND MEASURED RATHER THAN
            // ASSUMED: 40-80 ms against the reference server, which is
            // a few dropped frames once the typing has already
            // stopped. A worker thread would hide it and is what this
            // wants if the wait is ever felt on a slower server; the
            // shape to copy is ImageCache's, not a second event loop.
            runSearch(q);
        }
    }
}

// The results are sized to the room the keyboard leaves, and the
// keyboard's height depends on its own layout — so it is asked
// rather than assumed.
void App::sizeSearchResults() {
    if (here() == Screen::Search) {
        searchScreen.setKeysUp(keyboard.isOpen());
        searchScreen.setResultsBottom(keyboard.isOpen() ? keyboard.panelTop()
                                                        : ui::kCanvasHeight);
    }
}
