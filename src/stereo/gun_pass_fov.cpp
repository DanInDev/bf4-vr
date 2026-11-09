#include "stereo/gun_pass_fov.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <mutex>

#include "common/log.hpp"
#include "stereo/gun_pass_fov_math.hpp"

namespace bfvr::stereo::gun_pass_fov
{
#if defined(_M_X64)
	namespace
	{
		// Per-player view manager; view 0 = [[mgr + 8]]; +0x13D0 the gun pass FOV at the hip, degrees
		// (re/notes/first_person.md, candidate 1, confirmed live 2026-11-09).
		constexpr std::uintptr_t view_manager_global = 0x142673D80ull;
		constexpr std::uintptr_t view_hip_fov = 0x13D0;
		// The local soldier's current weapon (as weapon/physical_reload reads it) and its runtime zoom
		// levels: begin/end of an array of WeaponZoomLevelData*, RenderFov at +0x10 (0x140FB8290).
		constexpr std::uintptr_t local_soldier_global = 0x14230f260ull;
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_flags = 0x28;
		constexpr std::uintptr_t weapons_index_a = 0x8d8;
		constexpr std::uintptr_t weapons_index_b = 0x8e0;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_zoom_begin = 0x47d0;
		constexpr std::uintptr_t weapon_zoom_end = 0x47d8;
		constexpr std::uintptr_t zoom_render_fov = 0x10;
		constexpr std::size_t max_slots = 16;
		constexpr std::size_t max_levels = 8;

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		bool write(std::uintptr_t address, float value)
		{
			SIZE_T done = 0;
			return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		// What we changed, with the game's own value, to put back.
		struct changed
		{
			std::uintptr_t address{};
			float original{};
			float written{};
		};
		std::mutex lock;
		changed changes[64];
		int change_count{};
		float last_written{};
		unsigned writes{};

		// Records address's current value as the game's (unless it is already ours), then writes value.
		void set(std::uintptr_t address, float value)
		{
			float current{};
			if (!read(address, current))
			{
				return;
			}
			changed* entry = nullptr;
			for (int i = 0; i < change_count; ++i)
			{
				entry = changes[i].address == address ? &changes[i] : entry;
			}
			if (!entry)
			{
				if (!gun_pass_fov_math::plausible_degrees(current) || change_count >= 64)
				{
					return;
				}
				entry = &changes[change_count++];
				entry->address = address;
				entry->original = current;
			}
			if (current != value && write(address, value))
			{
				++writes;
			}
			entry->written = value;
		}

		std::uintptr_t current_weapon()
		{
			std::uintptr_t soldier{}, weapons{}, holder{}, begin{}, end{}, weapon{};
			std::uint32_t flags{}, index{};
			if (!read(local_soldier_global, soldier) || !soldier || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_flags, flags)
				|| !read(weapons + ((flags & 4) ? weapons_index_b : weapons_index_a), holder) || !read(holder + 0x8, index)
				|| !read(weapons + weapons_begin, begin) || !read(weapons + weapons_end, end) || !begin || end <= begin
				|| index >= (end - begin) / 8 || index >= max_slots || !read(begin + index * 8, weapon))
			{
				return 0;
			}
			return weapon;
		}
	}

	bool apply(float half_fov)
	{
		const float degrees = gun_pass_fov_math::degrees_of(half_fov);
		std::uintptr_t manager{}, views{}, view{};
		if (!read(view_manager_global, manager) || !read(manager + 8, views) || !read(views, view) || !view)
		{
			return false;
		}
		const std::lock_guard guard(lock);
		set(view + view_hip_fov, degrees);
		// Level 0 is the hip (the game uses +0x13D0 for it); the others are the sights' zoom.
		if (const std::uintptr_t weapon = current_weapon())
		{
			std::uintptr_t begin{}, end{};
			if (read(weapon + weapon_zoom_begin, begin) && read(weapon + weapon_zoom_end, end) && begin && end > begin && (end - begin) / 8 <= max_levels)
			{
				for (std::uintptr_t at = begin + 8; at < end; at += 8)
				{
					std::uintptr_t level{};
					if (read(at, level) && level)
					{
						set(level + zoom_render_fov, degrees);
					}
				}
			}
		}
		last_written = degrees;
		return true;
	}

	void restore()
	{
		const std::lock_guard guard(lock);
		for (int i = 0; i < change_count; ++i)
		{
			float current{};
			// Only where our value still stands (a level reload may have rebuilt the object).
			if (read(changes[i].address, current) && current == changes[i].written)
			{
				write(changes[i].address, changes[i].original);
			}
		}
		change_count = 0;
	}

	std::string report()
	{
		const std::lock_guard guard(lock);
		char text[160];
		std::snprintf(text, sizeof(text), "gun_pass_fov written=%.2f deg values_changed=%d writes=%u", last_written, change_count, writes);
		return text;
	}
#else
	bool apply(float)
	{
		return false;
	}

	void restore()
	{
	}

	std::string report()
	{
		return "gun_pass_fov: x64 only";
	}
#endif
}
