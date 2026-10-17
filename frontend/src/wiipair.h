// Pairing a Wii Remote: the Wii's own search, pairing, and forgetting one.
// wiiremote.h says how it works and includes this; moved out of it in the
// restructure (#398).

#pragma once

#include <atomic>
#include <string>
#include <vector>

namespace wiiremote {

// --- Pairing (slow: call from a worker thread, never the frame thread) ------

// The Wii's own search, through the root helper. Fills `found` with the
// address of each Wii Remote that answered (may be none). False, with `err`,
// only when the search itself could not run.
bool search(std::vector<std::string>* found, std::string* err,
            const std::atomic<bool>* cancel = nullptr);

// Pairs, trusts and connects the Remote at `address`. False, with `err`.
bool pair(const std::string& address, std::string* err);

// Forgets a paired Remote.
bool remove(const std::string& address, std::string* err);

}  // namespace wiiremote
