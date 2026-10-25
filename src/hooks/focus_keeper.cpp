#include "hooks/focus_keeper.hpp"

#include <atomic>

#include <MinHook.h>

#include "common/config.hpp"
#include "common/log.hpp"

namespace bfvr::hooks
{
	namespace
	{
		using get_foreground_window_t = HWND(WINAPI*)();
		using clip_cursor_t = BOOL(WINAPI*)(const RECT*);
		using set_cursor_pos_t = BOOL(WINAPI*)(int, int);

		get_foreground_window_t original_get_foreground_window{};
		clip_cursor_t original_clip_cursor{};
		set_cursor_pos_t original_set_cursor_pos{};

		std::atomic<HWND> window{};
		WNDPROC original_window_procedure{};
		bool unicode_window{};
		std::atomic<unsigned long long> hidden{};

		bool really_in_front()
		{
			return original_get_foreground_window() == window.load();
		}

		LRESULT CALLBACK window_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
		{
			switch (message)
			{
			case WM_ACTIVATEAPP:
				if (!wparam)
				{
					++hidden;
					return 0;
				}
				break;
			case WM_ACTIVATE:
				if (LOWORD(wparam) == WA_INACTIVE)
				{
					++hidden;
					return 0;
				}
				break;
			case WM_KILLFOCUS:
				return 0;
			case WM_NCACTIVATE:
				if (!wparam)
				{
					return TRUE;
				}
				break;
			default:
				break;
			}
			return unicode_window ? CallWindowProcW(original_window_procedure, hwnd, message, wparam, lparam)
				: CallWindowProcA(original_window_procedure, hwnd, message, wparam, lparam);
		}

		HWND WINAPI get_foreground_window_detour()
		{
			const HWND game = window.load();
			return game ? game : original_get_foreground_window();
		}

		BOOL WINAPI clip_cursor_detour(const RECT* rect)
		{
			// Releasing the clip is always fine; clipping only while the player is in the game.
			if (rect && !really_in_front())
			{
				return TRUE;
			}
			return original_clip_cursor(rect);
		}

		BOOL WINAPI set_cursor_pos_detour(int x, int y)
		{
			if (!really_in_front())
			{
				return TRUE;
			}
			return original_set_cursor_pos(x, y);
		}

		bool hook_api(const char* name, void* detour, void** original)
		{
			void* target{};
			MH_STATUS status = MH_CreateHookApiEx(L"user32", name, detour, original, &target);
			if (status == MH_OK)
			{
				status = MH_EnableHook(target);
			}
			if (status != MH_OK)
			{
				log::write("focus: hooking %s failed: %s", name, MH_StatusToString(status));
				return false;
			}
			return true;
		}
	}

	void keep_focus(HWND game_window)
	{
		if (!config::get_bool("keep_focus", true) || window.load() == game_window)
		{
			return;
		}
		if (window.load())
		{
			log::write("focus: second game window %p ignored", game_window);
			return;
		}
		// The cursor guards need the real foreground window, so they come first.
		hook_api("GetForegroundWindow", reinterpret_cast<void*>(&get_foreground_window_detour), reinterpret_cast<void**>(&original_get_foreground_window));
		hook_api("ClipCursor", reinterpret_cast<void*>(&clip_cursor_detour), reinterpret_cast<void**>(&original_clip_cursor));
		hook_api("SetCursorPos", reinterpret_cast<void*>(&set_cursor_pos_detour), reinterpret_cast<void**>(&original_set_cursor_pos));
		// The A/W variant must match the window, or its text messages (console input) get converted.
		unicode_window = IsWindowUnicode(game_window) != FALSE;
		original_window_procedure = reinterpret_cast<WNDPROC>(unicode_window ? GetWindowLongPtrW(game_window, GWLP_WNDPROC)
			: GetWindowLongPtrA(game_window, GWLP_WNDPROC));
		const LONG_PTR replaced = unicode_window
			? SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_procedure))
			: SetWindowLongPtrA(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_procedure));
		if (!original_window_procedure || !replaced)
		{
			log::write("focus: subclassing the game window failed: %lu", GetLastError());
			return;
		}
		window = game_window;
		log::write("focus: keeping window %p focused for the game (%s window)", game_window, unicode_window ? "unicode" : "ansi");
	}

	unsigned long long focus_losses_hidden()
	{
		return hidden.load();
	}
}
