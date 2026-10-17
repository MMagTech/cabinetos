// Where this console's games can be: its own storage and every drive plugged
// in (locations), how much room each has (spaceOf) and whether it can be
// unplugged (isExternal). storage.h includes this; moved out of it in the
// restructure (#398).

#pragma once

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <vector>

namespace storage {

// --- Storage locations ------------------------------------------------------
//
// `roms/` and `cache/` repeat on EVERY location rather than living once at the
// root, and that is not tidiness. Releasing the last keep on a game demotes it
// to the cache instead of deleting it, and a demotion has to be a rename inside
// one filesystem; with kept games on a big drive and one cache at the root, it
// would mean physically copying gigabytes between disks because somebody
// changed their mind. A rename cannot cross a filesystem — the kernel returns
// EXDEV — so this is a fact the layout has to carry, not a preference.
//
// PLUG IT IN AND IT IS USED. There is no setup screen and no confirmation,
// which is the Nintendo Switch's answer and the one open question 14 settled on:
// put a card in a Switch and it becomes where downloads go, full stop. So this
// LOOKS for drives every time it is asked rather than being told about them
// once.
//
// AND IT NEVER TAKES OVER A DRIVE. One folder named `CabinetOS/` on it, and
// nothing outside that folder is read, written or counted. A drive with
// somebody's films on it works as a games drive too, and unplugging it takes
// their films back untouched.
//
// THE FIRST ENTRY IS ALWAYS THE INTERNAL ROOT and is always present. Everything
// after it is a drive somebody plugged in, and any of them may vanish between
// one call and the next — which is fine, because nothing here remembers.
std::vector<std::string> locations();

// Where the cache lives, and everything that is not a game: saves, keeps,
// firmware, configuration. The internal disk, always.
const std::string& primaryLocation();


// A drive that was here the last time the console looked and is not here now,
// or empty.
//
// THE ONE THING THAT IS REMEMBERED, and only so the console can say a true
// sentence out loud. It is never used to decide where a file is — that is
// always read off the disk — because a remembered list that has gone stale is
// exactly the fault this design exists to avoid: pull the card out of a Steam
// Deck and it still shows the games as installed, with a Play button that does
// nothing.
//
// It reports a given drive ONCE. Having said it, the console forgets that drive
// and stops mentioning it, because a machine that complains about a drive you
// removed on purpose every time it boots is worse than one that says nothing.
std::string missingDriveToReport();

// What Settings > Storage shows for each location: how much room, and whether
// it is a USB drive. Read fresh, like everything else here.
//
// USB IS READ FROM WHERE THE KERNEL HUNG THE DEVICE, not from its name: the
// block device's sysfs path runs through the USB controller when it is on one.
// A second SATA or NVMe disk is "Internal" and a stick is "External",
// MMagTech's names, 2026-09-24.
struct Space {
    bool ok = false;
    int64_t freeBytes = 0;
    int64_t totalBytes = 0;
};
Space spaceOf(const std::string& location);
// A drive that can be unplugged while the console runs: USB, Thunderbolt, an
// SD card. What Storage calls "External" and offers Eject for.
bool isExternal(const std::string& location);
// The same question about a block device, before anything is mounted from it.
bool isExternalDevice(dev_t device);

}  // namespace storage
