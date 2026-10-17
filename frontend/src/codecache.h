// The codes read off game files when RomM had none, remembered by rom id in
// config/<name> so a file is read once: wii-codes.txt and wiiu-codes.txt, one
// object each. Moved out of wii.cpp and wiiu.cpp in the restructure (#398),
// where it was written twice.

#pragma once

#include <map>
#include <mutex>
#include <string>

namespace cab {

class CodeCache {
public:
    // `fileName` is in configDir(), which is asked on every use: the root is
    // chosen at run time, after this object exists.
    explicit CodeCache(const char* fileName);

    // The code remembered for the rom, which may be "" (the file was read and
    // held none); `known` says whether there is an answer at all.
    std::string remembered(int romId, bool* known);
    void remember(int romId, const std::string& code);

private:
    std::string path() const;
    void read();

    const char* fileName_;
    std::mutex lock_;
    std::map<int, std::string> codes_;
    bool read_ = false;
};

}  // namespace cab
