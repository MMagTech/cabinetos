// Start or stop one of the console's own root units, over D-Bus, with no
// password prompt. The polkit rules in /usr/share/polkit-1/rules.d/ name
// exactly which units the session may start or stop; anything else is
// refused, and `why` says so.
//
// Returns once systemd has taken the job, not when the unit has finished:
// each unit answers in a status file of its own.

#pragma once

#include <functional>
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

// WAITS FOR A ONESHOT UNIT JUST STARTED TO DO ITS JOB: until `done` says so,
// or the unit is no longer running, or `seconds` have passed, looking four
// times a second. True when `done` was reached. A oneshot is "activating"
// while its script runs and "inactive" or "failed" after; the first look can
// come before systemd has begun the job, so "inactive" ends the wait only
// once the unit was seen running, or after a second of never seeing it.
// Until this (#481, FILES-08) the drive claim waited its whole ten seconds
// for a folder a helper could not make.
bool awaitOneshot(const char* name, double seconds, const std::function<bool()>& done);

}  // namespace unit
