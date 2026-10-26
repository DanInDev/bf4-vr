#pragma once

#include <windows.h>

namespace bfvr::hooks
{
	// Owns what the game sees of its window (subclassed window procedure plus a few user32 hooks).
	//
	// keep_focus = 1: BF4 never learns that its window lost focus. Unfocused, the game drops to
	// 30 fps or stops presenting and leaves (virtual) fullscreen for a 1280x720 window, which the
	// headset shows as a blurry or frozen image whenever the SteamVR dashboard, Steam Link or another
	// window takes focus. Activation messages are filtered and GetForegroundWindow answers with the
	// game window; cursor clipping and warping only apply while the game really is in front.
	//
	// Virtual client size (render_size): the renderer compares the window's client size with its
	// display mode every frame (GetClientRect in 0x140BF4E60) and rebuilds the swap chain when they
	// differ. While a virtual mode is active, GetClientRect reports that size for the game window, and
	// mouse coordinates (window messages, ScreenToClient, ClientToScreen) are scaled to match.
	void attach_game_window(HWND game_window);
	void set_virtual_client_size(unsigned width, unsigned height); // 0 0 = the real size
	unsigned long long focus_losses_hidden();

	// Laser pointer (#23): puts the cursor at texture coordinates (0, 0 = top left) of the game's
	// image and presses the left button while pressed is true. Uses the real cursor (BF4's menus follow
	// it) when the game is in front, posted mouse messages otherwise. false without a game window.
	bool point_game_cursor(float u, float v, bool pressed);

	// A key press or release for the game (virtual-key code), through its Raw Input keyboard: a posted
	// WM_INPUT answered by the GetRawInputData hook. Works without focus. false without a game window.
	bool press_game_key(unsigned short virtual_key, bool down);
	unsigned long long synthetic_keys_read(); // synthetic keys the game has read (GetRawInputData)
}
