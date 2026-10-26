#include "weapon/physical_reload.hpp"

#include <windows.h>
#include <Xinput.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <mutex>
#include <string>

#include "body/arm_pose.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "stereo/tracking.hpp"
#include "weapon/profiles.hpp"
#include "weapon/reload.hpp"
#include "weapon/reload_hands.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// BF4 179547 (re/notes/weapon.md). Client: local soldier -> weapons component -> current slot.
		constexpr std::uintptr_t local_soldier_global = static_cast<std::uintptr_t>(0x14230f260ull); // x64 only: update() is compiled out on x86
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_flags = 0x28;
		constexpr std::uintptr_t weapons_index_a = 0x8d8;
		constexpr std::uintptr_t weapons_index_b = 0x8e0;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_unlock = 0x4850;  // -> unlock asset, +0x20 name ("U_AK12")
		constexpr std::uintptr_t unlock_name = 0x20;
		constexpr std::uintptr_t weapon_data = 0x30;      // SoldierWeaponData
		constexpr std::uintptr_t data_firing = 0x90;      // WeaponFiringData
		constexpr std::uintptr_t firing_primary = 0x10;   // FiringFunctionData
		constexpr std::uintptr_t function_capacity = 0x220; // Ammo.MagazineCapacity (counts the chamber)
		// Server (single player runs it in-process): local ServerSoldierEntity -> weapons component ->
		// slot array (same slot index as the client) -> ammo.
		constexpr std::uintptr_t server_soldier_global = static_cast<std::uintptr_t>(0x142311db8ull);
		constexpr std::uintptr_t server_soldier_weapons = 0x7c0;
		constexpr std::uintptr_t server_weapon_slots = 0x828;
		constexpr std::uintptr_t server_magazine = 0x440;  // int32 rounds in the gun, reserve at +4
		constexpr int max_slots = 16;
		// Rounds the game keeps beyond ours, so its magazine never hits 0 (its own reload).
		constexpr int spare_rounds = 2;
		constexpr ULONGLONG points_max_age_ms = 250;

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
			return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(value), nullptr);
		}

		struct game_weapon
		{
			bool valid{};
			int slot{ -1 };
			char name[48]{};
			int capacity{};          // game MagazineCapacity
			std::uintptr_t ammo{};   // server weapon + 0x440
			int magazine{};
			int reserve{};
		};

		game_weapon read_game_weapon()
		{
			game_weapon weapon;
			std::uintptr_t soldier{}, weapons{}, holder{}, begin{}, end{}, client{};
			std::uint32_t flags{}, index{};
			if (!read(local_soldier_global, soldier) || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_flags, flags)
				|| !read(weapons + ((flags & 4) ? weapons_index_b : weapons_index_a), holder) || !read(holder + 0x8, index)
				|| !read(weapons + weapons_begin, begin) || !read(weapons + weapons_end, end) || !begin || end <= begin
				|| index >= (end - begin) / 8 || index >= max_slots || !read(begin + index * 8, client) || !client)
			{
				return weapon;
			}
			std::uintptr_t unlock{}, name{}, data{}, firing{}, function{};
			if (!read(client + weapon_unlock, unlock) || !read(unlock + unlock_name, name) || !read(name, weapon.name))
			{
				return weapon;
			}
			weapon.name[sizeof(weapon.name) - 1] = 0;
			if (!read(client + weapon_data, data) || !read(data + data_firing, firing) || !read(firing + firing_primary, function)
				|| !read(function + function_capacity, weapon.capacity))
			{
				return weapon;
			}
			std::uintptr_t server_soldier{}, server_weapons{}, server_weapon{};
			if (!read(server_soldier_global, server_soldier) || !read(server_soldier + server_soldier_weapons, server_weapons)
				|| !read(server_weapons + server_weapon_slots + index * 8, server_weapon) || !server_weapon
				|| !read(server_weapon + server_magazine, weapon.magazine) || !read(server_weapon + server_magazine + 4, weapon.reserve))
			{
				return weapon;
			}
			// Grenades, the knife and gadgets carry -1: no magazine to reload.
			weapon.valid = weapon.magazine >= 0 && weapon.reserve >= 0 && weapon.capacity > 1 && weapon.capacity <= 500;
			weapon.slot = static_cast<int>(index);
			weapon.ammo = server_weapon + server_magazine;
			return weapon;
		}

		std::atomic<bool> physical{};
		std::atomic<bool> discard{ true };
		// For the pad filter on the XInput thread.
		std::atomic<bool> active{};
		std::atomic<bool> block_trigger{};
		std::atomic<bool> block_grip{};

		struct slot_memory
		{
			std::string name;
			std::uintptr_t ammo{}; // server weapon ammo: a new object (respawn, level) starts fresh
			reload gun;
			int written{ -1 };     // magazine value we last wrote into the game
			int written_reserve{ -1 };
		};

		std::mutex lock;
		std::map<int, slot_memory> slots;
		int current_slot{ -1 };
		reload_hands hands;
		LARGE_INTEGER last_update{};
		const char* recent[8]{};
		int recent_count{};
		std::uint64_t shots{};
		bool magazine_detached{};

		const char* event_name(reload_event event)
		{
			switch (event)
			{
			case reload_event::none: return "none";
			case reload_event::shot: return "shot";
			case reload_event::dry_fire: return "dry_fire";
			case reload_event::bolt_locked: return "bolt_locked";
			case reload_event::magazine_out: return "magazine_out";
			case reload_event::magazine_dropped: return "magazine_dropped";
			case reload_event::magazine_stowed: return "magazine_stowed";
			case reload_event::magazine_taken: return "magazine_taken";
			case reload_event::magazine_in: return "magazine_in";
			case reload_event::flicked_in: return "flicked_in";
			case reload_event::chambered: return "chambered";
			case reload_event::racked_empty: return "racked_empty";
			case reload_event::round_ejected: return "round_ejected";
			}
			return "?";
		}

		void remember(reload_event event)
		{
			if (event == reload_event::none || event == reload_event::shot)
			{
				return;
			}
			if (recent_count == static_cast<int>(std::size(recent)))
			{
				std::memmove(recent, recent + 1, sizeof(recent) - sizeof(recent[0]));
				--recent_count;
			}
			recent[recent_count++] = event_name(event);
		}

		// The game's magazine for our state: rounds in the gun plus the spare (empty: only the
		// spare, and the trigger is blocked).
		int game_magazine(const reload& gun)
		{
			return gun.rounds_in_gun() + spare_rounds;
		}

		void attach_magazine()
		{
			if (magazine_detached)
			{
				body::set_magazine_pose({});
				magazine_detached = false;
			}
		}

		// Called with lock held: hands the game back its own ammo (rounds in the gun, no spare).
		void release_slot(const game_weapon& weapon)
		{
			const auto found = slots.find(current_slot);
			if (found != slots.end() && weapon.valid && weapon.slot == current_slot && found->second.written >= 0)
			{
				const reload& gun = found->second.gun;
				write(weapon.ammo, gun.rounds_in_gun());
				write(weapon.ammo + 4, gun.state().reserve);
			}
			slots.clear();
			current_slot = -1;
			hands.reset();
			attach_magazine();
		}

		weapon::v3 v(const float* p)
		{
			return { p[0], p[1], p[2] };
		}

		weapon::q4 q(const float* p)
		{
			return { p[0], p[1], p[2], p[3] };
		}

		// The magazine pouch: 35 cm below the head, 10 cm ahead along the head's yaw.
		bool chest_point(weapon::v3& chest)
		{
			const stereo::head_sample head = stereo::latest_head_sample();
			if (!head.valid)
			{
				return false;
			}
			const float* l = head.eyes[0].position;
			const float* r = head.eyes[1].position;
			const float* o = head.eyes[0].orientation;
			// Forward (-z) of the head rotation, flattened.
			float fx = -2.0f * (o[0] * o[2] + o[1] * o[3]);
			float fz = -(1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1]));
			const float length = std::sqrt(fx * fx + fz * fz);
			if (length > 1e-4f)
			{
				fx /= length;
				fz /= length;
			}
			chest = { 0.5f * (l[0] + r[0]) + 0.10f * fx, 0.5f * (l[1] + r[1]) - 0.35f, 0.5f * (l[2] + r[2]) + 0.10f * fz };
			return true;
		}
	}

	void set_physical_reload(bool enabled)
	{
		physical = enabled;
	}

	void set_drop_discards_ammo(bool value)
	{
		discard = value;
	}

	void update_physical_reload()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock);
		LARGE_INTEGER now{}, frequency{};
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_update.QuadPart ? std::fmin(static_cast<float>(now.QuadPart - last_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last_update = now;

		const game_weapon weapon = read_game_weapon();
		if (!physical.load(std::memory_order_relaxed) || !weapon.valid)
		{
			if (!physical.load(std::memory_order_relaxed))
			{
				release_slot(weapon);
			}
			else
			{
				hands.reset();
				attach_magazine();
			}
			active = false;
			block_trigger = false;
			block_grip = false;
			return;
		}

		// A weapon switch keeps the old gun's state (its magazine may be out) for when it comes back.
		auto found = slots.find(weapon.slot);
		if (found == slots.end() || found->second.name != weapon.name || found->second.ammo != weapon.ammo)
		{
			slot_memory fresh{ weapon.name, weapon.ammo, reload(profile_for(weapon.name, weapon.capacity)), -1 };
			fresh.gun.load(weapon.magazine, weapon.reserve);
			found = slots.insert_or_assign(weapon.slot, std::move(fresh)).first;
			log::write("reload: %s slot %d %s, capacity %d, gun %d, reserve %d", weapon.name, weapon.slot,
				found->second.gun.profile().kind == variant::ak ? "AK" : "AR", found->second.gun.profile().capacity, weapon.magazine, weapon.reserve);
		}
		if (weapon.slot != current_slot)
		{
			hands.reset();
			current_slot = weapon.slot;
		}
		slot_memory& memory = found->second;
		reload& gun = memory.gun;
		gun.configure({ discard.load(std::memory_order_relaxed) });

		// Shots: the game counted down from what we wrote. Anything else (pickups) moves the reserve.
		if (memory.written >= 0)
		{
			for (int shot = 0; shot < memory.written - weapon.magazine && shot < 100; ++shot)
			{
				remember(gun.fire());
				++shots;
			}
			if (weapon.magazine > memory.written)
			{
				log::write("reload: the game raised %s's magazine %d -> %d; taking its rounds", weapon.name, memory.written, weapon.magazine);
				gun.load(weapon.magazine - spare_rounds, weapon.reserve);
			}
			else if (memory.written_reserve >= 0 && weapon.reserve != memory.written_reserve)
			{
				// The game itself changed the reserve since our write (pickup, resupply).
				gun.set_reserve(gun.state().reserve + weapon.reserve - memory.written_reserve);
			}
		}

		const body::weapon_points points = body::latest_weapon_points();
		const input::gamepad_state pad = input::latest();
		hand_frame frame;
		frame.seconds = seconds;
		frame.hands_valid = points.valid && points.hand_valid[0] && points.hand_valid[1] && GetTickCount64() - points.at < points_max_age_ms;
		frame.off_hand = v(points.hand[0]);
		frame.gun_hand = v(points.hand[1]);
		frame.gun_rotation = q(points.hand_rotation[1]);
		frame.magwell = v(points.magazine);
		frame.magazine_rotation = q(points.magazine_rotation);
		frame.has_slide = points.has_slide;
		frame.slide = v(points.slide);
		if (!chest_point(frame.chest))
		{
			frame.chest = { 0.0f, -0.35f, -0.1f };
		}
		frame.grip = (pad.buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
		frame.button = (pad.buttons & XINPUT_GAMEPAD_X) != 0;
		const hand_result result = hands.update(gun, frame);
		for (int i = 0; i < result.count; ++i)
		{
			remember(result.events[i]);
		}

		const int wanted = game_magazine(gun);
		if (wanted != weapon.magazine)
		{
			write(weapon.ammo, wanted);
		}
		if (gun.state().reserve != weapon.reserve)
		{
			write(weapon.ammo + 4, gun.state().reserve);
		}
		memory.written = wanted;
		memory.written_reserve = gun.state().reserve;

		if (result.draw == magazine_draw::with_gun || !points.valid)
		{
			attach_magazine();
		}
		else
		{
			body::magazine_pose pose;
			pose.detached = true;
			pose.position[0] = result.magazine_position.x;
			pose.position[1] = result.magazine_position.y;
			pose.position[2] = result.magazine_position.z;
			pose.rotation[0] = result.magazine_rotation.x;
			pose.rotation[1] = result.magazine_rotation.y;
			pose.rotation[2] = result.magazine_rotation.z;
			pose.rotation[3] = result.magazine_rotation.w;
			body::set_magazine_pose(pose);
			magazine_detached = true;
		}

		active = true;
		block_trigger = !gun.state().chamber;
		block_grip = result.off_hand_busy;
#endif
	}

	input::gamepad_state filter_pad(const input::gamepad_state& pad)
	{
		if (!active.load(std::memory_order_relaxed))
		{
			return pad;
		}
		input::gamepad_state out = pad;
		out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_X);
		if (block_grip.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
		}
		if (block_trigger.load(std::memory_order_relaxed))
		{
			out.right_trigger = 0;
		}
		return out;
	}

	std::string reload_pipe_command(const std::string& argument)
	{
		const std::lock_guard guard(lock);
		const auto found = slots.find(current_slot);
		if (!argument.empty())
		{
			if (!config::research_enabled())
			{
				return "error: 'reload <event>' needs research = 1 in bfvr.ini";
			}
			if (found == slots.end() || !active)
			{
				return "error: no physical reload active (reload = physical, a magazine weapon in hand)";
			}
			reload& gun = found->second.gun;
			reload_event event = reload_event::none;
			if (argument == "fire") event = gun.fire();
			else if (argument == "button") event = gun.press_button(false);
			else if (argument == "catch") event = gun.press_button(true);
			else if (argument == "grab") event = gun.grab_magazine();
			else if (argument == "take") event = gun.take_from_chest();
			else if (argument == "letgo") event = gun.let_go(false);
			else if (argument == "stow") event = gun.let_go(true);
			else if (argument == "insert") event = gun.insert();
			else if (argument == "rack") event = gun.rack();
			else return "error: usage: reload [fire|button|catch|grab|take|letgo|stow|insert|rack]";
			remember(event);
		}
		char text[768];
		if (found == slots.end())
		{
			std::snprintf(text, sizeof(text), "reload=%s active=0 (no magazine weapon in hand, or not in a level)", physical ? "physical" : "button");
			return text;
		}
		const reload& gun = found->second.gun;
		const gun_state& s = gun.state();
		const body::weapon_points points = body::latest_weapon_points();
		int length = std::snprintf(text, sizeof(text),
			"reload=%s active=%d weapon=%s slot=%d variant=%s capacity=%d\n"
			"magazine_in=%d magazine=%d chamber=%d bolt_locked=%d holding=%d held=%d reserve=%d game_written=%d shots=%llu\n"
			"magwell=(%.3f,%.3f,%.3f) slide=(%.3f,%.3f,%.3f) off_hand=(%.3f,%.3f,%.3f) points_valid=%d\nrecent:",
			physical ? "physical" : "button", active ? 1 : 0, found->second.name.c_str(), current_slot, gun.profile().kind == variant::ak ? "AK" : "AR",
			gun.profile().capacity, s.magazine_in ? 1 : 0, s.magazine_rounds, s.chamber ? 1 : 0, s.bolt_locked ? 1 : 0, s.holding ? 1 : 0,
			s.held_rounds, s.reserve, found->second.written, static_cast<unsigned long long>(shots), points.magazine[0], points.magazine[1],
			points.magazine[2], points.slide[0], points.slide[1], points.slide[2], points.hand[0][0], points.hand[0][1], points.hand[0][2],
			points.valid ? 1 : 0);
		for (int i = 0; i < recent_count && length > 0 && length < static_cast<int>(sizeof(text)); ++i)
		{
			length += std::snprintf(text + length, sizeof(text) - length, " %s", recent[i]);
		}
		return text;
	}
}
