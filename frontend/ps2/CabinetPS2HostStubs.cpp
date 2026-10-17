// CabinetOS's host layer for upstream PCSX2: the functions PCSX2 asks for
// that a console answers with nothing, or with one fixed answer, each with
// its reason. The ones that do real work, and the count of all of them, are
// in CabinetPS2Host.cpp; moved out of it in the restructure (#398).

#include "CabinetPS2Host.h"

#include <cstring>
#include <optional>
#include <string>

#include "fmt/format.h"

#include "common/Console.h"
#include "common/ProgressCallback.h"

#include "pcsx2/PrecompiledHeader.h"
#include "pcsx2/Achievements.h"
#include "pcsx2/GS.h"
#include "pcsx2/Host.h"
// Some of these are declared in the fullscreen UI headers rather than Host.h;
// CabinetPS2Host.cpp says which. The VM's, the window's and the text input's
// are in VMManager.h, GS.h and ImGuiManager.h.
#include "pcsx2/ImGui/FullscreenUI.h"
#include "pcsx2/ImGui/ImGuiFullscreen.h"
#include "pcsx2/ImGui/ImGuiManager.h"
#include "pcsx2/Input/InputManager.h"
#include "pcsx2/VMManager.h"

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
// The display's size and mode, which are the frontend's. The display path
// itself is in CabinetPS2Host.cpp.
// ---------------------------------------------------------------------------

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
// The VM's events nothing here acts on. The ones that do are in
// CabinetPS2Host.cpp.
// ---------------------------------------------------------------------------

void Host::OnVMStarting()
{
	Console.WriteLn("[ps2] VM starting");
}

void Host::OnVMPaused()
{
}

void Host::OnVMResumed()
{
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
