// What the standalone emulators' files share: the helpers, defined in
// standalone.cpp, and each emulator's own entry points, defined in its file.
// Internal: only standalone.cpp, standalone_eden.cpp and standalone_rpcs3.cpp
// include it. Moved out of standalone.cpp's anonymous namespace in the
// restructure (#398).

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "standalone.h"

namespace cab::standalone::detail {

// Where a game's folder records what was installed into it and deleted.
constexpr const char* kInstalledNote = ".cabinetos-installed";
// The note a folder holds from the start of a game until its saves are sent
// (beforeStart): a game's folder for xemu, a person's for the others.
// cache.cpp spells it out as well, to leave a game's folder holding one alone.
constexpr const char* kPlayingNote = ".cabinetos-playing";
// RPCS3's one user, as it names the folder. The console's people are told
// apart by whose save folder `hdd0` is, not by PS3 users.
constexpr const char* kPs3User = "00000001";

// Defined in standalone.cpp, each with what it does and why.
std::string binaryOf(const Emulator& e);
int64_t nowMs();
bool isDir(const std::string& p);
std::string readFile(const std::string& path);
bool writeFile(const std::string& path, const std::string& data);
bool placeFile(const std::string& from, const std::string& to);
struct IniSet {
    const char* section;
    const char* key;
    const char* value;
};
std::string applyIni(const std::string& text, const std::vector<IniSet>& sets,
                     bool withDefaults = true);
bool isFile(const std::string& p);
bool removeTree(const std::string& path);
std::vector<std::string> listDir(const std::string& dir);
bool endsWithNoCase(const std::string& s, const char* suffix);
std::string yamlQuote(const std::string& v);

// Switch (standalone_eden.cpp).
bool prepareEden(const Emulator& e, const std::string& saveDir, const std::string& player,
                 int players, quality::Level level, bool* missingKeys, std::string* err);
std::string edenSaveRoot(const Emulator& e, const std::string& saveDir);

// PlayStation 3 (standalone_rpcs3.cpp).
bool prepareRpcs3(const Emulator& e, const std::string& entryPath, const std::string& saveDir,
                  const std::string& player, int players, quality::Level level, bool* missing,
                  std::string* err);
bool installRpcs3(const Emulator& e, const std::string& entryPath,
                  const std::atomic<bool>& cancel, std::string* romPath, std::string* message);

}  // namespace cab::standalone::detail
