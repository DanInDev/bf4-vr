#include "weapon/hip_accuracy.hpp"

#include "common/controlled.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#include "safety/mp_guard.hpp"

namespace bfvr::weapon
{
	namespace
	{
		std::atomic<float> strength{ 1.0f };

#if defined(_M_X64)
		// Chain as weapon/physical_reload (re/notes/weapon.md "Recoil data", typeinfo GunSwayData).
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_data = 0x30;  // SoldierWeaponData
		constexpr std::uintptr_t data_firing = 0x90;  // WeaponFiringData
		constexpr std::uintptr_t firing_sway = 0x30;  // GunSwayData
		constexpr int max_slots = 16;
		// The NoZoom BaseValue and Moving dispersion of each stance, and the Zoom block's offset from it:
		// Stand (GunSwayStandData, Zoom +0xBC), Crouch and Prone (GunSwayCrouchProneData, Zoom +0x5C).
		// Jumping, sprinting and vaulting keep the game's values (their zoom values are not tighter).
		struct block
		{
			std::uintptr_t hip;
			std::uintptr_t zoom;
		};
		constexpr block blocks[] = {
			{ 0x10, 0x10 + 0xBC }, { 0x10 + 0x18, 0x10 + 0xBC + 0x18 },
			{ 0x188, 0x188 + 0x5C }, { 0x188 + 0x18, 0x188 + 0x5C + 0x18 },
			{ 0x240, 0x240 + 0x5C }, { 0x240 + 0x18, 0x240 + 0x5C + 0x18 },
		};
		constexpr std::size_t block_count = sizeof(blocks) / sizeof(blocks[0]);
		using values = float[block_count][dispersion_floats];

		struct patched_sway
		{
			std::uintptr_t sway{};
			values original{};
			values written{};
		};
		std::mutex lock_;
		std::vector<patched_sway> patched;
		ULONGLONG last_scan{};
		float applied{ -1.0f };
		unsigned long long writes{};

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		template <typename T>
		bool write(std::uintptr_t address, const T& value)
		{
			SIZE_T done = 0;
			return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		bool read_blocks(std::uintptr_t sway, bool zoom, values& out)
		{
			for (std::size_t b = 0; b < block_count; ++b)
			{
				if (!read(sway + (zoom ? blocks[b].zoom : blocks[b].hip), out[b]))
				{
					return false;
				}
			}
			return true;
		}

		bool write_blocks(std::uintptr_t sway, const values& in)
		{
			bool ok = true;
			for (std::size_t b = 0; b < block_count; ++b)
			{
				ok = write(sway + blocks[b].hip, in[b]) && ok;
			}
			return ok;
		}

		void restore_all()
		{
			for (const patched_sway& p : patched)
			{
				values current{};
				// Only where our values still stand: a freed asset's memory may hold something else now.
				if (read_blocks(p.sway, false, current) && std::memcmp(current, p.written, sizeof(current)) == 0)
				{
					write_blocks(p.sway, p.original);
				}
			}
			patched.clear();
		}

		// One weapon's GunSwayData: newly seen (or no longer holding what we wrote) keeps its hip values
		// as the originals; then the blend for the strength is written when it differs.
		void patch(std::uintptr_t sway, float t)
		{
			values hip{}, zoom{};
			if (!read_blocks(sway, false, hip) || !read_blocks(sway, true, zoom))
			{
				return;
			}
			auto found = std::find_if(patched.begin(), patched.end(), [&](const patched_sway& p) { return p.sway == sway; });
			if (found == patched.end() || std::memcmp(hip, found->written, sizeof(hip)) != 0)
			{
				if (found == patched.end())
				{
					if (patched.size() >= 64)
					{
						return;
					}
					patched.push_back({ sway });
					found = patched.end() - 1;
				}
				std::memcpy(found->original, hip, sizeof(hip));
			}
			values blended{};
			for (std::size_t b = 0; b < block_count; ++b)
			{
				blend_dispersion(found->original[b], zoom[b], t, blended[b]);
			}
			if (std::memcmp(blended, hip, sizeof(hip)) != 0)
			{
				write_blocks(sway, blended);
				++writes;
			}
			std::memcpy(found->written, blended, sizeof(blended));
		}
#endif
	}

	void set_hip_accuracy(float value)
	{
		strength = value;
	}

	float hip_accuracy_strength()
	{
		return strength.load(std::memory_order_relaxed);
	}

	void update_hip_accuracy()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock_);
		const float t = std::clamp(strength.load(std::memory_order_relaxed), 0.0f, 1.0f);
		if (safety::mod_disabled() || t <= 0.0f)
		{
			if (!patched.empty())
			{
				restore_all();
			}
			applied = 0.0f;
			return;
		}
		const ULONGLONG now = GetTickCount64();
		if (t == applied && now - last_scan < 250)
		{
			return;
		}
		last_scan = now;
		applied = t;
		std::uintptr_t soldier{}, weapons{}, begin{}, end{};
		if (!(soldier = game::local_soldier()) != 0 || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_begin, begin)
			|| !read(weapons + weapons_end, end) || !begin || end <= begin)
		{
			return;
		}
		const int slots = static_cast<int>(std::min<std::uintptr_t>((end - begin) / 8, max_slots));
		for (int slot = 0; slot < slots; ++slot)
		{
			std::uintptr_t client{}, data{}, firing{}, sway{};
			if (read(begin + slot * 8, client) && read(client + weapon_data, data) && read(data + data_firing, firing)
				&& read(firing + firing_sway, sway) && sway)
			{
				patch(sway, t);
			}
		}
#endif
	}

	std::string hip_accuracy_report()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock_);
		char text[256];
		std::snprintf(text, sizeof(text), "hip_accuracy=%.2f patched=%zu writes=%llu", strength.load(), patched.size(), writes);
		std::string out = text;
		for (const patched_sway& p : patched)
		{
			values zoom{};
			read_blocks(p.sway, true, zoom);
			std::snprintf(text, sizeof(text), "\n%016llX stand hip min/max %.2f/%.2f (game %.2f/%.2f, zoom %.2f/%.2f)", static_cast<unsigned long long>(p.sway),
				p.written[0][0], p.written[0][1], p.original[0][0], p.original[0][1], zoom[0][0], zoom[0][1]);
			out += text;
		}
		return out;
#else
		return "hip_accuracy: BF4 (x64) only";
#endif
	}
}
