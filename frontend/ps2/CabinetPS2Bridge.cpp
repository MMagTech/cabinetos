// The flat C face CabinetOS's frontend opens with dlopen.
//
// **WHY A SHARED OBJECT RATHER THAN LINKING PCSX2 INTO THE FRONTEND.** Two
// reasons, and either alone would decide it.
//
// The licence is the hard one. PCSX2 is GPLv3 and this repository is MIT.
// docs/LICENCES.md rests its whole position on one sentence — "the cores are
// `dlopen`ed rather than statically linked" — which is what keeps the frontend
// binary its own work rather than part of a combined GPLv3 one. Every libretro
// core already arrives this way; PlayStation 2 arrives the same way, and the
// argument does not have to be reopened.
//
// The practical one is the build. PCSX2 needs about thirty development
// packages the frontend's own builder image does not have and should not grow;
// its Containerfile is small on purpose, because it also builds twenty-one
// cores. Keeping the emulator behind a `.so` means the frontend's Makefile does
// not change at all.
//
// So this is deliberately a C API and deliberately dull: no C++ types cross it,
// nothing is thrown, and every function is safe to call when nothing is
// running. That is the same contract the libretro cores meet, and it is what
// lets frontend/src/core.cpp treat PlayStation 2 as one more thing it loads.

#include "CabinetPS2Host.h"

#include <atomic>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

namespace
{
	std::thread s_vm_thread;
	std::string s_error;
	std::atomic<bool> s_started{false};

	// The frame most recently handed over, kept so that `cps2_take_frame` can
	// lend a pointer that stays valid until the NEXT call. The frontend
	// uploads it to a texture immediately and never keeps it, which is the
	// same lifetime a libretro core's `video_refresh` buffer has.
	CabinetPS2::Frame s_frame;
	std::vector<int16_t> s_audio;
} // namespace

extern "C" {

// Starts a game. Returns 1 on success. Blocks only long enough to get the VM
// running; the emulator itself runs on a thread of its own from here on,
// because PCSX2's VMManager::Execute does not return until the game stops.
int cps2_start(const char* disc, const char* data_root, const char* resources, const char* memory_card,
	float upscale, int fast_boot)
{
	if (s_started.load())
		return 0;

	CabinetPS2::Config config;
	config.disc_path = disc ? disc : "";
	config.data_root = data_root ? data_root : "";
	config.resources_dir = resources ? resources : "";
	config.memory_card = memory_card ? memory_card : "";
	config.upscale = upscale > 0.0f ? upscale : 1.0f;
	config.fast_boot = fast_boot != 0;
	config.stop_after = 0;

	s_error.clear();
	s_started.store(true);

	s_vm_thread = std::thread([config] {
		std::string error;
		if (!CabinetPS2::Run(config, &error))
			s_error = error;
		s_started.store(false);
	});

	return 1;
}

// Why the last start failed, or an empty string. Valid until the next call to
// cps2_start.
const char* cps2_error(void)
{
	return s_error.c_str();
}

// Whether the emulated machine is actually running, as opposed to still being
// built. The frontend asks this for the same reason it asks a libretro core:
// tearing a machine down while it is still being assembled frees memory out
// from under the thread doing the assembling.
int cps2_running(void)
{
	return CabinetPS2::IsRunning() ? 1 : 0;
}

// Asks the game to stop and WAITS for it. Blocking is the point: PCSX2 flushes
// the memory card during shutdown, so a frontend that captured the card before
// this returned would upload the card as it was when the game started. The
// libretro path has the same rule about retro_unload_game and it was paid for
// once already.
void cps2_stop(void)
{
	CabinetPS2::RequestStop();
	if (s_vm_thread.joinable())
		s_vm_thread.join();
	s_started.store(false);
}

void cps2_set_pad(unsigned port, uint32_t buttons, float left_x, float left_y, float right_x, float right_y,
	float left_trigger, float right_trigger)
{
	CabinetPS2::Pad pad;
	pad.buttons = buttons;
	pad.leftX = left_x;
	pad.leftY = left_y;
	pad.rightX = right_x;
	pad.rightY = right_y;
	pad.leftTrigger = left_trigger;
	pad.rightTrigger = right_trigger;
	CabinetPS2::SetPad(port, pad);
}

// Lends the newest frame if there is one newer than `since`. The pointer stays
// valid until the next call. Returns 1 when something new was handed over.
int cps2_take_frame(const uint32_t** pixels, unsigned* width, unsigned* height, uint64_t* serial)
{
	if (!CabinetPS2::TakeFrame(&s_frame, *serial))
		return 0;

	*pixels = s_frame.pixels.data();
	*width = s_frame.width;
	*height = s_frame.height;
	*serial = s_frame.serial;
	return 1;
}

// Fills `dest` with up to `max_frames` stereo frames and returns how many were
// written. Interleaved 16-bit, the same shape every libretro core produces.
unsigned cps2_drain_audio(int16_t* dest, unsigned max_frames)
{
	if (!dest || max_frames == 0)
		return 0;

	s_audio.clear();
	CabinetPS2::DrainAudio(&s_audio, max_frames);

	const unsigned frames = static_cast<unsigned>(s_audio.size() / 2);
	if (frames > 0)
		std::memcpy(dest, s_audio.data(), s_audio.size() * sizeof(int16_t));
	return frames;
}

unsigned cps2_audio_sample_rate(void)
{
	return CabinetPS2::AudioSampleRate();
}

// Where the card named in cps2_start will be, so the frontend can restore it
// beforehand and capture it afterwards. Writes into `out` and returns its
// length, or 0 if it would not fit.
unsigned cps2_memory_card_path(const char* data_root, const char* name, char* out, unsigned out_size)
{
	const std::string path = CabinetPS2::MemoryCardPath(data_root ? data_root : "", name ? name : "");
	if (path.size() + 1 > out_size)
		return 0;
	std::memcpy(out, path.c_str(), path.size() + 1);
	return static_cast<unsigned>(path.size());
}

// Live performance, for the frontend's own speed reading and for the readback
// cost the picture path rests on.
void cps2_metrics(float* fps, float* speed, double* readback_us)
{
	const CabinetPS2::Metrics m = CabinetPS2::GetMetrics();
	if (fps)
		*fps = m.fps;
	if (speed)
		*speed = m.speed;
	if (readback_us)
		*readback_us = m.readback_us;
}

// The version of PCSX2 behind this, so the frontend can print what it is
// actually running rather than what it believes it pinned. Every libretro core
// answers the same question through retro_get_system_info, and the reason is
// the same: a core that does not know which revision it is cannot be audited.
const char* cps2_version(void)
{
	return CABINETOS_PS2_VERSION;
}

} // extern "C"
