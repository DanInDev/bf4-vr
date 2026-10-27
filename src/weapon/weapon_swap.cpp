#include "weapon/weapon_swap.hpp"

#include <windows.h>
#include <Xinput.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "body/arm_pose.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "hooks/game_window.hpp"
#include "input/handedness.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/tracking.hpp"
#include "ui/vr_panel.hpp"
#include "weapon/hand_signal.hpp"
#include "weapon/holsters.hpp"
#include "weapon/melee_gesture.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// BF4 179547: local soldier -> weapons component -> current slot index (re/notes/aim.md).
		constexpr std::uintptr_t local_soldier_global = static_cast<std::uintptr_t>(0x14230f260ull); // x64 only
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_flags = 0x28;
		constexpr std::uintptr_t weapons_index_a = 0x8d8;
		constexpr std::uintptr_t weapons_index_b = 0x8e0;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_unlock = 0x4850;
		constexpr std::uintptr_t unlock_name = 0x20;
		constexpr ULONGLONG key_hold_ms = 80;
		constexpr ULONGLONG check_ms = 600;
		constexpr ULONGLONG points_max_age_ms = 250;

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		struct slot_state
		{
			bool valid{};
			int current{ -1 };
			char name[48]{}; // of the current weapon
		};

		slot_state read_slot()
		{
			slot_state state;
			std::uintptr_t soldier{}, weapons{}, holder{}, begin{}, end{}, weapon{}, unlock{}, name{};
			std::uint32_t flags{}, index{};
			if (!read(local_soldier_global, soldier) || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_flags, flags)
				|| !read(weapons + ((flags & 4) ? weapons_index_b : weapons_index_a), holder) || !read(holder + 0x8, index)
				|| !read(weapons + weapons_begin, begin) || !read(weapons + weapons_end, end) || !begin || index >= (end - begin) / 8)
			{
				return state;
			}
			state.valid = true;
			state.current = static_cast<int>(index);
			if (read(begin + index * 8, weapon) && read(weapon + weapon_unlock, unlock) && read(unlock + unlock_name, name) && read(name, state.name))
			{
				state.name[sizeof(state.name) - 1] = 0;
			}
			return state;
		}

		unsigned short key_for(holster which)
		{
			switch (which)
			{
			case holster::primary: return '1';
			case holster::secondary: return '2';
			case holster::gadget1: return '3';
			case holster::gadget2: return '4';
			case holster::knife: return '6';
			case holster::none: break;
			}
			return 0;
		}

		// Already holding what the holster holds (slot 0 primary, 1 secondary, the knife by name).
		bool already(holster which, const slot_state& slot)
		{
			switch (which)
			{
			case holster::primary: return slot.current == 0;
			case holster::secondary: return slot.current == 1;
			case holster::knife: return std::strstr(slot.name, "Knife") != nullptr;
			default: return false;
			}
		}

		std::atomic<bool> enabled{ true };
		std::atomic<bool> grip_claimed{};
		// Squad order (campaign "engage", key Q) by hand signal.
		constexpr unsigned short order_key = 'Q';
		std::atomic<bool> signal_enabled{ true };
		std::atomic<bool> signal_raised{};
		hand_signal signal;
		ULONGLONG order_up_at{};
		std::uint64_t orders{};

		// Melee by hand (#15): pad right-stick click for one stab.
		std::atomic<bool> melee_enabled{ true };
		std::atomic<bool> knife_grip{ true };
		melee_gesture melee;
		std::atomic<ULONGLONG> melee_until{};
		constexpr ULONGLONG melee_pulse_ms = 80;
		constexpr ULONGLONG after_holster_ms = 600; // bringing a drawn gun forward is not a strike
		ULONGLONG last_in_holster{};
		std::uint64_t strikes{};

		std::mutex lock;
		holsters zones;
		LARGE_INTEGER last_update{};
		// A switch in flight: key down, key up after key_hold_ms, checked after check_ms, one retry.
		holster pending{ holster::none };
		int pending_from{ -1 };
		ULONGLONG key_up_at{}, check_at{};
		unsigned short key_down{};
		int attempts{};
		std::uint64_t switches{}, failed{};
		const char* last_result{ "none" };
		holster last_zone{ holster::none };

		void send(holster which, ULONGLONG now)
		{
			key_down = key_for(which);
			hooks::press_game_key(key_down, true);
			key_up_at = now + key_hold_ms;
			check_at = now + check_ms;
			++attempts;
		}

		// Called with lock held.
		void request(holster which, ULONGLONG now)
		{
			const slot_state slot = read_slot();
			if (which == holster::none || !slot.valid || key_for(which) == 0 || pending != holster::none)
			{
				return;
			}
			if (already(which, slot))
			{
				last_result = "already in hand";
				return;
			}
			pending = which;
			pending_from = slot.current;
			attempts = 0;
			send(which, now);
		}

		// Called with lock held: Q down now, up after key_hold_ms (advance).
		void give_order(ULONGLONG now)
		{
			if (order_up_at || !hooks::press_game_key(order_key, true))
			{
				return;
			}
			order_up_at = now + key_hold_ms;
			++orders;
			log::write("squad: order given (Q)");
		}

		// Called with lock held.
		void advance(ULONGLONG now)
		{
			if (order_up_at && now >= order_up_at)
			{
				hooks::press_game_key(order_key, false);
				order_up_at = 0;
			}
			if (key_down && now >= key_up_at)
			{
				hooks::press_game_key(key_down, false);
				key_down = 0;
			}
			if (pending == holster::none || key_down || now < check_at)
			{
				return;
			}
			const slot_state slot = read_slot();
			if (slot.valid && slot.current != pending_from)
			{
				++switches;
				last_result = "switched";
				log::write("holsters: %s -> slot %d %s", holster_name(pending), slot.current, slot.name);
				pending = holster::none;
			}
			else if (attempts < 2)
			{
				send(pending, now); // the game may have been busy (reload, sprint start)
			}
			else
			{
				++failed;
				last_result = "no switch (empty holster, or the game refused)";
				log::write("holsters: %s: no switch", holster_name(pending));
				pending = holster::none;
			}
		}

		// Head position and yaw (atan2(-forward.x, -forward.z)) in OpenXR LOCAL.
		bool head_pose(v3& position, float& yaw)
		{
			const stereo::head_sample head = stereo::latest_head_sample();
			if (!head.valid)
			{
				return false;
			}
			const float* l = head.eyes[0].position;
			const float* r = head.eyes[1].position;
			const float* o = head.eyes[0].orientation;
			position = { 0.5f * (l[0] + r[0]), 0.5f * (l[1] + r[1]), 0.5f * (l[2] + r[2]) };
			const float fx = -2.0f * (o[0] * o[2] + o[1] * o[3]);
			const float fz = -(1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1]));
			yaw = std::atan2(-fx, -fz);
			return true;
		}
	}

	void set_melee_gesture(bool on)
	{
		melee_enabled = on;
	}

	void set_knife_grip(bool on)
	{
		knife_grip = on;
	}

	void set_holsters(bool on)
	{
		enabled = on;
	}

	void set_squad_signal(bool on)
	{
		signal_enabled = on;
	}

	void update_weapon_swap()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock);
		const ULONGLONG now = GetTickCount64();
		advance(now);
		const slot_state held = read_slot();
		body::set_knife_in_hand(knife_grip.load(std::memory_order_relaxed) && held.valid && std::strstr(held.name, "Knife") != nullptr);
		LARGE_INTEGER counter{}, frequency{};
		QueryPerformanceCounter(&counter);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_update.QuadPart ? std::fmin(static_cast<float>(counter.QuadPart - last_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last_update = counter;
		const body::weapon_points points = body::latest_weapon_points();
		v3 head;
		float head_yaw{};
		const bool head_valid = head_pose(head, head_yaw);
		if (last_zone != holster::none)
		{
			last_in_holster = now;
		}
		{
			const stereo::head_sample head_sample = stereo::latest_head_sample();
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			const bool busy = grenade_in_hand() || reload_owns_off_hand() || ui::vr_panel_view().open || now - last_in_holster < after_holster_ms;
			if (melee_enabled.load(std::memory_order_relaxed) && head_sample.valid
				&& melee.update(stereo::eye_math::centre_pose(head_sample), hands.grip[1].position, hands.valid[1], busy, seconds))
			{
				melee_until = now + melee_pulse_ms;
				++strikes;
			}
		}
		if (signal_enabled.load(std::memory_order_relaxed))
		{
			signal_frame raised;
			raised.head_valid = head_valid;
			raised.head = head;
			raised.head_yaw = head_yaw;
			raised.hand_valid = points.hand_valid[0] && now - points.at < points_max_age_ms;
			raised.off_hand = { points.hand[0][0], points.hand[0][1], points.hand[0][2] };
			// A grenade in the off hand is thrown, not waved.
			raised.off_grip = (input::latest().buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0 && !grenade_in_hand();
			const signal_result told = signal.update(raised, seconds);
			signal_raised = told.raised;
			if (told.order)
			{
				give_order(now);
			}
		}
		else
		{
			signal_raised = false;
		}
		if (!enabled.load(std::memory_order_relaxed))
		{
			zones.reset();
			grip_claimed = false;
			last_zone = holster::none;
			return;
		}
		holster_frame frame;
		frame.seconds = seconds;
		frame.head_valid = head_valid;
		frame.head = head;
		frame.head_yaw = head_yaw;
		frame.hand_valid = points.hand_valid[1] && now - points.at < points_max_age_ms;
		frame.gun_hand = { points.hand[1][0], points.hand[1][1], points.hand[1][2] };
		// While a grenade is in the off hand, the gun hand's grip pulls its pin, never draws.
		frame.gun_grip = (input::latest().buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0 && !grenade_in_hand();
		frame.left_handed = input::left_handed();
		const holster_result result = zones.update(frame);
		last_zone = result.zone;
		grip_claimed = result.zone != holster::none;
		if (result.draw != holster::none)
		{
			request(result.draw, now);
		}
#endif
	}

	input::gamepad_state filter_swap_pad(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (grip_claimed.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_RIGHT_SHOULDER);
		}
		if (signal_raised.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
		}
		if (GetTickCount64() < melee_until.load(std::memory_order_relaxed))
		{
			out.buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
			out.connected = true;
		}
		return out;
	}

	std::string holster_pipe_command(const std::string& argument)
	{
		const std::lock_guard guard(lock);
		if (!argument.empty())
		{
			if (!config::research_enabled())
			{
				return "error: 'holster <name>' needs research = 1 in bfvr.ini";
			}
			if (argument == "order")
			{
				give_order(GetTickCount64());
				return "squad order sent (Q); orders " + std::to_string(orders);
			}
			holster which = holster::none;
			for (const holster h : { holster::primary, holster::secondary, holster::knife, holster::gadget1, holster::gadget2 })
			{
				if (argument == holster_name(h))
				{
					which = h;
				}
			}
			if (which == holster::none)
			{
				return "error: usage: holster [primary|secondary|knife|gadget1|gadget2|order]";
			}
			request(which, GetTickCount64());
		}
		const slot_state slot = read_slot();
		char text[384];
		std::snprintf(text, sizeof(text),
			"holsters=%d slot=%d weapon=%s zone=%s pending=%s switches=%llu failed=%llu last=%s body_yaw=%.2f\nsquad_signal=%d raised=%d orders=%llu\nmelee_gesture=%d strikes=%llu forward_speed=%.2f",
			enabled ? 1 : 0, slot.current, slot.name, holster_name(last_zone), holster_name(pending), static_cast<unsigned long long>(switches),
			static_cast<unsigned long long>(failed), last_result, zones.body_yaw(), signal_enabled ? 1 : 0, signal_raised ? 1 : 0,
			static_cast<unsigned long long>(orders), melee_enabled ? 1 : 0, static_cast<unsigned long long>(strikes), melee.forward_speed());
		return text;
	}
}
