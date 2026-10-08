// Remote Play: Sunshine on the console, Moonlight on a phone, tablet or
// computer to play from. docs/SETTINGS.md, Remote Play; issue #286.
//
// OFF BY DEFAULT, AND OFF MEANS NOTHING RUNS: Sunshine is
// cabinetos-remoteplay.service, started and stopped by the switch, and this
// module asks Sunshine nothing unless watch(true) was called, which the app
// does only while the switch is on.
//
// PAIRING IS ON THE TELEVISION, never on Sunshine's web page (which answers
// the console alone, origin_web_ui_allowed = pc). Moonlight asks to pair and
// shows four digits; Sunshine holds the request for five minutes; the console
// sees it here, asks for the digits with its own PIN pad, and hands them over.
// That is Sunshine's own API (GET and POST /api/pin, /api/clients/list and
// /unpair) on localhost, with a login the console made for itself
// (/usr/libexec/cabinetos-remoteplay).
//
// THE CALLS ARE QUICK BUT NOT FREE, so they are made on a thread of their own
// every two seconds while watching, and the app reads the last answer.

#pragma once

#include <string>
#include <vector>

namespace remoteplay {

// A device that is paired, by the name the console gave it.
//
// THE NAMES ARE THE CONSOLE'S. Moonlight sends the same word, "roth", from
// every phone, tablet and computer (left over from its first versions), so
// the name a device asks with says nothing, and Sunshine's API can enable,
// disable or remove a paired device but not rename it. So the console keeps
// its own names, by Sunshine's uuid, in /var/lib/cabinetos/remoteplay/
// names.json, and "roth" is never shown.
struct Device {
    std::string id;     // Sunshine's uuid for it, for rename() and remove()
    std::string name;
};

// A device asking to pair right now. Its own name is "roth" (above), so
// there is nothing to show of it.
struct Request {
    std::string id;     // Sunshine's pairing id, for pair()
};

// Start or stop asking Sunshine. While on, a thread asks every two seconds.
//
// `fresh` when Sunshine has only just been started (the switch turned on):
// nothing is streaming yet, and the log already there is the last Sunshine's.
// Read as current, a stream switched off mid-way (no "CLIENT DISCONNECTED"
// written) looked like a device still streaming, and the console set its own
// controllers aside with nobody playing (A9, 2026-10-08). Without it (the
// console starting under a running Sunshine, back from Steam), the log is read
// from its start, which is how a stream already open is found.
void watch(bool on, bool fresh = false);

// WHETHER A DEVICE IS STREAMING NOW: Sunshine's own "CLIENT CONNECTED" and
// "CLIENT DISCONNECTED" lines, read from its log as they are written. Sunshine
// has no hook or API call for a connection that drops (a phone locked, out of
// range), and keeps that device's controllers afterwards, so this is the one
// place the console can learn it. The lines are those of the pinned version
// (build_files/install-sunshine.sh); moving the pin means checking them.
bool streaming();

// The last answers. `generation()` changes when either list does, so the app
// rebuilds its rows only then.
std::vector<Device> paired();
std::vector<Request> waiting();
int generation();

// Hand over the four digits for a request. True when Sunshine took them; a
// wrong code is only known to Moonlight, which then says pairing failed, and
// the request is gone.
bool pair(const std::string& requestId, const std::string& digits, const std::string& name,
          std::string* why);
// Turn a request down.
bool decline(const std::string& requestId);
// Forget a paired device: it has to pair again to play.
bool remove(const std::string& deviceId, std::string* why);
// THE SAME DEVICE PAIRED BEFORE: the other paired entries holding the same
// client certificate as `deviceId`. Moonlight keeps one certificate per
// install, so a phone that pairs again arrives as a second entry with the
// first one's certificate, and Sunshine then refuses that certificate
// outright ("Client certificate identity is not enabled": it must match
// exactly one entry). Read from Sunshine's state file, since its API does
// not show certificates. Found 2026-10-07, MMagTech's iPhone paired twice.
// A STOPGAP: Sunshine fixed it itself in v2026.1007.173111, a pre-release
// (a completed pairing replaces every record with the same certificate,
// LizardByte/Sunshine#5696). When the pin moves to a stable release with
// that fix, delete this and its use in main.cpp. Until then a phone pairing
// again can fail once, if Moonlight checks before the console has tidied.
std::vector<std::string> samePairedDevice(const std::string& deviceId);
// Give a paired device another name. The console's alone (see Device).
void rename(const std::string& deviceId, const std::string& name);
// "Device 1", "Device 2"...: the first not already a paired device's name.
std::string nextName();

}  // namespace remoteplay
