// What the standalone emulators' files share: the helpers, defined in
// standalone.cpp, and each emulator's own entry points, defined in its file.
// Internal: only standalone.cpp include it. Moved out of standalone.cpp's
// anonymous namespace in the restructure (#398).

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

}  // namespace cab::standalone::detail
