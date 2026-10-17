// Saves, and which way they travel: the copy of RomM's rows, a directory save
// zipped and unzipped, the files a core writes, and the battery a core
// exposes. A save is written to this console before any upload is tried.
// Moved out of main.cpp in the restructure (#398).

#pragma once

#include <cstdint>
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

bool savesFor(romm::Client& client, int romId, std::vector<romm::Asset>* rows,
              std::string* err);
std::vector<uint8_t> saveBytesFor(romm::Client& client, int romId, const romm::Asset& row);
void restoreDirSave(romm::Client& client, int romId, const char* tag, const std::string& root,
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
        const std::string& saveDir, int romId, const char* tag, romm::Client& client);
void syncFileSaves(GameSession& sess, Uploader& up);
void syncSave(GameSession& sess, Uploader& up);
void restoreBatterySave(cab::Core& core, GameSession& session, const std::string& saveDir,
                        const storage::User& user, romm::Client& client);
