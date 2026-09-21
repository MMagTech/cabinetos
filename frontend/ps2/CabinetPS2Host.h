// CabinetOS's host layer for upstream PCSX2.
//
// PCSX2 is not a libretro core and has no retro_run. It is a whole emulator
// that expects a frontend to exist around it: it calls out through the `Host`
// namespace for its settings, its window, its lifecycle and its messages, and
// upstream ships exactly two implementations of that — the Qt desktop app and
// the headless GS runner. Neither can run inside this console, so this is the
// third. Cabinet wrote the same layer for the Mac and called it the same thing.
//
// WHAT IT OWES PCSX2 IS MEASURED, NOT GUESSED: 55 `Host::` functions, three
// `InputManager::ConvertHostKeyboard*` and the `g_host_hotkeys` table. The
// count comes from linking with `-Wl,-z,defs` and reading what the linker
// refuses. docs/PCSX2-HOST-SURFACE.md has the whole list and where it came
// from, and `cores/build-pcsx2.sh` re-derives it on demand.
//
// THREADING, AND IT IS THE SAME SHAPE CABINET FOUND ON THE MAC: Run() blocks
// for the entire life of the game and must be given a thread of its own. PCSX2
// starts its own GS thread underneath it. RequestStop() is safe from any
// thread and is the only way out.

#pragma once

#include <cstdint>
#include <string>

namespace CabinetPS2
{
	struct Config
	{
		/// The disc image. CHD, ISO and the other formats CDVD knows.
		std::string disc_path;

		/// Where PCSX2 keeps BIOS, memory cards, save states and its cache.
		/// CabinetOS lays this out; PCSX2 only reads it. On a console this is
		/// under storage::root(), the same tree every other core already uses.
		std::string data_root;

		/// PCSX2's own resources folder — the game database, the GS shaders and
		/// the fonts. **Startup FAILS without it rather than degrading**, which
		/// is worth knowing before blaming anything else: it prints "Resources
		/// directory is missing" and stops.
		std::string resources_dir;

		/// The memory card for this game, a bare filename PCSX2 resolves inside
		/// its memcards folder. EMPTY MEANS NO CARD AT ALL, which is what the
		/// probe wants: `pcsx2_shared_memory_cards` on the libretro core put
		/// every game's save in one file belonging to no rom, and nothing that
		/// runs before the save work is wired up should be able to write to a
		/// real card by accident.
		std::string memory_card;

		/// Skips the BIOS splash, as every frontend does.
		bool fast_boot = true;

		/// Runs as fast as it can rather than pacing to 60 Hz. For measurement
		/// only: a capped run reports 100% speed whether there is four times
		/// the headroom or none at all.
		bool unlimited = false;

		/// PCSX2's own log. It is the only way to answer questions like whether
		/// the recompilers took, so it is worth having rather than inferring
		/// from behaviour.
		bool verbose_log = false;

		/// Where to write PNG frames, empty for none. The capture goes through
		/// PCSX2's own GSQueueSnapshot, so it is the picture the GS actually
		/// produced rather than anything this layer re-encodes.
		std::string dump_dir;

		/// Which frame to start dumping at, and how many. A PS2 game needs
		/// hundreds of frames to get past its BIOS and logos, so dumping from
		/// zero produces a pile of black.
		uint32_t dump_first = 0;
		uint32_t dump_count = 0;

		/// Stop after this many frames, 0 for no limit.
		uint32_t stop_after = 0;

		/// The GS upscale multiplier. 1.0 is the PS2's native resolution.
		float upscale = 1.0f;
	};

	/// Boots the disc and runs until RequestStop, the frame limit, or the game
	/// ending. BLOCKS — give it its own thread. Returns false and fills error
	/// if the VM never started.
	bool Run(const Config& config, std::string* error);

	/// Asks the running game to stop. Safe from any thread.
	void RequestStop();

	bool IsRunning();

	/// Live performance, all zero when nothing is running.
	struct Metrics
	{
		float fps;
		float speed;
		uint64_t frames;
	};

	Metrics GetMetrics();
} // namespace CabinetPS2
