// CabinetOS's host layer for upstream PCSX2. See CabinetPS2Host.h.
//
// EVERY FUNCTION PCSX2 ASKS FOR IS HERE, AND THE COUNT IS MEASURED: 55 in the
// `Host` namespace, three `InputManager::ConvertHostKeyboard*`, and the
// `g_host_hotkeys` table — which is a VARIABLE, not a function, and is the one
// a dlopen of an incomplete layer fails on before naming any of the others.
// docs/PCSX2-HOST-SURFACE.md, and `cores/build-pcsx2.sh` re-derives the list.
//
// MOST OF THEM ARE DELIBERATELY EMPTY, and that is not laziness. A console has
// no clipboard, no file selector, no achievements login, no Big Picture mode to
// exit to and no game list of PCSX2's own — CabinetOS has its own library, from
// RomM. Upstream's own gsrunner answers most of them the same way. **An empty
// body with a reason beside it is the honest answer; the dangerous one is a
// function that pretends to do something.**
//
// Signatures are taken verbatim from upstream's pcsx2-gsrunner/Main.cpp at the
// pinned revision rather than transcribed from Host.h by hand, because a
// mistyped one is a link error at best and an ODR violation at worst.

#include "CabinetPS2Host.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <optional>
#include <thread>

#include "fmt/format.h"

#include "common/Assertions.h"
#include "common/Console.h"
#include "common/FileSystem.h"
#include "common/MemorySettingsInterface.h"
#include "common/Path.h"
#include "common/ProgressCallback.h"
#include "common/StringUtil.h"

#include "pcsx2/PrecompiledHeader.h"
#include "pcsx2/Achievements.h"
#include "pcsx2/GS.h"
#include "pcsx2/GS/Renderers/Common/GSDevice.h"
#include "pcsx2/Host.h"
// SIX OF THE 55 ARE DECLARED IN THE FULLSCREEN UI HEADERS RATHER THAN Host.h,
// which is not obvious and cost the first compile: LocaleCircleConfirm,
// RequestExitApplication and RequestExitBigPicture live in FullscreenUI.h, and
// ShouldPreferHostFileSelector, OpenHostFileSelectorAsync and its two callback
// types in ImGuiFullscreen.h. They are still `Host::` functions and the linker
// still demands them; only the declaration moved.
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/MTGS.h"
#include "pcsx2/PerformanceMetrics.h"
#include "pcsx2/SIO/Pad/Pad.h"
#include "pcsx2/VMManager.h"

namespace
{
	// Everything PCSX2 is told lives in memory. There is no pcsx2.ini on a
	// console and there must not be: CabinetOS owns configuration, the way it
	// already answers every libretro core's options from catalog::optionOverrides
	// rather than letting a core read a file of its own.
	MemorySettingsInterface s_settings;

	CabinetPS2::Config s_config;
	std::atomic<bool> s_running{false};
	std::atomic<bool> s_stop_requested{false};
	std::atomic<uint64_t> s_frames{0};

	// Owned by the GS thread.
	uint32_t s_dumped = 0;
} // namespace

// ---------------------------------------------------------------------------
// The hotkey table.
//
// PCSX2 expects the frontend to publish one. CabinetOS binds nothing here: the
// in-game overlay is the frontend's and every button goes through it, so a
// hotkey PCSX2 owned would be a second, invisible input path onto the same pad.
// The table has to EXIST regardless — it is `g_host_hotkeys`, an extern the
// emulator links against, and leaving it out is the first thing a dlopen
// complains about.
// ---------------------------------------------------------------------------
BEGIN_HOTKEY_LIST(g_host_hotkeys)
END_HOTKEY_LIST()

// ---------------------------------------------------------------------------
// Settings. All in memory; nothing is persisted and nothing is reloaded.
// ---------------------------------------------------------------------------

void Host::CommitBaseSettingChanges()
{
	// Nothing to save — the settings layer is memory and dies with the game.
}

void Host::LoadSettings(SettingsInterface& si, std::unique_lock<std::mutex>& lock)
{
	// CabinetOS has no per-game settings of its own to overlay yet. When it
	// does, this is where the quality tier of open question 23 would land.
}

void Host::CheckForSettingsChanges(const Pcsx2Config& old_config)
{
}

bool Host::RequestResetSettings(bool folders, bool core, bool controllers, bool hotkeys, bool ui)
{
	// No UI is running, so no settings reset can be asked for.
	return false;
}

void Host::SetDefaultUISettings(SettingsInterface& si)
{
	// PCSX2's own UI never runs here.
}

bool Host::LocaleCircleConfirm()
{
	// Whether Circle confirms rather than Cross. A console-wide decision that
	// belongs with the rest of the input model, not to PCSX2 alone — so it
	// takes the western default until CabinetOS has somewhere to state it.
	return false;
}

std::unique_ptr<ProgressCallback> Host::CreateHostProgressCallback()
{
	return ProgressCallback::CreateNullProgressCallback();
}

// ---------------------------------------------------------------------------
// Messages. PCSX2 talks to its own users about patches.zip and unsafe
// settings; none of that means anything inside somebody else's frontend. The
// text goes to the log, where it is useful, and not onto the television.
// ---------------------------------------------------------------------------

void Host::ReportInfoAsync(const std::string_view title, const std::string_view message)
{
	if (!message.empty())
		Console.WriteLn(fmt::format("[ps2] {}{}", title.empty() ? "" : fmt::format("{}: ", title), message));
}

void Host::ReportErrorAsync(const std::string_view title, const std::string_view message)
{
	if (!message.empty())
		Console.Error(fmt::format("[ps2] {}{}", title.empty() ? "" : fmt::format("{}: ", title), message));
}

void Host::OpenURL(const std::string_view url)
{
	// A console has no browser to open one in.
}

bool Host::CopyTextToClipboard(const std::string_view text)
{
	return false; // No clipboard.
}

std::string Host::GetTextFromClipboard()
{
	return {};
}

void Host::BeginTextInput()
{
	// CabinetOS owns the on-screen keyboard and PCSX2 never asks for text in
	// this configuration — its own UI is the only thing that would.
}

void Host::EndTextInput()
{
}

// ---------------------------------------------------------------------------
// The display path. THESE SIX ARE THE ONLY ONES THAT ARE REAL WORK.
//
// Surfaceless for now, deliberately. PCSX2 renders and the GS produces a
// picture, which is what a capture reads; nothing is presented to a window.
// The real path hands back the Vulkan surface frontend/src/vkhost.cpp already
// owns — a device, a queue and an image that crosses into the GLES texture the
// UI draws. Open question 20.
// ---------------------------------------------------------------------------

std::optional<WindowInfo> Host::GetTopLevelWindowInfo()
{
	// Unreferenced in this configuration — the linker does not even ask for it
	// — but implemented because upstream's gsrunner and Cabinet's host both do,
	// and building only what the linker complains about leaves this layer one
	// configuration change from a link error.
	WindowInfo wi;
	wi.type = WindowInfo::Type::Surfaceless;
	return wi;
}

std::optional<WindowInfo> Host::AcquireRenderWindow(bool recreate_window)
{
	WindowInfo wi;
	wi.type = WindowInfo::Type::Surfaceless;
	wi.surface_width = 0;
	wi.surface_height = 0;
	wi.surface_scale = 1.0f;
	return wi;
}

void Host::ReleaseRenderWindow()
{
}

void Host::BeginPresentFrame()
{
	const uint64_t frame = s_frames.fetch_add(1);

	if (s_config.dump_count == 0 || s_config.dump_dir.empty())
		return;
	if (frame < s_config.dump_first || s_dumped >= s_config.dump_count)
		return;

	// PCSX2's own snapshot, so what lands on disk is the picture the GS
	// produced rather than anything this layer re-encoded. Same call gsrunner
	// makes, for the same reason.
	GSQueueSnapshot(Path::Combine(s_config.dump_dir, fmt::format("frame{:05}.png", frame)));
	s_dumped++;
}

void Host::RequestResizeHostDisplay(s32 width, s32 height)
{
	// The window is the frontend's and PCSX2 does not get to resize it. On a
	// console it is the television and it does not change.
}

bool Host::IsFullscreen()
{
	// A console is always fullscreen. Saying anything else here would make
	// PCSX2 offer to change something that cannot change.
	return true;
}

void Host::SetFullscreen(bool enabled)
{
}

// ---------------------------------------------------------------------------
// VM lifecycle. CabinetOS watches these rather than driving from them — Run()
// below owns the sequence — but OnVMDestroyed is how a game that ends by
// itself gets noticed.
// ---------------------------------------------------------------------------

void Host::OnVMStarting()
{
	Console.WriteLn("[ps2] VM starting");
}

void Host::OnVMStarted()
{
	s_running.store(true);
	Console.WriteLn("[ps2] VM started");
}

void Host::OnVMDestroyed()
{
	s_running.store(false);
	Console.WriteLn("[ps2] VM destroyed");
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
}

void Host::OnGameChanged(const std::string& title, const std::string& elf_override, const std::string& disc_path,
	const std::string& disc_serial, u32 disc_crc, u32 current_crc)
{
	// THE SERIAL AND CRC ARE WORTH PRINTING RATHER THAN SWALLOWING. Cabinet's
	// Mac save states are keyed by exactly these two, so they are what any
	// future question about a state travelling between the two will start from.
	Console.WriteLn(fmt::format("[ps2] game: \"{}\" serial={} crc={:08X}",
		title.empty() ? "(none)" : title, disc_serial.empty() ? "(none)" : disc_serial, disc_crc));
}

void Host::OnPerformanceMetricsUpdated()
{
}

void Host::OnSaveStateLoading(const std::string_view filename)
{
}

void Host::OnSaveStateLoaded(const std::string_view filename, bool was_successful)
{
}

void Host::OnSaveStateSaved(const std::string_view filename)
{
}

void Host::RequestVMShutdown(bool allow_confirm, bool allow_save_state, bool default_save_state)
{
	// The game asked to stop — a PS2 title returning to the browser, say.
	// Nothing is confirmed, because there is nobody to confirm with.
	s_stop_requested.store(true);
	VMManager::SetState(VMState::Stopping);
}

// ---------------------------------------------------------------------------
// Threading.
// ---------------------------------------------------------------------------

void Host::RunOnCPUThread(std::function<void()> function, bool block /* = false */)
{
	// Run() below IS the CPU thread, so anything posted here can simply run.
	// That is only true because nothing else in this layer posts from another
	// thread; if that changes this needs a real queue, the way Cabinet's has
	// one.
	function();
}

void Host::PumpMessagesOnCPUThread()
{
	// The one place the emulator gives the frontend the CPU thread between
	// frames. The frame limit is enforced here rather than by counting from
	// outside, because this is the only point at which stopping is safe.
	if (s_config.stop_after != 0 && s_frames.load() >= s_config.stop_after)
		s_stop_requested.store(true);

	if (s_stop_requested.load() && VMManager::GetState() == VMState::Running)
		VMManager::SetState(VMState::Stopping);
}

// ---------------------------------------------------------------------------
// The game list, achievements, capture and the application shell. All of these
// belong to a PCSX2 that owns its own window and its own library. This one
// does not.
// ---------------------------------------------------------------------------

void Host::RefreshGameListAsync(bool invalidate_cache)
{
	// CabinetOS's library is RomM's. PCSX2 never gets to scan a directory.
}

void Host::CancelGameListRefresh()
{
}

void Host::OnCaptureStarted(const std::string& filename)
{
}

void Host::OnCaptureStopped()
{
}

void Host::RequestExitApplication(bool allow_confirm)
{
	// PCSX2 asking the whole application to quit means, here, leave the game.
	// The console itself is not something a game may close.
	s_stop_requested.store(true);
}

void Host::RequestExitBigPicture()
{
	// There is no Big Picture mode to leave; the console IS the big picture.
	s_stop_requested.store(true);
}

void Host::OnAchievementsLoginSuccess(const char* username, u32 points, u32 sc_points, u32 unread_messages)
{
}

void Host::OnAchievementsLoginRequested(Achievements::LoginRequestReason reason)
{
}

void Host::OnAchievementsHardcoreModeChanged(bool enabled)
{
}

void Host::OnAchievementsRefreshed()
{
}

bool Host::InBatchMode()
{
	// "Batch mode" is PCSX2 quitting when the game does. A console goes back to
	// Home instead, and the frontend decides that, not the emulator.
	return false;
}

bool Host::InNoGUIMode()
{
	return true;
}

bool Host::ShouldPreferHostFileSelector()
{
	return false;
}

void Host::OpenHostFileSelectorAsync(std::string_view title, bool select_directory, FileSelectorCallback callback,
	FileSelectorFilters filters, std::string_view initial_directory)
{
	// No file selector. The callback must still be invoked — dropping it leaves
	// whatever asked waiting for an answer that never comes.
	if (callback)
		callback(std::string());
}

// ---------------------------------------------------------------------------
// Input devices and localisation.
// ---------------------------------------------------------------------------

void Host::OnInputDeviceConnected(const std::string_view identifier, const std::string_view device_name)
{
}

void Host::OnInputDeviceDisconnected(const InputBindingKey key, const std::string_view identifier)
{
}

void Host::SetMouseMode(bool relative_mode, bool hide_cursor)
{
}

void Host::SetMouseLock(bool state)
{
}

int Host::LocaleSensitiveCompare(std::string_view lhs, std::string_view rhs)
{
	// Byte order. PCSX2 uses this to sort its own game list, which never runs.
	return lhs.compare(rhs);
}

s32 Host::Internal::GetTranslatedStringImpl(
	const std::string_view context, const std::string_view msg, char* tbuf, size_t tbuf_space)
{
	// Untranslated: hand back the original. Same as gsrunner.
	if (msg.size() > tbuf_space)
		return -1;
	if (msg.empty())
		return 0;

	std::memcpy(tbuf, msg.data(), msg.size());
	return static_cast<s32>(msg.size());
}

std::string Host::TranslatePluralToString(const char* context, const char* msg, const char* disambiguation, int count)
{
	// %n is PCSX2's plural placeholder. Substituting it is the whole job here;
	// there is no plural form to choose between without a translation.
	std::string ret(msg);
	const std::string count_str = fmt::format("{}", count);
	for (;;)
	{
		const std::string::size_type pos = ret.find("%n");
		if (pos == std::string::npos)
			break;
		ret.replace(pos, 2, count_str);
	}
	return ret;
}

// ---------------------------------------------------------------------------
// The three InputManager functions outside the Host namespace. A keyboard is
// not how anybody plays this console, and PCSX2's own bindings are cleared
// before the VM starts, so none of these resolves to anything.
// ---------------------------------------------------------------------------

std::optional<u32> InputManager::ConvertHostKeyboardStringToCode(const std::string_view str)
{
	return std::nullopt;
}

std::optional<std::string> InputManager::ConvertHostKeyboardCodeToString(u32 code)
{
	return std::nullopt;
}

const char* InputManager::ConvertHostKeyboardCodeToIcon(u32 code)
{
	return nullptr;
}

// ---------------------------------------------------------------------------
// CabinetPS2 — the far smaller surface pointing the other way.
// ---------------------------------------------------------------------------

namespace
{
	bool ConfigureFolders(const CabinetPS2::Config& config, std::string* error)
	{
		EmuFolders::AppRoot = Path::GetDirectory(FileSystem::GetProgramPath());

		// PCSX2 REFUSES TO START WITHOUT ITS RESOURCES FOLDER and says so in one
		// line. Checking here rather than letting it fail later is the
		// difference between a clear message and a puzzle.
		if (!FileSystem::DirectoryExists(config.resources_dir.c_str()))
		{
			*error = fmt::format("PCSX2's resources folder is not at {}. It holds the game "
								 "database, the fonts and the GS shaders, and PCSX2 will not "
								 "start without it.",
				config.resources_dir);
			return false;
		}

		EmuFolders::Resources = config.resources_dir;
		EmuFolders::DataRoot = config.data_root;
		return true;
	}
} // namespace

bool CabinetPS2::Run(const Config& config, std::string* error)
{
	s_config = config;
	s_stop_requested.store(false);
	s_frames.store(0);
	s_dumped = 0;

	if (!ConfigureFolders(config, error))
		return false;

	const char* hw_error = nullptr;
	if (!VMManager::PerformEarlyHardwareChecks(&hw_error))
	{
		*error = hw_error ? hw_error : "this machine cannot run PCSX2";
		return false;
	}

	// The font. PCSX2's ImGui layer will not initialise without one, and the
	// failure is a null dereference rather than a message.
	{
		const std::string roboto =
			Path::Combine(EmuFolders::Resources, "fonts" FS_OSPATH_SEPARATOR_STR "Roboto-Regular.ttf");
		auto data = FileSystem::MapBinaryFileForRead(roboto.c_str());
		if (data.empty())
		{
			*error = fmt::format("PCSX2's font is missing at {}", roboto);
			return false;
		}
		std::vector<ImGuiManager::FontInfo> fonts;
		ImGuiManager::FontInfo fi{};
		fi.data = data;
		fi.exclude_ranges = {};
		fi.face_name = nullptr;
		fi.is_emoji_font = false;
		fonts.push_back(fi);
		ImGuiManager::SetFonts(std::move(fonts));
	}

	Host::Internal::SetBaseSettingsLayer(&s_settings);
	VMManager::SetDefaultSettings(s_settings, true, true, true, true, true);

	// --- what CabinetOS overrides, and why each one ---

	// Vulkan. The A9 has it, PCSX2 supports it natively, and it is what the
	// frontend's own host already speaks. Open question 20.
	s_settings.SetIntValue("EmuCore/GS", "Renderer", static_cast<int>(GSRendererType::VK));
	s_settings.SetFloatValue("EmuCore/GS", "upscale_multiplier", s_config.upscale);

	// PCSX2's own input is off entirely. Every pad on this console reaches a
	// game through the frontend, and a second path onto the same hardware is
	// how a button ends up doing two things.
	s_settings.SetBoolValue("InputSources", "SDL", false);
	s_settings.SetBoolValue("InputSources", "XInput", false);
	Pad::ClearPortBindings(s_settings, 0);
	s_settings.ClearSection("Hotkeys");

	// Audio is the frontend's, as it is for all twenty-one libretro cores.
	// Null rather than absent: the emulator runs correctly and silently rather
	// than pretending.
	s_settings.SetStringValue("SPU2/Output", "OutputModule", "nullout");

	// NO MEMORY CARDS. Deliberate and load-bearing while the save work is not
	// wired up: a card PCSX2 chose for itself belongs to no rom, cannot be
	// synced, and — worse — could be written over something real. Burnout 3's
	// card is the only true PS2 save on the reference server.
	for (u32 i = 0; i < 2; i++)
	{
		s_settings.SetBoolValue("MemoryCards", fmt::format("Slot{}_Enable", i + 1).c_str(), !config.memory_card.empty());
		s_settings.SetStringValue("MemoryCards", fmt::format("Slot{}_Filename", i + 1).c_str(),
			(i == 0) ? config.memory_card.c_str() : "");
	}

	// PCSX2's on-screen messages are its frontend talking to its own users in
	// the middle of somebody else's. The log keeps them; the television does not.
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowFPS", false);
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowResolution", false);
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowGSStats", false);
	s_settings.SetBoolValue("EmuCore/GS", "OsdShowMessages", false);

	s_settings.SetBoolValue("Logging", "EnableSystemConsole", true);
	s_settings.SetBoolValue("Logging", "EnableTimestamps", true);
	s_settings.SetBoolValue("Logging", "EnableVerbose", config.verbose_log);

	if (config.unlimited)
	{
		s_settings.SetBoolValue("EmuCore/GS", "FrameLimitEnable", false);
		s_settings.SetIntValue("EmuCore/GS", "VsyncEnable", 0);
	}

	s_settings.SetBoolValue("EmuCore", "EnableFastBoot", config.fast_boot);

	VMManager::Internal::LoadStartupSettings();
	EmuFolders::EnsureFoldersExist();

	if (!VMManager::Internal::CPUThreadInitialize())
	{
		*error = "PCSX2's CPU thread would not initialise";
		return false;
	}

	VMManager::ApplySettings();

	VMBootParameters params;
	params.filename = config.disc_path;
	params.fast_boot = config.fast_boot;

	bool ok = false;
	if (VMManager::Initialize(params) == VMBootResult::StartupSuccess)
	{
		if (config.unlimited)
			VMManager::SetLimiterMode(LimiterModeType::Unlimited);

		VMManager::SetState(VMState::Running);
		while (VMManager::GetState() == VMState::Running)
			VMManager::Execute();

		VMManager::Shutdown(false);
		ok = true;
	}
	else
	{
		*error = fmt::format("PCSX2 would not boot {}", config.disc_path);
	}

	VMManager::Internal::CPUThreadShutdown();
	s_running.store(false);
	return ok;
}

void CabinetPS2::RequestStop()
{
	s_stop_requested.store(true);
}

bool CabinetPS2::IsRunning()
{
	return s_running.load();
}

CabinetPS2::Metrics CabinetPS2::GetMetrics()
{
	Metrics m{};
	m.frames = s_frames.load();
	if (!s_running.load())
		return m;

	m.fps = PerformanceMetrics::GetFPS();
	m.speed = PerformanceMetrics::GetSpeed();
	return m;
}
