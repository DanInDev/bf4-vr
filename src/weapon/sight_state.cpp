#include "weapon/sight_state.hpp"

#include "common/controlled.hpp"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <iterator>

namespace bfvr::weapon
{
#if defined(_M_X64)
	namespace
	{
		// The local soldier's current weapon (as weapon/physical_reload and stereo/gun_pass_fov read it).
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
		// The aiming simulation's resolved zoom levels, the optic's modifiers applied: ZoomLevelData* per level from
		// +0x238 (level 0 = the hip); FieldOfView +0x10, FieldOfViewSP +0x14 (the campaign's when > 0). Live
		// 2026-11-19 (#138): hip 55/SP 45, iron 42, red dot / holo 50/SP 40, magnifier level 2 27, ACOG / PSO-1
		// 17.2, SRS rifle scope 8.7, CL6X 11.6; the game's own aim used exactly these (scope game_fov 40, 17.2,
		// 8.7, 42 against hip 45). WeaponZoomLevelData.RenderFov is 55/83.3 for every optic: not the zoom.
		constexpr std::uintptr_t aiming_zoom_levels = 0x238;
		constexpr std::uintptr_t zoom_fov = 0x10;
		constexpr std::uintptr_t zoom_fov_sp = 0x14;
		// Aiming simulation: +0x1E8 current zoom level, +0x1F8 the level the next aim uses (live diff
		// 2026-11-10: the magnifier toggle flips it 1 <-> 2 at the hip; re/notes/weapon.md).
		constexpr std::uintptr_t weapon_aiming = 0x4988;
		constexpr std::uintptr_t aiming_current_level = 0x1e8;
		constexpr std::uintptr_t aiming_selected_level = 0x1f8;
		constexpr std::uintptr_t max_slots = 16;
		constexpr int max_levels = 8;
		// Per slot in the weapons component: the weapon's unlock, then a tree of every unlock on it (the
		// weapon, its optic, grip, barrel, camo): +0x20 root node, +0x28 size (u32). Node: +0x00 / +0x08
		// children, +0x18 the unlock asset (live walk 2026-11-10: AK-12 6 unlocks, SCAR-H 5, M67 and knife 1).
		constexpr std::uintptr_t slot_records = 0x120;
		constexpr std::uintptr_t slot_record_size = 0xc0;
		constexpr std::uintptr_t record_root = 0x20;
		constexpr std::uintptr_t record_size = 0x28;
		constexpr std::uintptr_t node_unlock = 0x18;
		constexpr std::uint32_t max_unlocks = 32;

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		bool read_name(std::uintptr_t unlock, char (&out)[48])
		{
			std::uintptr_t name{};
			if (!read(unlock + unlock_name, name) || !read(name, out))
			{
				return false;
			}
			out[sizeof(out) - 1] = 0;
			return true;
		}

		// The slot's unlock tree, when its record belongs to this weapon's unlock.
		void read_attachments(std::uintptr_t weapons, std::uint32_t slot, std::uintptr_t weapon_unlock_asset, sight_state& state)
		{
			const std::uintptr_t record = weapons + slot_records + slot * slot_record_size;
			std::uintptr_t unlock{}, root{};
			std::uint32_t size{};
			if (!read(record, unlock) || unlock != weapon_unlock_asset || !read(record + record_root, root) || !read(record + record_size, size)
				|| !root || size == 0 || size > max_unlocks)
			{
				return;
			}
			std::uintptr_t stack[max_unlocks]{};
			int depth = 0;
			std::uint32_t visited = 0;
			stack[depth++] = root;
			while (depth > 0 && visited < size)
			{
				const std::uintptr_t node = stack[--depth];
				std::uintptr_t left{}, right{}, asset{};
				char name[48]{};
				if (!read(node, left) || !read(node + 8, right) || !read(node + node_unlock, asset) || !asset || !read_name(asset, name))
				{
					return;
				}
				++visited;
				if (state.unlock_count < static_cast<int>(std::size(state.unlocks)))
				{
					std::memcpy(state.unlocks[state.unlock_count++], name, sizeof(name));
				}
				const gun_optics before = state.optics;
				add_optic(state.optics, name);
				if ((state.optics.reflex && !before.reflex) || (state.optics.magnified && !before.magnified))
				{
					std::memcpy(state.optic, name, sizeof(state.optic));
				}
				if (const foregrip_kind grip = classify_foregrip(name); grip != foregrip_kind::none && state.foregrip == foregrip_kind::none)
				{
					state.foregrip = grip;
					std::memcpy(state.grip, name, sizeof(state.grip));
				}
				for (const std::uintptr_t child : { left, right })
				{
					if (child && depth < static_cast<int>(max_unlocks))
					{
						stack[depth++] = child;
					}
				}
			}
			state.attachments_known = visited == size;
		}
	}

	sight_state read_sight_state()
	{
		sight_state state;
		std::uintptr_t soldier{}, weapons{}, holder{}, begin{}, end{}, weapon{};
		std::uint32_t flags{}, index{};
		if (!(soldier = game::local_soldier()) != 0 || !soldier || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_flags, flags)
			|| !read(weapons + ((flags & 4) ? weapons_index_b : weapons_index_a), holder) || !read(holder + 0x8, index)
			|| !read(weapons + weapons_begin, begin) || !read(weapons + weapons_end, end) || !begin || end <= begin
			|| index >= (end - begin) / 8 || index >= max_slots || !read(begin + index * 8, weapon) || !weapon)
		{
			return state;
		}
		std::uintptr_t unlock{}, zoom_begin{}, zoom_end{}, aiming{};
		std::int32_t selected{}, current{};
		if (!read(weapon + weapon_unlock, unlock) || !read_name(unlock, state.weapon)
			|| !read(weapon + weapon_zoom_begin, zoom_begin) || !read(weapon + weapon_zoom_end, zoom_end) || !read(weapon + weapon_aiming, aiming)
			|| !read(aiming + aiming_selected_level, selected) || !read(aiming + aiming_current_level, current))
		{
			return state;
		}
		read_attachments(weapons, index, unlock, state);
		const std::uintptr_t levels = zoom_begin && zoom_end > zoom_begin ? (zoom_end - zoom_begin) / 8 : 0;
		if (levels > static_cast<std::uintptr_t>(max_levels) || selected < 0 || current < 0)
		{
			return state;
		}
		state.levels = static_cast<int>(levels);
		state.selected = selected;
		state.current = current;
		for (int i = 0; i < state.levels; ++i)
		{
			std::uintptr_t each{};
			float fov{}, fov_sp{};
			if (read(aiming + aiming_zoom_levels + static_cast<std::uintptr_t>(i) * 8, each) && each && read(each + zoom_fov, fov) && read(each + zoom_fov_sp, fov_sp))
			{
				const float used = fov_sp > 0.0f ? fov_sp : fov;
				if (used > 1.0f && used < 170.0f)
				{
					state.level_fov[i] = used;
				}
			}
		}
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
		char text[400];
		std::snprintf(text, sizeof(text),
			"sight_state valid=%d weapon=%s levels=%d selected=%d current=%d selected_type=%d attachments_known=%d optic=%s reflex=%d magnifier=%d magnified=%d (%.1fx) zoom_fov=",
			s.valid ? 1 : 0, s.weapon, s.levels, s.selected, s.current, static_cast<int>(s.selected_type), s.attachments_known ? 1 : 0, s.optic[0] ? s.optic : "none",
			s.optics.reflex ? 1 : 0, s.optics.magnifier ? 1 : 0, s.optics.magnified ? 1 : 0, s.optics.magnification);
		std::string out = text;
		for (int i = 0; i < s.levels && i < 8; ++i)
		{
			char one[24];
			std::snprintf(one, sizeof(one), "%s%.1f", i ? "/" : "", s.level_fov[i]);
			out += one;
		}
		const float power = zoom_power(s);
		char tail[120];
		std::snprintf(tail, sizeof(tail), " power=%.2f foregrip=%s grip=%s", power, foregrip_name(s.foregrip), s.grip[0] ? s.grip : "none");
		return out + tail;
	}

	float zoom_power(const sight_state& state)
	{
		return state.valid ? level_power(state.level_fov, state.levels, static_cast<int>(std::size(state.level_fov)), state.selected) : 0.0f;
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

	float zoom_power(const sight_state&)
	{
		return 0.0f;
	}
#endif
}
