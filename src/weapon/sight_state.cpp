#include "weapon/sight_state.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>

namespace bfvr::weapon
{
#if defined(_M_X64)
	namespace
	{
		// The local soldier's current weapon (as weapon/physical_reload and stereo/gun_pass_fov read it).
		constexpr std::uintptr_t local_soldier_global = 0x14230f260ull;
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_flags = 0x28;
		constexpr std::uintptr_t weapons_index_a = 0x8d8;
		constexpr std::uintptr_t weapons_index_b = 0x8e0;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_unlock = 0x4850; // -> unlock asset, +0x20 name
		constexpr std::uintptr_t unlock_name = 0x20;
		// Runtime zoom levels: WeaponZoomLevelData* array; SightType at +0x2C (typeinfo).
		constexpr std::uintptr_t weapon_zoom_begin = 0x47d0;
		constexpr std::uintptr_t weapon_zoom_end = 0x47d8;
		constexpr std::uintptr_t zoom_sight_type = 0x2c;
		// Aiming simulation: +0x1E8 current zoom level, +0x1F8 the level the next aim uses (live diff
		// 2026-11-10: the magnifier toggle flips it 1 <-> 2 at the hip; re/notes/weapon.md).
		constexpr std::uintptr_t weapon_aiming = 0x4988;
		constexpr std::uintptr_t aiming_current_level = 0x1e8;
		constexpr std::uintptr_t aiming_selected_level = 0x1f8;
		constexpr std::uintptr_t max_slots = 16;
		constexpr int max_levels = 8;

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}
	}

	sight_state read_sight_state()
	{
		sight_state state;
		std::uintptr_t soldier{}, weapons{}, holder{}, begin{}, end{}, weapon{};
		std::uint32_t flags{}, index{};
		if (!read(local_soldier_global, soldier) || !soldier || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_flags, flags)
			|| !read(weapons + ((flags & 4) ? weapons_index_b : weapons_index_a), holder) || !read(holder + 0x8, index)
			|| !read(weapons + weapons_begin, begin) || !read(weapons + weapons_end, end) || !begin || end <= begin
			|| index >= (end - begin) / 8 || index >= max_slots || !read(begin + index * 8, weapon) || !weapon)
		{
			return state;
		}
		std::uintptr_t unlock{}, name{}, zoom_begin{}, zoom_end{}, aiming{};
		std::int32_t selected{}, current{};
		if (!read(weapon + weapon_unlock, unlock) || !read(unlock + unlock_name, name) || !read(name, state.weapon)
			|| !read(weapon + weapon_zoom_begin, zoom_begin) || !read(weapon + weapon_zoom_end, zoom_end) || !read(weapon + weapon_aiming, aiming)
			|| !read(aiming + aiming_selected_level, selected) || !read(aiming + aiming_current_level, current))
		{
			return state;
		}
		state.weapon[sizeof(state.weapon) - 1] = 0;
		const std::uintptr_t levels = zoom_begin && zoom_end > zoom_begin ? (zoom_end - zoom_begin) / 8 : 0;
		if (levels > static_cast<std::uintptr_t>(max_levels) || selected < 0 || current < 0)
		{
			return state;
		}
		state.levels = static_cast<int>(levels);
		state.selected = selected;
		state.current = current;
		std::uintptr_t level{};
		std::int32_t type{};
		if (selected < state.levels && read(zoom_begin + static_cast<std::uintptr_t>(selected) * 8, level) && level && read(level + zoom_sight_type, type)
			&& type >= 0 && type <= 2)
		{
			state.selected_type = static_cast<sight_type>(type);
		}
		state.valid = true;
		return state;
	}

	std::string sight_state_report()
	{
		const sight_state s = read_sight_state();
		char text[160];
		std::snprintf(text, sizeof(text), "sight_state valid=%d weapon=%s levels=%d selected=%d current=%d selected_type=%d", s.valid ? 1 : 0, s.weapon, s.levels,
			s.selected, s.current, static_cast<int>(s.selected_type));
		return text;
	}
#else
	sight_state read_sight_state()
	{
		return {};
	}

	std::string sight_state_report()
	{
		return "sight_state: BF4 (x64) only";
	}
#endif
}
