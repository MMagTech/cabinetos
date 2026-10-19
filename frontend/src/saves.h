// Saves, and which way they travel: the copy of RomM's rows, a directory save
// zipped and unzipped, the files a core writes, and the battery a core
// exposes. A save is written to this console before any upload is tried.
// Moved out of main.cpp in the restructure (#398).

#pragma once

#include <atomic>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "catalog.h"
#include "core.h"
#include "dirsave.h"
#include "filesave.h"
#include "gamesession.h"
#include "romm.h"
#include "standalone.h"
#include "storage.h"
#include "uploader.h"

bool savesFor(romm::Client& client, const storage::User& user, int romId,
              std::vector<romm::Asset>* rows, std::string* err);
std::vector<uint8_t> saveBytesFor(romm::Client& client, const storage::User& user, int romId,
                                  const romm::Asset& row);

// WHAT A RESTORE NEEDS FROM RomM, fetched on the launch worker once the game
// is on the disk (#481, LAUNCH-17): the rows RomM holds for one game and the
// bytes of each row a restore may pick. The stages behind the curtain
// (launch.cpp) write what was fetched and never ask the server, so a slow or
// hanging server holds the launch's busy clock, not the picture. One per game
// a launch reads saves for: the game's own, and the Mii Channel's for a Wii
// game (wiimii.h).
struct SaveFetch {
    int romId = 0;
    std::string title;       // the Mii Channel's, for the name its zip is owed under
    bool asked = false;      // the worker asked: a token, and a tag the saves travel under
    bool listed = false;     // RomM, or its copy offline, answered
    std::string error;       // why not, when it did not
    std::vector<romm::Asset> rows;
    std::map<int, std::vector<uint8_t>> bytes;   // by row id, for the rows a restore may pick
    // savemirror::mark() taken before the ask. A save of this game that this
    // console sends after it makes these rows older than the console's own
    // copy (saves.cpp, fetchIsStale).
    uint64_t mark = 0;
};
// The worker's half of a restore: the rows, then the bytes of the newest row
// under `tag` in each region (the file saves and the battery) and of the
// newest of any kind (a folder save). Stops between requests once `cancel`
// is set. Asks nothing without a tag or a token, as the restores did.
void fetchSaves(romm::Client& client, const storage::User& user, int romId, const char* tag,
                const std::atomic<bool>* cancel, SaveFetch* out);
void restoreDirSave(const SaveFetch& fetched, const char* tag, const std::string& root,
                    const std::string& title, const std::string& oldCard = std::string());
std::vector<cab::DirEntry> listDirSave(const GameSession& sess);
void syncDirSave(GameSession& sess, Uploader& up);
void finishInterruptedSave(const cab::standalone::Emulator& emu, const std::string& saveDir,
                           Uploader& up);
std::string saveRowName(const std::string& fsStem, const std::string& coreRowName,
                        const std::string& region);
std::string regionOfRow(const std::string& fileName);
std::vector<cab::FileSaveState> restoreFileSaves(
        const std::vector<catalog::SaveFile>& specs, const std::string& fsStem,
        const std::string& saveDir, const char* tag, const SaveFetch& fetched);
void syncFileSaves(GameSession& sess, Uploader& up);
void syncSave(GameSession& sess, Uploader& up);
void restoreBatterySave(cab::Core& core, GameSession& session, const std::string& saveDir,
                        const storage::User& user, const SaveFetch& fetched);
