// The Xbox 360 profile the console signs in for every game, in Xenia Edge's
// own file format.
//
// ONE PROFILE ID, FOR GOOD, ON EVERY CONSOLE. Measured on the A9 2026-09-29:
// Forza Horizon 2 calls its save "tampered" under any profile ID but the one
// it was made under, and loads it when copied in under its own. So every
// console, and every person on it, uses `kXuid`, and it must never change: a
// new one strands every save made under the old. The same rule as the
// original Xbox's EEPROM (tools/xbox-eeprom.py). Someone leaving CabinetOS
// copies a save into their own Xenia under this ID and it loads.
//
// THE NAME IS THE PERSON'S. What games show (a lobby, a menu) is the name on
// Home, cut to the 360's fifteen characters (MMagTech, 2026-09-29). It lives
// only in the profile file, written before every game, so nothing about it is
// kept per person and a person removed and added back needs nothing rebuilt.
//
// THE FILE. `content/<XUID>/FFFE07D1/00010000/<XUID>/Account`, 404 bytes: a
// 16-byte HMAC-SHA1 of the rest, then an RC4 of an 8-byte confounder and
// Xenia's X_XAMACCOUNTINFO (0x17C bytes, big-endian, everything zero but the
// gamertag), keyed by an HMAC of that hash. The key is XeKey 0x19, as Xenia
// Edge publishes it (src/xenia/kernel/util/crypto_utils.cc); the method is
// its ProfileManager::EncryptAccountFile. Checked 2026-09-29 against a
// profile Edge made itself: decrypted, and rebuilt byte for byte. The profile
// ID is the folder's name, not in the file. docs/PROJECT.md, open question 34.

#pragma once

#include <string>

namespace cab::x360profile {

// E03 is an offline profile, as Xenia makes them; 43414231 is "CAB1".
constexpr const char* kXuid = "E030000043414231";

// The name a game shows for `name`: what an Xbox 360 would accept, letters,
// digits and single spaces (punctuation becomes a space), at most fifteen
// characters. "Player" when nothing is left.
std::string gamertag(const std::string& name);

// Writes the profile, signed in as `gamertag`, to `path` (its folders made).
bool writeAccount(const std::string& path, const std::string& gamertag, std::string* err);

// The gamertag in a profile file, or empty if it does not decrypt. For checks.
std::string readAccount(const std::string& path);

// Where the profile goes under a content folder.
std::string accountPath(const std::string& contentRoot);

}  // namespace cab::x360profile
