// What the drive watcher (drives.cpp) reads off the kernel and the filesystem
// rather than udisks: a disk's partitions as the kernel has them, whether a
// drive holds the console's games, and whether anything still has a drive
// mounted. Moved out of drives.cpp in the restructure (#398).

#pragma once

#include <sys/types.h>

#include <string>
#include <vector>

namespace drives {

// Defined in drives_fs.cpp, each with what it does and why.
int kernelPartitions(const std::string& device);
bool holdsConsoleFiles(const std::string& mount);
bool anyMounted(const std::vector<dev_t>& devs, const std::vector<std::string>& nodes);

}  // namespace drives
