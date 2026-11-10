#include "hooks/game_window.hpp"

#include <atomic>
#include <cstdint>
#include <cstring>

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
		using get_client_rect_t = BOOL(WINAPI*)(HWND, LPRECT);
		using point_conversion_t = BOOL(WINAPI*)(HWND, LPPOINT);
		using get_raw_input_data_t = UINT(WINAPI*)(HRAWINPUT, UINT, LPVOID, PUINT, UINT);

		get_foreground_window_t original_get_foreground_window{};
		clip_cursor_t original_clip_cursor{};
		set_cursor_pos_t original_set_cursor_pos{};
		get_client_rect_t original_get_client_rect{};
		point_conversion_t original_screen_to_client{};
		point_conversion_t original_client_to_screen{};
		get_raw_input_data_t original_get_raw_input_data{};

		// Synthetic keys (weapon slots, docs/tasks/weapon-swap.md): BF4 reads the keyboard through Raw
		// Input. A posted WM_INPUT carries a marker handle; GetRawInputData answers it with the key.
		constexpr std::uintptr_t key_handle_base = 0x7bf5a000;
		constexpr unsigned key_ring = 32;
		struct key_event
		{
			USHORT make{};
			USHORT flags{};
			USHORT vkey{};
			// #34: a mouse button instead (RI_MOUSE_LEFT_BUTTON_DOWN/UP): menu clicks while the window is
			// not in front, where posted WM_LBUTTON* reach the window but the game reads buttons as Raw Input.
			bool mouse{};
			USHORT buttons{};
		};
		key_event key_events[key_ring];
		std::atomic<unsigned> next_key{};
		std::atomic<unsigned long long> keys_answered{};

		bool synthetic_key(HRAWINPUT handle, unsigned& index)
		{
			const auto value = reinterpret_cast<std::uintptr_t>(handle);
			if (value < key_handle_base || value >= key_handle_base + key_ring)
			{
				return false;
			}
			index = static_cast<unsigned>(value - key_handle_base);
			return true;
		}

		UINT WINAPI get_raw_input_data_detour(HRAWINPUT handle, UINT command, LPVOID data, PUINT size, UINT header_size)
		{
			unsigned index{};
			if (!synthetic_key(handle, index) || !size || header_size != sizeof(RAWINPUTHEADER))
			{
				return original_get_raw_input_data(handle, command, data, size, header_size);
			}
			RAWINPUT input{};
			input.header.dwSize = sizeof(RAWINPUT);
			input.header.wParam = RIM_INPUT;
			const key_event& key = key_events[index];
			if (key.mouse)
			{
				input.header.dwType = RIM_TYPEMOUSE;
				input.data.mouse.usFlags = MOUSE_MOVE_RELATIVE;
				input.data.mouse.usButtonFlags = key.buttons;
			}
			else
			{
				input.header.dwType = RIM_TYPEKEYBOARD;
				input.data.keyboard.MakeCode = key.make;
				input.data.keyboard.Flags = key.flags;
				input.data.keyboard.VKey = key.vkey;
				input.data.keyboard.Message = (key.flags & RI_KEY_BREAK) ? WM_KEYUP : WM_KEYDOWN;
			}
			const UINT needed = command == RID_HEADER ? sizeof(RAWINPUTHEADER) : sizeof(RAWINPUT);
			if (!data)
			{
				*size = needed;
				return 0;
			}
			if (*size < needed)
			{
				*size = needed;
				return static_cast<UINT>(-1);
			}
			std::memcpy(data, &input, needed);
			++keys_answered;
			return needed;
		}

		std::atomic<HWND> window{};
		WNDPROC original_window_procedure{};
		bool unicode_window{};
		bool focus_kept{};
		std::atomic<unsigned long long> hidden{};
		std::atomic<ULONGLONG> pointer_at{}; // last point_game_cursor call (game_pointer_active)
		std::atomic<unsigned> virtual_width{};
		std::atomic<unsigned> virtual_height{};

		bool really_in_front()
		{
			return original_get_foreground_window() == window.load();
		}

		// Virtual-to-real client scale; false when no virtual size is active.
		bool client_scale(HWND hwnd, float& x, float& y)
		{
			const unsigned w = virtual_width.load();
			const unsigned h = virtual_height.load();
			RECT real{};
			if (!w || !h || hwnd != window.load() || !original_get_client_rect(hwnd, &real) || real.right <= 0 || real.bottom <= 0)
			{
				return false;
			}
			x = static_cast<float>(w) / static_cast<float>(real.right);
			y = static_cast<float>(h) / static_cast<float>(real.bottom);
			return true;
		}

		// Research: window messages that can change the game's fullscreen state, logged (first 80) to
		// find what makes it leave its virtual fullscreen (2026-10-26).
		std::atomic<int> logged_messages{};
		bool log_messages{};

		void log_message(UINT message, WPARAM wparam, LPARAM lparam)
		{
			switch (message)
			{
			case WM_ACTIVATEAPP: case WM_ACTIVATE: case WM_NCACTIVATE: case WM_SETFOCUS: case WM_KILLFOCUS: case WM_SIZE:
			case WM_SYSCOMMAND: case WM_WINDOWPOSCHANGED: case WM_DISPLAYCHANGE: case WM_ENTERSIZEMOVE: case WM_EXITSIZEMOVE:
			case WM_SHOWWINDOW: case WM_STYLECHANGED:
				if (logged_messages++ < 80)
				{
					log::write("window: message 0x%04X wparam 0x%llX lparam 0x%llX", message, static_cast<unsigned long long>(wparam),
						static_cast<unsigned long long>(lparam));
				}
				break;
			default:
				break;
			}
		}

		LRESULT CALLBACK window_procedure(HWND hwnd, UINT message, WPARAM wparam, LPARAM lparam)
		{
			if (log_messages)
			{
				log_message(message, wparam, lparam);
			}
			if (focus_kept)
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
			}
			// Client-coordinate mouse messages (the wheel messages carry screen coordinates).
			float sx{}, sy{};
			if (message >= WM_MOUSEFIRST && message <= WM_MOUSELAST && message != WM_MOUSEWHEEL && message != WM_MOUSEHWHEEL
				&& client_scale(hwnd, sx, sy))
			{
				const int x = static_cast<short>(LOWORD(lparam));
				const int y = static_cast<short>(HIWORD(lparam));
				lparam = MAKELPARAM(static_cast<short>(x * sx), static_cast<short>(y * sy));
			}
			return unicode_window ? CallWindowProcW(original_window_procedure, hwnd, message, wparam, lparam)
				: CallWindowProcA(original_window_procedure, hwnd, message, wparam, lparam);
		}

		HWND WINAPI get_foreground_window_detour()
		{
			const HWND game = window.load();
			return game && focus_kept ? game : original_get_foreground_window();
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

		BOOL WINAPI get_client_rect_detour(HWND hwnd, LPRECT rect)
		{
			const BOOL result = original_get_client_rect(hwnd, rect);
			const unsigned w = virtual_width.load();
			const unsigned h = virtual_height.load();
			if (result && rect && w && h && hwnd == window.load())
			{
				*rect = { 0, 0, static_cast<LONG>(w), static_cast<LONG>(h) };
			}
			return result;
		}

		BOOL WINAPI screen_to_client_detour(HWND hwnd, LPPOINT point)
		{
			const BOOL result = original_screen_to_client(hwnd, point);
			float sx{}, sy{};
			if (result && point && client_scale(hwnd, sx, sy))
			{
				point->x = static_cast<LONG>(point->x * sx);
				point->y = static_cast<LONG>(point->y * sy);
			}
			return result;
		}

		BOOL WINAPI client_to_screen_detour(HWND hwnd, LPPOINT point)
		{
			float sx{}, sy{};
			if (point && client_scale(hwnd, sx, sy))
			{
				point->x = static_cast<LONG>(point->x / sx);
				point->y = static_cast<LONG>(point->y / sy);
			}
			return original_client_to_screen(hwnd, point);
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
				log::write("window: hooking %s failed: %s", name, MH_StatusToString(status));
				return false;
			}
			return true;
		}
	}

	void attach_game_window(HWND game_window)
	{
		if (window.load())
		{
			if (window.load() != game_window)
			{
				log::write("window: second game window %p ignored", game_window);
			}
			return;
		}
		// The guards need the real foreground window and client size, so these come first.
		hook_api("GetForegroundWindow", reinterpret_cast<void*>(&get_foreground_window_detour), reinterpret_cast<void**>(&original_get_foreground_window));
		hook_api("GetClientRect", reinterpret_cast<void*>(&get_client_rect_detour), reinterpret_cast<void**>(&original_get_client_rect));
		hook_api("ScreenToClient", reinterpret_cast<void*>(&screen_to_client_detour), reinterpret_cast<void**>(&original_screen_to_client));
		hook_api("ClientToScreen", reinterpret_cast<void*>(&client_to_screen_detour), reinterpret_cast<void**>(&original_client_to_screen));
		hook_api("GetRawInputData", reinterpret_cast<void*>(&get_raw_input_data_detour), reinterpret_cast<void**>(&original_get_raw_input_data));
		focus_kept = config::get_bool("keep_focus", true);
		log_messages = config::research_enabled();
		if (focus_kept)
		{
			hook_api("ClipCursor", reinterpret_cast<void*>(&clip_cursor_detour), reinterpret_cast<void**>(&original_clip_cursor));
			hook_api("SetCursorPos", reinterpret_cast<void*>(&set_cursor_pos_detour), reinterpret_cast<void**>(&original_set_cursor_pos));
		}

		// The A/W variant must match the window, or its text messages (console input) get converted.
		unicode_window = IsWindowUnicode(game_window) != FALSE;
		original_window_procedure = reinterpret_cast<WNDPROC>(unicode_window ? GetWindowLongPtrW(game_window, GWLP_WNDPROC)
			: GetWindowLongPtrA(game_window, GWLP_WNDPROC));
		const LONG_PTR replaced = unicode_window
			? SetWindowLongPtrW(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_procedure))
			: SetWindowLongPtrA(game_window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&window_procedure));
		if (!original_window_procedure || !replaced)
		{
			log::write("window: subclassing the game window failed: %lu", GetLastError());
			return;
		}
		// Only now do the hooks start answering for this window.
		window = game_window;
		log::write("window: attached %p (%s window, keep_focus=%d)", game_window, unicode_window ? "unicode" : "ansi", focus_kept ? 1 : 0);
	}

	void set_virtual_client_size(unsigned width, unsigned height)
	{
		const bool changed = virtual_width.exchange(width) != width;
		if (virtual_height.exchange(height) != height || changed)
		{
			log::write("window: virtual client size %ux%u", width, height);
		}
	}

	bool press_game_key(unsigned short virtual_key, bool down)
	{
		const HWND hwnd = window.load();
		if (!hwnd || !original_get_raw_input_data)
		{
			return false;
		}
		const unsigned index = next_key++ % key_ring;
		key_event& key = key_events[index];
		key.mouse = false;
		key.vkey = virtual_key;
		key.make = static_cast<USHORT>(MapVirtualKeyW(virtual_key, MAPVK_VK_TO_VSC));
		key.flags = down ? RI_KEY_MAKE : RI_KEY_BREAK;
		return PostMessageW(hwnd, WM_INPUT, RIM_INPUT, static_cast<LPARAM>(key_handle_base + index)) != FALSE;
	}

	unsigned long long synthetic_keys_read()
	{
		return keys_answered.load();
	}

	bool point_game_cursor(float u, float v, bool pressed)
	{
		const HWND hwnd = window.load();
		if (!hwnd || !(u >= 0.0f && u <= 1.0f && v >= 0.0f && v <= 1.0f))
		{
			return false;
		}
		RECT client{};
		if (!(original_get_client_rect ? original_get_client_rect(hwnd, &client) : GetClientRect(hwnd, &client)))
		{
			return false;
		}
		const LONG x = static_cast<LONG>(u * static_cast<float>(client.right));
		const LONG y = static_cast<LONG>(v * static_cast<float>(client.bottom));
		POINT screen{ x, y };
		if (original_client_to_screen ? !original_client_to_screen(hwnd, &screen) : !ClientToScreen(hwnd, &screen))
		{
			return false;
		}
		pointer_at = GetTickCount64();
		static bool was_pressed{};
		const HWND foreground = original_get_foreground_window ? original_get_foreground_window() : GetForegroundWindow();
		if (foreground == hwnd)
		{
			original_set_cursor_pos ? original_set_cursor_pos(screen.x, screen.y) : SetCursorPos(screen.x, screen.y);
			if (pressed != was_pressed)
			{
				INPUT input{};
				input.type = INPUT_MOUSE;
				input.mi.dwFlags = pressed ? MOUSEEVENTF_LEFTDOWN : MOUSEEVENTF_LEFTUP;
				SendInput(1, &input, sizeof(input));
			}
		}
		else
		{
			// Client coordinates of the real window; the window procedure scales them to the virtual size.
			const LPARAM position = MAKELPARAM(x, y);
			PostMessageW(hwnd, WM_MOUSEMOVE, pressed ? MK_LBUTTON : 0, position);
			if (pressed != was_pressed)
			{
				PostMessageW(hwnd, pressed ? WM_LBUTTONDOWN : WM_LBUTTONUP, pressed ? MK_LBUTTON : 0, position);
				// The game reads the buttons as Raw Input (monitor 2026-11-09: posted clicks only hovered the
				// crate menu's items, Done never closed it): the same synthetic WM_INPUT as the keys.
				if (original_get_raw_input_data)
				{
					const unsigned index = next_key++ % key_ring;
					key_event& button = key_events[index];
					button.mouse = true;
					button.buttons = static_cast<USHORT>(pressed ? RI_MOUSE_LEFT_BUTTON_DOWN : RI_MOUSE_LEFT_BUTTON_UP);
					PostMessageW(hwnd, WM_INPUT, RIM_INPUT, static_cast<LPARAM>(key_handle_base + index));
				}
			}
		}
		was_pressed = pressed;
		return true;
	}

	bool game_pointer_active()
	{
		return GetTickCount64() - pointer_at.load(std::memory_order_relaxed) < 150;
	}

	unsigned long long focus_losses_hidden()
	{
		return hidden.load();
	}
}
