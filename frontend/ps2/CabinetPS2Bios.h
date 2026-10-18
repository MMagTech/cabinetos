// Which PlayStation 2 BIOS a game starts on, chosen by the game's region
// (#416, "PCSX2 loads the wrong bios for the region of the game").
//
// PCSX2 LEFT TO ITSELF TAKES THE FIRST BIOS IT FINDS (BiosTools.cpp,
// FindBiosImage), in the folder's order and whatever the game, so a console
// with a Japanese BIOS beside its USA one played every game on whichever came
// first. The rule is MMagTech's (#416, 2026-10-18), the same for every
// console:
//
//   1. a BIOS of the game's region, the newest if there are several;
//   2. if there is none, the newest USA BIOS;
//   3. if there is no USA BIOS either, the newest there is.
//
// So a console with one BIOS always plays on it. The game's region comes from
// its disc code as RomM reads it off the disc (`title_id`, `SLUS-21090`),
// never from a file name; a game RomM has no code for goes to step 2. A
// BIOS's region and version are PCSX2's own reading of its contents (IsBIOS,
// the ROMVER entry), so a BIOS's file name does not matter either.
//
// Neither RetroArch nor Batocera chooses (checked 2026-10-18): RetroArch's
// PS2 core lists every BIOS as a core option whose default is the first one
// found, and Batocera points PCSX2 at its BIOS folder and lets it find one.
//
// Switching BIOS between games costs nothing else here: each game has its own
// memory card, PCSX2 keeps the PS2's settings (NVRAM) in a file beside each
// BIOS, and the console takes no PS2 save states, the one thing PCSX2 warns
// about when the BIOS changes.

#pragma once

#include <string>

namespace CabinetPS2
{
	/// The file name, inside `bios_dir`, of the BIOS to start this game on,
	/// with one `[ps2] BIOS ...` line on stderr saying which and why, so the
	/// diagnostic report shows it. Empty when the folder holds no PS2 BIOS,
	/// which leaves PCSX2 to look and fail as it always has.
	std::string ChooseBios(const std::string& bios_dir, const std::string& disc_code);
} // namespace CabinetPS2
