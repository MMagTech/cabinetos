// Does every option value the console sets exist in the core it is set on?
//
// THE OTHER HALF OF THE VERSION-BUMP CHECK (#63, MMagTech 2026-10-02).
// build-core.sh fails when a core's options differ from cores/options/<core>.txt;
// this fails when the console's own tables (catalog::optionOverrides,
// quality::coreOptions at every level and on both N64 renderers, and every
// choice of every sysopts row) name a key a core does not declare, or a value
// it does not accept, according to those same files. Run by CI on every
// frontend build, with no core and no GPU:
//
//   cabinetos-frontend --check-option-tables cores/options
//
// Exits 0 when everything fits, 1 with one line per misfit otherwise.
//
// One gap, said plainly: catalog's N64 renderer choice is only made where
// there is Vulkan, so on a machine without it that line is not checked here.
// The console's own launch-time check (core.cpp, checkOverrides) covers it on
// every real console.

#pragma once

#include <string>

namespace optcheck {

int run(const std::string& dir);

}  // namespace optcheck
