// This person's Miis into a Wii game's NAND before it boots, from the Mii
// Channel's own save (#275). Moved out of main.cpp in the restructure (#398).

#pragma once

#include <string>

#include "romm.h"

void copyMiisInto(romm::Client& client, int platformId, const std::string& platformFsSlug,
                  const std::string& core, const std::string& saveDir);
