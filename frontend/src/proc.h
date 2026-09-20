// Running another program, without a shell anywhere in it.
//
// WHY THIS IS ITS OWN FILE. Two things in this console shell out — the
// NetworkManager plumbing and the Bluetooth plumbing — and both of them feed
// strings CHOSEN BY STRANGERS into the command they run. A Wi-Fi scan returns
// SSIDs that anybody within radio range picked; a Bluetooth scan returns device
// names that anybody within radio range picked. Somebody who names their
// network `"; rm -rf ~"` should be a row in a list and nothing else.
//
// So there is exactly ONE place in this program that starts a process, it takes
// an argv array, and it never builds a command line. Duplicating that into two
// files would be duplicating the part that must not be got wrong twice.
//
// IT BLOCKS, AND IT SAYS SO IN ITS NAME'S PLACE — here, because there is
// nowhere else to say it. A Wi-Fi scan takes seconds and a Bluetooth scan takes
// tens of them. Callers run this on a worker, exactly as romm::Client is run,
// and a frame loop that calls it has stopped drawing.

#pragma once

#include <string>
#include <vector>

namespace proc {

struct Result {
    // The process's exit status, or -1 when it never ran at all. Those are
    // different failures: a missing binary and a binary that ran and refused
    // need different sentences.
    int status = -1;
    bool timedOut = false;
    // Captured separately because they answer different questions: a tool puts
    // its data on one and its reason for failing on the other, and the reason
    // is the half a person can act on.
    std::string out;
    std::string err;

    bool ok() const { return status == 0; }
};

// argv[0] is looked up on PATH. Nothing is expanded, globbed or quoted, because
// nothing is parsed: this is fork, dup, execvp.
//
// The child's stdin is /dev/null. That is not tidiness — `nmcli` and
// `bluetoothctl` both prompt when they want something, and a child that
// inherits a terminal can sit on that prompt until the deadline.
Result run(const std::vector<std::string>& args, int timeoutSeconds);

// Whitespace off both ends. Every one of these tools newline-terminates
// everything, and a trailing newline in an error message reads badly on a
// television.
std::string trimmed(const std::string& s);

// Split on newlines, dropping a trailing empty line.
std::vector<std::string> lines(const std::string& s);

}  // namespace proc
