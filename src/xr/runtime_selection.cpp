#include "xr/runtime_selection.hpp"

#include <windows.h>
#include <tlhelp32.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>

#include "common/config.hpp"

namespace bfvr::xr
{
	namespace
	{
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

		// openvrpaths.vrpath lists the SteamVR install as the first "runtime" entry.
		std::filesystem::path steamvr_manifest()
		{
			wchar_t local_app_data[MAX_PATH]{};
			if (!GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, MAX_PATH))
			{
				return {};
			}

			std::ifstream file(std::filesystem::path(local_app_data) / "openvr" / "openvrpaths.vrpath");
			std::stringstream contents;
			contents << file.rdbuf();
			const std::string text = contents.str();

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

			const char* manifest = sizeof(void*) == 8 ? "steamxr_win64.json" : "steamxr_win32.json";
			return std::filesystem::path(directory) / manifest;
		}
	}

	bool steamvr_running()
	{
		return process_running(L"vrserver.exe");
	}

	std::string select_runtime()
	{
		// Re-evaluated on every instance creation; only a value we did not set ourselves is the user's.
		static bool set_by_us = false;
		char existing[MAX_PATH]{};
		std::size_t length{};
		if (!set_by_us && getenv_s(&length, existing, "XR_RUNTIME_JSON") == 0 && length > 1)
		{
			return std::string("XR_RUNTIME_JSON already set: ") + existing;
		}
		_putenv_s("XR_RUNTIME_JSON", "");
		set_by_us = false;

		std::string choice = config::get("xr_runtime", "auto");
		if (choice == "auto")
		{
			choice = process_running(L"vrserver.exe") ? "steamvr" : "system";
		}
		if (choice == "system")
		{
			return "system active runtime";
		}

		std::filesystem::path manifest = choice == "steamvr" ? steamvr_manifest() : std::filesystem::path(choice);
		if (manifest.empty() || !std::filesystem::exists(manifest))
		{
			return "requested runtime '" + choice + "' not found; using the system active runtime";
		}

		// _putenv_s updates both the CRT copy and the process environment the loader reads.
		_putenv_s("XR_RUNTIME_JSON", manifest.string().c_str());
		set_by_us = true;
		return choice + " via " + manifest.string();
	}
}
