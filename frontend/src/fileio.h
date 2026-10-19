// Whole files read and written. cab::readBytes, writeBytes and removeFile
// came out of filesave; sanitisedStem out of main.cpp, as it was (#398, Part
// 3 section 21).
//
// EVERY FILE THE CONSOLE REPLACES GOES THROUGH writeBytes (#482): a
// temporary beside it, every byte checked, fsync, then a rename. A power cut
// or a full disk halfway through leaves the old file, never an empty or cut
// one. The fsync matters on the console's btrfs, which commits every 30 s: a
// renamed file whose bytes were never synced can come back empty.

#pragma once

#include <sys/types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace cab {

std::vector<uint8_t> readBytes(const std::string& path);

// Writes `<path>.placing`, checks and syncs it, then renames it over `path`.
// Creates the parent directories. `mode` is the new file's (0600 for a
// credential), less the umask, set before any byte is written. False, with
// the old file untouched and no temporary left, on any failure.
bool writeBytes(const std::string& path, const void* data, size_t size, mode_t mode = 0666);
bool writeBytes(const std::string& path, const std::vector<uint8_t>& data, mode_t mode = 0666);
bool writeBytes(const std::string& path, const std::string& text, mode_t mode = 0666);

// Removes the file, and reports only whether it is gone afterwards. A file
// that was never there is a success.
bool removeFile(const std::string& path);

}  // namespace cab

std::string sanitisedStem(const std::string& title);

// The stem as RomM keeps an uploaded file's name (sanitize_filename in
// backend/utils/filesystem.py, 5.3.1 and 5.4.0): \ / : | become '-', and
// * ? " < > and control characters go, as do leading spaces. RomM 5.3.1
// also drops '+', 5.4.0 keeps it; dropping it always matches both. A state
// or screenshot named with this is listed on RomM under the name it was
// sent with, which is what state rotation compares (#484, ROMM-02).
std::string rommKeptStem(const std::string& stem);
