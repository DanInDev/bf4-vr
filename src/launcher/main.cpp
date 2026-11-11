// bf-vr launcher: installs and removes the dinput8.dll proxy in the game folder, sets the game and
// bfvr.ini up for VR, edits bfvr.ini and starts the game through the EA app. Games: Battlefield 4
// (the VR mod) and Battlefield 3 (research build); launcher/install.hpp holds what differs.
//
// Usage: bfvr_launcher.exe [--game bf4|bf3] [--game-dir <folder>]
// Defaults: the game chosen last time (bfvr_launcher.ini beside the launcher), the install folder from
// the registry. The mod's dinput8.dll (BF3's in the folder bf3) and bfvr.ini.example are taken from
// the launcher's own folder.

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
#include "launcher/settings_view.hpp"
#include "launcher/theme.hpp"
#include "launcher/update.hpp"
#include "launcher/vr_setup.hpp"

namespace bfvr::launcher
{
	namespace
	{

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
			{ { 0, 0, 0, 0 } },
		};
		// The Settings page's view (launcher/settings_view): the in-VR panel, without its title bar.
		constexpr RECT settings_area{ 10, 26, 410, 224 };

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

		const game_profile& game() { return *app.game.profile; }
		std::filesystem::path mod_dll()
		{
			const std::string_view config = BFVR_BUILD_CONFIG;
			return find_mod_dll(app.launcher_dir, game(), std::wstring(config.begin(), config.end()).c_str());
		}
		std::filesystem::path ini_path() { return app.game.dir / L"bfvr.ini"; }
		// Remembers the chosen game between starts.
		std::filesystem::path launcher_ini() { return app.launcher_dir / L"bfvr_launcher.ini"; }

		const game_profile& other_game()
		{
			for (const game_profile* profile : game_profiles)
			{
				if (profile != app.game.profile)
				{
					return *profile;
				}
			}
			return game();
		}

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
			case IDC_SETUP: return theme::button::primary;
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
				return std::wstring(game().build) + L" (verified)";
			case build_check::missing:
				return std::wstring(game().exe) + L" not found in this folder";
			case build_check::unreadable:
				return std::wstring(game().exe) + L" has no readable PE header";
			case build_check::mismatch:
				swprintf_s(text, L"unsupported (PE timestamp 0x%08X, machine 0x%04X); the mod only supports build %s",
					app.build.timestamp, app.build.machine, game().build);
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
			{
				// "2026-11-05T10:25:35" reads as "2026-11-05 10:25".
				std::wstring when = widen(app.proxy.deployed);
				if (when.size() >= 16 && when[10] == L'T')
				{
					when = when.substr(0, 10) + L" " + when.substr(11, 5);
				}
				return L"Installed (" + widen(app.proxy.config) + L" build, " + when + L"). Remove it before multiplayer.";
			}
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
			show(dialog, IDC_FOLDER, have_dir ? app.game.dir.wstring() : std::wstring(L"not found: choose the ") + game().name + L" folder", have_dir ? tone::normal : tone::bad);
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
				show(dialog, IDC_RUNTIME_STATUS, std::wstring(L"SteamVR installed, not running: start it before ") + game().brand, tone::normal);
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
			if (!game().vr_setup)
			{
				// Research build: which game settings VR needs is not known yet, so none are touched.
				show(dialog, IDC_PROFILE_STATUS, L"left as they are (research build)", tone::normal);
			}
			else if (profile_found)
			{
				show_pending(dialog, IDC_PROFILE_STATUS, profile_pending(app.profile));
			}
			else
			{
				show(dialog, IDC_PROFILE_STATUS, std::wstring(L"not found yet: start ") + game().name + L" once so it creates its settings", tone::bad);
			}
			if (have_dir && !game().vr_setup)
			{
				const bool present = std::filesystem::exists(ini_path(), error);
				show(dialog, IDC_INI_STATUS, present ? L"bfvr.ini present (flat screen in the headset)" : L"bfvr.ini is created by the setup", present ? tone::good : tone::normal);
			}
			else if (have_dir)
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

		void load_settings(HWND dialog)
		{
			settings_view::load(GetDlgItem(dialog, IDC_SETTINGS_VIEW), app.game.dir.empty() ? std::filesystem::path() : ini_path(),
				app.launcher_dir / L"bfvr.ini.example");

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

		// From the Settings page's view: saved, undone, or an error.
		void settings_message(HWND dialog, const std::wstring& text, bool ok)
		{
			show(dialog, IDC_MESSAGE, text, ok ? tone::normal : tone::bad);
			refresh_setup(dialog);
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

		// Asks GitHub for the latest release only on this click; offers its page when it is newer.
		void check_update(HWND dialog)
		{
			say(dialog, L"Asking GitHub for the latest release...");
			UpdateWindow(dialog);
			const release latest = latest_release();
			const std::wstring current = widen(std::string(mod_version()));
			if (!latest.ok)
			{
				say(dialog, L"Update check: " + widen(latest.error) + L". This is version " + current + L".");
				return;
			}
			if (compare_versions(latest.tag, mod_version()) <= 0)
			{
				say(dialog, L"Up to date: version " + current + L" (latest release " + widen(latest.tag) + L").");
				return;
			}
			const std::wstring text = L"Release " + widen(latest.tag) + L" is out (this is " + current + L"). Open its page?";
			say(dialog, text);
			if (MessageBoxW(dialog, text.c_str(), L"bf-vr launcher", MB_YESNO | MB_ICONINFORMATION) == IDYES && !latest.url.empty())
			{
				ShellExecuteW(dialog, L"open", widen(latest.url).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
			}
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
			if (game().vr_setup)
			{
				add(apply_ini(ini_path(), app.launcher_dir / L"bfvr.ini.example"));
				add(apply_profile(app.profile, game_running(app.game)));
			}
			else
			{
				// Only the template: the VR values are BF4's, and the game's own settings stay untouched.
				const bool seeded = ini::write(ini_path(), {}, app.launcher_dir / L"bfvr.ini.example");
				add({ seeded, seeded ? L"bfvr.ini is in place (flat screen in the headset)." : L"bfvr.ini could not be written to " + ini_path().wstring() + L"." });
			}
			if (ok)
			{
				message += std::wstring(L"\r\nReady: start SteamVR (or Link), then Start ") + game().name + L".";
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
				&& MessageBoxW(dialog, (std::wstring(L"The mod is not installed, so ") + game().name + L" starts without VR. Start anyway?").c_str(),
					L"bf-vr launcher", MB_YESNO | MB_ICONQUESTION) != IDYES)
			{
				return;
			}
			const launch_command command = launch_of(app.game);
			if (reinterpret_cast<INT_PTR>(ShellExecuteW(dialog, L"open", command.file.c_str(), command.arguments.empty() ? nullptr : command.arguments.c_str(),
					command.directory.empty() ? nullptr : command.directory.c_str(), SW_SHOWNORMAL)) <= 32)
			{
				fail(dialog, game().launch_link ? L"Windows did not accept the EA app's launch link. Is the EA app installed?"
					: L"Could not start " + command.file + L".");
				return;
			}
			say(dialog, std::wstring(L"Launch sent to the EA app; ") + game().brand + L" opens in a minute"
				+ (game().launch_link ? L"" : L" (its campaign menu in about two)") + L". If nothing happens, check the EA app for a prompt.");
		}

		// The texts that name the game, the header brand and the fresh state of the chosen game.
		void show_game(HWND dialog)
		{
			const std::wstring name = game().name;
			std::wstring upper = name;
			CharUpperW(upper.data());
			theme::set_brand(game().brand, game().name);
			SetWindowTextW(dialog, (L"bf-vr launcher - " + name + L" VR (campaign only)").c_str());
			SetDlgItemTextW(dialog, IDC_GAME, (std::wstring(L"Switch to ") + other_game().brand).c_str());
			SetDlgItemTextW(dialog, IDC_START, (L"Start " + name).c_str());
			SetDlgItemTextW(dialog, IDC_SETUP_GAME_HEADING, upper.c_str());
			SetDlgItemTextW(dialog, IDC_SETUP_PROFILE_LABEL, (std::wstring(game().brand) + L" SETTINGS").c_str());
			SetDlgItemTextW(dialog, IDC_RESTORE_PROFILE, (std::wstring(L"Restore ") + game().brand + L" settings").c_str());
			SetDlgItemTextW(dialog, IDC_SETUP_WARNING, game().warning);
			SetDlgItemTextW(dialog, IDC_SETUP_HINT, game().vr_setup
				? (L"One click installs the mod, writes bfvr.ini for VR and sets " + name + L"'s own settings (a backup is kept).").c_str()
				: L"One click installs the research build and writes bfvr.ini. The game's own settings are not changed.");
			SetDlgItemTextW(dialog, IDC_SETUP, game().vr_setup ? L"Set up for VR" : L"Set up");
			say(dialog, L"bf-vr " + widen(std::string(mod_version())) + L"  \x00B7  " + game().status);
			refresh(dialog, true);
			load_settings(dialog);
			InvalidateRect(dialog, nullptr, TRUE);
		}

		void choose_game(const game_profile& profile, const std::filesystem::path& dir)
		{
			app.game = { dir.empty() ? find_install(profile) : dir, profile.exe, &profile };
			app.profile = find_profile(profile);
		}

		void switch_game(HWND dialog)
		{
			if (settings_view::unsaved() && MessageBoxW(dialog, L"Your settings changes are not saved. Switch the game anyway?", L"bf-vr launcher",
					MB_YESNO | MB_ICONQUESTION) != IDYES)
			{
				return;
			}
			choose_game(other_game(), {});
			ini::write(launcher_ini(), { { "game", game().id } }, {});
			show_game(dialog);
			show_page(dialog, app.proxy.state == proxy_state::ours ? play : setup);
		}

		void choose_folder(HWND dialog)
		{
			BROWSEINFOW browse{};
			browse.hwndOwner = dialog;
			const std::wstring title = std::wstring(L"Choose the ") + game().name + L" folder (the one with " + game().exe + L")";
			browse.lpszTitle = title.c_str();
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
				init_window(dialog);
				settings_view::create(dialog, IDC_SETTINGS_VIEW, to_pixels(dialog, settings_area), settings_message);
				show_game(dialog);
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
			case WM_MOUSEWHEEL:
				if (app.current == settings)
				{
					settings_view::scroll(GetDlgItem(dialog, IDC_SETTINGS_VIEW), GET_WHEEL_DELTA_WPARAM(wparam));
					return TRUE;
				}
				return FALSE;
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
				case IDC_GAME: switch_game(dialog); break;
				case IDC_CHANGE_FOLDER: choose_folder(dialog); break;
				case IDC_OPEN_FOLDER: open_path(dialog, app.game.dir); break;
				case IDC_INSTALL: report(dialog, deploy(app.game, mod_dll(), BFVR_BUILD_CONFIG)); break;
				case IDC_REMOVE: report(dialog, remove(app.game)); break;
				case IDC_START: start_game(dialog); break;
				case IDC_SETUP: set_up(dialog); break;
				case IDC_RESTORE_PROFILE: report(dialog, restore_profile(app.profile, game_running(app.game))); break;
				case IDC_OPEN_LOG: open_log(dialog); break;
				case IDC_CHECK_UPDATE: check_update(dialog); break;
				case IDCANCEL:
					if (settings_view::unsaved() && MessageBoxW(dialog, L"Your settings changes are not saved. Close anyway?", L"bf-vr launcher",
							MB_YESNO | MB_ICONQUESTION) != IDYES)
					{
						break;
					}
					KillTimer(dialog, refresh_timer);
					EndDialog(dialog, 0);
					break;
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

	const game_profile* chosen = profile_by_id(ini::read(launcher_ini())["game"]);
	std::filesystem::path game_dir;
	int count{};
	wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &count);
	for (int i = 1; arguments && i + 1 < count; ++i)
	{
		if (_wcsicmp(arguments[i], L"--game-dir") == 0)
		{
			game_dir = arguments[++i];
		}
		else if (_wcsicmp(arguments[i], L"--game") == 0)
		{
			if (const game_profile* named = profile_by_id(narrow(arguments[++i])))
			{
				chosen = named;
			}
		}
	}
	LocalFree(arguments);
	choose_game(chosen ? *chosen : bf4_profile, game_dir);

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
