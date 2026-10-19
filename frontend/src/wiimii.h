// This person's Miis into a Wii game's NAND before it boots, from the Mii
// Channel's own save (#275). Moved out of main.cpp in the restructure (#398).

#pragma once

#include <string>

#include "romm.h"
#include "saves.h"

// THE MII CHANNEL ON THIS SERVER: the Wii game whose title is the Mii
// Channel's, asked of RomM online and remembered for when it is away. On the
// launch worker (launch_routes.cpp), which then fetches its save with the
// game's (fetchSaves). False when there is none.
bool findMiiChannel(romm::Client& client, int platformId, int* romId, std::string* title);

// On the frame thread, before the game loads, from what the worker fetched
// for the Mii Channel (`mii`, its romId 0 when there is none).
void copyMiisInto(const SaveFetch& mii, const std::string& platformFsSlug,
                  const std::string& core, const std::string& saveDir);
