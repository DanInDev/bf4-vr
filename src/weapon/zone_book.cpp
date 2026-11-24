#include "weapon/zone_book.hpp"

#include <windows.h>

#include <atomic>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

#include "common/config.hpp"
#include "common/log.hpp"
#include "weapon/zone_defaults.hpp"

namespace bfvr::weapon::zone_book
{
	namespace
	{
		std::mutex mutex;
		zone_tuning::book tunes;
		bool loaded{};
		std::string gun_in_hand;
		std::atomic<bool> on{ true };

		std::filesystem::path file_path()
		{
			const std::wstring ini = config::ini_path();
			return ini.empty() ? std::filesystem::path() : std::filesystem::path(ini).parent_path() / L"bfvr_zones.ini";
		}

		// With mutex held.
		std::string load_locked()
		{
			tunes.clear();
			loaded = true;
			// #152: the shipped tunes first; the player's own file overrides them zone by zone.
			zone_tuning::book shipped;
			for (const std::string& warning : zone_tuning::parse(zone_tuning::built_in_defaults(), shipped))
			{
				log::write("zones: built-in tunes %s", warning.c_str());
			}
			tunes = shipped;
			const std::filesystem::path path = file_path();
			if (path.empty())
			{
				return "error: no bfvr.ini path";
			}
			std::ifstream file(path, std::ios::binary);
			if (!file)
			{
				return {}; // nothing tuned by the player
			}
			const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			zone_tuning::book own;
			const auto warnings = zone_tuning::parse(text, own);
			tunes = zone_tuning::with_overrides(shipped, own);
			for (const std::string& warning : warnings)
			{
				log::write("zones: bfvr_zones.ini %s", warning.c_str());
			}
			log::write("zones: read %ls, %zu guns (%zu warnings)", path.c_str(), tunes.size(), warnings.size());
			return warnings.empty() ? std::string() : "error: " + std::to_string(warnings.size()) + " unusable lines (bfvr.log)";
		}

		void ensure_loaded()
		{
			if (!loaded)
			{
				load_locked();
			}
		}
	}

	zone_tuning::gun_tunes get(const std::string& gun)
	{
		std::lock_guard lock(mutex);
		ensure_loaded();
		const auto found = tunes.find(gun);
		return found == tunes.end() ? zone_tuning::gun_tunes{} : found->second;
	}

	zone_tuning::tune get(const std::string& gun, zone_tuning::zone which)
	{
		const int index = static_cast<int>(which);
		return index >= 0 && index < zone_tuning::zone_count ? get(gun).zones[index] : zone_tuning::tune{};
	}

	void set(const std::string& gun, zone_tuning::zone which, const zone_tuning::tune& value)
	{
		const int index = static_cast<int>(which);
		if (gun.empty() || index < 0 || index >= zone_tuning::zone_count)
		{
			return;
		}
		std::lock_guard lock(mutex);
		ensure_loaded();
		tunes[gun].zones[index] = zone_tuning::clamped(value);
	}

	void set_enabled(bool enabled)
	{
		on = enabled;
	}

	bool enabled()
	{
		return on.load(std::memory_order_relaxed);
	}

	void set_current_gun(const std::string& gun)
	{
		std::lock_guard lock(mutex);
		gun_in_hand = gun;
	}

	std::string current_gun()
	{
		std::lock_guard lock(mutex);
		return gun_in_hand;
	}

	std::string load()
	{
		std::lock_guard lock(mutex);
		return load_locked();
	}

	std::string save()
	{
		std::string text;
		{
			std::lock_guard lock(mutex);
			ensure_loaded();
			text = zone_tuning::format(tunes);
		}
		const std::filesystem::path path = file_path();
		if (path.empty())
		{
			return "error: no bfvr.ini path";
		}
		const std::wstring temporary = path.wstring() + L".tmp";
		{
			std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
			file.write(text.data(), static_cast<std::streamsize>(text.size()));
			if (!file)
			{
				return "error: cannot write bfvr_zones.ini.tmp";
			}
		}
		if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			return "error: cannot replace bfvr_zones.ini (" + std::to_string(GetLastError()) + ")";
		}
		log::write("zones: saved %ls", path.c_str());
		return "saved " + path.string();
	}

	std::string report()
	{
		std::lock_guard lock(mutex);
		ensure_loaded();
		return "file " + file_path().string() + "\ngun in hand " + (gun_in_hand.empty() ? "-" : gun_in_hand) + ", tunes " + (on ? "on" : "off") + "\n" + zone_tuning::format(tunes);
	}
}
