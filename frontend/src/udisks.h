// What the drive watcher (drives.cpp) asks of udisks over D-Bus: its names,
// one property at a time, the block devices and a block device's object path,
// a call that may not ask for a password, and a wait for a systemd unit's
// reload. Moved out of drives.cpp in the restructure (#398).

#pragma once

#include <sys/types.h>
#include <systemd/sd-bus.h>

#include <cstdint>
#include <string>
#include <vector>

namespace drives {

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

// Defined in udisks.cpp, each with what it does and why.
bool boolProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
              bool* out);
bool u64Prop(sd_bus* bus, const std::string& path, const char* iface, const char* name,
             uint64_t* out);
bool stringProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
                const char* type, std::string* out);
bool pathBytesProp(sd_bus* bus, const std::string& path, const char* iface, const char* name,
                   std::string* out);
std::string driveOf(sd_bus* bus, const std::string& block);
bool mountPoints(sd_bus* bus, const std::string& path, std::vector<std::string>* out);
std::vector<std::string> blockDevices(sd_bus* bus);
std::string blockPathFor(dev_t d);
bool callNoPrompt(sd_bus* bus, const std::string& path, const char* iface, const char* method,
                  std::string* mountedAt, std::string* why);
void waitReloaded(sd_bus* bus, const char* unitName, int seconds);

}  // namespace drives
