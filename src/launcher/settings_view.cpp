#include "launcher/settings_view.hpp"

#include <windowsx.h>

#include <algorithm>
#include <cmath>
#include <fstream>
#include <iterator>
#include <memory>
#include <vector>

#include "common/settings.hpp"
#include "common/settings_store.hpp"
#include "launcher/ini_file.hpp"
#include "launcher/theme.hpp"
#include "ui/panel_canvas.hpp"
#include "ui/settings_panel.hpp"

namespace bfvr::launcher::settings_view
{
	namespace
	{
		constexpr wchar_t class_name[] = L"BfvrSettingsView";
		// The panel's title bar is the launcher's own header here: drawn from below it.
		constexpr int title_crop = 72;
		constexpr int shown_height = ui::settings_panel::height - title_crop;

		struct state
		{
			settings::registry store;
			ui::settings_panel panel;
			std::filesystem::path ini;
			std::filesystem::path seed;
			notify_fn notify{};
			HWND dialog{};
			bool tracking{};
		};
		std::unique_ptr<state> view_state;

		state& self()
		{
			if (!view_state)
			{
				view_state = std::make_unique<state>();
				for (const auto& setting : settings::startup_definitions())
				{
					view_state->store.add(setting);
				}
				for (const auto& setting : settings::live_definitions())
				{
					view_state->store.add(setting);
				}
				view_state->panel.set_close_label("Undo changes");
				view_state->panel.set_hint("Click a setting to change it. Save writes bfvr.ini; the game reads it when it starts.");
			}
			return *view_state;
		}

		std::string read_file(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			return { std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>() };
		}

		// The panel's size and place in the window: as large as fits, centred.
		struct fitting
		{
			float scale;
			int x;
			int width;
			int height;
		};

		fitting fit(HWND view)
		{
			RECT client{};
			GetClientRect(view, &client);
			const float scale = std::min(static_cast<float>(client.right) / ui::settings_panel::width, static_cast<float>(client.bottom) / shown_height);
			const int width = static_cast<int>(std::lround(ui::settings_panel::width * scale));
			const int height = static_cast<int>(std::lround(shown_height * scale));
			return { scale, (client.right - width) / 2, width, height };
		}

		ui::target hit(HWND view, int x, int y)
		{
			const fitting place = fit(view);
			if (place.scale <= 0.0f || x < place.x || x >= place.x + place.width || y >= place.height)
			{
				return {};
			}
			const float u = (x - place.x) / (ui::settings_panel::width * place.scale);
			const float v = (y / place.scale + title_crop) / ui::settings_panel::height;
			return self().panel.hit(u, v, self().store);
		}

		void tell(const std::wstring& text, bool ok)
		{
			state& s = self();
			if (s.notify && s.dialog)
			{
				s.notify(s.dialog, text, ok);
			}
		}

		bool save()
		{
			state& s = self();
			std::error_code error;
			const std::string existing = std::filesystem::exists(s.ini, error) ? read_file(s.ini) : read_file(s.seed);
			const std::string merged = s.store.merge_ini(existing);
			const std::filesystem::path temporary = s.ini.wstring() + L".tmp";
			{
				std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
				file.write(merged.data(), static_cast<std::streamsize>(merged.size()));
				if (!file)
				{
					return false;
				}
			}
			if (!MoveFileExW(temporary.c_str(), s.ini.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				return false;
			}
			s.store.mark_saved();
			return true;
		}

		void act(HWND view, ui::settings_panel::action result)
		{
			state& s = self();
			switch (result)
			{
			case ui::settings_panel::action::changed:
				tell(L"Unsaved changes: Save writes them to bfvr.ini.", true);
				break;
			case ui::settings_panel::action::save:
				if (s.ini.empty())
				{
					tell(L"Choose the game folder first (Setup).", false);
				}
				else if (save())
				{
					tell(L"Saved to bfvr.ini. The game reads it when it starts; while it runs, change settings in VR (hold the menu button).", true);
				}
				else
				{
					tell(L"Could not write " + s.ini.wstring() + L".", false);
				}
				break;
			case ui::settings_panel::action::close:
				load(view, s.ini, s.seed);
				tell(L"Changes undone: the settings are as bfvr.ini has them.", true);
				break;
			case ui::settings_panel::action::none:
				break;
			}
			InvalidateRect(view, nullptr, FALSE);
		}

		void paint(HWND view)
		{
			PAINTSTRUCT paint_info{};
			const HDC dc = BeginPaint(view, &paint_info);
			RECT client{};
			GetClientRect(view, &client);
			FillRect(dc, &client, theme::brush(theme::surface::background));
			const fitting place = fit(view);
			std::vector<std::uint32_t> pixels;
			state& s = self();
			if (place.width > 0 && place.height > 0
				&& ui::rasterise(s.panel.draw(s.store, ""), place.width, place.height, pixels, {}, place.scale, -title_crop))
			{
				BITMAPINFO info{};
				info.bmiHeader.biSize = sizeof(info.bmiHeader);
				info.bmiHeader.biWidth = place.width;
				info.bmiHeader.biHeight = -place.height;
				info.bmiHeader.biPlanes = 1;
				info.bmiHeader.biBitCount = 32;
				info.bmiHeader.biCompression = BI_RGB;
				SetDIBitsToDevice(dc, place.x, 0, static_cast<DWORD>(place.width), static_cast<DWORD>(place.height), 0, 0, 0,
					static_cast<UINT>(place.height), pixels.data(), &info, DIB_RGB_COLORS);
			}
			EndPaint(view, &paint_info);
		}

		LRESULT CALLBACK view_proc(HWND view, UINT message, WPARAM wparam, LPARAM lparam)
		{
			state& s = self();
			switch (message)
			{
			case WM_ERASEBKGND:
				return 1;
			case WM_PAINT:
				paint(view);
				return 0;
			case WM_MOUSEMOVE:
			{
				if (!s.tracking)
				{
					TRACKMOUSEEVENT track{ sizeof(track), TME_LEAVE, view, 0 };
					s.tracking = TrackMouseEvent(&track) != FALSE;
				}
				const ui::target over = hit(view, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
				if (!(over == s.panel.hovered()))
				{
					s.panel.hover(over);
					InvalidateRect(view, nullptr, FALSE);
				}
				return 0;
			}
			case WM_MOUSELEAVE:
				s.tracking = false;
				s.panel.hover({});
				InvalidateRect(view, nullptr, FALSE);
				return 0;
			case WM_SETCURSOR:
				if (LOWORD(lparam) == HTCLIENT && s.panel.hovered().kind != ui::target_kind::none)
				{
					SetCursor(LoadCursorW(nullptr, IDC_HAND));
					return TRUE;
				}
				break;
			case WM_LBUTTONDOWN:
			{
				SetFocus(view);
				const ui::target on = hit(view, GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam));
				if (on.kind != ui::target_kind::none)
				{
					act(view, s.panel.click(on, s.store));
				}
				return 0;
			}
			case WM_MOUSEWHEEL:
				scroll(view, GET_WHEEL_DELTA_WPARAM(wparam));
				return 0;
			case WM_GETDLGCODE:
				return DLGC_WANTARROWS;
			}
			return DefWindowProcW(view, message, wparam, lparam);
		}
	}

	HWND create(HWND dialog, int id, const RECT& area, notify_fn notify)
	{
		static const ATOM registered = []
		{
			WNDCLASSEXW window_class{ sizeof(window_class) };
			window_class.lpfnWndProc = view_proc;
			window_class.hInstance = GetModuleHandleW(nullptr);
			window_class.hCursor = LoadCursorW(nullptr, IDC_ARROW);
			window_class.lpszClassName = class_name;
			return RegisterClassExW(&window_class);
		}();
		if (!registered)
		{
			return nullptr;
		}
		state& s = self();
		s.dialog = dialog;
		s.notify = notify;
		return CreateWindowExW(0, class_name, L"", WS_CHILD | WS_TABSTOP, area.left, area.top, area.right - area.left, area.bottom - area.top,
			dialog, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
	}

	void load(HWND view, const std::filesystem::path& ini, const std::filesystem::path& seed)
	{
		state& s = self();
		s.ini = ini;
		s.seed = seed;
		const ini::values values = ini.empty() ? ini::values{} : ini::read(ini);
		s.store.load([&](const std::string& name) -> std::optional<std::string>
		{
			for (const auto& [key, value] : values)
			{
				if (_stricmp(key.c_str(), name.c_str()) == 0)
				{
					return value;
				}
			}
			return std::nullopt;
		});
		if (s.panel.pages().empty())
		{
			s.panel.set_pages(ui::default_pages(s.store));
		}
		if (view)
		{
			InvalidateRect(view, nullptr, FALSE);
		}
	}

	bool unsaved()
	{
		return view_state && view_state->store.changed();
	}

	void scroll(HWND view, int wheel_delta)
	{
		state& s = self();
		const bool up = wheel_delta > 0;
		if (up ? s.panel.can_scroll_up() : s.panel.can_scroll_down())
		{
			s.panel.click({ up ? ui::target_kind::rows_up : ui::target_kind::rows_down }, s.store);
			InvalidateRect(view, nullptr, FALSE);
		}
	}
}
