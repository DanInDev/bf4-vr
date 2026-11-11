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
#include "safety/mp_rules.hpp"

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
		std::atomic<bool> campaign_active{};

		template <typename T>
		bool read(std::uint64_t address, T& value)
		{
			SIZE_T done = 0;
			return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		// BF3 1147186 (x86): ClientGameContext* global; +0x10 the current level, level+0x68 its path
		// ("Levels/SP_Earthquake/SP_Earthquake"). Found and read live on 2026-11-11
		// (bf3-vr re/notes/guard.md). No multiplayer flag is known there: the path and the command
		// line decide.
		constexpr std::uint32_t bf3_game_context_global = 0x02380B58;

		level_info loaded_level()
		{
			level_info info;
			std::uint64_t path = 0;
#if defined(_M_X64)
			std::uint64_t context = 0, level = 0, data = 0;
			if (!read(game_context_global, context) || !read(context + 0x28, level) || !read(level + 0x38, data) || !read(data + 0x10, path)
				|| !read(data + 0xa0, info.is_multiplayer))
			{
				return {};
			}
#else
			std::uint32_t context = 0, level = 0, text_pointer = 0;
			if (!read(bf3_game_context_global, context) || !read(context + 0x10, level) || !read(level + 0x68, text_pointer))
			{
				return {};
			}
			path = text_pointer;
#endif
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
				const level_kind kind = judge_level(built_game(), level.path);
				campaign_active = kind == level_kind::campaign && level.is_multiplayer == 0;
				if (kind != level_kind::none)
				{
					if (level.path != last_path)
					{
						log::write("safety: level %s (multiplayer flag %u)", level.path.c_str(), level.is_multiplayer);
						last_path = level.path;
					}
					// Menus are allowed; joining a server loads a non-campaign level next.
					if (kind == level_kind::forbidden)
					{
						disable("level is not a campaign level", level.path);
					}
					else if (kind == level_kind::campaign && level.is_multiplayer != 0)
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
		if (const char* reason = command_line_refusal(built_game(), GetCommandLineA()))
		{
			log::write("safety: %s; the mod stays off", reason);
			return false;
		}
		return true;
	}

	void start_multiplayer_guard()
	{
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
	}

	bool mod_disabled()
	{
		return disabled;
	}

	bool campaign_level_loaded()
	{
		return campaign_active;
	}
}
