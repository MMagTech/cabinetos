// File access, the session's half. docs/SETTINGS.md, Storage; issue #69.
//
// Root does the work (/usr/libexec/cabinetos-files, behind
// cabinetos-files.service and cabinetos-files-password.service); this starts
// and stops those, reads the one state file they write, and reads the
// password so Settings can show it. What it means on screen is the app's.

#pragma once

#include <cstdint>
#include <string>

namespace files {

struct State {
    bool on = false;
    bool failed = false;
    std::string reason;   // failed
    int64_t at = 0;       // when root wrote it, epoch seconds; 0 if never
};

// For the TV loop: read the state and the password from here instead.
// `--files-dir`, holding `state` and `password`.
void setDir(const std::string& dir);

State read();
std::string password();   // "XXXX-XXXX", empty until first turned on

bool turnOn(std::string* why);
bool turnOff(std::string* why);
bool newPassword(std::string* why);

}  // namespace files
