// What the console has to show: the store of cards, Home's shelves and the
// Library's tiles, loaded off RomM at start and at an account switch, or off
// the drive with no server; and a kept game's record, the writer beside the
// library until the RomM client's batch (i). Moved out of main.cpp in the
// restructure (#398).

#pragma once

#include <map>
#include <string>
#include <vector>

#include "covercache.h"
#include "design.h"
#include "romm.h"
#include "screens.h"

struct Library {
    std::vector<design::Card> cards;
    // The games behind the cards, same order. A card is what is drawn; this is
    // what is launched, and the launch needs the platform and the file name.
    std::vector<romm::Game> games;
    // Index into `cards`, or -1. The hero is the most recently played game THIS
    // CONSOLE CAN PLAY — not simply the most recent, because a Resume that
    // cannot run is worse than no hero at all.
    int heroIndex = -1;
    std::string heroPlatform;
    // Indices into `cards`, in recency order, with the hero removed — so the
    // shelf never shows the same cover twice and never loses one. Home shows
    // THIS, not the whole library: the library belongs to the Library screen.
    std::vector<int> shelf;
    // The second shelf. Drawn only when there are any: an empty Favorites row
    // is worse than no Favorites row.
    std::vector<int> favorites;

    // What the Library screen shows. EVERY platform the server holds, including
    // the ones this console cannot play — those carry their reason instead of a
    // count and cannot be opened. docs/PROJECT.md is explicit that the answer is
    // neither "show everything" nor "hide quietly": somebody who owns Switch
    // games and sees nothing will reasonably conclude the scan failed.
    std::vector<screens::Tile> platformTiles;
    std::vector<screens::Tile> collectionTiles;
    // Every platform's full name, qualified where two share one ("Arcade
    // (FinalBurn Neo)"), for anything that names a system outside a tile:
    // Settings' Downloads groups by it.
    std::map<int, std::string> platformNames;
    // What a tile's cached cover is validated against: the platform's own
    // `updated_at` and `rom_count`, as the server reported them this boot.
    // Filled for playable platforms only, because nothing else gets a cover.
    std::map<int, covercache::Tile> tileCache;
    // Every game this console has fetched so far, keyed by rom id, so a game
    // that is recent AND a favourite AND in a platform's grid is one card
    // rather than three. THE VALUE IS AN INDEX INTO `cards`, which is why
    // `cards` is append-only — see below.
    std::map<int, int> byRomId;
};

int appendGame(Library& lib, const romm::Game& g);
bool loadTileGames(romm::Client& client, Library& lib, screens::Tile& tile,
                   bool* more = nullptr);
Library loadLibrary(romm::Client& client);
Library offlineLibrary();
std::string gameRecordJson(const romm::Game& g);
