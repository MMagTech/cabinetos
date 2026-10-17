// The core's disc drive (#162): the interface a core registers
// (environmentDiscs) and the Core calls the pause menu's disc rows make.
// Moved out of core.cpp in the restructure (#398). The start disc is asked of
// the core in Core::loadGame (core.cpp), between the core registering its
// drive and retro_load_game, and the drive is forgotten with the core in
// Core::load and Core::unload.

#include "core.h"
#include "core_state.h"

#include <optional>
#include <string>

#include "libretro.h"

namespace cab {
namespace corestate {

// THE CORE'S DISC DRIVE (#162): eject, put a disc in, close, and the start-
// on-disc call, as a core hands them over. Version 0 cores (Genesis Plus GX)
// give the first seven only; the rest stay null. Saturn and Dolphin register
// theirs from inside retro_load_game, so this is read after the load as
// well as before it. Cleared with the core, never with the game.
retro_disk_control_ext_callback gDisk{};
bool gDiskSet = false;
// Asked for before the load: the list's index and the exact path the core
// will build for it (RetroArch hands back what get_image_path said).
int gInitialDisc = -1;
std::string gInitialDiscPath;

// The disc calls of the environment callback. core.cpp's environment asks
// here before its own switch; nothing for any other call.
std::optional<bool> environmentDiscs(unsigned cmd, void* data) {
    switch (cmd) {
        // THE DISC DRIVE (#162). Version 1: the extended interface, with the
        // start-on-disc call and each disc's path. Answering nothing here is
        // what left every core on its first disc until 2026-10-11.
        case RETRO_ENVIRONMENT_GET_DISK_CONTROL_INTERFACE_VERSION:
            *static_cast<unsigned*>(data) = 1;
            return true;
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_INTERFACE: {
            const auto* cb = static_cast<const retro_disk_control_callback*>(data);
            if (!cb) return false;
            gDisk = {};
            gDisk.set_eject_state = cb->set_eject_state;
            gDisk.get_eject_state = cb->get_eject_state;
            gDisk.get_image_index = cb->get_image_index;
            gDisk.set_image_index = cb->set_image_index;
            gDisk.get_num_images = cb->get_num_images;
            gDisk.replace_image_index = cb->replace_image_index;
            gDisk.add_image_index = cb->add_image_index;
            gDiskSet = true;
            return true;
        }
        case RETRO_ENVIRONMENT_SET_DISK_CONTROL_EXT_INTERFACE: {
            const auto* cb = static_cast<const retro_disk_control_ext_callback*>(data);
            if (!cb) return false;
            gDisk = *cb;
            gDiskSet = true;
            return true;
        }

        default:
            return std::nullopt;
    }
}

}  // namespace corestate

using namespace corestate;

bool Core::hasDiscControl() const {
    return !gIsPs2 && gDiskSet && gDisk.get_num_images && gDisk.set_image_index &&
           gDisk.set_eject_state;
}
int Core::discCount() const {
    return hasDiscControl() && gameLoaded_ ? static_cast<int>(gDisk.get_num_images()) : 0;
}
int Core::discIndex() const {
    return gDiskSet && gDisk.get_image_index && gameLoaded_
               ? static_cast<int>(gDisk.get_image_index())
               : -1;
}
bool Core::discEjected() const {
    return gDiskSet && gDisk.get_eject_state && gameLoaded_ && gDisk.get_eject_state();
}
bool Core::setDiscEjected(bool ejected) {
    if (!hasDiscControl() || !gameLoaded_) return false;
    return gDisk.set_eject_state(ejected);
}
bool Core::setDiscIndex(int index) {
    if (!hasDiscControl() || !gameLoaded_ || index < 0) return false;
    return gDisk.set_image_index(static_cast<unsigned>(index));
}
std::string Core::discPath(int index) const {
    if (!gDiskSet || !gDisk.get_image_path || index < 0) return {};
    char buf[4096] = {};
    if (!gDisk.get_image_path(static_cast<unsigned>(index), buf, sizeof buf)) return {};
    return buf;
}
void Core::setInitialDisc(int index, const std::string& path) {
    gInitialDisc = index;
    gInitialDiscPath = path;
}

}  // namespace cab
