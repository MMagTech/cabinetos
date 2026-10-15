// Favourites not yet on the server (#267): rom id to wanted state, per person,
// in favorites-owed.json. The lock is favorites.cpp's own and there is one;
// the frame thread, the upload worker and the online reload all take it.
// Moved out of main.cpp in the restructure (#398).

#pragma once

#include <map>
#include <string>

#include "romm.h"
#include "storage.h"

std::map<int, bool> favOwed(const storage::User& u);
void oweFavorite(const storage::User& u, int romId, bool on);
bool sendFavoritesAs(romm::Client& c, const storage::User& who, std::string* err);
