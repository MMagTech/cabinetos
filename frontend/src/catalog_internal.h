// What catalog.cpp shares with the files split out of it (coreoptions.cpp,
// savetable.cpp): the manifest's name for a core, which only catalog.cpp's
// table can answer. Internal: nothing outside the three includes it.

#pragma once

#include <string>

namespace catalog {

// Defined in catalog.cpp, with what it does and why.
std::string manifestName(const std::string& core);

}  // namespace catalog
