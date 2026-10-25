#include "safety/mp_guard.hpp"

#include <windows.h>

#include <atomic>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <string>

#include <MinHook.h>

#include "common/log.hpp"
#include "input/gamepad.hpp"

namespace bfvr::safety
{
	namespace
	{
		// BF4 179547: ClientGameContext* global; +0x28 current Level*, Level+0x38 LevelData*,
		// LevelData+0x10 level path, +0xA0 IsMultiplayer (1 in the front end, 0 in the campaign). Both
		// come from the same LevelData, so they always describe the same level (the global "not
		// multiplayer" byte at 0x142737840 is 0 in the front end and lags behind level loads).
		constexpr std::uint64_t game_context_global = 0x142670d80;
		constexpr DWORD poll_ms = 250;

		struct level_info
		{
			std::string path; // empty while no level is loaded
			std::uint8_t is_multiplayer{};
		};

		std::atomic<bool> disabled{};
		std::atomic<bool> front_end_active{};

		template <typename T>
		bool read(std::uint64_t address, T& value)
		{
			SIZE_T done = 0;
			return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		std::string lower(std::string text)
		{
			for (char& c : text)
			{
				c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
			}
			return text;
		}

		level_info loaded_level()
		{
			level_info info;
			std::uint64_t context = 0, level = 0, data = 0, path = 0;
			if (!read(game_context_global, context) || !read(context + 0x28, level) || !read(level + 0x38, data) || !read(data + 0x10, path)
				|| !read(data + 0xa0, info.is_multiplayer))
			{
				return {};
			}
			// Read only up to the end of the string's page: a full-size read would fail on a string
			// near an uncommitted page and the level would go unchecked.
			char text[128]{};
			SIZE_T done = 0;
			const std::uint64_t to_page_end = 0x1000 - (path & 0xfff);
			const SIZE_T length = static_cast<SIZE_T>(to_page_end < sizeof(text) - 1 ? to_page_end : sizeof(text) - 1);
			if (path == 0 || !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(path)), text, length, &done))
			{
				return {};
			}
			text[sizeof(text) - 1] = '\0';
			info.path = text;
			return info;
		}

		bool campaign_level(const std::string& path)
		{
			return lower(path).rfind("levels/sp/", 0) == 0;
		}

		// The front end is the menu level shared with multiplayer; its IsMultiplayer flag is set.
		bool front_end(const std::string& path)
		{
			return lower(path) == "levels/frontend/frontend";
		}

		void disable(const char* reason, const std::string& path)
		{
			disabled = true;
			const MH_STATUS status = MH_DisableHook(MH_ALL_HOOKS);
			input::publish({});
			log::write("safety: %s (level '%s'); all hooks disabled (%s). Remove dinput8.dll before multiplayer.", reason, path.c_str(),
				MH_StatusToString(status));
		}

		DWORD WINAPI guard_thread(LPVOID)
		{
			std::string last_path;
			for (;;)
			{
				if (disabled)
				{
					// A hook installed late (e.g. a wait-for-decryption thread) must not stay enabled.
					MH_DisableHook(MH_ALL_HOOKS);
					Sleep(poll_ms);
					continue;
				}
				const level_info level = loaded_level();
				front_end_active = !level.path.empty() && front_end(level.path);
				if (!level.path.empty())
				{
					if (level.path != last_path)
					{
						log::write("safety: level %s (multiplayer flag %u)", level.path.c_str(), level.is_multiplayer);
						last_path = level.path;
					}
					if (front_end(level.path))
					{
						// Menus are allowed; joining a server loads a non-campaign level next.
					}
					else if (!campaign_level(level.path))
					{
						disable("level is not a campaign level", level.path);
					}
					else if (level.is_multiplayer != 0)
					{
						disable("campaign level data is marked multiplayer", level.path);
					}
				}
				Sleep(poll_ms);
			}
		}
	}

	bool command_line_allows_mod()
	{
		const std::string line = lower(GetCommandLineA());
		for (const char* marker : { "state_connecttogameid", "state_claimreservation", "-webmode mp" })
		{
			if (line.find(marker) != std::string::npos)
			{
				log::write("safety: multiplayer command line (%s); the mod stays off", marker);
				return false;
			}
		}
		return true;
	}

	void start_multiplayer_guard()
	{
#if !defined(_M_X64)
		log::write("safety: level guard targets BF4 x64 only; not running");
#else
		const HANDLE thread = CreateThread(nullptr, 0, &guard_thread, nullptr, 0, nullptr);
		if (thread)
		{
			CloseHandle(thread);
		}
		else
		{
			// Without the guard the mod must not run at all.
			disable("guard thread could not start", {});
		}
#endif
	}

	bool mod_disabled()
	{
		return disabled;
	}

	bool front_end_loaded()
	{
		return front_end_active;
	}
}
