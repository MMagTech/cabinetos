#include "drives.h"

#include "storage.h"
#include "unit.h"

#include <systemd/sd-bus.h>
#include <systemd/sd-login.h>

#include <atomic>
#include <cctype>
#include <chrono>
#include <climits>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <dirent.h>
#include <fcntl.h>
#include <fstream>
#include <map>
#include <mutex>
#include <set>
#include <sstream>
#include <string>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/sysmacros.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace drives {

namespace {

constexpr const char* kUDisks = "org.freedesktop.UDisks2";
constexpr const char* kRoot = "/org/freedesktop/UDisks2";
constexpr const char* kManagerPath = "/org/freedesktop/UDisks2/Manager";
constexpr const char* kManager = "org.freedesktop.UDisks2.Manager";
constexpr const char* kBlock = "org.freedesktop.UDisks2.Block";
constexpr const char* kFs = "org.freedesktop.UDisks2.Filesystem";
constexpr const char* kPartition = "org.freedesktop.UDisks2.Partition";
constexpr const char* kPartitionTable = "org.freedesktop.UDisks2.PartitionTable";
constexpr const char* kDrive = "org.freedesktop.UDisks2.Drive";
constexpr const char* kBlockPrefix = "/org/freedesktop/UDisks2/block_devices/";
// Linux filesystem data: the GPT type of an ext4 partition.
constexpr const char* kLinuxData = "0fc63daf-8483-4772-8e79-3d69d8477de4";
// Where File access shows a drive: a second mount, root's (cabinetos-files).
// Not "held elsewhere": Eject and Format have it let go.
constexpr const char* kFilesView = "/run/cabinetos-files/";
// Makes CabinetOS/ on an ext4 drive whose top folder is root's
// (/usr/libexec/cabinetos-drive-claim; 63-cabinetos-drives.rules).
constexpr const char* kClaimUnit = "cabinetos-drive-claim.service";

// A drive that turns up this soon after start was plugged in before the
// console started and was only slow to be seen, not news. It is mounted all
// the same, without "connected". Failures are still said.
constexpr auto kQuietAfterStart = std::chrono::seconds(10);

std::mutex gM;
std::deque<Notice> gNotices;
std::deque<std::string> gEjects;
std::atomic<bool> gEjecting{false};
std::deque<std::string> gFormats;
std::atomic<bool> gFormatting{false};
std::map<std::string, Unusable> gUnusable;   // by drive object path
std::vector<Drive> gFound;
bool gStarted = false;

void post(Event e, bool external) {
    std::lock_guard<std::mutex> lk(gM);
    gNotices.push_back({e, external});
}

// The console on its own seat, the active session there. This is what polkit
// checks, so anything else would only be refused.
bool onSeat() {
    char* session = nullptr;
    if (sd_pid_get_session(0, &session) < 0 || !session) return false;
    char* seat = nullptr;
    const bool ok = sd_session_get_seat(session, &seat) >= 0 && seat &&
                    sd_session_is_active(session) > 0;
    std::free(seat);
    std::free(session);
    return ok;
}

// Partitions that belong to an operating system rather than to anybody's
// files, by GPT type (and MBR type): EFI system, Microsoft reserved, Windows
// recovery. A mini PC that came with Windows on a second disk has all three.
bool systemPartitionType(const std::string& t) {
    static const char* kTypes[] = {
        "c12a7328-f81f-11d2-ba4b-00a0c93ec93b",   // EFI system
        "e3c9e316-0b5c-4db8-817d-f92df00215ae",   // Microsoft reserved
        "de94bba4-06d1-4d40-a16a-bfd50179d6ac",   // Windows recovery
        "0xef", "0x27",                           // EFI, recovery (MBR)
    };
    for (const char* k : kTypes)
        if (t == k) return true;
    return false;
}

bool boolProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
              bool* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    int v = 0;
    const int r = sd_bus_get_property_trivial(bus, kUDisks, path.c_str(), iface, name, &err,
                                              'b', &v);
    sd_bus_error_free(&err);
    if (r < 0) return false;
    *out = v != 0;
    return true;
}

bool u64Prop(sd_bus* bus, const std::string& path, const char* iface, const char* name,
             uint64_t* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    const int r = sd_bus_get_property_trivial(bus, kUDisks, path.c_str(), iface, name, &err,
                                              't', out);
    sd_bus_error_free(&err);
    return r >= 0;
}

// `s` or `o`. False when the object has no such interface, which is how
// "is this a partition" and "does it have a partition table" are asked.
bool stringProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
                const char* type, std::string* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_get_property(bus, kUDisks, path.c_str(), iface, name, &err, &reply,
                                      type);
    if (r >= 0) {
        const char* s = nullptr;
        if (sd_bus_message_read(reply, type, &s) >= 0 && s) *out = s;
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return r >= 0;
}

// An `ay` property that is a path with its NUL: Block.PreferredDevice.
bool pathBytesProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
                   std::string* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_get_property(bus, kUDisks, path.c_str(), iface, name, &err, &reply,
                                      "ay");
    if (r >= 0) {
        const void* p = nullptr;
        size_t n = 0;
        if (sd_bus_message_read_array(reply, 'y', &p, &n) >= 0 && p) {
            out->assign(static_cast<const char*>(p), n);
            while (!out->empty() && out->back() == '\0') out->pop_back();
        }
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return r >= 0;
}

std::string driveOf(sd_bus* bus, const std::string& block) {
    std::string d;
    stringProp(bus, block, kBlock, "Drive", "o", &d);
    return d == "/" ? std::string() : d;
}

// How many partitions a partition table lists. -1 when it is not one.
int partitionCount(sd_bus* bus, const std::string& path) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_get_property(bus, kUDisks, path.c_str(), kPartitionTable, "Partitions",
                                      &err, &reply, "ao");
    sd_bus_error_free(&err);
    int n = -1;
    if (r >= 0 && sd_bus_message_enter_container(reply, 'a', "o") > 0) {
        n = 0;
        const char* p = nullptr;
        while (sd_bus_message_read(reply, "o", &p) > 0) ++n;
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    return n;
}

bool readable(const std::string& type) {
    return type == "exfat" || type == "ntfs" || type == "ext4";
}

bool isDirectory(const std::string& p) {
    struct stat st;
    return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

bool canWrite(const std::string& dir) {
    const std::string probe = dir + "/.cabinetos-write-test";
    const int fd = ::open(probe.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (fd < 0) return false;
    ::close(fd);
    ::unlink(probe.c_str());
    return true;
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

// False when the object has no filesystem on it at all.
bool mountPoints(sd_bus* bus, const std::string& path, std::vector<std::string>* out) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_get_property(bus, kUDisks, path.c_str(), kFs, "MountPoints", &err,
                                      &reply, "aay");
    sd_bus_error_free(&err);
    if (r < 0) {
        sd_bus_message_unref(reply);
        return false;
    }
    if (sd_bus_message_enter_container(reply, 'a', "ay") > 0) {
        const void* p = nullptr;
        size_t n = 0;
        while (sd_bus_message_read_array(reply, 'y', &p, &n) > 0) {
            // udisks sends the path with its terminating NUL.
            std::string s(static_cast<const char*>(p), n);
            while (!s.empty() && s.back() == '\0') s.pop_back();
            if (!s.empty()) out->push_back(s);
        }
        sd_bus_message_exit_container(reply);
    }
    sd_bus_message_unref(reply);
    return true;
}

std::vector<std::string> blockDevices(sd_bus* bus) {
    std::vector<std::string> out;
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    if (sd_bus_call_method(bus, kUDisks, kManagerPath, kManager, "GetBlockDevices", &err, &reply,
                           "a{sv}", 0) >= 0) {
        if (sd_bus_message_enter_container(reply, 'a', "o") > 0) {
            const char* p = nullptr;
            while (sd_bus_message_read(reply, "o", &p) > 0)
                if (p) out.push_back(p);
            sd_bus_message_exit_container(reply);
        }
    } else {
        std::fprintf(stderr, "[drives] udisks did not answer: %s\n",
                     err.message ? err.message : "?");
    }
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return out;
}

// udisks' object path for a kernel block device: every byte that is not a
// letter or a digit becomes _xx.
std::string blockPathFor(dev_t d) {
    char link[64];
    std::snprintf(link, sizeof link, "/sys/dev/block/%u:%u", major(d), minor(d));
    char resolved[PATH_MAX];
    if (!::realpath(link, resolved)) return "";
    const char* name = std::strrchr(resolved, '/');
    std::string out = kBlockPrefix;
    for (const char* c = name ? name + 1 : resolved; *c; ++c) {
        if (std::isalnum(static_cast<unsigned char>(*c))) {
            out += *c;
        } else {
            char esc[4];
            std::snprintf(esc, sizeof esc, "_%02x", static_cast<unsigned char>(*c));
            out += esc;
        }
    }
    return out;
}

// One call that takes only auth.no_user_interaction: a password prompt on a
// television is the same as a refusal, so it is asked for as one.
bool callNoPrompt(sd_bus* bus, const std::string& path, const char* iface, const char* method,
                  std::string* mountedAt, std::string* why) {
    sd_bus_error err = SD_BUS_ERROR_NULL;
    sd_bus_message* reply = nullptr;
    const int r = sd_bus_call_method(bus, kUDisks, path.c_str(), iface, method, &err, &reply,
                                     "a{sv}", 1, "auth.no_user_interaction", "b", 1);
    const bool ok = r >= 0;
    if (ok && mountedAt) {
        const char* s = nullptr;
        if (sd_bus_message_read(reply, "s", &s) >= 0 && s) *mountedAt = s;
    }
    if (!ok && why) *why = err.message ? err.message : std::strerror(-r);
    sd_bus_error_free(&err);
    sd_bus_message_unref(reply);
    return ok;
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

// What the console knows about one block device it has looked at.
struct Seen {
    std::string drive;
    bool mounted = false;
    bool external = true;
};

struct Watcher {
    sd_bus* bus = nullptr;
    std::chrono::steady_clock::time_point startedAt;
    std::set<std::string> ownDrives;       // the console's own, never touched
    std::map<std::string, Seen> seen;      // by block object path
    std::map<std::string, Event> announced;   // what was last said of each drive
    std::set<std::string> ejected;         // drives Eject let go of
    std::set<std::string> fresh;           // drives Format just made: SteamLibrary/ too
    std::vector<std::string> added;        // filled by the signal handlers
    std::vector<std::string> removed;
    std::vector<std::string> changed;      // an interface went, the block stayed

    // THE CONSOLE'S OWN DRIVE, found from the partitions it boots from.
    // Nothing on it is ever mounted by this, whatever else is on it.
    void findOwnDrives() {
        for (const char* p : {"/boot", "/boot/efi", "/sysroot", "/"}) {
            struct stat st;
            if (::stat(p, &st) != 0) continue;
            const std::string block = blockPathFor(st.st_dev);
            if (block.empty()) continue;
            const std::string d = driveOf(bus, block);
            if (!d.empty() && ownDrives.insert(d).second)
                std::fprintf(stderr, "[drives] the console's own drive is %s\n", d.c_str());
        }
    }

    // Waits for the helper, up to ten seconds: it is a mkdir.
    void claim(const std::string& at) {
        std::string why;
        if (!unit::start(kClaimUnit, &why)) {
            std::fprintf(stderr, "[drives] could not ask for CabinetOS/ on %s: %s\n", at.c_str(),
                         why.c_str());
            return;
        }
        for (int i = 0; i < 40 && !isDirectory(at + "/CabinetOS"); ++i) usleep(250 * 1000);
        std::fprintf(stderr, "[drives] CabinetOS/ on %s %s\n", at.c_str(),
                     isDirectory(at + "/CabinetOS") ? "made by the helper" : "not made");
    }

    bool usable(const std::string& mountedAt) {
        const std::string claim = mountedAt + "/CabinetOS";
        for (const std::string& loc : storage::locations())
            if (loc == claim) return true;
        return false;
    }

    // Said once per drive and per verdict, and only for an external one. A
    // drive that cannot be used is also listed for Storage, which is how an
    // internal one is seen at all.
    void say(const std::string& drive, bool external, Event e, bool blank = false) {
        if (e == Event::WrongFormat || e == Event::CouldNotUse) {
            Unusable u;
            u.id = drive;
            u.external = external;
            u.wrongFormat = e == Event::WrongFormat;
            u.blank = blank;
            u64Prop(bus, drive, kDrive, "Size", &u.sizeBytes);
            stringProp(bus, drive, kDrive, "Model", "s", &u.model);
            std::lock_guard<std::mutex> lk(gM);
            gUnusable[drive] = u;
            gNotices.push_back({Event::Changed, external});
        }
        auto it = announced.find(drive);
        if (it != announced.end() && it->second == e) return;
        announced[drive] = e;
        // An internal drive gets no notice, but Storage still has to be
        // redrawn: found on the VM, where a freshly formatted internal drive
        // mounted and Storage went on listing it as missing.
        post(external ? e : Event::Changed, external);
    }

    void usableAfterAll(const std::string& drive) {
        std::lock_guard<std::mutex> lk(gM);
        if (gUnusable.erase(drive)) gNotices.push_back({Event::Changed, false});
    }

    void consider(const std::string& block, bool quiet) {
        if (seen.count(block)) return;
        bool ignore = false;
        if (!boolProp(bus, block, kBlock, "HintIgnore", &ignore) || ignore) return;
        const std::string drive = driveOf(bus, block);
        if (drive.empty() || ownDrives.count(drive)) return;   // loop, zram, the console

        uint64_t devnum = 0, size = 0;
        u64Prop(bus, block, kBlock, "DeviceNumber", &devnum);
        u64Prop(bus, block, kBlock, "Size", &size);
        const bool external = storage::isExternalDevice(static_cast<dev_t>(devnum));

        std::vector<std::string> mps;
        const bool hasFs = mountPoints(bus, block, &mps);
        if (hasFs && !mps.empty()) {
            // Mounted already: by this console before a restart in place, by
            // an fstab line, or by hand. In use, and nothing needs saying.
            seen[block] = {drive, true, external};
            announced[drive] = Event::Connected;
            std::fprintf(stderr, "[drives] %s is already mounted at %s\n", block.c_str(),
                         mps.front().c_str());
            return;
        }

        std::string usage, type, partType, tableType;
        stringProp(bus, block, kBlock, "IdUsage", "s", &usage);
        stringProp(bus, block, kBlock, "IdType", "s", &type);
        const bool isPartition = stringProp(bus, block, kPartition, "Type", "s", &partType);
        const bool hasTable = stringProp(bus, block, kPartitionTable, "Type", "s", &tableType);

        // Its partitions arrive as objects of their own. A TABLE WITH NO
        // PARTITIONS is as good as blank: what Windows' Disk Management
        // leaves on a new SSD it was asked to "initialise".
        const bool emptyTable = !isPartition && hasTable && partitionCount(bus, block) == 0;
        if (!isPartition && hasTable && !emptyTable) return;
        if (isPartition && systemPartitionType(partType)) return;

        const bool rightFormat = usage == "filesystem" && readable(type);
        const bool blank = !isPartition && size > 0 && (emptyTable || usage.empty());
        if (!rightFormat) {
            // A filesystem of another kind, an encrypted one, or nothing at
            // all on the whole disk. Swap, RAID members, an empty partition
            // and a card reader with no card are none of these and pass by.
            if (usage == "filesystem" || usage == "crypto" || blank) {
                seen[block] = {drive, false, external};
                std::fprintf(stderr, "[drives] %s is %s; only exFAT, NTFS and ext4 are used\n",
                             block.c_str(), blank ? "blank" : type.c_str());
                say(drive, external, Event::WrongFormat, blank);
            }
            return;
        }
        if (!hasFs) return;   // udisks has not finished with it; it comes again

        seen[block] = {drive, false, external};
        std::string at, why;
        if (!callNoPrompt(bus, block, kFs, "Mount", &at, &why)) {
            std::fprintf(stderr, "[drives] could not mount %s: %s\n", block.c_str(), why.c_str());
            say(drive, external, Event::CouldNotUse);
            return;
        }
        seen[block].mounted = true;
        // AN ext4 DRIVE MADE ELSEWHERE has a top folder owned by root, and
        // the console cannot make its folder there; a root helper makes
        // CabinetOS/ (and nothing else), owned by the console's user. One the
        // console formatted is its user's already.
        if (type == "ext4" && !isDirectory(at + "/CabinetOS") && !canWrite(at)) claim(at);
        if (fresh.count(drive) && canWrite(at)) {
            // Steam's library goes in a folder of its own, so its files never
            // land loose at the top of the drive (MMagTech, 2026-10-04). Steam
            // is told about it at each handover (cabinetos-steam-libraries).
            fresh.erase(drive);
            if (::mkdir((at + "/SteamLibrary").c_str(), 0755) == 0)
                std::fprintf(stderr, "[drives] made SteamLibrary/ on %s\n", at.c_str());
        }
        if (!usable(at)) {
            // Read-only: a Windows drive left hibernated, or a stick with its
            // lock switch on. Mounted, and no game can be kept on it.
            std::fprintf(stderr, "[drives] mounted %s at %s, but CabinetOS/ cannot be written\n",
                         block.c_str(), at.c_str());
            say(drive, external, Event::CouldNotUse);
            return;
        }
        std::fprintf(stderr, "[drives] mounted %s (%s, %s) at %s%s\n", block.c_str(),
                     type.c_str(), external ? "external" : "internal", at.c_str(),
                     quiet ? ", already attached" : "");
        usableAfterAll(drive);
        if (quiet) {
            // No notice, but File access, if on, still has to show it.
            announced[drive] = Event::Connected;
            post(Event::Changed, external);
        } else {
            say(drive, external, Event::Connected);
        }
    }

    void forget(const std::string& block) {
        auto it = seen.find(block);
        if (it == seen.end()) return;
        const Seen gone = it->second;
        seen.erase(it);
        for (const auto& [b, s] : seen)
            if (s.drive == gone.drive) return;   // another part of it is still here
        announced.erase(gone.drive);
        {
            std::lock_guard<std::mutex> lk(gM);
            if (gUnusable.erase(gone.drive)) gNotices.push_back({Event::Changed, false});
        }
        if (ejected.erase(gone.drive)) return;   // Eject already said "Safe to unplug"
        if (!gone.mounted) return;
        std::fprintf(stderr, "[drives] %s was pulled out while in use\n", gone.drive.c_str());
        post(gone.external ? Event::Removed : Event::Changed, gone.external);
    }

    // A block whose contents changed while attached: wiped, formatted,
    // repartitioned. Looked at again as if new; say() keeps it from repeating
    // a notice it already gave.
    void reconsider(const std::string& block) {
        auto it = seen.find(block);
        if (it != seen.end()) {
            if (it->second.mounted) return;
            seen.erase(it);
        }
        consider(block, false);
    }

    // EVERY DRIVE BUT THE CONSOLE'S OWN, as udisks has it now: what Format
    // needs to know (drives.h, Drive). Rebuilt whole after anything happens;
    // a Changed notice when it differs.
    Drive describe(const std::string& drive, const std::vector<std::string>& blocks) {
        Drive d;
        d.id = drive;
        u64Prop(bus, drive, kDrive, "Size", &d.sizeBytes);
        stringProp(bus, drive, kDrive, "Model", "s", &d.model);
        for (const std::string& b : blocks) {
            if (driveOf(bus, b) != drive) continue;
            uint64_t devnum = 0;
            if (u64Prop(bus, b, kBlock, "DeviceNumber", &devnum))
                d.external = storage::isExternalDevice(static_cast<dev_t>(devnum));
            std::string usage, type;
            stringProp(bus, b, kBlock, "IdUsage", "s", &usage);
            stringProp(bus, b, kBlock, "IdType", "s", &type);
            std::vector<std::string> mps;
            mountPoints(bus, b, &mps);
            bool ours = false;
            for (const std::string& m : mps) {
                if (m.rfind("/run/media/", 0) == 0) {
                    d.mounts.push_back(m);
                    ours = true;
                } else if (m.rfind(kFilesView, 0) != 0) {
                    d.heldElsewhere = true;
                }
            }
            if (ours || !mps.empty() || usage.empty()) continue;
            if (usage == "filesystem" && readable(type)) {
                std::string partType;
                if (!stringProp(bus, b, kPartition, "Type", "s", &partType) ||
                    !systemPartitionType(partType)) {
                    d.unchecked = true;   // would not mount: cannot see in
                    continue;
                }
            }
            d.otherParts++;   // another format, encrypted, Windows' own
        }
        return d;
    }

    void rescan() {
        const std::vector<std::string> blocks = blockDevices(bus);
        std::set<std::string> drives;
        for (const std::string& b : blocks) {
            const std::string d = driveOf(bus, b);
            if (!d.empty() && !ownDrives.count(d)) drives.insert(d);
        }
        std::vector<Drive> now;
        for (const std::string& d : drives) now.push_back(describe(d, blocks));
        auto same = [](const std::vector<Drive>& x, const std::vector<Drive>& y) {
            if (x.size() != y.size()) return false;
            for (size_t i = 0; i < x.size(); ++i)
                if (x[i].id != y[i].id || x[i].mounts != y[i].mounts ||
                    x[i].otherParts != y[i].otherParts || x[i].heldElsewhere != y[i].heldElsewhere ||
                    x[i].unchecked != y[i].unchecked || x[i].sizeBytes != y[i].sizeBytes)
                    return false;
            return true;
        };
        std::lock_guard<std::mutex> lk(gM);
        if (same(now, gFound)) return;
        gFound = std::move(now);
        gNotices.push_back({Event::Changed, true});
    }

    // LET GO OF A DRIVE: unmount every filesystem on it that the console
    // mounted, have File access let go of its view, and check the kernel's
    // mount table that nothing on the machine still has it. On failure
    // everything unmounted here is mounted back, so the drive is not left
    // missing from Storage while still held. Eject and Format both.
    bool letGo(const std::string& drive, const char* what) {
        // Every filesystem on the drive, not only the one holding CabinetOS/.
        // UDISKS' OWN MOUNTS ONLY (under /run/media): File access's view of a
        // drive is a second mount made by root, which udisks will not let
        // the session unmount ("Not authorized", found on the A9 with File
        // access on). That one is root's to take down, below.
        std::vector<dev_t> devs;
        std::vector<std::string> nodes;
        std::vector<std::string> released;   // unmounted here, to mount back on failure
        auto putBack = [&]() {
            for (const std::string& b : released) {
                std::string at, why;
                if (callNoPrompt(bus, b, kFs, "Mount", &at, &why)) {
                    if (auto it = seen.find(b); it != seen.end()) it->second.mounted = true;
                    std::fprintf(stderr, "[drives] %s: put %s back at %s\n", what, b.c_str(),
                                 at.c_str());
                }
            }
        };
        auto ours = [](const std::vector<std::string>& mps) {
            for (const std::string& m : mps)
                if (m.rfind("/run/media/", 0) == 0) return true;
            return false;
        };
        for (const std::string& b : blockDevices(bus)) {
            if (driveOf(bus, b) != drive) continue;
            uint64_t n = 0;
            if (u64Prop(bus, b, kBlock, "DeviceNumber", &n)) devs.push_back(static_cast<dev_t>(n));
            std::string node;
            if (pathBytesProp(bus, b, kBlock, "Device", &node)) nodes.push_back(node);
            for (int tries = 0; tries < 4; ++tries) {
                std::vector<std::string> mps;
                if (!mountPoints(bus, b, &mps) || !ours(mps)) break;
                std::string why;
                if (!callNoPrompt(bus, b, kFs, "Unmount", nullptr, &why)) {
                    std::fprintf(stderr, "[drives] %s: could not unmount %s: %s\n", what,
                                 b.c_str(), why.c_str());
                    putBack();
                    return false;
                }
                released.push_back(b);
                std::fprintf(stderr, "[drives] %s: unmounted %s\n", what, b.c_str());
            }
            if (auto it = seen.find(b); it != seen.end()) it->second.mounted = false;
        }
        // ANYTHING LEFT is File access's view: root rebuilds it without this
        // drive (cabinetos-files refresh), and a computer copying a file from
        // it keeps it busy, so this then refuses rather than cutting the copy
        // off. Waited for, up to ten seconds, because the reload returns when
        // systemd has the job, not when it is done.
        if (anyMounted(devs, nodes)) {
            std::string why;
            const bool asked = unit::reload("cabinetos-files.service", &why);
            std::fprintf(stderr, "[drives] %s: asked File access to let go: %s\n", what,
                         asked ? "taken" : why.c_str());
            for (int i = 0; i < 40 && anyMounted(devs, nodes); ++i) usleep(250 * 1000);
        }
        if (anyMounted(devs, nodes)) {
            std::fprintf(stderr, "[drives] %s: %s is still mounted somewhere\n", what,
                         drive.c_str());
            putBack();
            return false;
        }
        return true;
    }

    // THE ONE THING HERE THAT ERASES ANYTHING. Everything Storage checked
    // before offering Format is checked again now, from udisks, not from what
    // was remembered: not the console's own drive, not mounted by anything
    // but the console, nothing it reads left unmounted, and no file under
    // CabinetOS/ on any of its filesystems. Any of them and nothing is
    // written. Then its filesystems are let go of, and only then wiped.
    bool formatOne(const std::string& drive) {
        if (drive.empty() || ownDrives.count(drive)) return false;
        const std::vector<std::string> blocks = blockDevices(bus);
        const Drive d = describe(drive, blocks);
        if (d.heldElsewhere || d.unchecked) {
            std::fprintf(stderr, "[drives] format: %s is %s; not formatted\n", drive.c_str(),
                         d.heldElsewhere ? "mounted by something else" : "not readable");
            return false;
        }
        for (const std::string& m : d.mounts) {
            if (holdsConsoleFiles(m)) {
                std::fprintf(stderr, "[drives] format: %s holds the console's files; not formatted\n",
                             m.c_str());
                return false;
            }
        }
        // The whole disk: the one block of the drive that is not a partition.
        std::string disk;
        for (const std::string& b : blocks) {
            if (driveOf(bus, b) != drive) continue;
            std::string partType;
            if (stringProp(bus, b, kPartition, "Type", "s", &partType)) continue;
            if (!disk.empty()) return false;   // two whole disks under one drive: stop
            disk = b;
        }
        if (disk.empty()) return false;
        if (!letGo(drive, "format")) return false;
        std::fprintf(stderr, "[drives] format: %s (%d mounted, %d other) is let go of; wiping it\n",
                     disk.c_str(), static_cast<int>(d.mounts.size()), d.otherParts);
        // A NEW, EMPTY PARTITION TABLE over the whole disk. udisks wipes every
        // signature on it first. The old partitions go with the table.
        std::string why;
        {
            sd_bus_error err = SD_BUS_ERROR_NULL;
            const int r = sd_bus_call_method(bus, kUDisks, disk.c_str(), kBlock, "Format", &err,
                                             nullptr, "sa{sv}", "gpt", 1,
                                             "auth.no_user_interaction", "b", 1);
            if (r < 0) why = err.message ? err.message : std::strerror(-r);
            sd_bus_error_free(&err);
            if (r < 0) {
                std::fprintf(stderr, "[drives] format: no partition table: %s\n", why.c_str());
                return false;
            }
        }
        // NAMED "Games", or "Games 2" and on if a drive of that name is
        // attached, so two drives this formatted are told apart in Storage
        // and in File access. MMagTech, 2026-09-25.
        std::set<std::string> taken;
        for (const std::string& b : blockDevices(bus)) {
            std::string label;
            if (stringProp(bus, b, kBlock, "IdLabel", "s", &label) && !label.empty())
                taken.insert(label);
        }
        std::string label = "Games";
        for (int n = 2; taken.count(label) && n < 100; ++n) label = "Games " + std::to_string(n);
        // ext4, its top folder the console's user's (take-ownership), and no
        // blocks held back for root (-m 0): a games drive has no root
        // processes to save from a full disk, and the 5% default is 200 GB
        // of a 4 TB drive that neither the console nor Steam could use.
        sd_bus_error err = SD_BUS_ERROR_NULL;
        sd_bus_message* call = nullptr;
        sd_bus_message* reply = nullptr;
        int r = sd_bus_message_new_method_call(bus, &call, kUDisks, disk.c_str(), kPartitionTable,
                                               "CreatePartitionAndFormat");
        if (r >= 0)
            r = sd_bus_message_append(call, "ttssa{sv}s", uint64_t(0), uint64_t(0), kLinuxData,
                                      "", 1, "auth.no_user_interaction", "b", 1, "ext4");
        if (r >= 0) r = sd_bus_message_open_container(call, 'a', "{sv}");
        if (r >= 0)
            r = sd_bus_message_append(call, "{sv}{sv}{sv}", "label", "s", label.c_str(),
                                      "take-ownership", "b", 1, "auth.no_user_interaction", "b",
                                      1);
        if (r >= 0) r = sd_bus_message_open_container(call, 'e', "sv");
        if (r >= 0) r = sd_bus_message_append(call, "s", "mkfs-args");
        if (r >= 0) r = sd_bus_message_open_container(call, 'v', "as");
        if (r >= 0) r = sd_bus_message_append(call, "as", 2, "-m", "0");
        if (r >= 0) r = sd_bus_message_close_container(call);
        if (r >= 0) r = sd_bus_message_close_container(call);
        if (r >= 0) r = sd_bus_message_close_container(call);
        // Formatting a large drive takes a while; the default 25 s is not it.
        if (r >= 0) r = sd_bus_call(bus, call, 10ULL * 60 * 1000 * 1000, &err, &reply);
        if (r < 0) why = err.message ? err.message : std::strerror(-r);
        sd_bus_error_free(&err);
        sd_bus_message_unref(call);
        sd_bus_message_unref(reply);
        if (r < 0) {
            std::fprintf(stderr, "[drives] format: no ext4 partition: %s\n", why.c_str());
            return false;
        }
        std::fprintf(stderr, "[drives] format: %s is ext4 now, named %s\n", drive.c_str(),
                     label.c_str());
        fresh.insert(drive);
        // What was said of it before (blank, connected) no longer holds: the
        // new partition is news, and says "connected" when it mounts.
        announced.erase(drive);
        for (auto it = seen.begin(); it != seen.end();)
            it = it->second.drive == drive ? seen.erase(it) : std::next(it);
        {
            std::lock_guard<std::mutex> lk(gM);
            gUnusable.erase(drive);
        }
        return true;
    }

    bool ejectOne(const std::string& location) {
        struct stat st;
        if (::stat(location.c_str(), &st) != 0) return false;
        const std::string drive = driveOf(bus, blockPathFor(st.st_dev));
        if (drive.empty()) {
            std::fprintf(stderr, "[drives] eject: no drive behind %s\n", location.c_str());
            return false;
        }
        if (!letGo(drive, "eject")) return false;
        ejected.insert(drive);
        // Unmounted is already safe: unmounting wrote everything out. Powering
        // it off is what makes the drive's light go out, and some enclosures
        // cannot do it, which is not a failure worth a word.
        // RETRIED FOR A FEW SECONDS: right after an unmount the system may
        // still be reading the drive once more, and udisks refuses with
        // "Device or resource busy". Found on the A9 ejecting with File
        // access on, where two unmounts land a quarter second apart.
        std::string why;
        bool off = false;
        for (int i = 0; i < 8 && !off; ++i) {
            if (i) usleep(500 * 1000);
            off = callNoPrompt(bus, drive, kDrive, "PowerOff", nullptr, &why);
        }
        if (off)
            std::fprintf(stderr, "[drives] eject: %s is powered off\n", drive.c_str());
        else
            std::fprintf(stderr, "[drives] eject: %s stays powered (%s)\n", drive.c_str(),
                         why.c_str());
        return true;
    }

    void run() {
        for (;;) {
            while (sd_bus_process(bus, nullptr) > 0) {
            }
            const bool quiet = std::chrono::steady_clock::now() - startedAt < kQuietAfterStart;
            std::vector<std::string> a, r, c;
            a.swap(added);
            r.swap(removed);
            c.swap(changed);
            for (const std::string& b : r) forget(b);
            for (const std::string& b : a) {
                if (seen.count(b)) reconsider(b);
                else consider(b, quiet);
            }
            for (const std::string& b : c) reconsider(b);
            if (!a.empty() || !r.empty() || !c.empty()) rescan();

            std::string fmt;
            {
                std::lock_guard<std::mutex> lk(gM);
                if (!gFormats.empty()) {
                    fmt = gFormats.front();
                    gFormats.pop_front();
                }
            }
            if (!fmt.empty()) {
                if (!formatOne(fmt)) post(Event::FormatFailed, true);
                rescan();
                std::lock_guard<std::mutex> lk(gM);
                gFormatting = false;
                gNotices.push_back({Event::Changed, true});
                continue;
            }

            std::string loc;
            {
                std::lock_guard<std::mutex> lk(gM);
                if (!gEjects.empty()) {
                    loc = gEjects.front();
                    gEjects.pop_front();
                }
            }
            if (!loc.empty()) {
                const bool ok = ejectOne(loc);
                rescan();
                post(ok ? Event::SafeToUnplug : Event::EjectFailed, true);
                continue;
            }
            sd_bus_wait(bus, 250 * 1000);
        }
    }
};

int onAdded(sd_bus_message* m, void* userdata, sd_bus_error*) {
    const char* p = nullptr;
    if (sd_bus_message_read(m, "o", &p) >= 0 && p &&
        std::strncmp(p, kBlockPrefix, std::strlen(kBlockPrefix)) == 0)
        static_cast<Watcher*>(userdata)->added.push_back(p);
    return 0;
}

int onRemoved(sd_bus_message* m, void* userdata, sd_bus_error*) {
    const char* p = nullptr;
    if (sd_bus_message_read(m, "o", &p) < 0 || !p) return 0;
    char** ifaces = nullptr;
    if (sd_bus_message_read_strv(m, &ifaces) < 0) return 0;
    bool block = false;
    for (char** i = ifaces; i && *i; ++i) {
        if (std::strcmp(*i, kBlock) == 0) block = true;
        std::free(*i);
    }
    std::free(ifaces);
    auto* w = static_cast<Watcher*>(userdata);
    if (block) w->removed.push_back(p);
    else if (std::strncmp(p, kBlockPrefix, std::strlen(kBlockPrefix)) == 0)
        w->changed.push_back(p);   // e.g. its partition table was wiped
    return 0;
}

}  // namespace

void start() {
    if (gStarted) return;
    gStarted = true;
    // A test run that names its own drives is not plugging anything in.
    if (const char* env = std::getenv("CABINETOS_DRIVES"); env && *env) return;
    if (!onSeat()) {
        std::fprintf(stderr, "[drives] not the console's own seat; not mounting drives\n");
        return;
    }
    auto* w = new Watcher();   // lives as long as the process
    if (sd_bus_open_system(&w->bus) < 0) {
        std::fprintf(stderr, "[drives] no system bus; drives will not be mounted\n");
        delete w;
        return;
    }
    // Listening BEFORE the first look, so a drive plugged in between the two
    // is not missed. Its signal waits in the queue and is handled as news.
    sd_bus_match_signal(w->bus, nullptr, kUDisks, kRoot, "org.freedesktop.DBus.ObjectManager",
                        "InterfacesAdded", onAdded, w);
    sd_bus_match_signal(w->bus, nullptr, kUDisks, kRoot, "org.freedesktop.DBus.ObjectManager",
                        "InterfacesRemoved", onRemoved, w);
    w->startedAt = std::chrono::steady_clock::now();
    w->findOwnDrives();
    for (const std::string& b : blockDevices(w->bus)) w->consider(b, true);
    w->rescan();
    // One redraw after the first look: Storage, and File access if it is on
    // and was built before this process started (a restart in place). Found
    // on the A9: a drive already mounted at start never reached File access.
    post(Event::Changed, true);
    std::thread([w] { w->run(); }).detach();
}

Notice poll() {
    std::lock_guard<std::mutex> lk(gM);
    if (gNotices.empty()) return {};
    const Notice n = gNotices.front();
    gNotices.pop_front();
    if (n.event == Event::SafeToUnplug || n.event == Event::EjectFailed) gEjecting = false;
    return n;
}

std::vector<Unusable> unusable() {
    std::lock_guard<std::mutex> lk(gM);
    std::vector<Unusable> out;
    for (const auto& [drive, u] : gUnusable) out.push_back(u);
    return out;
}

void eject(const std::string& location) {
    std::lock_guard<std::mutex> lk(gM);
    gEjecting = true;
    gEjects.push_back(location);
}

bool ejecting() { return gEjecting.load(); }

std::vector<Drive> found() {
    std::lock_guard<std::mutex> lk(gM);
    return gFound;
}

const Drive* driveAt(const std::vector<Drive>& all, const std::string& location) {
    // <mount>/CabinetOS
    const std::string mount = location.substr(0, location.rfind('/'));
    for (const Drive& d : all)
        for (const std::string& m : d.mounts)
            if (m == mount) return &d;
    return nullptr;
}

bool mayFormat(const Drive& d) {
    if (d.heldElsewhere || d.unchecked) return false;
    for (const std::string& m : d.mounts)
        if (holdsConsoleFiles(m)) return false;
    return true;
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

void format(const std::string& driveId) {
    std::lock_guard<std::mutex> lk(gM);
    gFormatting = true;
    gFormats.push_back(driveId);
}

bool formatting() { return gFormatting.load(); }

}  // namespace drives
