// What firstrun.cpp shares with serveraddress.cpp: a whole file read, one
// written through a temporary that is fsynced and renamed, the time now, and
// one string out of a JSON object. Internal: nothing outside the two includes
// it. Not fileio.h's readBytes and writeBytes, which do not fsync. Defined in
// firstrun.cpp.

#pragma once

#include <string>

namespace firstrun {

std::string readAll(const std::string& path);
bool writeAll(const std::string& path, const std::string& body, std::string* err);
std::string isoNow();
std::string jsonString(const std::string& body, const char* key);

}  // namespace firstrun
