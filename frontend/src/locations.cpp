// The places this console keeps games: its own root and a `CabinetOS/` folder
// on every drive plugged in, looked for every time it is asked, with the room
// each has and whether it can be unplugged (locations.h). Moved out of
// storage.cpp in the restructure (#398).

#include "locations.h"

#include <dirent.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>

#include <algorithm>
#include <cctype>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <json-c/json.h>

#include "steam.h"
#include "storage.h"
#include "storage_internal.h"

namespace storage {
namespace {

// The filesystem a path is on. Two paths on the same one are the same disk,
// whatever their names say, and a "drive" that is really a folder on the
// internal disk is not a drive.
dev_t deviceOf(const std::string& path) {
    struct stat st;
    return ::stat(path.c_str(), &st) == 0 ? st.st_dev : 0;
}

// Where a plugged-in drive turns up. Nothing here is a CabinetOS invention:
// `/run/media/<user>/` is where udisks mounts a USB stick on Fedora and
// therefore on Bazzite, and `/var/mnt/` is where an fstab-mounted second
// internal disk conventionally goes on an image-based system.
//
// $CABINETOS_DRIVES overrides the search with an explicit colon-separated list,
// which is how this gets tested on a machine where nobody can plug anything in.
std::vector<std::string> driveSearchPaths() {
    std::vector<std::string> out;
    if (const char* env = std::getenv("CABINETOS_DRIVES"); env && *env) {
        std::string s = env;
        size_t start = 0;
        while (start <= s.size()) {
            const size_t colon = s.find(':', start);
            const std::string one =
                s.substr(start, colon == std::string::npos ? std::string::npos : colon - start);
            if (!one.empty()) out.push_back(one);
            if (colon == std::string::npos) break;
            start = colon + 1;
        }
        return out;
    }
    const char* user = std::getenv("USER");
    out.push_back(std::string("/run/media/") + (user ? user : "cabinet"));
    out.push_back("/var/mnt");
    return out;
}

// The one folder this console claims on somebody else's drive.
const char* kDriveFolder = "CabinetOS";

std::string drivesMemoPath() { return configDir() + "/drives.json"; }

std::vector<std::string> readRememberedDrives() {
    std::vector<std::string> out;
    FILE* f = std::fopen(drivesMemoPath().c_str(), "rb");
    if (!f) return out;
    std::string body;
    char buf[1024];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) body.append(buf, n);
    std::fclose(f);
    json_object* o = json_tokener_parse(body.c_str());
    if (o && json_object_get_type(o) == json_type_array) {
        for (size_t i = 0; i < json_object_array_length(o); ++i)
            if (const char* v = json_object_get_string(json_object_array_get_idx(o, i)))
                out.push_back(v);
    }
    if (o) json_object_put(o);
    return out;
}

void writeRememberedDrives(const std::vector<std::string>& drives) {
    makeDirs(configDir());
    json_object* a = json_object_new_array();
    for (const std::string& d : drives)
        json_object_array_add(a, json_object_new_string(d.c_str()));
    const std::string tmp = drivesMemoPath() + ".part";
    if (FILE* f = std::fopen(tmp.c_str(), "wb")) {
        std::fputs(json_object_to_json_string_ext(a, JSON_C_TO_STRING_PRETTY), f);
        std::fputc('\n', f);
        std::fclose(f);
        ::rename(tmp.c_str(), drivesMemoPath().c_str());
    }
    json_object_put(a);
}

}  // namespace

std::vector<std::string> locations() {
    std::vector<std::string> out{root()};
    const dev_t here = deviceOf(root());
    for (const std::string& where : driveSearchPaths()) {
        DIR* d = ::opendir(where.c_str());
        if (!d) continue;   // nothing mounted there. The ordinary case.
        std::vector<std::string> mounts;
        while (struct dirent* e = ::readdir(d)) {
            if (e->d_name[0] == '.') continue;
            mounts.push_back(where + "/" + e->d_name);
        }
        ::closedir(d);
        std::sort(mounts.begin(), mounts.end());
        for (const std::string& mount : mounts) {
            if (!isDir(mount)) continue;
            // A folder on the internal disk is not a second drive, whatever it
            // is called. This is the check that stops the console treating its
            // own storage as removable and then "losing" it.
            if (deviceOf(mount) == here) continue;
            const std::string claim = mount + "/" + kDriveFolder;
            // PLUG IT IN AND IT IS USED: the folder is made without asking,
            // because a drive plugged into a games console is a games drive and
            // a confirmation dialogue is the thing this is trying not to have.
            // Nothing outside this folder is ever touched.
            if (!isDir(claim) && !makeDirs(claim)) continue;
            if (!writable(claim)) continue;
            out.push_back(claim);
        }
    }
    return out;
}

const std::string& primaryLocation() { return root(); }

std::string missingDriveToReport() {
    const std::vector<std::string> now = locations();
    std::vector<std::string> present(now.begin() + 1, now.end());
    const std::vector<std::string> before = readRememberedDrives();

    std::string gone;
    for (const std::string& d : before) {
        if (std::find(present.begin(), present.end(), d) == present.end()) {
            gone = d;
            break;
        }
    }
    // Whatever is here now is what gets remembered, so a drive that has just
    // been reported missing is forgotten in the same breath and is not
    // mentioned again.
    if (before != present) writeRememberedDrives(present);
    return gone;
}

Space spaceOf(const std::string& location) {
    Space out;
    struct statvfs v;
    if (::statvfs(location.c_str(), &v) != 0) return out;
    out.ok = true;
    // f_bavail, not f_bfree: what an ordinary user may still write, which is
    // what a download is. The difference is the root reserve on ext4.
    out.freeBytes = static_cast<int64_t>(v.f_bavail) * static_cast<int64_t>(v.f_frsize);
    out.totalBytes = static_cast<int64_t>(v.f_blocks) * static_cast<int64_t>(v.f_frsize);
    // STEAM'S SLICE IS NOT THE CONSOLE'S (#223, MMagTech 2026-10-03). It is a
    // fixed-size file on the main drive, so its blocks are already out of
    // the free space; taking it out of the size as well makes the console's
    // part of the drive the drive minus the slice. Then the 80% keep share,
    // "Storage almost full" and Settings, Storage all work on that part, and
    // kept games overflow to another drive exactly as before, by the same
    // rule. Without it, the slice would read as console data.
    if (steam::sliceOn(location))
        out.totalBytes = std::max<int64_t>(0, out.totalBytes - steam::sliceBytes());
    return out;
}

bool isExternalDevice(dev_t d) {
    // A partition's sysfs link resolves to a path under its disk, and the
    // disk's under whatever carries it: .../usb2/2-1/.../block/sdb/sdb1.
    // Anything on USB can be unplugged. So can a disk the kernel marks
    // removable (an SD card: `removable` is 1 on the disk), and anything
    // behind a port the kernel knows faces outwards (`removable` reads
    // "removable" on the device, which is how a Thunderbolt enclosure shows,
    // since its NVMe drive sits on PCIe like an internal one). Walked from the
    // partition up to /sys/devices.
    //
    // A filesystem with no single block device behind it (btrfs reports an
    // anonymous one) resolves nowhere and is reported as internal, which is
    // the safe answer: it only changes a label and hides Eject.
    if (d == 0) return false;
    char link[64];
    std::snprintf(link, sizeof link, "/sys/dev/block/%u:%u", major(d), minor(d));
    char resolved[PATH_MAX];
    if (!::realpath(link, resolved)) return false;
    std::string at = resolved;
    if (at.find("/usb") != std::string::npos) return true;
    while (at.size() > std::strlen("/sys/devices")) {
        if (FILE* f = std::fopen((at + "/removable").c_str(), "rb")) {
            char buf[32] = {};
            const size_t n = std::fread(buf, 1, sizeof buf - 1, f);
            std::fclose(f);
            std::string v(buf, n);
            while (!v.empty() && std::isspace(static_cast<unsigned char>(v.back()))) v.pop_back();
            if (v == "1" || v == "removable") return true;
        }
        const size_t slash = at.rfind('/');
        if (slash == std::string::npos) break;
        at.erase(slash);
    }
    return false;
}

bool isExternal(const std::string& location) { return isExternalDevice(deviceOf(location)); }

}  // namespace storage
