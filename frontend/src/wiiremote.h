// Real Wii Remotes: pairing them, setting each one up as it connects, and
// reading its buttons for the console's own menus. docs/PROJECT.md open
// question 35, "DECIDED, MMagTech 2026-10-02"; issue #200.
//
// WHAT A WII REMOTE IS HERE: anything whose Bluetooth name starts
// "Nintendo RVL-CNT", the test Dolphin and Wii software use. Never the device
// id: MMagTech's TechKen copies report Samsung's wireless keyboard id
// (04E8:7021), so the kernel's Wii Remote driver and SDL both pass them by.
// What the console gets is a raw HID node (/dev/hidrawN) and nothing else,
// which is also what Dolphin reads, so the console drives the Remote itself.
//
// PAIRING, measured on the A9 2026-10-02:
//   1. A copy answers only the Wii's own search, the limited inquiry, which
//      only root can run: cabinetos-wii-search.service does it.
//   2. bluetoothd must be told of the Remote before it can pair it; an L2CAP
//      connect to its control channel does that, as `cabinet`.
//   3. Device1.Pair through bluetoothd, so its PIN callbacks run. BlueZ offers
//      the Wii PIN first (the console's address, reversed), which Nintendo's
//      Remotes take; the TechKens refuse it, and BlueZ retries and asks this
//      session's agent, which answers 0000. One rule for both.
//   4. Trusted, so it may reconnect by itself when a button is pressed.
//
// SET UP ON EVERY CONNECT, or it sits connected in the dark: a TechKen left
// without host commands showed no light, hung up after 3 seconds and called
// again, about 230 times in 17 minutes. Sent each time: a status request, its
// player light, and a report mode (buttons, continuous). A status report
// from the Remote (an extension plugged in or out) stops its reports until the
// mode is sent again, so it is.

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

struct Paired {
    std::string address;
    std::string name;
    bool connected = false;
    int light = 0;   // 1-4 while connected out of a game, else 0
};
// Every paired Wii Remote, as refresh() last read them, with whether each is
// connected now. Cheap: for drawing Settings.
std::vector<Paired> known();

// --- Whether Remote games are playable ---------------------------------------

// True while any Wii Remote is paired. Cheap: answered from what refresh()
// last read. Remote games un-grey on it (PROJECT.md question 35).
bool anyPaired();
// Reads it again from bluetoothd. Slow; run at start and after pair/remove.
void refresh();

// --- The running Remotes ------------------------------------------------------

// Reads whether a Remote is paired (one bluetoothctl call), then starts the
// thread that finds connected Remotes, sets each up with its player light,
// and turns their buttons into the console's own controller presses (SDL
// gamepad events from a pad id no real pad has). Idempotent.
void start();

// How many Remotes are connected now.
int connected();

// While a game runs Dolphin drives the Remotes: the console stops sending
// them anything and reads only HOME, held, for its own menu. When the game
// ends every Remote is set up again with its own light.
void setGameRunning(bool running);

// True once per hold: HOME held about a second on any Remote while a game
// runs. A press of HOME is the game's own HOME menu, as on a Wii.
bool takeHomeHold();

// EACH REMOTE THAT IS ON, out of a game, for the battery row on Home (#293):
// its light (W1-W4), its battery byte (-1 until it has answered; the Wii's own
// scale, battery::segmentsOfWii) and its own low flag. In a game Dolphin has
// the Remotes and this is empty. Cheap.
struct Battery {
    int light = 0;
    std::string address;
    int byte = -1;
    bool low = false;
};
std::vector<Battery> batteries();

// --- The sensor bar -----------------------------------------------------------

// Where the sensor bar sits, as a Wii's own settings ask it: above the TV or
// below it. Below unless set, which is Dolphin's default too. Saved in
// config/settings.json; Dolphin is told through dolphin_sensor_bar_position.
bool sensorBarAbove();
void setSensorBarAbove(bool above);

}  // namespace wiiremote
