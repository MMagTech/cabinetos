// A save state file's layout: the core's own bytes, alone or in RetroArch's
// RASTATE blocks with the achievement progress beside them (#279).
//
// RETROARCH'S LAYOUT, TO THE BYTE (tasks/task_save.c, RASTATE version 1,
// read 2026-10-18): "RASTATE" and a version byte of 1, then blocks, each a
// four-letter name, a 32-bit little-endian length and the data padded with
// zeros to a multiple of 8. "MEM " holds the core's state exactly as
// retro_serialize made it, "ACHV" rcheevos' progress
// (rc_client_serialize_progress_sized), and "END " with no data ends it.
// RetroArch loads either kind (content_deserialize_state): a file that does
// not start with "RASTATE" is all core data, which is what every state this
// console wrote before #279 is.
//
// ONLY A STATE WITH PROGRESS IS WRAPPED. Somebody not signed in to
// RetroAchievements, or a game it has no set for, writes the core's bytes
// alone, as before, so their states load as they always have everywhere.
// The core's bytes are never changed, only put inside the blocks.

#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace statefile {

// The core's state and the achievement progress, as one file. `progress`
// empty: the core's bytes as they are.
std::vector<uint8_t> join(const std::vector<uint8_t>& core,
                          const std::vector<uint8_t>& progress);

// What a state file holds, pointing into it. Core data alone has no
// progress (null, 0).
struct Parts {
    bool wrapped = false;   // RetroArch's blocks, not core data alone
    const uint8_t* core = nullptr;
    size_t coreSize = 0;
    const uint8_t* progress = nullptr;
    size_t progressSize = 0;
};
// False, with a line on stderr, for blocks that do not hold together (a
// length past the end, no core block, a version other than 1): a state cut
// short or from somewhere else, which is refused rather than handed to the
// core. Blocks it does not know (RetroArch's replay, "RPLY") are passed over.
bool split(const std::vector<uint8_t>& file, Parts* out);

}  // namespace statefile
