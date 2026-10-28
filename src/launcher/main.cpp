// bf-vr launcher: installs and removes the dinput8.dll proxy in the BF4 folder, sets BF4 and bfvr.ini
// up for VR, edits bfvr.ini and starts BF4 through the EA app.
//
// Usage: bfvr_launcher.exe [--game-dir <folder>]   (default: the install folder from the registry)
// The mod's dinput8.dll and bfvr.ini.example are taken from the launcher's own folder.

#include <windows.h>
#include <commctrl.h>
#include <shellapi.h>
#include <shlobj.h>
#include <uxtheme.h>
#include <windowsx.h>

#include <filesystem>
#include <map>
#include <string>

#include "launcher/ini_file.hpp"
#include "launcher/install.hpp"
#include "launcher/resource.hpp"
#include "launcher/theme.hpp"
#include "launcher/vr_setup.hpp"

namespace bfvr::launcher
{
	namespace
	{
		struct toggle
		{
			int control;
			const char* key;
			bool fallback;
		};

		// Defaults as in common/config.hpp.
		constexpr toggle toggles[] = {
			{ IDC_THEATER, "theater", true },
			{ IDC_STEREO, "stereo", false },
			{ IDC_STEREO_FIRST_PERSON, "stereo_first_person", true },
			{ IDC_GAMEPAD, "gamepad", true },
		};
		constexpr const wchar_t* runtimes[] = { L"auto", L"system", L"steamvr" };

		// The content IDs bf4.exe itself hands to the EA app (tools/game.ps1); the Play button's offer
		// key is rejected over this link as "EntitlementNotFound".
		constexpr wchar_t ea_launch_link[] = L"origin2://game/launch/?offerIds=1007968,1011575,1011576,1011577,1010268,1010269,1010270,"
			L"1010271,1010958,1010959,1010960,1010961,1007077,1016751,1016757,1016754,1015365,1015364,1015363,1015362";

		constexpr UINT_PTR refresh_timer = 1;
		constexpr UINT fit_message = WM_APP + 1;

		enum page
		{
			play,
			setup,
			settings,
			page_count,
		};

		// Dialog units, as in launcher.rc: the header and footer bands, the Play key art and each page's panels.
		constexpr RECT header_band{ 0, 0, 420, 22 };
		constexpr RECT footer_band{ 0, 226, 420, 262 };
		constexpr RECT art{ 10, 32, 260, 218 };
		constexpr RECT panels[page_count][3] = {
			{ { 270, 32, 410, 160 } },
			{ { 10, 32, 210, 122 }, { 10, 128, 210, 218 }, { 220, 32, 410, 218 } },
			{ { 10, 32, 410, 218 } },
		};

		enum class tone
		{
			normal,
			good,
			bad,
		};

		struct
		{
			game_install game;
			std::filesystem::path launcher_dir;
			std::filesystem::path profile;
			build_info build{};
			proxy_status proxy{};
			bool running{};
			page current{ play };
			std::map<int, tone> tones;
		} app;

		std::filesystem::path mod_dll() { return app.launcher_dir / L"dinput8.dll"; }
		std::filesystem::path ini_path() { return app.game.dir / L"bfvr.ini"; }

		std::wstring widen(const std::string& text)
		{
			if (text.empty())
			{
				return {};
			}
			const int size = MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(CP_ACP, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
			return wide;
		}

		// bfvr::config reads the file as narrow (ANSI) text, so values are written the same way.
		std::string narrow(const std::wstring& text)
		{
			if (text.empty())
			{
				return {};
			}
			const int size = WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
			std::string ansi(static_cast<std::size_t>(size), '\0');
			WideCharToMultiByte(CP_ACP, 0, text.data(), static_cast<int>(text.size()), ansi.data(), size, nullptr, nullptr);
			return ansi;
		}

		std::wstring window_text(HWND dialog, int control)
		{
			const HWND item = GetDlgItem(dialog, control);
			std::wstring text(static_cast<std::size_t>(GetWindowTextLengthW(item)) + 1, L'\0');
			text.resize(static_cast<std::size_t>(GetWindowTextW(item, text.data(), static_cast<int>(text.size()))));
			const auto first = text.find_first_not_of(L" \t");
			const auto last = text.find_last_not_of(L" \t");
			return first == std::wstring::npos ? std::wstring() : text.substr(first, last - first + 1);
		}

		// A status line; the tone picks its colour (WM_CTLCOLORSTATIC).
		void show(HWND dialog, int control, const std::wstring& text, tone shade)
		{
			app.tones[control] = shade;
			SetDlgItemTextW(dialog, control, text.c_str());
			InvalidateRect(GetDlgItem(dialog, control), nullptr, TRUE);
		}

		void say(HWND dialog, const std::wstring& message) { show(dialog, IDC_MESSAGE, message, tone::normal); }

		void fail(HWND dialog, const std::wstring& message)
		{
			show(dialog, IDC_MESSAGE, message, tone::bad);
			MessageBoxW(dialog, message.c_str(), L"bf-vr launcher", MB_OK | MB_ICONWARNING);
		}

		bool is_on(HWND dialog, int control) { return theme::checked(GetDlgItem(dialog, control)); }

		RECT to_pixels(HWND dialog, RECT area)
		{
			MapDialogRect(dialog, &area);
			return area;
		}

		int page_of(int control) { return control >= 1100 && control < 1400 ? control / 100 - 11 : -1; }

		template<typename visit>
		void for_each_child(HWND dialog, visit&& visitor)
		{
			for (HWND child = GetWindow(dialog, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT))
			{
				visitor(child, GetDlgCtrlID(child));
			}
		}

		void show_page(HWND dialog, page shown)
		{
			app.current = shown;
			for_each_child(dialog, [&](HWND child, int control)
			{
				const int owner = page_of(control);
				if (owner >= 0)
				{
					ShowWindow(child, owner == shown ? SW_SHOWNA : SW_HIDE);
				}
			});
			theme::set_checked(GetDlgItem(dialog, IDC_TAB_PLAY), shown == play);
			theme::set_checked(GetDlgItem(dialog, IDC_TAB_SETUP), shown == setup);
			theme::set_checked(GetDlgItem(dialog, IDC_TAB_SETTINGS), shown == settings);
			// Enter starts the game only where the Start button is visible.
			SendMessageW(dialog, DM_SETDEFID, shown == play ? IDC_START : IDOK, 0);
			InvalidateRect(dialog, nullptr, TRUE);
		}

		// What a control sits on, so its background matches: header and footer, a panel, or the page.
		theme::surface backdrop_of(HWND dialog, HWND control)
		{
			const int id = GetDlgCtrlID(control);
			if (id == IDC_MESSAGE || page_of(id) < 0)
			{
				return theme::surface::chrome;
			}
			RECT area{};
			GetWindowRect(control, &area);
			MapWindowPoints(nullptr, dialog, reinterpret_cast<POINT*>(&area), 2);
			const POINT middle{ (area.left + area.right) / 2, (area.top + area.bottom) / 2 };
			for (const RECT& panel : panels[app.current])
			{
				const RECT pixels = to_pixels(dialog, panel);
				if (panel.right > panel.left && PtInRect(&pixels, middle))
				{
					return theme::surface::panel;
				}
			}
			return theme::surface::background;
		}

		theme::button style_of(int control)
		{
			switch (control)
			{
			case IDC_TAB_PLAY:
			case IDC_TAB_SETUP:
			case IDC_TAB_SETTINGS: return theme::button::tab;
			case IDC_MINIMIZE: return theme::button::minimize;
			case IDCANCEL: return theme::button::close;
			case IDC_START: return theme::button::launch;
			case IDC_SETUP:
			case IDC_SAVE: return theme::button::primary;
			case IDC_THEATER:
			case IDC_STEREO:
			case IDC_STEREO_FIRST_PERSON:
			case IDC_GAMEPAD: return theme::button::toggle;
			default: return theme::button::normal;
			}
		}

		// Page captions carry their style in the tens digit of their ID (resource.hpp).
		enum caption
		{
			heading = 4,
			label = 5,
			hint = 7,
			warning = 8,
		};

		int caption_of(int control) { return page_of(control) >= 0 ? control % 100 / 10 : 0; }

		COLORREF ink_of(int control)
		{
			switch (caption_of(control))
			{
			case heading: return theme::color::text;
			case label:
			case label + 1:
			case hint: return theme::color::muted;
			case warning: return theme::color::accent;
			}
			const auto shade = app.tones.find(control);
			switch (shade == app.tones.end() ? tone::normal : shade->second)
			{
			case tone::good: return theme::color::accent;
			case tone::bad: return theme::color::error;
			default: return theme::color::text;
			}
		}

		void apply_fonts(HWND dialog)
		{
			for_each_child(dialog, [&](HWND child, int control)
			{
				const int kind = caption_of(control);
				if (kind < heading)
				{
					return;
				}
				const theme::font role = kind == heading ? theme::font::heading : kind == label || kind == label + 1 ? theme::font::label : theme::font::note;
				SendMessageW(child, WM_SETFONT, reinterpret_cast<WPARAM>(theme::get(role)), TRUE);
			});
		}

		// The template size as the whole window: the system frame is gone (WM_NCCALCSIZE).
		void fit(HWND dialog, bool center)
		{
			const RECT size = to_pixels(dialog, RECT{ 0, 0, footer_band.right, footer_band.bottom });
			RECT place{};
			GetWindowRect(dialog, &place);
			if (center)
			{
				MONITORINFO monitor{ sizeof(monitor) };
				GetMonitorInfoW(MonitorFromWindow(dialog, MONITOR_DEFAULTTONEAREST), &monitor);
				place.left = (monitor.rcWork.left + monitor.rcWork.right - size.right) / 2;
				place.top = (monitor.rcWork.top + monitor.rcWork.bottom - size.bottom) / 2;
			}
			SetWindowPos(dialog, nullptr, place.left, place.top, size.right, size.bottom, SWP_NOZORDER | SWP_NOACTIVATE);
		}

		void paint(HWND dialog)
		{
			PAINTSTRUCT paint_info{};
			const HDC target = BeginPaint(dialog, &paint_info);
			RECT client{};
			GetClientRect(dialog, &client);
			const HDC dc = CreateCompatibleDC(target);
			const HBITMAP bitmap = CreateCompatibleBitmap(target, client.right, client.bottom);
			const HGDIOBJ old = SelectObject(dc, bitmap);

			FillRect(dc, &client, theme::brush(theme::surface::background));
			theme::draw_header(dc, to_pixels(dialog, header_band));
			RECT footer = to_pixels(dialog, footer_band);
			footer.bottom = client.bottom;
			theme::draw_footer(dc, footer);
			if (app.current == play)
			{
				theme::draw_art(dc, to_pixels(dialog, art));
			}
			for (const RECT& panel : panels[app.current])
			{
				if (panel.right > panel.left)
				{
					theme::draw_panel(dc, to_pixels(dialog, panel));
				}
			}

			BitBlt(target, 0, 0, client.right, client.bottom, dc, 0, 0, SRCCOPY);
			SelectObject(dc, old);
			DeleteObject(bitmap);
			DeleteDC(dc);
			EndPaint(dialog, &paint_info);
		}

		void init_window(HWND dialog)
		{
			theme::create(dialog);
			apply_fonts(dialog);
			for_each_child(dialog, [](HWND child, int)
			{
				if ((GetWindowLongW(child, GWL_STYLE) & BS_TYPEMASK) == BS_OWNERDRAW)
				{
					wchar_t name[16]{};
					GetClassNameW(child, name, 16);
					if (_wcsicmp(name, L"Button") == 0)
					{
						theme::attach(child);
					}
				}
			});
			SetWindowTheme(GetDlgItem(dialog, IDC_RUNTIME), L"DarkMode_CFD", nullptr);
			const UINT dpi = GetDpiForWindow(dialog);
			for (const auto [kind, metric] : { std::pair{ ICON_BIG, SM_CXICON }, std::pair{ ICON_SMALL, SM_CXSMICON } })
			{
				const int size = GetSystemMetricsForDpi(metric, dpi);
				const HANDLE icon = LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_APP), IMAGE_ICON, size, size, LR_SHARED);
				SendMessageW(dialog, WM_SETICON, kind, reinterpret_cast<LPARAM>(icon));
			}
			theme::frame(dialog);
			fit(dialog, true);
		}

		// Dialog procedures return message results through DWLP_MSGRESULT.
		INT_PTR answer(HWND dialog, LRESULT result)
		{
			SetWindowLongPtrW(dialog, DWLP_MSGRESULT, result);
			return TRUE;
		}

		std::wstring build_text()
		{
			wchar_t text[256]{};
			switch (app.build.result)
			{
			case build_check::ok:
				return L"179547 (verified)";
			case build_check::missing:
				return L"bf4.exe not found in this folder";
			case build_check::unreadable:
				return L"bf4.exe has no readable PE header";
			case build_check::mismatch:
				swprintf_s(text, L"unsupported (PE timestamp 0x%08X, machine 0x%04X); the mod only supports build 179547",
					app.build.timestamp, app.build.machine);
				return text;
			}
			return {};
		}

		std::wstring mod_text()
		{
			std::error_code error;
			switch (app.proxy.state)
			{
			case proxy_state::absent:
				return std::filesystem::exists(mod_dll(), error) ? L"not installed" : L"not installed; dinput8.dll is missing beside the launcher";
			case proxy_state::ours:
				return L"INSTALLED (" + widen(app.proxy.config) + L" build, " + widen(app.proxy.deployed) + L"): remove before multiplayer";
			case proxy_state::foreign:
				return L"another mod's dinput8.dll is present; bf-vr leaves it untouched";
			}
			return {};
		}

		void refresh_setup(HWND dialog);

		void refresh(HWND dialog, bool full)
		{
			const bool have_dir = !app.game.dir.empty();
			const bool was_running = app.running;
			app.running = have_dir && game_running(app.game);
			if (!full && app.running == was_running)
			{
				return;
			}

			app.build = have_dir ? check_build(app.game) : build_info{ build_check::missing };
			app.proxy = have_dir ? read_proxy(app.game) : proxy_status{};

			std::error_code error;
			const bool dll_present = std::filesystem::exists(mod_dll(), error);
			show(dialog, IDC_FOLDER, have_dir ? app.game.dir.wstring() : L"not found: choose the Battlefield 4 folder", have_dir ? tone::normal : tone::bad);
			show(dialog, IDC_BUILD, have_dir ? build_text() : L"-", app.build.result == build_check::ok ? tone::good : tone::bad);
			const tone mod_tone = app.proxy.state == proxy_state::ours ? tone::good
				: app.proxy.state == proxy_state::foreign || !dll_present ? tone::bad : tone::normal;
			for (const int control : { IDC_MOD, IDC_MOD_SETUP })
			{
				show(dialog, control, have_dir ? mod_text() : L"-", mod_tone);
			}
			show(dialog, IDC_RUNNING, app.running ? L"running (close it to install or remove the mod)" : L"not running", app.running ? tone::good : tone::normal);
			SetDlgItemTextW(dialog, IDC_INSTALL, app.proxy.state == proxy_state::ours ? L"Update mod" : L"Install mod");

			EnableWindow(GetDlgItem(dialog, IDC_OPEN_FOLDER), have_dir);
			EnableWindow(GetDlgItem(dialog, IDC_INSTALL), have_dir && dll_present && app.build.result == build_check::ok
				&& !app.running && app.proxy.state != proxy_state::foreign);
			EnableWindow(GetDlgItem(dialog, IDC_REMOVE), app.proxy.state == proxy_state::ours && !app.running);
			EnableWindow(GetDlgItem(dialog, IDC_START), have_dir && !app.running);
			EnableWindow(GetDlgItem(dialog, IDC_SAVE), have_dir);
			EnableWindow(GetDlgItem(dialog, IDC_RELOAD), have_dir);
			refresh_setup(dialog);
		}

		std::wstring pending_text(const std::vector<const wanted_setting*>& pending)
		{
			if (pending.empty())
			{
				return L"ready for VR";
			}
			std::wstring text = std::to_wstring(pending.size()) + L" to change: ";
			bool first = true;
			for (const wanted_setting* setting : pending)
			{
				if (*setting->label)
				{
					text += (first ? L"" : L", ") + std::wstring(setting->label);
					first = false;
				}
			}
			return text;
		}

		void show_runtime(HWND dialog)
		{
			const runtime_status runtime = read_runtime();
			if (runtime.steamvr_running)
			{
				show(dialog, IDC_RUNTIME_STATUS, L"SteamVR is running: the mod uses it (Steam Link)", tone::good);
			}
			else if (runtime.system_present)
			{
				show(dialog, IDC_RUNTIME_STATUS, runtime.system_name + L" (system default). For Steam Link start SteamVR first", tone::normal);
			}
			else if (runtime.steamvr_installed)
			{
				show(dialog, IDC_RUNTIME_STATUS, L"SteamVR installed, not running: start it before BF4", tone::normal);
			}
			else
			{
				show(dialog, IDC_RUNTIME_STATUS, L"no OpenXR runtime found: install SteamVR (Steam Link) or Meta Horizon Link", tone::bad);
			}
		}

		void show_pending(HWND dialog, int control, const std::vector<const wanted_setting*>& pending)
		{
			show(dialog, control, pending_text(pending), pending.empty() ? tone::good : tone::normal);
		}

		// The setup lines; the runtime is re-read every timer tick (SteamVR may start any time).
		void refresh_setup(HWND dialog)
		{
			const bool have_dir = !app.game.dir.empty();
			std::error_code error;
			const bool profile_found = !app.profile.empty() && std::filesystem::exists(app.profile, error);
			if (profile_found)
			{
				show_pending(dialog, IDC_PROFILE_STATUS, profile_pending(app.profile));
			}
			else
			{
				show(dialog, IDC_PROFILE_STATUS, L"not found yet: start Battlefield 4 once so it creates its settings", tone::bad);
			}
			if (have_dir)
			{
				show_pending(dialog, IDC_INI_STATUS, ini_pending(ini_path()));
			}
			else
			{
				show(dialog, IDC_INI_STATUS, L"-", tone::normal);
			}
			show_runtime(dialog);
			EnableWindow(GetDlgItem(dialog, IDC_SETUP), have_dir && app.build.result == build_check::ok && !app.running);
			EnableWindow(GetDlgItem(dialog, IDC_RESTORE_PROFILE), !app.running && std::filesystem::exists(profile_backup(app.profile), error));
		}

		void update_stereo_options(HWND dialog)
		{
			EnableWindow(GetDlgItem(dialog, IDC_STEREO_FIRST_PERSON), is_on(dialog, IDC_STEREO));
		}

		void load_settings(HWND dialog)
		{
			const ini::values values = app.game.dir.empty() ? ini::values{} : ini::read(ini_path());
			for (const auto& item : toggles)
			{
				theme::set_checked(GetDlgItem(dialog, item.control), ini::get_bool(values, item.key, item.fallback));
			}
			const auto runtime = values.find("xr_runtime");
			SetDlgItemTextW(dialog, IDC_RUNTIME, runtime == values.end() || runtime->second.empty() ? L"auto" : widen(runtime->second).c_str());
			update_stereo_options(dialog);

			// bfvr::config lets BFVR_<KEY> environment variables override the file.
			std::wstring overridden;
			for (const wchar_t* variable : { L"BFVR_THEATER", L"BFVR_STEREO", L"BFVR_STEREO_FIRST_PERSON", L"BFVR_GAMEPAD", L"BFVR_XR_RUNTIME" })
			{
				if (GetEnvironmentVariableW(variable, nullptr, 0) > 1)
				{
					overridden += std::wstring(overridden.empty() ? L"" : L", ") + variable;
				}
			}
			if (!overridden.empty())
			{
				say(dialog, L"Note: " + overridden + L" set in the environment override bfvr.ini.");
			}
		}

		void save_settings(HWND dialog)
		{
			ini::changes edits;
			for (const auto& item : toggles)
			{
				edits.emplace_back(item.key, is_on(dialog, item.control) ? "1" : "0");
			}
			const std::wstring runtime = window_text(dialog, IDC_RUNTIME);
			if (runtime.find(L'#') != std::wstring::npos)
			{
				fail(dialog, L"The runtime path cannot contain '#': bfvr.ini treats it as the start of a comment.");
				return;
			}
			edits.emplace_back("xr_runtime", runtime.empty() ? "auto" : narrow(runtime));

			if (ini::write(ini_path(), edits, app.launcher_dir / L"bfvr.ini.example"))
			{
				say(dialog, L"Settings saved to bfvr.ini; the game reads them when it starts.");
				refresh_setup(dialog);
			}
			else
			{
				fail(dialog, L"Could not write " + ini_path().wstring() + L".");
			}
		}

		void report(HWND dialog, const result& outcome)
		{
			if (outcome.ok)
			{
				say(dialog, outcome.message);
			}
			else
			{
				fail(dialog, outcome.message);
			}
			refresh(dialog, true);
		}

		void open_path(HWND dialog, const std::filesystem::path& path)
		{
			if (reinterpret_cast<INT_PTR>(ShellExecuteW(dialog, L"open", path.c_str(), nullptr, nullptr, SW_SHOWNORMAL)) > 32)
			{
				return;
			}
			const std::wstring arguments = L"\"" + path.wstring() + L"\"";
			if (reinterpret_cast<INT_PTR>(ShellExecuteW(dialog, L"open", L"notepad.exe", arguments.c_str(), nullptr, SW_SHOWNORMAL)) <= 32)
			{
				fail(dialog, L"Could not open " + path.wstring() + L".");
			}
		}

		void open_log(HWND dialog)
		{
			const auto log = app.game.dir / L"bfvr.log";
			std::error_code error;
			if (!std::filesystem::exists(log, error))
			{
				say(dialog, L"No bfvr.log yet: it is written when the game starts with the mod installed.");
				return;
			}
			open_path(dialog, log);
		}

		// Everything a new user needs: the mod, its VR settings and BF4's own settings, each reported.
		void set_up(HWND dialog)
		{
			std::wstring message;
			bool ok = true;
			const auto add = [&](const result& step)
			{
				message += (message.empty() ? L"" : L"\r\n") + step.message;
				ok = ok && step.ok;
			};
			if (app.proxy.state == proxy_state::foreign)
			{
				add({ false, L"Mod not installed: another mod's dinput8.dll is in the game folder." });
			}
			else
			{
				add(deploy(app.game, mod_dll(), BFVR_BUILD_CONFIG));
			}
			add(apply_ini(ini_path(), app.launcher_dir / L"bfvr.ini.example"));
			add(apply_profile(app.profile, game_running(app.game)));
			if (ok)
			{
				message += L"\r\nReady: start SteamVR (or Link), then Start Battlefield 4.";
			}
			report(dialog, { ok, message });
			load_settings(dialog);
			if (ok)
			{
				show_page(dialog, play);
			}
		}

		void start_game(HWND dialog)
		{
			if (app.proxy.state != proxy_state::ours
				&& MessageBoxW(dialog, L"The mod is not installed, so Battlefield 4 starts without VR. Start anyway?",
					L"bf-vr launcher", MB_YESNO | MB_ICONQUESTION) != IDYES)
			{
				return;
			}
			if (reinterpret_cast<INT_PTR>(ShellExecuteW(dialog, L"open", ea_launch_link, nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
			{
				fail(dialog, L"Windows did not accept the EA app's launch link. Is the EA app installed?");
				return;
			}
			say(dialog, L"Launch sent to the EA app; BF4 opens in a minute. If nothing happens, check the EA app for a prompt.");
		}

		void choose_folder(HWND dialog)
		{
			BROWSEINFOW browse{};
			browse.hwndOwner = dialog;
			browse.lpszTitle = L"Choose the Battlefield 4 folder (the one with bf4.exe)";
			browse.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
			PIDLIST_ABSOLUTE chosen = SHBrowseForFolderW(&browse);
			if (!chosen)
			{
				return;
			}
			wchar_t path[MAX_PATH]{};
			if (SHGetPathFromIDListW(chosen, path))
			{
				app.game.dir = path;
				say(dialog, L"");
				refresh(dialog, true);
				load_settings(dialog);
			}
			CoTaskMemFree(chosen);
		}

		INT_PTR CALLBACK dialog_proc(HWND dialog, UINT message, WPARAM wparam, LPARAM lparam)
		{
			switch (message)
			{
			case WM_INITDIALOG:
				for (const wchar_t* runtime : runtimes)
				{
					SendDlgItemMessageW(dialog, IDC_RUNTIME, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(runtime));
				}
				init_window(dialog);
				refresh(dialog, true);
				load_settings(dialog);
				show_page(dialog, app.proxy.state == proxy_state::ours ? play : setup);
				SetTimer(dialog, refresh_timer, 2000, nullptr);
				return TRUE;
			case WM_TIMER:
				refresh(dialog, false);
				show_runtime(dialog);
				return TRUE;
			case WM_NCCALCSIZE:
				if (wparam)
				{
					return answer(dialog, 0);
				}
				return FALSE;
			case WM_NCHITTEST:
			{
				POINT point{ GET_X_LPARAM(lparam), GET_Y_LPARAM(lparam) };
				ScreenToClient(dialog, &point);
				return answer(dialog, point.y < to_pixels(dialog, header_band).bottom ? HTCAPTION : HTCLIENT);
			}
			case WM_ERASEBKGND:
				return answer(dialog, 1);
			case WM_PAINT:
				paint(dialog);
				return TRUE;
			case WM_DRAWITEM:
			{
				const auto& item = *reinterpret_cast<const DRAWITEMSTRUCT*>(lparam);
				theme::draw_button(item, style_of(static_cast<int>(item.CtlID)), backdrop_of(dialog, item.hwndItem));
				return TRUE;
			}
			case WM_CTLCOLORSTATIC:
			{
				const auto control = reinterpret_cast<HWND>(lparam);
				const theme::surface area = backdrop_of(dialog, control);
				const auto dc = reinterpret_cast<HDC>(wparam);
				SetTextColor(dc, ink_of(GetDlgCtrlID(control)));
				SetBkColor(dc, theme::fill(area));
				return reinterpret_cast<INT_PTR>(theme::brush(area));
			}
			case WM_CTLCOLOREDIT:
			case WM_CTLCOLORLISTBOX:
			{
				const auto dc = reinterpret_cast<HDC>(wparam);
				SetTextColor(dc, theme::color::text);
				SetBkColor(dc, theme::color::raised);
				SetDCBrushColor(dc, theme::color::raised);
				return reinterpret_cast<INT_PTR>(GetStockObject(DC_BRUSH));
			}
			case WM_DPICHANGED:
				theme::create(dialog);
				apply_fonts(dialog);
				PostMessageW(dialog, fit_message, 0, 0);
				return FALSE;
			case fit_message:
				fit(dialog, false);
				InvalidateRect(dialog, nullptr, TRUE);
				return TRUE;
			case WM_ACTIVATE:
				if (LOWORD(wparam) != WA_INACTIVE)
				{
					refresh(dialog, true);
				}
				return FALSE;
			case WM_COMMAND:
				switch (LOWORD(wparam))
				{
				case IDC_TAB_PLAY: show_page(dialog, play); break;
				case IDC_TAB_SETUP: show_page(dialog, setup); break;
				case IDC_TAB_SETTINGS: show_page(dialog, settings); break;
				case IDC_MINIMIZE: ShowWindow(dialog, SW_MINIMIZE); break;
				case IDC_THEATER:
				case IDC_STEREO:
				case IDC_STEREO_FIRST_PERSON:
				case IDC_GAMEPAD:
				{
					const HWND toggle = GetDlgItem(dialog, LOWORD(wparam));
					theme::set_checked(toggle, !theme::checked(toggle));
					update_stereo_options(dialog);
					break;
				}
				case IDC_CHANGE_FOLDER: choose_folder(dialog); break;
				case IDC_OPEN_FOLDER: open_path(dialog, app.game.dir); break;
				case IDC_INSTALL: report(dialog, deploy(app.game, mod_dll(), BFVR_BUILD_CONFIG)); break;
				case IDC_REMOVE: report(dialog, remove(app.game)); break;
				case IDC_SAVE: save_settings(dialog); break;
				case IDC_RELOAD: load_settings(dialog); say(dialog, L"Settings reloaded from bfvr.ini."); break;
				case IDC_START: start_game(dialog); break;
				case IDC_SETUP: set_up(dialog); break;
				case IDC_RESTORE_PROFILE: report(dialog, restore_profile(app.profile, game_running(app.game))); break;
				case IDC_OPEN_LOG: open_log(dialog); break;
				case IDCANCEL: KillTimer(dialog, refresh_timer); EndDialog(dialog, 0); break;
				default: return FALSE;
				}
				return TRUE;
			}
			return FALSE;
		}
	}
}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int)
{
	using namespace bfvr::launcher;

	// Our own dinput8.dll sits beside the launcher; nothing in this process may ever load it.
	SetDefaultDllDirectories(LOAD_LIBRARY_SEARCH_SYSTEM32);

	wchar_t path[MAX_PATH]{};
	const DWORD length = GetModuleFileNameW(nullptr, path, MAX_PATH);
	app.launcher_dir = length && length < MAX_PATH ? std::filesystem::path(path).parent_path() : std::filesystem::current_path();

	int count{};
	wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
	for (int i = 1; arguments && i + 1 < count; ++i)
	{
		if (_wcsicmp(arguments[i], L"--game-dir") == 0)
		{
			app.game.dir = arguments[++i];
		}
	}
	LocalFree(arguments);
	if (app.game.dir.empty())
	{
		app.game.dir = find_install();
	}
	app.profile = find_profile();

	const INITCOMMONCONTROLSEX controls{ sizeof(controls), ICC_STANDARD_CLASSES };
	InitCommonControlsEx(&controls);
	const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);

	const INT_PTR result = DialogBoxParamW(instance, MAKEINTRESOURCEW(IDD_MAIN), nullptr, dialog_proc, 0);
	theme::destroy();

	if (SUCCEEDED(com))
	{
		CoUninitialize();
	}
	return static_cast<int>(result);
}
