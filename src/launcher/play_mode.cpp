#include "launcher/play_mode.hpp"

#include <windows.h>

#include <fstream>
#include <string>

#include "launcher/vr_setup.hpp"

namespace bfvr::launcher
{
	namespace
	{
		const wchar_t* name_of(play_mode mode) { return mode == play_mode::vr ? L"VR" : L"normal"; }

		// Through a temporary file, so a failed copy never leaves half a profile behind.
		bool copy_over(const std::filesystem::path& from, const std::filesystem::path& to)
		{
			const std::filesystem::path temporary = std::filesystem::path(to) += L".tmp";
			if (!CopyFileW(from.c_str(), temporary.c_str(), FALSE))
			{
				return false;
			}
			if (!MoveFileExW(temporary.c_str(), to.c_str(), MOVEFILE_REPLACE_EXISTING))
			{
				std::error_code error;
				std::filesystem::remove(temporary, error);
				return false;
			}
			return true;
		}

		bool write_marker(const std::filesystem::path& profile, play_mode mode)
		{
			std::ofstream file(mode_marker(profile), std::ios::binary | std::ios::trunc);
			file << (mode == play_mode::vr ? "vr\n" : "normal\n");
			return static_cast<bool>(file.flush());
		}
	}

	std::filesystem::path mode_copy(const std::filesystem::path& profile, play_mode mode)
	{
		return std::filesystem::path(profile) += mode == play_mode::vr ? L".bfvr-vr" : L".bfvr-normal";
	}

	std::filesystem::path mode_marker(const std::filesystem::path& profile) { return std::filesystem::path(profile) += L".bfvr-mode"; }

	play_mode current_mode(const std::filesystem::path& profile, bool mod_installed)
	{
		std::string word;
		if (!profile.empty())
		{
			std::ifstream(mode_marker(profile)) >> word;
		}
		if (word == "vr")
		{
			return play_mode::vr;
		}
		if (word == "normal")
		{
			return play_mode::normal;
		}
		return mod_installed ? play_mode::vr : play_mode::normal;
	}

	result switch_profile(const std::filesystem::path& profile, play_mode current, play_mode target, bool game_running, bool set_vr_values)
	{
		std::error_code error;
		if (game_running)
		{
			return { false, L"BF4 settings not switched: the game is running and writes its settings back when it closes. Close it first." };
		}
		if (profile.empty() || !std::filesystem::exists(profile, error))
		{
			return { false, L"BF4 settings not found (" + profile.wstring() + L"). Start Battlefield 4 once so it creates them." };
		}

		std::wstring message = set_vr_values ? std::wstring() : std::wstring(L"BF4 settings: already the ") + name_of(target) + L" ones.";
		bool first_vr = false;
		if (current != target)
		{
			const std::filesystem::path kept = mode_copy(profile, current);
			if (!copy_over(profile, kept))
			{
				return { false, L"BF4 settings not switched: " + kept.wstring() + L" could not be written." };
			}
			message = std::wstring(L"BF4 settings: the ") + name_of(current) + L" ones kept in " + kept.filename().wstring();

			std::filesystem::path source = mode_copy(profile, target);
			bool from_backup = false;
			if (target == play_mode::normal && !std::filesystem::exists(source, error) && std::filesystem::exists(profile_backup(profile), error))
			{
				source = profile_backup(profile);
				from_backup = true;
			}
			if (std::filesystem::exists(source, error))
			{
				if (!copy_over(source, profile))
				{
					return { false, L"BF4 settings not switched: " + profile.wstring() + L" could not be replaced." };
				}
				message += std::wstring(L", your ") + name_of(target) + L" ones are back" + (from_backup ? L" (from the backup made before the first VR setup)." : L".");
			}
			else if (target == play_mode::vr)
			{
				first_vr = true;
				message += L".";
			}
			else
			{
				message += L". No normal ones were saved before, so BF4 keeps these: what you change in the game now stays your normal settings.";
			}
		}

		if (target == play_mode::vr && (first_vr || set_vr_values))
		{
			const result values = set_profile_for_vr(profile);
			if (!values.ok)
			{
				return values;
			}
			message += (message.empty() ? L"" : L" ") + values.message;
		}

		if (!write_marker(profile, target))
		{
			// The live profile must not be taken for the other mode's at the next switch.
			if (current != target)
			{
				copy_over(mode_copy(profile, current), profile);
			}
			return { false, L"BF4 settings not switched: " + mode_marker(profile).wstring() + L" could not be written." };
		}
		return { true, message };
	}
}
