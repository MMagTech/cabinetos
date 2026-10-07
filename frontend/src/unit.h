// Start or stop one of the console's own root units, over D-Bus, with no
// password prompt. The polkit rules in /usr/share/polkit-1/rules.d/ name
// exactly which units the session may start or stop; anything else is
// refused, and `why` says so.
//
// Returns once systemd has taken the job, not when the unit has finished:
// each unit answers in a status file of its own.

#pragma once

#include <string>

namespace unit {

bool start(const char* name, std::string* why);
bool stop(const char* name, std::string* why);
bool reload(const char* name, std::string* why);

// The unit's ActiveState as systemd has it: "active", "activating",
// "deactivating", "inactive", "failed", or "" when systemd cannot be asked.
// For a unit that runs while it is on (Developer access's sshd), where there
// is no status file of its own to read. Reading needs no permission.
std::string state(const char* name);

}  // namespace unit
