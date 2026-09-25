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

}  // namespace unit
