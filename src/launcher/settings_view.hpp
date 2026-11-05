#pragma once

#include <windows.h>

#include <filesystem>
#include <string>

// The launcher's Settings page: the in-VR settings panel (ui/settings_panel) drawn in a child
// window, without its title bar, for every live setting plus the ones the game reads when it
// starts. It edits bfvr.ini directly (comments and other keys kept); the running game does not
// see changes until it starts again (the in-VR panel changes it live).
namespace bfvr::launcher::settings_view
{
	// Reports what happened (saved, changes undone, an error) for the launcher's message line.
	using notify_fn = void (*)(HWND dialog, const std::wstring& text, bool ok);

	HWND create(HWND dialog, int id, const RECT& area, notify_fn notify);
	// Reads bfvr.ini again: a missing file shows the defaults, and saving starts it from seed.
	void load(HWND view, const std::filesystem::path& ini, const std::filesystem::path& seed);
	bool unsaved();
	// The mouse wheel over the page (the dialog forwards it): positive scrolls up.
	void scroll(HWND view, int wheel_delta);
}
