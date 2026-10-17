// What this console still owes RomM: a marker on the disk for each save,
// state and screenshot not yet uploaded, so it survives a crash or a power
// cut. cache.h includes this; moved out of it in the restructure (#398).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "users.h"

namespace cache {

// --- What has not reached RomM yet ------------------------------------------
//
// The one genuinely irreplaceable thing on the machine. A ROM comes back, a
// state that has been uploaded comes back, and a state written two minutes ago
// on a console that cannot see its server comes back from nowhere.
//
// Eviction never takes save data — it cannot, it only walks `cache/` — so this
// is not protecting files from the evictor. It is making "unsynced" a FACT ON
// DISK rather than something only the running process knows. A marker is
// written before an upload is attempted and removed when it succeeds, so a
// queue interrupted by a crash or a power cut is still visible on the next
// boot, and its bytes still count against the save floor.
//
// Per user, because the thing it is tracking is a save.
//
// A MARKER SAYS WHAT TO SEND, NOT ONLY THAT SOMETHING IS OWED (2026-09-26).
// It used to hold the byte count and nothing else, so nothing could ever send
// it again: a state saved while the server was away stayed on this console
// for good, and a save went only if the game changed it next time. Now it
// names the kind, the emulator tag, the name it travels under and the file on
// disk (and a state's picture), and owed() reads them back so the uploader
// can try again. The byte count stays on the first line, where pendingBytes()
// reads it. A marker from before this has only that line, and stays owed.
struct Owed {
    int romId = 0;
    bool isState = false;
    bool isScreenshot = false;   // #79; `emulator` is then just "screenshot"
    std::string emulator;
    std::string fileName;     // the name it travels under
    std::string localPath;    // the bytes to send
    std::string shotName;     // a state's picture: its name, and where it is
    std::string shotPath;
};
void markPending(const storage::User& u, const Owed& o, int64_t bytes);

// Everything this person's console still owes the server that can be sent
// again, oldest first.
std::vector<Owed> owed(const storage::User& u);
void clearPending(const storage::User& u, int romId, const std::string& fileName);

// Whether this console still owes the server this exact file. Read off the
// disk like everything else here, so it survives the console being switched
// off between the save and the upload.
//
// IT IS WHAT STOPS A LAUNCH UNDOING A SAVE. A game saved with no network
// leaves a newer file on this machine than anything the server holds; without
// this the next launch would fetch the server's older copy and write it over
// the top, and the person would lose the session they made offline.
bool isPending(const storage::User& u, int romId, const std::string& fileName);

// Across everybody on the machine: the floor is a fact about the disk, and the
// disk does not care whose upload is queued on it.
int64_t pendingBytes();

// How many things this one person still owes the server, sendable or not (a
// marker from before 2026-09-26 cannot be sent again but is still owed).
// Removing a person (#194) asks before throwing any of them away.
int pendingCount(const storage::User& u);

}  // namespace cache
