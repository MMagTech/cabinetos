// A headless PS2 boot against upstream PCSX2, for answering questions that
// cannot be answered on the television.
//
// WHY IT EXISTS AT ALL, given that the point is a game on a 65-inch screen:
// linking a static library proves nothing about whether a host layer is
// complete — Cabinet's own comment on CabinetPS2Smoke.cpp says so, and it is
// why that file exists too. **An executable is the only thing that resolves
// symbols.** Building this is how a Host function this layer forgot gets found,
// and it found several.
//
// The second reason is the one that matters today. Two bugs are open against
// the LIBRETRO PlayStation 2 core and neither reproduces from a shell — a
// "tunnel" field of view while racing, and a pause-menu exit that hung the
// console. The libretro core is LRPS2, a hard fork off PCSX2 1.7.1; this is
// upstream 2.8.2. **Rendering the same game through both and comparing the
// pictures is a measurement rather than an argument**, and this is the end of
// it that did not exist before.
//
//   cabinet-ps2-probe --disc <path> --resources <dir> --data <dir>
//                     [--frames N] [--dump <dir> --dump-from N --dump-count N]
//                     [--upscale N] [--uncapped] [--verbose]
//
// It writes NO memory card unless one is named, and nothing here should name
// one: rom 604's card is the only real PS2 save on the reference server.

#include "CabinetPS2Host.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <string>
#include <thread>
#include <vector>

namespace
{
	void Usage(const char* argv0)
	{
		std::fprintf(stderr,
			"usage: %s --disc <path> --resources <dir> --data <dir>\n"
			"          [--frames N] [--dump <dir>] [--dump-from N] [--dump-count N]\n"
			"          [--upscale N] [--uncapped] [--verbose]\n",
			argv0);
	}

	const char* Param(int argc, char** argv, int& i)
	{
		if (i + 1 >= argc)
		{
			std::fprintf(stderr, "%s needs a value\n", argv[i]);
			std::exit(2);
		}
		return argv[++i];
	}
} // namespace

int main(int argc, char** argv)
{
	CabinetPS2::Config config;
	config.fast_boot = true;
	config.stop_after = 900;

	// Taps Start and Cross, once a second, from this frame on. It is how a
	// game gets past its own menus with nobody holding a pad, and it is the
	// only end-to-end check there is that input reaches the emulated machine
	// at all: everything upstream of the DualShock 2 can be correct and the
	// picture still never changes.
	uint32_t press_from = 0;

	for (int i = 1; i < argc; i++)
	{
		if (!std::strcmp(argv[i], "--disc"))
			config.disc_path = Param(argc, argv, i);
		else if (!std::strcmp(argv[i], "--resources"))
			config.resources_dir = Param(argc, argv, i);
		else if (!std::strcmp(argv[i], "--data")) {
			// One directory for the probe's convenience, laid out the way
			// PCSX2 lays out a data root. The console sets each of these
			// separately, because its BIOS, its saves and its scratch live in
			// three different places for three different reasons.
			const std::string root = Param(argc, argv, i);
			config.bios_dir = root + "/bios";
			config.memcards_dir = root + "/memcards";
			config.scratch_dir = root;
		}
		else if (!std::strcmp(argv[i], "--memory-card"))
			config.memory_card = Param(argc, argv, i);
		else if (!std::strcmp(argv[i], "--frames"))
			config.stop_after = static_cast<uint32_t>(std::atoi(Param(argc, argv, i)));
		else if (!std::strcmp(argv[i], "--dump"))
			config.dump_dir = Param(argc, argv, i);
		else if (!std::strcmp(argv[i], "--dump-from"))
			config.dump_first = static_cast<uint32_t>(std::atoi(Param(argc, argv, i)));
		else if (!std::strcmp(argv[i], "--dump-count"))
			config.dump_count = static_cast<uint32_t>(std::atoi(Param(argc, argv, i)));
		else if (!std::strcmp(argv[i], "--upscale"))
			config.upscale = static_cast<float>(std::atof(Param(argc, argv, i)));
		else if (!std::strcmp(argv[i], "--uncapped"))
			config.unlimited = true;
		else if (!std::strcmp(argv[i], "--press-from"))
			press_from = static_cast<uint32_t>(std::atoi(Param(argc, argv, i)));
		else if (!std::strcmp(argv[i], "--verbose"))
			config.verbose_log = true;
		else if (!std::strcmp(argv[i], "--help"))
		{
			Usage(argv[0]);
			return 0;
		}
		else
		{
			std::fprintf(stderr, "unknown argument: %s\n", argv[i]);
			Usage(argv[0]);
			return 2;
		}
	}

	if (config.disc_path.empty() || config.resources_dir.empty() || config.bios_dir.empty())
	{
		Usage(argv[0]);
		return 2;
	}

	// Default the dump window to somewhere a PS2 game is actually drawing.
	// **Dumping from frame zero produces a pile of black** — the BIOS splash,
	// the licence screen and a publisher logo all come first — and a black
	// capture is the single most misread result on this project.
	if (!config.dump_dir.empty() && config.dump_count == 0)
		config.dump_count = 3;

	std::atomic<bool> finished{false};
	std::string error;
	bool ok = false;

	// Run() blocks for the life of the game, exactly as Cabinet's does, so it
	// gets a thread and this one reports on it.
	std::thread vm([&] {
		ok = CabinetPS2::Run(config, &error);
		finished.store(true);
	});

	auto last = std::chrono::steady_clock::now();
	uint64_t last_frames = 0;
	bool held = false;

	// Drain the sound the way the frontend's mixer will, and keep the loudest
	// sample seen. **A SILENT EMULATOR THAT RUNS PERFECTLY IS A REAL FAILURE
	// MODE ON THIS PROJECT** — it happened with a PlayStation 2 game that
	// played with sound and a black screen, and the mirror image is just as
	// easy to ship. Counting samples proves the pipe is connected; the peak
	// proves something is actually in it.
	uint64_t audio_frames = 0;
	int16_t audio_peak = 0;
	std::vector<int16_t> audio;
	while (!finished.load())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(press_from ? 120 : 500));

		// 48000 Hz stereo, and this loop runs about eight times a second at
		// most, so ask for a generous slice. Anything not produced yet simply
		// comes back as the silence AudioStream pads with.
		audio.clear();
		CabinetPS2::DrainAudio(&audio, 4096);
		audio_frames += audio.size() / 2;
		for (int16_t s : audio)
			audio_peak = std::max<int16_t>(audio_peak, static_cast<int16_t>(s < 0 ? -s : s));

		const CabinetPS2::Metrics mid = CabinetPS2::GetMetrics();
		if (press_from != 0 && mid.frames >= press_from)
		{
			// Alternate held and released. A button that is never let go is a
			// button most menus ignore after the first frame.
			held = !held;
			CabinetPS2::Pad pad;
			if (held)
				pad.buttons = (1u << 3) | (1u << 0); // Start and Cross (B)
			CabinetPS2::SetPad(0, pad);
		}

		const CabinetPS2::Metrics m = CabinetPS2::GetMetrics();
		const auto now = std::chrono::steady_clock::now();
		const double secs = std::chrono::duration<double>(now - last).count();
		if (m.frames != last_frames)
		{
			// readback is the number the picture path rests on: it is what
			// reading one finished frame out of the GS costs, and whether it
			// is small enough decides whether PS2 can reach the frontend this
			// way or needs PCSX2's Vulkan image shared directly.
			std::printf("[probe] frames=%llu fps=%.1f speed=%.0f%% readback=%.0fus (+%llu in %.1fs)\n",
				static_cast<unsigned long long>(m.frames), m.fps, m.speed, m.readback_us,
				static_cast<unsigned long long>(m.frames - last_frames), secs);
			std::fflush(stdout);
			last_frames = m.frames;
			last = now;
		}
	}

	vm.join();

	if (!ok)
	{
		std::fprintf(stderr, "[probe] FAILED: %s\n", error.c_str());
		return 1;
	}

	const CabinetPS2::Metrics m = CabinetPS2::GetMetrics();
	std::printf("[probe] done, %llu frames\n", static_cast<unsigned long long>(m.frames));

	// The last frame that reached the handover, proving the readback produced
	// a real picture and not an empty buffer — the distinction this project has
	// misread more than once.
	std::printf("[probe] audio: %llu frames drained at %u Hz, loudest sample %d\n",
		static_cast<unsigned long long>(audio_frames), CabinetPS2::AudioSampleRate(), audio_peak);
	if (audio_peak == 0)
		std::printf("[probe] THAT IS SILENCE. The pipe is connected and nothing is coming through.\n");

	CabinetPS2::Frame frame;
	if (CabinetPS2::TakeFrame(&frame, 0))
	{
		uint32_t brightest = 0;
		for (uint32_t px : frame.pixels)
		{
			const uint32_t r = px & 0xFF, g = (px >> 8) & 0xFF, b = (px >> 16) & 0xFF;
			brightest = std::max(brightest, std::max(r, std::max(g, b)));
		}
		std::printf("[probe] last frame %ux%u, brightest channel %u\n", frame.width, frame.height, brightest);
		if (brightest == 0)
			std::printf("[probe] THAT IS A BLACK FRAME. The readback ran and the picture is empty.\n");
	}
	else
	{
		std::printf("[probe] no frame ever reached the handover\n");
	}
	return 0;
}
