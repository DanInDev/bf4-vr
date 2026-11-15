#include "body/pose_book.hpp"

#include <windows.h>

#include <filesystem>
#include <fstream>
#include <iterator>
#include <mutex>

#include "common/config.hpp"
#include "common/log.hpp"

namespace bfvr::body::pose_book
{
	namespace
	{
		std::mutex mutex;
		poses::table table;
		bool loaded{};

		std::filesystem::path file_path()
		{
			const std::wstring ini = config::ini_path();
			return ini.empty() ? std::filesystem::path() : std::filesystem::path(ini).parent_path() / L"bfvr_poses.ini";
		}

		// With mutex held.
		std::string load_locked()
		{
			table = poses::defaults();
			loaded = true;
			const std::filesystem::path path = file_path();
			if (path.empty())
			{
				return "error: no bfvr.ini path";
			}
			std::ifstream file(path, std::ios::binary);
			if (!file)
			{
				log::write("poses: no %ls, built-in tweaks", path.c_str());
				return {};
			}
			const std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
			const auto warnings = poses::parse(text, table);
			for (const std::string& warning : warnings)
			{
				log::write("poses: bfvr_poses.ini %s", warning.c_str());
			}
			log::write("poses: read %ls (%zu warnings)", path.c_str(), warnings.size());
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

	poses::table current()
	{
		std::lock_guard lock(mutex);
		ensure_loaded();
		return table;
	}

	poses::tweak get(poses::grip which)
	{
		const int index = static_cast<int>(which);
		if (index < 0 || index >= poses::grip_count)
		{
			return {};
		}
		std::lock_guard lock(mutex);
		ensure_loaded();
		return table[index];
	}

	void set(poses::grip which, const poses::tweak& value)
	{
		const int index = static_cast<int>(which);
		if (index < 0 || index >= poses::grip_count)
		{
			return;
		}
		std::lock_guard lock(mutex);
		ensure_loaded();
		table[index] = poses::clamped(value);
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
			text = poses::format(table);
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
				return "error: cannot write bfvr_poses.ini.tmp";
			}
		}
		if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
		{
			return "error: cannot replace bfvr_poses.ini (" + std::to_string(GetLastError()) + ")";
		}
		log::write("poses: saved %ls", path.c_str());
		return "saved " + path.string();
	}

	std::string report()
	{
		std::lock_guard lock(mutex);
		ensure_loaded();
		return "file " + file_path().string() + "\n" + poses::format(table);
	}
}
