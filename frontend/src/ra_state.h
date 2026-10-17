// What RetroAchievements' files share: the sign-in, owned by achievements.cpp.
// Internal: only achievements.cpp include it. Moved out of achievements.cpp's
// anonymous namespace in the restructure (#398).

#pragma once

#include <string>

#include "achievements.h"

#include "rc_client.h"

namespace ra {

// Defined in achievements.cpp, one each: rcheevos' client, the sign-in (the
// person, their RetroAchievements name and token, and where the sign-in
// stands) and --ra-sample's switch.
extern rc_client_t* gClient;
extern int gAccount;
extern std::string gUser;
extern std::string gToken;
extern bool gLoginBusy;
extern bool gLoggedIn;
extern bool gSample;

}  // namespace ra
