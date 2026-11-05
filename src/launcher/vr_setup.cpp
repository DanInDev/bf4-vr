#include "launcher/vr_setup.hpp"

#include <windows.h>
#include <shlobj.h>
#include <tlhelp32.h>

#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <sstream>

#include "launcher/ini_file.hpp"

namespace bfvr::launcher
{
	namespace
	{
		// Verified on the development PC (docs/FINDINGS.md, re/notes/render-quality.md, re/notes/haptics.md).
		constexpr wanted_setting profile_settings[] = {
			{ "GstRender.MantleEnabled", "0", L"DirectX 11 (Mantle off; the mod hooks DirectX 11)" },
			{ "GstRender.WeaponDOF", "0", L"weapon depth of field off" },
			{ "GstRender.MotionBlurEnabled", "0", L"motion blur off" },
			{ "GstRender.MotionBlur", "0", L"motion blur amount 0" },
			{ "GstRender.VSyncEnabled", "0", L"monitor vsync off (the headset paces frames)" },
			{ "GstInput.Vibration", "1", L"pad vibration on (controller haptics)" },
		};

		// From the headset tests (docs/HEADSET-TESTS.md): what the user plays with on a Quest 3. The last
		// field is the mod's own default for a missing key (core.cpp, camera_hook.cpp, hud_capture.cpp).
		constexpr wanted_setting ini_settings[] = {
			{ "theater", "1", L"VR on", "1" },
			{ "gamepad", "1", L"VR controllers as the pad", "1" },
			{ "stereo", "1", L"head-tracked 3D", "0" },
			{ "stereo_render", "pair", L"both eyes from one frame (no judder)", "aer" },
			{ "render_size", "2160x2160", L"render 2160x2160 (sharp)", "0" },
			{ "view_crop_h", "0.78", L"rendered width 0.78 (what the lenses show)", "1" },
			{ "view_crop_v", "0.72", L"rendered height 0.72", "1" },
			{ "hud_layer", "1", L"HUD on its own sharp layer", "0" },
		};

		std::string read_text(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			std::stringstream contents;
			contents << file.rdbuf();
			return contents.str();
		}

		bool same_value(const std::string& have, const char* want)
		{
			char* end_have{};
			char* end_want{};
			errno = 0;
			const double a = std::strtod(have.c_str(), &end_have);
			const double b = std::strtod(want, &end_want);
			if (!have.empty() && *end_have == '\0' && *end_want == '\0' && errno == 0)
			{
				return a == b;
			}
			return _stricmp(have.c_str(), want) == 0;
		}

		std::string trim(const std::string& text)
		{
			const auto first = text.find_first_not_of(" \t\r");
			const auto last = text.find_last_not_of(" \t\r");
			return first == std::string::npos ? std::string() : text.substr(first, last - first + 1);
		}

		// The profile: "Key value" per line, LF endings. Returns key -> value (last one wins).
		ini::values read_profile(const std::filesystem::path& profile)
		{
			ini::values values;
			std::istringstream lines(read_text(profile));
			for (std::string line; std::getline(lines, line);)
			{
				line = trim(line);
				const auto space = line.find_first_of(" \t");
				if (space != std::string::npos)
				{
					values[line.substr(0, space)] = trim(line.substr(space + 1));
				}
			}
			return values;
		}

		std::vector<const wanted_setting*> pending(std::span<const wanted_setting> settings, const ini::values& values, bool case_insensitive_keys)
		{
			std::vector<const wanted_setting*> out;
			for (const wanted_setting& setting : settings)
			{
				auto found = values.find(setting.key);
				if (found == values.end() && case_insensitive_keys)
				{
					for (auto it = values.begin(); it != values.end(); ++it)
					{
						if (_stricmp(it->first.c_str(), setting.key) == 0)
						{
							found = it;
						}
					}
				}
				const bool missing = found == values.end();
				if (missing ? !setting.fallback || !same_value(setting.fallback, setting.value) : !same_value(found->second, setting.value))
				{
					out.push_back(&setting);
				}
			}
			return out;
		}

		bool process_running(const wchar_t* name)
		{
			const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
			if (snapshot == INVALID_HANDLE_VALUE)
			{
				return false;
			}
			PROCESSENTRY32W entry{ sizeof(entry) };
			bool found = false;
			for (BOOL more = Process32FirstW(snapshot, &entry); more && !found; more = Process32NextW(snapshot, &entry))
			{
				found = _wcsicmp(entry.szExeFile, name) == 0;
			}
			CloseHandle(snapshot);
			return found;
		}

		// The first "runtime" directory in %LOCALAPPDATA%\openvr\openvrpaths.vrpath, as the mod reads it.
		std::filesystem::path steamvr_dir()
		{
			wchar_t local_app_data[MAX_PATH]{};
			if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
			{
				return {};
			}
			const std::string text = read_text(std::filesystem::path(local_app_data) / "openvr" / "openvrpaths.vrpath");
			const auto key = text.find("\"runtime\"");
			const auto open = key == std::string::npos ? key : text.find('"', text.find('[', key));
			const auto close = open == std::string::npos ? open : text.find('"', open + 1);
			if (close == std::string::npos)
			{
				return {};
			}
			std::string directory;
			for (auto i = open + 1; i < close; ++i)
			{
				if (text[i] == '\\' && i + 1 < close)
				{
					++i;
				}
				directory += text[i];
			}
			return directory;
		}

		std::wstring from_utf8(const std::string& text)
		{
			if (text.empty())
			{
				return {};
			}
			const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
			std::wstring wide(static_cast<std::size_t>(size), L'\0');
			MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
			return wide;
		}
	}

	std::span<const wanted_setting> vr_profile_settings() { return profile_settings; }
	std::span<const wanted_setting> vr_ini_settings() { return ini_settings; }

	std::filesystem::path find_profile()
	{
		PWSTR documents{};
		std::filesystem::path path;
		if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_Documents, KF_FLAG_DEFAULT, nullptr, &documents)))
		{
			path = std::filesystem::path(documents) / L"Battlefield 4" / L"settings" / L"PROFSAVE_profile";
		}
		CoTaskMemFree(documents);
		return path;
	}

	std::filesystem::path profile_backup(const std::filesystem::path& profile)
	{
		return std::filesystem::path(profile) += L".bfvr-backup";
	}

	std::vector<const wanted_setting*> profile_pending(const std::filesystem::path& profile)
	{
		return pending(profile_settings, read_profile(profile), false);
	}

	std::vector<const wanted_setting*> ini_pending(const std::filesystem::path& ini)
	{
		return pending(ini_settings, ini::read(ini), true);
	}

	result apply_profile(const std::filesystem::path& profile, bool game_running)
	{
		std::error_code error;
		if (game_running)
		{
			return { false, L"BF4 settings not changed: the game is running and writes its settings back when it closes. Close it first." };
		}
		if (profile.empty() || !std::filesystem::exists(profile, error))
		{
			return { false, L"BF4 settings not found (" + profile.wstring() + L"). Start Battlefield 4 once so it creates them, then set up again." };
		}
		const std::filesystem::path backup = profile_backup(profile);
		if (!std::filesystem::exists(backup, error) && !std::filesystem::copy_file(profile, backup, error))
		{
			return { false, L"BF4 settings not changed: the backup " + backup.wstring() + L" could not be written." };
		}

		std::string text = read_text(profile);
		std::istringstream lines(text);
		std::string out;
		std::vector<bool> done(std::size(profile_settings));
		for (std::string line; std::getline(lines, line);)
		{
			const std::string key = trim(line.substr(0, line.find_first_of(" \t")));
			for (std::size_t i = 0; i < std::size(profile_settings); ++i)
			{
				if (key == profile_settings[i].key)
				{
					line = std::string(profile_settings[i].key) + " " + profile_settings[i].value;
					done[i] = true;
				}
			}
			out += line + "\n";
		}
		for (std::size_t i = 0; i < std::size(profile_settings); ++i)
		{
			if (!done[i])
			{
				out += std::string(profile_settings[i].key) + " " + profile_settings[i].value + "\n";
			}
		}

		const std::filesystem::path temporary = std::filesystem::path(profile) += L".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			file << out;
			if (!file.flush())
			{
				return { false, L"BF4 settings not changed: " + temporary.wstring() + L" could not be written." };
			}
		}
		if (!MoveFileExW(temporary.c_str(), profile.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			std::filesystem::remove(temporary, error);
			return { false, L"BF4 settings not changed: " + profile.wstring() + L" could not be replaced." };
		}
		return { true, L"BF4 settings set for VR (original kept as " + backup.filename().wstring() + L")." };
	}

	result restore_profile(const std::filesystem::path& profile, bool game_running)
	{
		std::error_code error;
		const std::filesystem::path backup = profile_backup(profile);
		if (game_running)
		{
			return { false, L"Not restored: the game is running and writes its settings back when it closes. Close it first." };
		}
		if (!std::filesystem::exists(backup, error))
		{
			return { false, L"No backup to restore: bf-vr has not changed the BF4 settings." };
		}
		if (!CopyFileW(backup.c_str(), profile.c_str(), FALSE))
		{
			return { false, L"Restoring " + profile.wstring() + L" failed." };
		}
		std::filesystem::remove(backup, error);
		return { true, L"BF4 settings restored to what they were before the VR setup." };
	}

	result apply_ini(const std::filesystem::path& ini_path, const std::filesystem::path& seed)
	{
		ini::changes edits;
		for (const wanted_setting& setting : ini_settings)
		{
			edits.emplace_back(setting.key, setting.value);
		}
		if (!ini::write(ini_path, edits, seed))
		{
			return { false, L"Could not write " + ini_path.wstring() + L"." };
		}
		return { true, L"Mod settings set for VR in bfvr.ini." };
	}

	std::string manifest_name(const std::string& json)
	{
		const auto runtime = json.find("\"runtime\"");
		const auto name = json.find("\"name\"", runtime == std::string::npos ? 0 : runtime);
		const auto colon = name == std::string::npos ? name : json.find(':', name);
		const auto open = colon == std::string::npos ? colon : json.find('"', colon);
		const auto close = open == std::string::npos ? open : json.find('"', open + 1);
		return close == std::string::npos ? std::string() : json.substr(open + 1, close - open - 1);
	}

	runtime_status read_runtime()
	{
		runtime_status status;
		wchar_t value[1024]{};
		DWORD size = sizeof(value);
		if (RegGetValueW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime", RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ,
			nullptr, value, &size) == ERROR_SUCCESS)
		{
			const std::filesystem::path manifest = value;
			std::error_code error;
			status.system_present = std::filesystem::exists(manifest, error);
			const std::string name = status.system_present ? manifest_name(read_text(manifest)) : std::string();
			status.system_name = name.empty() ? manifest.filename().wstring() : from_utf8(name);
		}
		const std::filesystem::path steamvr = steamvr_dir();
		std::error_code error;
		status.steamvr_installed = !steamvr.empty() && std::filesystem::exists(steamvr / L"steamxr_win64.json", error);
		status.steamvr_running = process_running(L"vrserver.exe");
		return status;
	}
}
