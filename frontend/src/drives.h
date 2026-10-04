// Extra drives: mounting one when it appears, and Eject.
// docs/SETTINGS.md, Storage; issue #67.
//
// WHY THIS EXISTS. storage::locations() has always looked for a drive in
// /run/media/<user>/, which is where udisks mounts one. But udisks only mounts
// when somebody asks it to, and on a desktop the somebody is the desktop's
// automounter. The image strips the desktop, so on the A9 a drive plugged in
// was seen by the kernel and mounted by nothing, and Storage showed only the
// main drive. Found 2026-09-25 with a 4 TB Samsung T9.
//
// So the console asks udisks itself, over D-Bus, for every drive but its own,
// internal or external alike (MMagTech, 2026-09-25: an extra drive is an
// extra drive). Decided the same day, and changed 2026-10-04 (#236):
//
//   - exFAT, NTFS AND ext4. Anything else, and a blank drive, is "isn't
//     exFAT, NTFS or ext4". ext4 because only a Linux filesystem holds
//     Steam's Windows games properly (Proton's per-game files have names
//     exFAT does not allow), and one drive can then hold both.
//   - FORMAT ANY DRIVE THE CONSOLE IS NOT USING FOR ITS GAMES, as a PS5 or an
//     Xbox does: "I got this new drive, and I want to just plug it into my
//     CabinetOS and go" (MMagTech, 2026-10-04). Using it for its games means
//     a file under CabinetOS/ on it; the console's empty folders do not
//     count. Never the console's own drive, never a drive mounted by
//     something other than the console (an fstab line, by hand), never one
//     with a filesystem the console reads but could not mount (it cannot see
//     what is in it). It makes one ext4 partition holding CabinetOS/ and
//     SteamLibrary/. Replaces "a blank drive only" (2026-09-25).
//   - Windows' own partitions (EFI, reserved, recovery) are never mounted.
//   - Notices are for external drives. An internal one that cannot be used
//     would otherwise say so at every boot; it is simply not in Storage.
//   - THE CONSOLE IS THE ONLY THING THAT MOUNTS A DRIVE. Bazzite's own
//     automounters (internal ext4 at boot, SD cards, "steamgames") are
//     removed from the image (strip-desktop.sh): each would take a drive
//     somewhere the console does not look, or race it.
//
// Mounting a removable filesystem and powering a drive off are `allow_active:
// yes` in udisks' stock policy; an internal one is `filesystem-mount-system`,
// granted by 63-cabinetos-drives.rules. The console is the active session on
// seat0, and over SSH none of it is allowed, which is correct.
//
// Everything here runs on one worker thread with its own bus connection; the
// frame loop only calls poll().

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace drives {

// Mount whatever is attached now, silently, then watch for drives coming and
// going. Waits for the first pass so the storage tree built after it sees the
// drives. Does nothing when this process is not the console on its own seat (a
// headless run over SSH), because udisks would refuse it anyway.
void start();

enum class Event {
    None,
    Connected,      // mounted and usable: CabinetOS/ is on it and writable
    WrongFormat,    // not exFAT, NTFS or ext4, or blank
    CouldNotUse,    // exFAT, NTFS or ext4, and it would not mount or cannot be written
    Removed,        // pulled out without Eject
    SafeToUnplug,   // Eject finished: nothing on the machine has it open
    EjectFailed,    // something still has it open
    FormatFailed,   // Format did not finish; the drive may be blank or half done
    Changed,        // no notice; the list of unusable drives changed
};

struct Notice {
    Event event = Event::None;
    bool external = true;
};

// Non-blocking; call once a frame. One notice per call.
Notice poll();

// DRIVES THE CONSOLE FOUND AND CANNOT USE, for Storage to list greyed out with
// the reason. Without it a blank SSD fitted inside the PC would be invisible:
// internal drives get no notices. MMagTech, 2026-09-25.
struct Unusable {
    std::string id;            // the drive, for format()
    std::string model;         // "SanDisk 3.2 Gen1", for the confirm
    bool external = true;
    bool wrongFormat = true;   // false: exFAT, NTFS or ext4 that would not mount
    bool blank = false;        // nothing on it at all
    uint64_t sizeBytes = 0;
};
std::vector<Unusable> unusable();

// Eject the drive a storage location is on: unmount every filesystem on it,
// check nothing on the machine still has it mounted, then power it off. On the
// worker; the answer arrives through poll().
void eject(const std::string& location);

// True from eject() until its answer has been taken by poll().
bool ejecting();

// EVERY DRIVE BUT THE CONSOLE'S OWN, in use or not, with what Format needs
// to know about it. Rebuilt from udisks whenever a drive comes, goes or
// changes; a Changed notice follows.
struct Drive {
    std::string id;
    std::string model;
    bool external = true;
    uint64_t sizeBytes = 0;
    // The console's mounts of its filesystems, under /run/media/<user>/.
    std::vector<std::string> mounts;
    // Filesystems on it the console does not read: another format, an
    // encrypted one, Windows' EFI partition.
    int otherParts = 0;
    // Mounted by something other than the console (an fstab line, by hand):
    // somebody's own arrangement, never formatted.
    bool heldElsewhere = false;
    // A filesystem the console reads that is not mounted (it would not
    // mount): what is in it cannot be checked, so it is never formatted.
    bool unchecked = false;
};
std::vector<Drive> found();

// The drive a storage location is on, from found(); nullptr if none.
const Drive* driveAt(const std::vector<Drive>& all, const std::string& location);

// FORMAT IS OFFERED: not held elsewhere, nothing unchecked, and no file under
// CabinetOS/ on any of its mounts. Reads the drive (stops at the first file).
bool mayFormat(const Drive& d);

// WHAT IS ON IT, for the confirm. Counted on the calling thread, at most half
// a second; `counted` false means it ran out of time or met a folder it could
// not open, and `bytes` is then the space its filesystems have in use.
struct Contents {
    bool counted = true;
    int64_t files = 0;
    int64_t bytes = 0;
    int otherParts = 0;
};
Contents contents(const Drive& d);

// FORMAT A DRIVE as one ext4 partition named "Games" ("Games 2" and on if a
// drive of that name is attached), holding CabinetOS/ and SteamLibrary/,
// after checking again, from udisks, everything mayFormat checked. Its
// filesystems are unmounted first, File access's view of them included. On
// the worker. Success is the drive then mounting as any drive does, with
// "connected"; failure is FormatFailed.
void format(const std::string& driveId);
bool formatting();

}  // namespace drives
