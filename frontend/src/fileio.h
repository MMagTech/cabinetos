// Whole files read and written. cab::readBytes, writeBytes and removeFile
// came out of filesave; sanitisedStem and writeLocal out of main.cpp, as they
// were (#398, Part 3 section 21).
//
// writeLocal IS NOT writeBytes: it truncates the file in place, makes no
// folders and does not check fclose. Moving its callers to writeBytes changes
// what survives a power cut, so that is a separate, tested step.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace cab {

std::vector<uint8_t> readBytes(const std::string& path);

// Writes through a temporary and renames, so a console losing power halfway
// through placing a card leaves the old one rather than half of a new one.
// Creates the parent directories.
bool writeBytes(const std::string& path, const std::vector<uint8_t>& data);

// Removes the file, and reports only whether it is gone afterwards. A file
// that was never there is a success.
bool removeFile(const std::string& path);

}  // namespace cab

std::string sanitisedStem(const std::string& title);
bool writeLocal(const std::string& path, const std::vector<uint8_t>& data);
