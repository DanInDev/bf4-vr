#include "weapon/unlock_all.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "common/log.hpp"
#include "safety/mp_guard.hpp"

namespace bfvr::weapon
{
	namespace
	{
		std::atomic<bool> wanted{ true };

#if defined(_M_X64)
		// BF4 179547 (re/symbols/bf4-179547.md, re/notes/weapon.md "The weapon crate and unlocks"): every
		// reader of the settings goes through one cache filled by the settings getter (0x140688C50).
		constexpr std::uintptr_t settings_cache = 0x1423717C0;
		constexpr std::uintptr_t settings_vtable = 0x141C58A20; // SyncedBFSettings
		constexpr std::uintptr_t all_unlocks_unlocked = 0x54;    // Boolean
		// The unlock check: mov rax, [rip+0x2219C42] (the cache) ... cmp byte ptr [rax+0x54], 0
		struct code_check
		{
			std::uintptr_t address;
			std::uint8_t bytes[7];
			std::size_t length;
		};
		constexpr code_check checks[] = {
			{ 0x140157B77, { 0x48, 0x8B, 0x05, 0x42, 0x9C, 0x21, 0x02 }, 7 },
			{ 0x140157BA3, { 0x80, 0x78, 0x54, 0x00 }, 4 },
		};

		std::mutex lock_;
		int verified{ -1 }; // -1 not checked, 0 wrong build, 1 ok
		ULONGLONG last_check{};
		std::uintptr_t object{};   // the settings object the mod wrote to
		std::uint8_t original{};   // the game's value before the mod's first write there
		bool ours{};
		unsigned long long sets{}, restores{};

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		bool write(std::uintptr_t address, std::uint8_t value)
		{
			SIZE_T done = 0;
			return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		bool verify_code()
		{
			for (const code_check& check : checks)
			{
				std::uint8_t bytes[7]{};
				SIZE_T done = 0;
				if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(check.address), bytes, check.length, &done)
					|| done != check.length || std::memcmp(bytes, check.bytes, check.length) != 0)
				{
					return false;
				}
			}
			return true;
		}

		// The active SyncedBFSettings, or 0 while the cache is empty or holds something else.
		std::uintptr_t settings()
		{
			std::uintptr_t p{}, vtable{};
			if (!read(settings_cache, p) || !read(p, vtable) || vtable != settings_vtable)
			{
				return 0;
			}
			return p;
		}
#endif
	}

	void set_unlock_all_weapons(bool on)
	{
		wanted = on;
	}

	void update_unlock_all_weapons()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock_);
		const ULONGLONG now = GetTickCount64();
		if (now - last_check < 250)
		{
			return;
		}
		last_check = now;
		if (verified < 0)
		{
			verified = verify_code() ? 1 : 0;
			if (!verified)
			{
				log::write("unlock_all_weapons: the unlock check's code differs from build 179547; not touching the settings");
			}
		}
		if (!verified)
		{
			return;
		}
		const std::uintptr_t p = settings();
		if (!p)
		{
			return;
		}
		if (p != object)
		{
			// A new settings object (or the first): nothing of ours stands there.
			object = p;
			ours = false;
		}
		std::uint8_t flag{};
		if (!read(p + all_unlocks_unlocked, flag))
		{
			return;
		}
		const bool level = safety::campaign_level_loaded() && !safety::mod_disabled();
		switch (unlock_all_step(wanted.load(std::memory_order_relaxed), level, flag != 0, ours))
		{
		case unlock_step::set:
			if (!ours)
			{
				original = flag;
			}
			if (write(p + all_unlocks_unlocked, 1))
			{
				ours = true;
				++sets;
				if (sets == 1)
				{
					log::write("unlock_all_weapons: every crate weapon unlocked (memory only)");
				}
			}
			break;
		case unlock_step::restore:
			if (write(p + all_unlocks_unlocked, original))
			{
				ours = false;
				++restores;
			}
			break;
		case unlock_step::none:
			break;
		}
#endif
	}

	std::string unlock_all_report()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock_);
		const std::uintptr_t p = settings();
		std::uint8_t flag{};
		const bool have = p && read(p + all_unlocks_unlocked, flag);
		char text[320];
		std::snprintf(text, sizeof(text),
			"unlock_all_weapons=%d code=%s settings=%016llX AllUnlocksUnlocked=%s ours=%d original=%d sets=%llu restores=%llu level=%d",
			wanted.load() ? 1 : 0, verified < 0 ? "unchecked" : verified ? "ok" : "MISMATCH", static_cast<unsigned long long>(p),
			have ? (flag ? "1" : "0") : "-", ours ? 1 : 0, original, sets, restores,
			safety::campaign_level_loaded() && !safety::mod_disabled() ? 1 : 0);
		return text;
#else
		return "unlock_all_weapons: BF4 (x64) only";
#endif
	}
}
