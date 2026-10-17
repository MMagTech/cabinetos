// What storage.cpp shares with the files split out of it (locations.cpp,
// users.cpp): whether a path is a directory, and whether this process can
// write in one, asked by writing. Internal: nothing outside the three
// includes it. Defined in storage.cpp, each with what it does and why.

#pragma once

#include <string>

namespace storage {

bool isDir(const std::string& p);
bool writable(const std::string& dir);

}  // namespace storage
