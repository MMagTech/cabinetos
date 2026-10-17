// The address of the RomM server, first run's one lasting output, and where
// it comes from. In namespace firstrun, as before, so none of its callers
// changes; firstrun.h includes this. Moved out of it in the restructure
// (#398).

#pragma once

#include <string>

namespace firstrun {

// --- The server address, which is first run's one lasting output ------------
//
// WHERE IT LIVES AND WHY IT IS NOT /etc. The session script reads
// /etc/cabinetos/session.env, which is root-owned; the session runs as an
// unprivileged user and cannot write it. Rather than invent a privileged helper
// for one string that is not a secret, first run writes its own file next to
// the console's other machine-local state, and `main` reads both.
//
// ROOT'S ANSWER WINS. /etc/cabinetos/session.env is what root put there and is
// the documented way to set a console up by hand; a file the session wrote must
// not silently override it. So the order is: --romm, then $CABINETOS_ROMM, then
// session.env itself, then this. First run only ever writes its own file when
// the two before it said nothing.
//
// WHY session.env IS READ DIRECTLY AND NOT ONLY THROUGH THE ENVIRONMENT. The
// session script sources it and exports it, so a frontend started by the
// session sees it either way — but one started over SSH does not, and that is
// how every check of this gets made. Without this, `--first-run` on the
// reference console reports "NEEDED" on a machine that has been set up and
// working for a day. An instrument that lies about the thing it exists to
// report is worse than no instrument, which this project has now paid for
// twice.
std::string serverAddress();

// Which of the four it came from, for a report that has to be trusted.
std::string serverAddressSource();

bool setServerAddress(const std::string& address, std::string* err);

// Removes the address first run wrote, for signing out. Root's answer, if
// there is one, is not this program's to remove and stays.
void forgetServerAddress();

}  // namespace firstrun
