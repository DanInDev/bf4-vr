#pragma once

#include <filesystem>
#include <span>
#include <string>
#include <vector>

#include "launcher/install.hpp"

// Setting up a user's BF4 for VR (#16): the game's own profile (DirectX 11, no weapon depth of field or
// motion blur, pad vibration on) and the mod's bfvr.ini (head-tracked stereo, pair rendering, the
// 2160x2160 render size and view crop tuned in the headset tests), plus where the VR runtime stands.
namespace bfvr::launcher
{
	struct wanted_setting
	{
		const char* key;
		const char* value;
		const wchar_t* label; // what the user sees, e.g. "motion blur off"
		const char* fallback{}; // the value used when the key is missing (the mod's default), or none
	};

	// BF4's profile, Documents\Battlefield 4\settings\PROFSAVE_profile ("Key value" lines).
	std::span<const wanted_setting> vr_profile_settings();
	// The game folder's bfvr.ini.
	std::span<const wanted_setting> vr_ini_settings();

	// Empty when Documents cannot be resolved; the file itself appears after BF4's first start.
	std::filesystem::path find_profile();
	std::filesystem::path profile_backup(const std::filesystem::path& profile);

	// Settings whose current value differs from the wanted one (numbers compared as numbers, so
	// "0.000000" equals "0"). A missing file counts every setting as pending.
	std::vector<const wanted_setting*> profile_pending(const std::filesystem::path& profile);
	std::vector<const wanted_setting*> ini_pending(const std::filesystem::path& ini);

	// Backs the profile up once (the first, untouched copy is kept), then sets the wanted values,
	// keeping every other line. Refused while the game runs: BF4 writes the profile back on exit.
	result apply_profile(const std::filesystem::path& profile, bool game_running);
	result restore_profile(const std::filesystem::path& profile, bool game_running);
	// The wanted mod settings into bfvr.ini (seeded from the template when it does not exist yet).
	result apply_ini(const std::filesystem::path& ini, const std::filesystem::path& seed);

	// The same choice the mod makes at start-up (xr/runtime_selection.cpp): SteamVR when it is running,
	// else the system's active OpenXR runtime.
	struct runtime_status
	{
		std::wstring system_name; // the active runtime manifest's "name", or its file name
		bool system_present{};
		bool steamvr_installed{};
		bool steamvr_running{};
	};
	runtime_status read_runtime();

	// For tests and the status line: the "name" in an OpenXR runtime manifest.
	std::string manifest_name(const std::string& json);
}
