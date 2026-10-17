// A GameTDB controller list, as wii.cpp and wiiu.cpp each read their own
// (wii-controls.txt, wiiu-controls.txt): the letters of every controller a
// game takes, by its code. One object per list, so the two never merge; it
// is read once, on first use, from whichever thread asks first. Moved out of
// wii.cpp and wiiu.cpp in the restructure (#398), where it was written twice.

#pragma once

#include <map>
#include <mutex>
#include <string>

namespace cab {

class GameTdb {
public:
    // `envVar` names a list to read in place of `defaultPath`; `tag` and
    // `missing` are the log's: "[tag] no controller list at <path>; <missing>".
    GameTdb(const char* envVar, const char* defaultPath, const char* tag, const char* missing);

    // The letters of every controller the game takes, sorted, or "" when it is
    // not on the list.
    std::string controls(const std::string& code);

private:
    void load();

    const char* envVar_;
    const char* defaultPath_;
    const char* tag_;
    const char* missing_;
    std::map<std::string, std::string> controls_;
    std::once_flag loaded_;
};

}  // namespace cab
