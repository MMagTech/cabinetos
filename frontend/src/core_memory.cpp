// The emulated machine's memory: save states, the battery save, the memory
// regions, and the memory map a core hands over for RetroAchievements
// (environmentMemory). Moved out of core.cpp in the restructure (#398).
// Core::loadGame (core.cpp) clears the map before retro_load_game, because a
// core hands its map over inside it.

#include "core.h"
#include "core_state.h"

#include <algorithm>
#include <cstring>
#include <optional>
#include <string>
#include <vector>

#include "libretro.h"

namespace cab {
namespace corestate {

// THE CORE'S MEMORY MAP, KEPT FOR RETROACHIEVEMENTS (#74). A core hands it
// over once, inside retro_load_game, and owns the descriptors' storage only
// for the length of that call; the pointers INTO the emulated machine stay
// valid until the game is unloaded. So the descriptors are copied, and the
// address-space names with them. rcheevos' rc_libretro reads this the way
// RetroArch hands it over. Cleared with the game.
std::vector<retro_memory_descriptor> gMemDescs;
std::vector<std::string> gMemNames;
retro_memory_map gMemMap{nullptr, 0};
// Counts the maps handed over. Dolphin sends its map from inside the first
// retro_run, not from retro_load_game, so whoever laid memory out from an
// earlier one does it again when this moves.
unsigned gMemMapGeneration = 0;

void keepMemoryMap(const retro_memory_map* mm) {
    ++gMemMapGeneration;
    gMemDescs.clear();
    gMemNames.clear();
    gMemMap = {nullptr, 0};
    if (!mm || !mm->descriptors || mm->num_descriptors == 0) return;
    gMemDescs.assign(mm->descriptors, mm->descriptors + mm->num_descriptors);
    // Reserved first: the descriptors point at these strings, so the vector
    // must not move once they do.
    gMemNames.reserve(gMemDescs.size());
    for (retro_memory_descriptor& d : gMemDescs) {
        gMemNames.emplace_back(d.addrspace ? d.addrspace : "");
        d.addrspace = d.addrspace ? gMemNames.back().c_str() : nullptr;
    }
    gMemMap.descriptors = gMemDescs.data();
    gMemMap.num_descriptors = static_cast<unsigned>(gMemDescs.size());
}

// The memory-map call of the environment callback. core.cpp's environment
// asks here before its own switch; nothing for any other call.
std::optional<bool> environmentMemory(unsigned cmd, void* data) {
    switch (cmd) {
        case RETRO_ENVIRONMENT_SET_MEMORY_MAPS:
            keepMemoryMap(static_cast<const retro_memory_map*>(data));
            return true;

        default:
            return std::nullopt;
    }
}

}  // namespace corestate

using namespace corestate;

size_t Core::stateSize() const {
    return (gameLoaded_ && g.serialize_size) ? g.serialize_size() : 0;
}

bool Core::saveState(std::vector<uint8_t>& out) {
    if (!gameLoaded_ || !g.serialize_size || !g.serialize) return false;
    const size_t n = g.serialize_size();
    if (n == 0) return false;  // some cores genuinely cannot; that is not an error
    out.resize(n);
    return g.serialize(out.data(), n);
}

size_t Core::saveRamSize() const {
    if (!gameLoaded_ || !g.get_memory_size) return 0;
    return g.get_memory_size(RETRO_MEMORY_SAVE_RAM);
}

bool Core::readSaveRam(std::vector<uint8_t>& out) const {
    out.clear();
    const size_t n = saveRamSize();
    if (n == 0 || !g.get_memory_data) return false;   // no battery in this cart
    const void* p = g.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!p) return false;
    out.resize(n);
    std::memcpy(out.data(), p, n);
    return true;
}

bool Core::writeSaveRam(const std::vector<uint8_t>& data) {
    const size_t n = saveRamSize();
    if (n == 0 || data.empty() || !g.get_memory_data) return false;
    void* p = g.get_memory_data(RETRO_MEMORY_SAVE_RAM);
    if (!p) return false;
    // Sizes can differ between a save written elsewhere and what this core
    // exposes. Copy what fits rather than refusing: a short save is a smaller
    // cart image and the remainder is already zeroed, and a long one is not
    // ours to truncate silently — but dropping the tail is still better than
    // losing the save entirely.
    std::memcpy(p, data.data(), std::min(n, data.size()));
    return true;
}

bool Core::loadState(const std::vector<uint8_t>& data) {
    if (!gameLoaded_ || !g.unserialize || data.empty()) return false;
    return g.unserialize(data.data(), data.size());
}

bool Core::saveRAM(std::vector<uint8_t>& out) const {
    return memoryRegion(RETRO_MEMORY_SAVE_RAM, out);
}

bool Core::memoryRegion(unsigned id, std::vector<uint8_t>& out) const {
    if (!gameLoaded_ || !g.get_memory_data || !g.get_memory_size) return false;
    const size_t n = g.get_memory_size(id);
    const void* p = g.get_memory_data(id);
    if (!p || n == 0) return false;
    out.assign(static_cast<const uint8_t*>(p), static_cast<const uint8_t*>(p) + n);
    return true;
}

const retro_memory_map* Core::memoryMap() const {
    return (gameLoaded_ && !gIsPs2 && gMemMap.num_descriptors > 0) ? &gMemMap : nullptr;
}

unsigned Core::memoryMapGeneration() const { return gMemMapGeneration; }

uint8_t* Core::memoryPointer(unsigned id, size_t* size) const {
    *size = 0;
    if (!gameLoaded_ || gIsPs2 || !g.get_memory_data || !g.get_memory_size) return nullptr;
    void* p = g.get_memory_data(id);
    const size_t n = g.get_memory_size(id);
    if (!p || n == 0) return nullptr;
    *size = n;
    return static_cast<uint8_t*>(p);
}

}  // namespace cab
