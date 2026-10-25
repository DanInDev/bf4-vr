#pragma once

#include <windows.h>

namespace bfvr::hooks
{
	// keep_focus = 1: BF4 never learns that its window lost focus. Unfocused, the game drops to
	// 30 fps or stops presenting, and leaves (virtual) fullscreen for a 1280x720 window, which the
	// headset shows as a blurry or frozen image whenever the SteamVR dashboard, Steam Link or another
	// window takes focus. The window's activation messages are filtered and GetForegroundWindow
	// answers with the game window; cursor clipping and warping still only apply while the game
	// really is in the foreground, so the mouse stays usable elsewhere.
	void keep_focus(HWND game_window);
	unsigned long long focus_losses_hidden();
}
