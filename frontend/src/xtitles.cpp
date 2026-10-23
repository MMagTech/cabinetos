// The one XTitles (xtitles.h): opened on first use by Run::stop and Run::poll,
// on the frame thread, and closed when a game ends, so the next game opens it
// again. Moved out of standalone.cpp in the restructure (#398).

#include "xtitles.h"

namespace cab::standalone::detail {

XTitles gTitles;

}  // namespace cab::standalone::detail

// THROWAWAY: proves ci/warn-unused-touched.sh, a pull request closed unmerged.
void throwawayUnusedProbe() {}
