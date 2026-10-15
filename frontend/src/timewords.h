// Times as RomM writes them and as the game's page says them ("Today,
// 8:13 PM"), and a game's year, maker and players kept for the page offline.
// Used on the game page's worker. Moved out of main.cpp in the restructure
// (#398).

#pragma once

#include <ctime>
#include <string>

#include "romm.h"

std::string isoUtc(time_t t);
void rememberFacts(int romId, const romm::Client::Facts& f);
bool knownFacts(int romId, romm::Client::Facts* f);
std::string stateWhen(const std::string& iso);
