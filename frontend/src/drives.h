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
// extra drive). Decided the same day:
//
//   - ONLY exFAT AND NTFS. Anything else, and a blank drive, is "isn't exFAT
//     or NTFS".
//   - FORMAT ONLY A BLANK DRIVE: no partition table and no filesystem, which
//     is what a new SSD is. A drive with anything on it, even something the
//     console cannot read, is never offered it. Asked for because a blank SSD
//     fitted inside the PC cannot be formatted anywhere else; guarded because
//     a formatting bug is a wiped drive (MMagTech, 2026-09-25). The check is
//     made when the drive is listed and again right before the format.
//   - Windows' own partitions (EFI, reserved, recovery) are never mounted.
//   - Notices are for external drives. An internal one that cannot be used
//     would otherwise say so at every boot; it is simply not in Storage.
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
    WrongFormat,    // not exFAT or NTFS, or blank
    CouldNotUse,    // exFAT or NTFS, and it would not mount or cannot be written
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
    bool wrongFormat = true;   // false: exFAT or NTFS that would not mount
    bool blank = false;        // nothing on it at all: Format is offered
    uint64_t sizeBytes = 0;
};
std::vector<Unusable> unusable();

// Eject the drive a storage location is on: unmount every filesystem on it,
// check nothing on the machine still has it mounted, then power it off. On the
// worker; the answer arrives through poll().
void eject(const std::string& location);

// True from eject() until its answer has been taken by poll().
bool ejecting();

// Format a BLANK drive (Unusable::blank) as one exFAT partition named
// "Games" (or "Games 2" and on), after checking again that it is blank and not the console's own.
// On the worker. Success is the drive then mounting as any drive does, with
// "connected"; failure is FormatFailed.
void format(const std::string& driveId);
bool formatting();

}  // namespace drives
