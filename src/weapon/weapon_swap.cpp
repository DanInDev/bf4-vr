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
#include "input/haptics.hpp"
#include "input/vr_controls.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/tracking.hpp"
#include "ui/vr_panel.hpp"
#include "weapon/body_rig.hpp"
#include "weapon/free_hands.hpp"
#include "weapon/grab_debug.hpp"
#include "weapon/hand_signal.hpp"
#include "weapon/holsters.hpp"
#include "weapon/melee_gesture.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/two_handed.hpp"
#include "weapon/use_reach.hpp"

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
		std::atomic<bool> off_grip_claimed{};
		std::atomic<float> published_body_yaw{};
		std::atomic<bool> body_yaw_known{};
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
		std::atomic<bool> c4_trigger{ true };

		// Use by reaching (#15): the game's interact key while the off hand reaches out and grips.
		constexpr unsigned short use_key = 'E';
		std::atomic<bool> use_enabled{ true };
		std::atomic<bool> use_reaching{};
		use_reach user;
		bool use_down{};
		std::uint64_t uses{};
		melee_gesture melee;
		// Free hands (#35).
		std::atomic<bool> free_hands_on{};
		std::atomic<bool> dropped_now{};
		free_hands dropper;
		int last_slot{ -2 };
		char last_name[48]{};
		std::uint64_t drops{};
		std::uint64_t pickups{};
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
		holster last_off_zone{ holster::none };

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
			input::haptic(0, input::pulses::click);
			log::write("squad: order given (Q)");
		}

		// Called with lock held.
		void hold_use(bool down)
		{
			if (down == use_down || !hooks::press_game_key(use_key, down))
			{
				return;
			}
			use_down = down;
			if (down)
			{
				++uses;
				input::haptic(0, input::pulses::grab);
				log::write("use: interact (E) held");
			}
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
				input::haptic(1, input::pulses::seat);
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

	void set_use_reach(bool on)
	{
		use_enabled = on;
	}

	void set_c4_trigger(bool on)
	{
		c4_trigger = on;
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
		// Slots 0 and 1 hold the primary and secondary guns; gadgets, grenades and the knife keep their own hold.
		body::set_gun_in_hand(held.valid && held.current >= 0 && held.current <= 1);
		// BF4's C4: the aim button (left trigger) throws one, fire (on the detonator) sets them off.
		input::set_item_left_trigger(c4_trigger.load(std::memory_order_relaxed) && held.valid && std::strstr(held.name, "_C4") != nullptr);
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
				input::haptic(1, input::pulses::strike);
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
		if (use_enabled.load(std::memory_order_relaxed))
		{
			use_frame reach;
			reach.head_valid = head_valid;
			reach.head = head;
			reach.head_yaw = head_yaw;
			const bool fresh = now - points.at < points_max_age_ms;
			reach.hand_valid = points.hand_valid[0] && fresh;
			reach.off_hand = { points.hand[0][0], points.hand[0][1], points.hand[0][2] };
			reach.gun_hand_valid = points.hand_valid[1] && fresh;
			reach.gun_hand = { points.hand[1][0], points.hand[1][1], points.hand[1][2] };
			reach.off_grip = (input::latest().buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
			reach.busy = grenade_in_hand() || reload_owns_off_hand() || off_hand_on_gun() || signal_raised.load() || ui::vr_panel_view().open;
			const use_result used = user.update(reach);
			use_reaching = used.reaching;
			hold_use(used.use);
		}
		else
		{
			use_reaching = false;
			hold_use(false);
		}
		{
			// Free hands (#35): the gun stays in hand only while the gun hand grips it.
			const bool changed = held.current != last_slot || std::strncmp(held.name, last_name, sizeof(last_name)) != 0;
			last_slot = held.current;
			std::memcpy(last_name, held.name, sizeof(last_name));
			free_hands_frame drop;
			drop.seconds = seconds;
			drop.enabled = free_hands_on.load(std::memory_order_relaxed);
			drop.gun_in_hand = held.valid && held.current >= 0 && held.current <= 1 && std::strstr(held.name, "Knife") == nullptr;
			drop.weapon_changed = changed;
			drop.hand_valid = points.hand_valid[1] && now - points.at < points_max_age_ms && points.has_floor;
			drop.gun_hand = { points.palm[1][0], points.palm[1][1], points.palm[1][2] };
			drop.grip = (input::latest().buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
			// The grip pulling a grenade pin, drawing from a holster or clicking the panel is not a release.
			drop.busy = grenade_in_hand() || grip_claimed.load() || ui::vr_panel_view().open;
			drop.floor_y = points.floor_y;
			const float* q = points.hand_rotation[1];
			drop.hand_yaw = std::atan2(2.0f * (q[0] * q[2] + q[1] * q[3]), 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
			const free_hands_result dropped = dropper.update(drop);
			if (dropped.event == free_hands_event::dropped)
			{
				++drops;
				input::haptic(1, input::pulses::tick);
			}
			else if (dropped.event == free_hands_event::picked_up)
			{
				++pickups;
				input::haptic(1, input::pulses::grab);
			}
			dropped_now = dropped.dropped;
			const float lying[3]{ dropped.position.x, dropped.position.y, dropped.position.z };
			body::set_dropped_gun(dropped.dropped, lying, dropped.yaw);
			if (dropped.dropped)
			{
				publish_grab_zone(grab_slot::dropped_gun, lying, dropper.config().pickup_reach, dropped.in_reach);
			}
			else
			{
				clear_grab_zone(grab_slot::dropped_gun);
			}
		}
		if (!enabled.load(std::memory_order_relaxed))
		{
			zones.reset();
			grip_claimed = false;
			off_grip_claimed = false;
			body_yaw_known = false;
			last_zone = holster::none;
			last_off_zone = holster::none;
			return;
		}
		holster_frame frame;
		frame.seconds = seconds;
		frame.head_valid = head_valid;
		frame.head = head;
		frame.head_yaw = head_yaw;
		frame.hand_valid = points.hand_valid[1] && now - points.at < points_max_age_ms;
		frame.gun_hand = { points.palm[1][0], points.palm[1][1], points.palm[1][2] };
		// While a grenade is in the off hand, the gun hand's grip pulls its pin, never draws.
		frame.gun_grip = (input::latest().buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0 && !grenade_in_hand();
		frame.left_handed = input::left_handed();
		frame.off_hand_valid = points.hand_valid[0] && now - points.at < points_max_age_ms;
		frame.off_hand = { points.palm[0][0], points.palm[0][1], points.palm[0][2] };
		frame.off_grip = (input::latest().buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
		frame.off_busy = grenade_in_hand() || off_hand_at_belt() || reload_owns_off_hand() || off_hand_on_gun() || signal_raised.load()
			|| use_reaching.load() || ui::vr_panel_view().open;
		const holster_result result = zones.update(frame);
		if (zones.body_known())
		{
			published_body_yaw = zones.body_yaw();
			body_yaw_known = true;
		}
		if (grab_debug_enabled() && result.has_centres)
		{
			for (int i = 0; i < 5; ++i)
			{
				const float centre[3]{ result.centres[i].x, result.centres[i].y, result.centres[i].z };
				publish_grab_zone(static_cast<grab_slot>(static_cast<int>(grab_slot::primary) + i), centre, result.radii[i], result.zone == static_cast<holster>(static_cast<int>(holster::primary) + i));
			}
		}
		if (result.zone != holster::none && result.zone != last_zone)
		{
			input::haptic(1, input::pulses::tick);
		}
		if (result.off_zone != holster::none && result.off_zone != last_off_zone)
		{
			input::haptic(0, input::pulses::tick);
		}
		last_zone = result.zone;
		last_off_zone = result.off_zone;
		grip_claimed = result.zone != holster::none;
		off_grip_claimed = result.off_zone != holster::none;
		if (result.draw != holster::none)
		{
			request(result.draw, now);
		}
#endif
	}

	bool body_yaw(float& yaw)
	{
		yaw = published_body_yaw.load(std::memory_order_relaxed);
		return body_yaw_known.load(std::memory_order_relaxed);
	}

	bool body_rig_point(const v3& offset, v3& point)
	{
		const stereo::head_sample head = stereo::latest_head_sample();
		if (!head.valid)
		{
			return false;
		}
		const float* l = head.eyes[0].position;
		const float* r = head.eyes[1].position;
		const v3 centre{ 0.5f * (l[0] + r[0]), 0.5f * (l[1] + r[1]), 0.5f * (l[2] + r[2]) };
		float yaw{};
		if (!body_yaw(yaw))
		{
			const float* o = head.eyes[0].orientation;
			yaw = std::atan2(2.0f * (o[0] * o[2] + o[1] * o[3]), 1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1]));
		}
		point = body_point(centre, yaw, offset, input::left_handed());
		return true;
	}

	input::gamepad_state filter_swap_pad(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (grip_claimed.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_RIGHT_SHOULDER);
		}
		if (signal_raised.load(std::memory_order_relaxed) || use_reaching.load(std::memory_order_relaxed) || off_grip_claimed.load(std::memory_order_relaxed))
		{
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
		}
		if (GetTickCount64() < melee_until.load(std::memory_order_relaxed))
		{
			out.buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
			out.connected = true;
		}
		if (dropped_now.load(std::memory_order_relaxed))
		{
			out.right_trigger = 0; // the gun lies on the floor (free hands)
		}
		return out;
	}

	void set_free_hands(bool on)
	{
		free_hands_on = on;
	}

	bool gun_dropped()
	{
		return dropped_now.load(std::memory_order_relaxed);
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
		char text[768];
		std::snprintf(text, sizeof(text),
			"holsters=%d slot=%d weapon=%s zone=%s pending=%s switches=%llu failed=%llu last=%s body_yaw=%.2f\nsquad_signal=%d raised=%d orders=%llu\nmelee_gesture=%d strikes=%llu forward_speed=%.2f\n"
			"use_reach=%d reaching=%d using=%d uses=%llu\nfree_hands=%d dropped=%d drops=%llu pickups=%llu floor_y=%.2f",
			enabled ? 1 : 0, slot.current, slot.name, holster_name(last_zone), holster_name(pending), static_cast<unsigned long long>(switches),
			static_cast<unsigned long long>(failed), last_result, zones.body_yaw(), signal_enabled ? 1 : 0, signal_raised ? 1 : 0,
			static_cast<unsigned long long>(orders), melee_enabled ? 1 : 0, static_cast<unsigned long long>(strikes), melee.forward_speed(),
			use_enabled ? 1 : 0, use_reaching ? 1 : 0, use_down ? 1 : 0, static_cast<unsigned long long>(uses),
			free_hands_on ? 1 : 0, dropped_now ? 1 : 0, static_cast<unsigned long long>(drops), static_cast<unsigned long long>(pickups),
			body::latest_weapon_points().floor_y);
		return text;
	}
}
