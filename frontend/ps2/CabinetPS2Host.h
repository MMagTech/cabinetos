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
#include <vector>

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

	/// The finished picture, as the GS produced it.
	///
	/// **THIS IS THE SAME SHAPE EIGHTEEN OF THE TWENTY-ONE LIBRETRO CORES
	/// ALREADY HAND THE FRONTEND** — a buffer of pixels, a width and a height —
	/// which is why PS2 needs no new picture path in the UI at all. PCSX2
	/// renders surfacelessly on its own thread and the finished frame is read
	/// back here; the frontend uploads it exactly as it uploads a Mega Drive's.
	///
	/// The alternative was sharing PCSX2's Vulkan image with the frontend
	/// directly, which is faster and needs upstream to enable two external
	/// memory extensions it does not. That is a real option and it is written
	/// up in docs/PCSX2-HOST-SURFACE.md; it is not the first thing to build,
	/// because this one costs nothing to try and the cost of the readback is a
	/// measurement rather than a guess.
	struct Frame
	{
		std::vector<uint32_t> pixels; // RGBA, `width * height` of them.
		unsigned width = 0;
		unsigned height = 0;
		uint64_t serial = 0; // Bumped every time a new frame lands.
	};

	/// Takes the newest frame if there is one newer than `since`. Safe from any
	/// thread; returns false and touches nothing when there is nothing new.
	///
	/// Copies rather than lending, deliberately: the GS thread overwrites its
	/// side whenever it likes, and a frontend holding a borrowed buffer across
	/// a draw call is a data race that would show up as tearing on a
	/// television and as nothing at all in a capture.
	bool TakeFrame(Frame* out, uint64_t since);

	/// A controller, in **exactly** the shape the frontend already produces.
	///
	/// This mirrors `PadState` in frontend/src/core.h field for field, and
	/// deliberately so: the frontend fills one of those in for all twenty-one
	/// libretro cores, and PlayStation 2 taking a different shape would mean
	/// two input paths to keep in step. This layer does the translation to
	/// PCSX2's DualShock 2 numbering, which is one table in one file.
	///
	/// `buttons` is a bitmask using **libretro's RetroPad ids as bit
	/// positions** — B is bit 0, Start is bit 3, and so on — which is what the
	/// frontend already builds and tests with `(buttons >> id) & 1`.
	struct Pad
	{
		uint32_t buttons = 0;
		float leftX = 0, leftY = 0;
		float rightX = 0, rightY = 0;

		/// HOW FAR THE TRIGGERS ARE PRESSED, 0 to 1, as well as the L2/R2 bits
		/// above — both, for the reason `PadState` gives: a trigger is two
		/// things at once, and the same physical pull has to serve a game that
		/// reads it as a continuous value and one that reads it as a switch.
		///
		/// **ANSWERING THIS FROM THE RIGHT STICK IS A REAL BUG THIS CONSOLE
		/// SHIPPED.** `RETRO_DEVICE_INDEX_ANALOG_BUTTON` is libretro's third
		/// analogue index and its id is a joypad button id, so code that tests
		/// for the LEFT index and treats everything else as the right stick
		/// answers "how far is the trigger pressed" with the stick's Y axis.
		/// That is why Crazy Taxi could not be driven until 2026-09-19.
		float leftTrigger = 0, rightTrigger = 0;
	};

	/// Hands a controller's state to the running game. Safe from any thread;
	/// it is applied on PCSX2's own CPU thread between frames.
	void SetPad(unsigned port, const Pad& pad);

	/// Drains the sound made since the last call, as interleaved 16-bit stereo
	/// — the same shape and the same contract as `Core::drainAudio()`, so the
	/// frontend's mixer needs to learn nothing new.
	///
	/// **THE CONSOLE OWNS AUDIO, AS IT DOES FOR EVERY OTHER CORE.** PCSX2 can
	/// make its own sound through cubeb or SDL and must not here: a second
	/// device open on the same hardware is a second volume, a second latency,
	/// and nothing the in-game overlay can duck. Getting PCSX2 to hand its
	/// sound over instead is this project's one patch to the emulator, and
	/// `cores/build-pcsx2.sh` says why beside it.
	void DrainAudio(std::vector<int16_t>* out, unsigned frames);

	/// The rate PCSX2 is producing at — 48000 for a PlayStation 2.
	unsigned AudioSampleRate();

	/// Where PCSX2 will put the card named in `Config::memory_card`, given the
	/// same data root. The frontend restores into this path before Run and
	/// captures from it after Run returns — **after**, because PCSX2 flushes
	/// the card during shutdown and a capture taken any earlier is the card as
	/// it was when the game started. That is the same rule the libretro path
	/// already follows with `retro_unload_game`, and it was paid for once.
	std::string MemoryCardPath(const std::string& data_root, const std::string& name);

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
		/// What reading one finished frame back out of the GS costs. The whole
		/// picture path rests on this being small enough; see TakeFrame.
		double readback_us;
	};

	Metrics GetMetrics();
} // namespace CabinetPS2
