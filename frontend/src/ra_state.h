// What RetroAchievements' files share: the sign-in, owned by achievements.cpp
// and the network (ra_net.cpp). Internal: only achievements.cpp and ra_net.cpp
// include it. Moved out of achievements.cpp's anonymous namespace in the
// restructure (#398).

#pragma once

#include <deque>
#include <functional>
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

// The network: one worker, in order (ra_net.cpp).
struct Job {
    std::string url;
    std::string post;
    bool isPost = false;
    std::string contentType;
    // Runs on the frame thread with the status and the body.
    std::function<void(int, const std::string&)> done;
};
void send(const rc_api_request_t& r, std::function<void(int, const std::string&)> done);
void setUserAgent(const std::string& consoleVersion, const char* clause);
void setUserAgentCore(const std::string& core);
void clearUserAgentCore();
void startWorker();
std::deque<Job> stopWorker();
void runAnswers();
void dropAnswers();

}  // namespace ra
