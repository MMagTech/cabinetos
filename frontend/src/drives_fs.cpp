// What the drive watcher reads off the kernel and the filesystem rather than
// udisks (drives_fs.h), and contents(), which Storage asks before a Format
// (drives.h). Moved out of drives.cpp in the restructure (#398).

#include "drives_fs.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <sstream>

#include "drives.h"

namespace drives {

// How many partitions the KERNEL has for a whole disk (/dev/sda: sda1 and
// on, each a folder with a `partition` file in /sys/class/block/sda). Not
// udisks' Partitions list: when a drive is plugged in, udisks announces the
// disk before its partitions, and the list is empty for that moment. The
// stick read as blank for a tenth of a second on the A9 (2026-10-04). The
// kernel has read the table before it announces the disk at all. -1 when
// it cannot tell.
int kernelPartitions(const std::string& device) {
    const std::string name = device.substr(device.rfind('/') + 1);
    if (name.empty()) return -1;
    const std::string dir = "/sys/class/block/" + name;
    DIR* d = ::opendir(dir.c_str());
    if (!d) return -1;
    int n = 0;
    while (struct dirent* e = ::readdir(d)) {
        if (e->d_name[0] == '.') continue;
        struct stat st;
        if (::stat((dir + "/" + e->d_name + "/partition").c_str(), &st) == 0) ++n;
    }
    ::closedir(d);
    return n;
}

// Every regular file under `dir`, without following links, until `stop`
// says enough (once it has, it must go on saying so). False when a folder
// could not be opened or `stop` ended it. ext4's lost+found is root's and
// empty unless a check put something there; not being able to open it is
// not a reason to doubt the count.
template <typename Stop>
bool walk(const std::string& dir, int64_t* files, int64_t* bytes, Stop stop) {
    DIR* d = ::opendir(dir.c_str());
    if (!d) return false;
    bool whole = true;
    while (struct dirent* e = ::readdir(d)) {
        if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, "..")) continue;
        if (stop()) {
            whole = false;
            break;
        }
        const std::string p = dir + "/" + e->d_name;
        struct stat st;
        if (::lstat(p.c_str(), &st) != 0) continue;
        if (S_ISDIR(st.st_mode)) {
            if (!walk(p, files, bytes, stop) &&
                (std::strcmp(e->d_name, "lost+found") != 0 || stop()))
                whole = false;
        } else if (S_ISREG(st.st_mode)) {
            ++*files;
            *bytes += static_cast<int64_t>(st.st_size);
        }
    }
    ::closedir(d);
    return whole;
}

// A file anywhere under <mount>/CabinetOS: the drive holds the console's
// games (or a download on its way there). Its empty folders do not count.
bool holdsConsoleFiles(const std::string& mount) {
    const std::string claim = mount + "/CabinetOS";
    struct stat st;
    if (::lstat(claim.c_str(), &st) != 0) return false;   // no folder, no games
    if (!S_ISDIR(st.st_mode)) return true;                // not ours to judge
    int64_t files = 0, bytes = 0;
    const bool whole = walk(claim, &files, &bytes, [&] { return files > 0; });
    // A folder in it that could not be opened may hold games: say it does.
    return files > 0 || !whole;
}

// Whether the kernel still has any of these devices mounted anywhere, by
// device number or by source (a FUSE mount such as ntfs-3g reports an
// anonymous device number and the real device as its source). Read straight
// from the mount table rather than from udisks, because "Safe to unplug"
// must mean NOTHING has it: not udisks' mount, not File access's view of it.
bool anyMounted(const std::vector<dev_t>& devs, const std::vector<std::string>& nodes) {
    std::ifstream in("/proc/self/mountinfo");
    std::string line;
    while (std::getline(in, line)) {
        std::istringstream f(line);
        std::string id, parent, majmin;
        f >> id >> parent >> majmin;
        for (dev_t d : devs) {
            char mm[32];
            std::snprintf(mm, sizeof mm, "%u:%u", major(d), minor(d));
            if (majmin == mm) return true;
        }
        const size_t dash = line.find(" - ");
        if (dash == std::string::npos) continue;
        std::istringstream tail(line.substr(dash + 3));
        std::string fstype, source;
        tail >> fstype >> source;
        for (const std::string& n : nodes)
            if (!n.empty() && source == n) return true;
    }
    return false;
}

Contents contents(const Drive& d) {
    Contents c;
    c.otherParts = d.otherParts;
    const auto until = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
    int64_t seen = 0;
    bool stopped = false;
    auto stop = [&] {
        // The clock is read every thousand entries, not every one.
        if (!stopped && ++seen % 1000 == 0 && std::chrono::steady_clock::now() > until)
            stopped = true;
        return stopped;
    };
    for (const std::string& m : d.mounts)
        if (!walk(m, &c.files, &c.bytes, stop)) c.counted = false;
    if (!c.counted) {
        c.bytes = 0;
        for (const std::string& m : d.mounts) {
            struct statvfs v;
            if (::statvfs(m.c_str(), &v) == 0)
                c.bytes += static_cast<int64_t>(v.f_blocks - v.f_bfree) *
                           static_cast<int64_t>(v.f_frsize);
        }
    }
    return c;
}

}  // namespace drives
