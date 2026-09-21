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
#include <string>
#include <thread>

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

	for (int i = 1; i < argc; i++)
	{
		if (!std::strcmp(argv[i], "--disc"))
			config.disc_path = Param(argc, argv, i);
		else if (!std::strcmp(argv[i], "--resources"))
			config.resources_dir = Param(argc, argv, i);
		else if (!std::strcmp(argv[i], "--data"))
			config.data_root = Param(argc, argv, i);
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

	if (config.disc_path.empty() || config.resources_dir.empty() || config.data_root.empty())
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
	while (!finished.load())
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(500));
		const CabinetPS2::Metrics m = CabinetPS2::GetMetrics();
		const auto now = std::chrono::steady_clock::now();
		const double secs = std::chrono::duration<double>(now - last).count();
		if (m.frames != last_frames)
		{
			std::printf("[probe] frames=%llu fps=%.1f speed=%.0f%% (+%llu in %.1fs)\n",
				static_cast<unsigned long long>(m.frames), m.fps, m.speed,
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
	return 0;
}
