// The diagnostic report (#195): making it, and handing it to a phone.
//
// WHAT MAKES IT IS NOT HERE. /usr/libexec/cabinetos-report gathers the report
// and replaces everything private in it; this only runs it, so the same file
// comes from Settings and from a command over Developer access, and a console
// whose frontend will not start can still make one.
//
// HOW IT LEAVES THE CONSOLE (MMagTech, 2026-09-30): the QR code on the screen
// is a link to the file, not the report. While the screen is open the console
// serves that one file on the home network, at an unguessable path, and
// closing the screen stops it. Nothing else is served and nothing is listening
// otherwise. The phone has to be on the same network; nothing is uploaded
// anywhere. The file is also in File access, under reports.

#pragma once

#include <string>

namespace report {

struct Made {
    bool ok = false;
    std::string path;   // the report, when ok
    std::string why;    // a short reason, when not
};

// Runs the report tool for this storage root. BLOCKS for a few seconds (about
// three on the A9): call it on a worker.
Made make(const std::string& storageRoot);

// Serves `path` at http://<address>:<port>/<token> until stop(), on a thread
// of its own. `address` is this console's address on the home network
// (IPv4); the link is bound to it alone, so it is not reachable over
// Tailscale. Fills `url` with the whole link.
bool serve(const std::string& path, const std::string& address, std::string* url,
           std::string* why);
void stop();
bool serving();

}  // namespace report
