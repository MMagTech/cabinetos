// What offline will need, fetched while online, and the server tried in the
// background while it is away (#88). Both run on detached threads with
// clients of their own. Moved out of main.cpp in the restructure (#398); the
// rest of offline comes with the frame loop in batch (g).

#pragma once

#include <string>

void backfillDrive(const std::string& address);
void startReconnect(const std::string& address);
