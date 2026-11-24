#include "weapon/weapon_swap.hpp"

#include "common/controlled.hpp"

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
#include "common/settings_store.hpp"
#include "hooks/game_window.hpp"
#include "input/handedness.hpp"
#include "input/haptics.hpp"
#include "input/vr_controls.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/extra_mesh.hpp"
#include "stereo/tracking.hpp"
#include "ui/vr_panel.hpp"
#include "weapon/action_aim.hpp"
#include "weapon/body_rig.hpp"
#include "weapon/free_hands.hpp"
#include "weapon/grab_debug.hpp"
#include "weapon/gun_traits.hpp"
#include "weapon/hand_signal.hpp"
#include "weapon/holsters.hpp"
#include "weapon/melee_gesture.hpp"
#include "weapon/grenade_hands.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/sling_primary.hpp"
#include "weapon/slot_capture_start.hpp"
#include "weapon/two_handed.hpp"
#include "weapon/use_reach.hpp"
#include "weapon/weapon_pickup.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// BF4 179547: local soldier -> weapons component -> current slot index (re/notes/aim.md).
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

		std::atomic<int> in_hand_slot{ -1 }; // the slot the mod treats as in hand (#83: not the quick throw's grenade)

		slot_state read_slot()
		{
			slot_state state;
			std::uintptr_t soldier{}, weapons{}, holder{}, begin{}, end{}, weapon{}, unlock{}, name{};
			std::uint32_t flags{}, index{};
			if (!(soldier = game::local_soldier()) != 0 || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_flags, flags)
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

		// The knife: by its name, or slot 7 with no name yet (right after a level start the knife's name is
		// still empty when it first comes out, monitor 2026-11-13: "knife -> slot 7 " with no name).
		bool is_knife(const slot_state& slot)
		{
			return std::strstr(slot.name, "Knife") != nullptr || (slot.current == 7 && slot.name[0] == 0);
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
			case holster::knife: return is_knife(slot);
			default: return false;
			}
		}

		std::atomic<bool> enabled{ true };
		std::atomic<body::body_size> body_size_now{ body::body_size{} }; // #46, #50
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
		// #70: the squad order and a melee strike turn the game's aim to where the head looks first; the C4 throw
		// along the off hand's ray while its trigger is held (live setting action_aim).
		action_aim order_aim;
		action_aim melee_aim;               // with the lead and tail (action_aim on)
		action_aim melee_plain{ { 0, 0 } }; // action_aim off: the stab at once, no aim
		std::atomic<bool> action_aim_on{ true };
		std::atomic<bool> head_aim_now{};   // the order's or the strike's aim window (advance), read by the camera hook
		std::atomic<bool> melee_down{};     // the stab's button (advance), read by the pad filter
		std::atomic<ULONGLONG> c4_aim_until{};
		ULONGLONG c4_trigger_since{};       // the C4's left trigger went down (0: up)
		constexpr ULONGLONG action_lead_ms = 100;
		constexpr ULONGLONG c4_tail_ms = 300;
		constexpr std::uint8_t c4_touch = 8; // a left trigger above this counts as pulled (C4 throw)
		std::uint64_t orders{};

		// Melee by hand (#15): pad right-stick click for one stab.
		std::atomic<bool> melee_enabled{ true };
		std::atomic<bool> knife_grip{ true };
		std::atomic<bool> c4_trigger{ true };

		// Use by reaching (#15): the game's interact key while the off hand reaches out and grips.
		constexpr unsigned short use_key = 'E';
		std::atomic<bool> use_enabled{ true };
		std::atomic<bool> use_reaching{};
		float use_offset[3]{}; // the off palm from the head (forward, side, up), for the pipe
		use_reach user;
		std::atomic<bool> use_down{}; // read by the camera hook (use_reach_direction)
		std::uint64_t uses{};
		melee_gesture melee;
		// Free hands (#35).
		std::atomic<bool> free_hands_on{};
		std::atomic<float> slot_models_ahead_m{ 0.08f }; // live setting slot_models_ahead (#41)
		std::atomic<float> free_hands_release{}; // live setting free_hands_release, seconds
		std::atomic<bool> sling_on{ true }; // #38: a let-go gun hangs on the sling, not on the floor
		std::atomic<bool> sling_primary_on{ true }; // #58: the primary drawn on the sling while another weapon is out
		sling_primary sling_copy;
		bool sling_copy_was_shown{};
		bool sling_test_show{}; // research: pipe holster slingtest on|off (lock)
		std::atomic<bool> dropped_now{};
		std::atomic<bool> unarmed_now{}; // #136: the game's unarmed item is in hand
		std::atomic<bool> off_hand_now{}; // #38: the off hand holds the gun
		std::atomic<bool> off_grip_now{}; // #38: by the pistol grip (it fires with the off-hand trigger)
		free_hands dropper;
		int last_slot{ -2 };
		char last_name[48]{};
		std::uint64_t drops{};
		std::uint64_t pickups{};
		std::uint64_t hand_overs{};
		float last_grip_point[3]{}; // for the report (#38)
		bool last_grip_in_reach{};
		float last_dropped_at[3]{};
		bool last_slung{};
		bool holster_take{}; // #49: the gun hand gripped at the out-of-hand selected weapon's holster (next frame's free hands)
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
			if (action_aim_on.load(std::memory_order_relaxed))
			{
				// #70: aim first, Q after the lead (advance presses and releases it). The aim is on from now, so even
				// when the next frame comes after the lead, the aim reaches the game before Q does.
				if (order_up_at || !order_aim.start(now, key_hold_ms))
				{
					return;
				}
				head_aim_now = true;
			}
			else
			{
				if (order_up_at || !hooks::press_game_key(order_key, true))
				{
					return;
				}
				order_up_at = now + key_hold_ms;
			}
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
			// #70: the press and release come as events, so a slow frame delays them but never skips them.
			switch (order_aim.update(now))
			{
			case action_button::press:
				hooks::press_game_key(order_key, true);
				log::write("squad: Q down");
				break;
			case action_button::release:
				hooks::press_game_key(order_key, false);
				log::write("squad: Q up");
				break;
			default: break;
			}
			const action_button stab = melee_aim.update(now);
			const action_button plain = melee_plain.update(now);
			if (stab == action_button::press || plain == action_button::press)
			{
				melee_down = true;
			}
			if (stab == action_button::release || plain == action_button::release)
			{
				melee_down = false;
			}
			head_aim_now = order_aim.aiming(now) || melee_aim.aiming(now);
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

		// Held grenade (#36): the game draws its grenade only while it is the weapon in hand (the quick throw
		// keeps the gun drawn), so the first time a grenade is taken the game's grenade is selected (key 5) for a
		// moment, captured hidden (stereo/extra_mesh via body::set_grenade_weapon), and the gun selected again.
		enum class capture_stage
		{
			idle,
			to_grenade,
			capturing,
			back,
		};
		capture_stage grenade_capture{ capture_stage::idle };
		// While a capture has the game's grenade (or knife) selected, a trigger is the game's own throw or
		// stab: the triggers are kept from it (headset 2026-11-14: a grenade flew on a trigger press during a
		// 4 s capture that saw no grenade draw).
		std::atomic<bool> capture_active{};
		std::atomic<bool> grenade_capture_requested{};
		// #41: the same sequence selects the knife for its holster copy; which item this run is for.
		std::atomic<bool> knife_capture_requested{};
		stereo::extra_mesh::captured capture_item{ stereo::extra_mesh::captured::grenade };
		std::atomic<bool> slot_models_at_start{ true };
		slot_capture_start slot_captures;
		ULONGLONG knife_out_since{};
		int capture_from{ -1 };
		int capture_attempts{};
		ULONGLONG capture_deadline{};
		ULONGLONG capture_repress_at{}; // #41: the item's key again if the game has not switched yet
		unsigned short capture_key{};
		ULONGLONG capture_key_up{};
		std::uint64_t capture_count_at{};
		const char* capture_result{ "none" };
		constexpr unsigned short grenade_key = '5'; // slot 6, the hand grenade

		void press_capture_key(unsigned short key, ULONGLONG now)
		{
			if (capture_key)
			{
				hooks::press_game_key(capture_key, false);
			}
			capture_key = hooks::press_game_key(key, true) ? key : 0;
			capture_key_up = now + key_hold_ms;
		}

		unsigned short gun_key(int slot)
		{
			return slot == 0 ? '1' : '2';
		}

		// Called with lock held, every update.
		void advance_grenade_capture(const slot_state& held, ULONGLONG now)
		{
			if (capture_key && now >= capture_key_up)
			{
				hooks::press_game_key(capture_key, false);
				capture_key = 0;
			}
			switch (grenade_capture)
			{
			case capture_stage::idle:
			{
				namespace em = stereo::extra_mesh;
				const bool grenade = grenade_capture_requested.exchange(false);
				const bool knife = !grenade && knife_capture_requested.exchange(false);
				if (!grenade && !knife)
				{
					break;
				}
				if (!held.valid || held.current < 0 || held.current > 1 || pending != holster::none || key_down)
				{
					capture_result = "not now (no gun in hand, or a switch running)";
					break;
				}
				capture_item = grenade ? em::captured::grenade : em::captured::knife;
				capture_from = held.current;
				capture_count_at = em::item_captures(capture_item);
				capture_attempts = 0;
				if (grenade)
				{
					press_capture_key(grenade_key, now);
				}
				else
				{
					// The knife through the holster switch (its own resend): our single key 6 was not taken right
					// after the grenade's round trip, three times in a row, while `holster knife` came in 0.5 s
					// (monitor 2026-11-13).
					request(holster::knife, now);
				}
				grenade_capture = capture_stage::to_grenade;
				capture_deadline = now + 4000; // slow frames: the knife came out after more than 2 s at 9 fps
				capture_repress_at = now + 1200;
				log::write("%s capture: selecting the game's %s (from slot %d)", em::item_name(capture_item), em::item_name(capture_item), capture_from);
				break;
			}
			case capture_stage::to_grenade:
			{
				const bool selected = held.valid
					&& (capture_item == stereo::extra_mesh::captured::knife ? is_knife(held) : held.current == 6);
				if (selected)
				{
					grenade_capture = capture_stage::capturing;
					capture_deadline = now + 4000; // the knife waits half a second; a fresh level draws late at 9 fps
				}
				else if (capture_item == stereo::extra_mesh::captured::grenade && now >= capture_repress_at && now + 600 < capture_deadline)
				{
					// A key the game ignores is sent again, as the holster switch does for the knife.
					press_capture_key(grenade_key, now);
					capture_repress_at = now + 1200;
				}
				else if (now > capture_deadline)
				{
					// The game may still switch late (the knife took longer than 2 s at 9 fps, 2026-11-13, and then
					// stayed out): ask for the gun again either way; if it never left, that ends at once.
					capture_result = capture_item == stereo::extra_mesh::captured::knife ? "the game did not select its knife" : "the game did not select its grenade";
					log::write("%s capture: %s; back to slot %d", stereo::extra_mesh::item_name(capture_item), capture_result, capture_from);
					pending = holster::none; // the knife's holster switch stops resending
					press_capture_key(gun_key(capture_from), now);
					grenade_capture = capture_stage::back;
					capture_deadline = now + 1200;
				}
				break;
			}
			case capture_stage::capturing:
			{
				const bool done = stereo::extra_mesh::item_captures(capture_item) > capture_count_at;
				if (done || now > capture_deadline)
				{
					capture_result = done ? "captured" : capture_item == stereo::extra_mesh::captured::knife ? "no knife draw seen" : "no grenade draw seen";
					log::write("%s capture: %s; back to slot %d", stereo::extra_mesh::item_name(capture_item), capture_result, capture_from);
					pending = holster::none; // the knife's holster switch stops resending
					press_capture_key(gun_key(capture_from), now);
					grenade_capture = capture_stage::back;
					capture_deadline = now + 1200;
				}
				break;
			}
			case capture_stage::back:
				if (held.valid && held.current == capture_from)
				{
					grenade_capture = capture_stage::idle;
				}
				else if (now > capture_deadline)
				{
					if (++capture_attempts < 2)
					{
						press_capture_key(gun_key(capture_from), now); // the game may still have been busy
						capture_deadline = now + 1200;
					}
					else
					{
						grenade_capture = capture_stage::idle;
						log::write("grenade capture: the gun did not come back (slot %d)", held.current);
					}
				}
				break;
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

	void set_body_size(const body::body_size& size)
	{
		body_size_now = size;
	}

	void set_squad_signal(bool on)
	{
		signal_enabled = on;
	}

#if defined(_M_X64)
	namespace
	{
		// #58: capture the primary once it has been in hand a moment, and while another weapon is out hang
		// its copy at the sling (body/arm_pose, stereo/extra_mesh). Called with lock held.
		void update_sling_primary(const slot_state& held)
		{
			namespace em = stereo::extra_mesh;
			static ULONGLONG last_tick{};
			const ULONGLONG tick = GetTickCount64();
			sling_primary_frame frame;
			frame.enabled = sling_primary_on.load(std::memory_order_relaxed);
			frame.slot_valid = held.valid;
			frame.slot = held.current;
			frame.name = held.name;
			frame.seconds = last_tick ? std::fmin(static_cast<float>(tick - last_tick) / 1000.0f, 0.1f) : 0.0f;
			last_tick = tick;
			frame.captured = em::item_captured(em::captured::primary);
			frame.attempts = em::item_capture_attempts(em::captured::primary);
			v3 at{};
			float yaw{};
			frame.sling_valid = body_rig_point(sling_point, at) && body_yaw(yaw);
			frame.test_show = sling_test_show;
			const sling_primary_result result = sling_copy.update(frame);
			if (result.forget)
			{
				em::forget_item(em::captured::primary);
				log::write("sling: %s is the primary now; its copy for the sling is taken again", held.name);
			}
			body::set_primary_capture(result.capture);
			const float position[3]{ at.x, at.y, at.z };
			body::set_sling_primary(result.show, position, yaw);
			if (result.show != sling_copy_was_shown)
			{
				log::write("sling: the primary (%s) %s", sling_copy.primary_name().c_str(), result.show ? "hangs on the sling (another weapon out)" : "off the sling");
				sling_copy_was_shown = result.show;
			}
		}

		// #41: at level start the grenade and the knife are selected for a moment so their copies stand at the
		// pouch and the holster from the start, not only after the first grab. Called with lock held.
		void update_slot_captures(const slot_state& held, ULONGLONG now)
		{
			namespace em = stereo::extra_mesh;
			static ULONGLONG last_tick{};
			slot_capture_frame frame;
			frame.enabled = slot_models_at_start.load(std::memory_order_relaxed);
			frame.slot_valid = held.valid;
			frame.slot = held.current;
			frame.seconds = last_tick ? std::fmin(static_cast<float>(now - last_tick) / 1000.0f, 0.1f) : 0.0f;
			last_tick = now;
			frame.busy = pending != holster::none || key_down || grenade_capture != capture_stage::idle
				|| grenade_capture_requested.load() || knife_capture_requested.load();
			frame.primary_pending = held.current == 0 && sling_primary_on.load(std::memory_order_relaxed) && !em::item_captured(em::captured::primary)
				&& em::item_capture_attempts(em::captured::primary) < sling_primary_config{}.max_attempts;
			frame.grenade_captured = em::item_captured(em::captured::grenade);
			frame.knife_captured = em::item_captured(em::captured::knife);
			switch (slot_captures.update(frame))
			{
			case slot_capture_request::grenade:
				log::write("slot models: the grenade is selected once for its pouch copy");
				grenade_capture_requested = true;
				break;
			case slot_capture_request::knife:
				log::write("slot models: the knife is selected once for its holster copy");
				knife_capture_requested = true;
				break;
			case slot_capture_request::none:
				break;
			}
		}
	}
#endif

	void update_weapon_swap()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock);
		const ULONGLONG now = GetTickCount64();
		advance(now);
		// #83: the game's quick throw selects the grenade for ~200 ms during a physical throw; the gun stays "in hand".
		static slot_state last_held;
		const slot_state read_now = read_slot();
		const slot_state held = read_now.valid && hide_quick_throw_switch(grenade_throw_pending(), read_now.current, last_held.current) ? last_held : read_now;
		if (!hide_quick_throw_switch(grenade_throw_pending(), read_now.current, last_held.current))
		{
			last_held = read_now;
		}
		in_hand_slot = held.valid ? held.current : -1;
		const bool knife_out = held.valid && is_knife(held);
		body::set_knife_in_hand(knife_grip.load(std::memory_order_relaxed) && knife_out);
		// #41: the game's knife out is captured once for its holster (body/arm_pose, stereo/extra_mesh), half a
		// second after it came out (the gun is no longer drawn by then).
		knife_out_since = knife_out ? (knife_out_since ? knife_out_since : now) : 0;
		body::set_knife_weapon(knife_out, knife_out && now - knife_out_since >= 500);
		// Slots 0 and 1 hold the primary and secondary guns. The hand grenade (slot 6) and the C4 detonator are held by
		// the controller grip (#68, gun_hand_pose); other gadgets and the knife keep their own hold.
		// #136: the game's unarmed item sits in a gun slot: to the gun hold, the captures and the sling it is no weapon.
		const bool unarmed = held.valid && is_unarmed(held.name);
		unarmed_now = unarmed;
		const slot_state gun = unarmed ? slot_state{} : held;
		body::set_gun_in_hand(gun.valid && gun.current >= 0 && gun.current <= 1);
		// The unarmed hand is held as you hold the controller (#68's rule), not the game's unarmed run / crouch animation.
		body::set_gun_hand_item(unarmed || (held.valid && (held.current == 6 || std::strstr(held.name, "_C4") != nullptr)));
		body::set_c4_out(held.valid && std::strstr(held.name, "_C4") != nullptr);
		body::set_gun_hand_empty(unarmed);
		// Slot 6: the hand grenade (re/notes/weapon.md "Weapon slots"), selected for the capture (#36).
		advance_grenade_capture(gun, now);
		capture_active = grenade_capture != capture_stage::idle;
		body::set_grenade_weapon(held.valid && held.current == 6 && grenade_capture != capture_stage::idle
			&& capture_item == stereo::extra_mesh::captured::grenade);
		update_sling_primary(gun);
		update_slot_captures(gun, now);
		// BF4's C4: the aim button (left trigger) throws one, fire (on the detonator) sets them off.
		const bool c4_out = c4_trigger.load(std::memory_order_relaxed) && held.valid && std::strstr(held.name, "_C4") != nullptr;
		// #70: while the C4's throw (left trigger) is held, the game's aim goes along the off hand's ray. The trigger
		// stays closed to the game until it has been held for the lead (the game polls on its own thread, so an
		// open gate would let a fresh pull through before this frame sees it), so the throw already reads that aim.
		const bool c4_aims = c4_out && action_aim_on.load(std::memory_order_relaxed);
		c4_trigger_since = c4_aims && input::latest().left_trigger > c4_touch ? (c4_trigger_since ? c4_trigger_since : now) : 0;
		if (c4_trigger_since)
		{
			c4_aim_until = now + c4_tail_ms;
		}
		input::set_item_left_trigger(c4_out && (!c4_aims || (c4_trigger_since && now - c4_trigger_since >= action_lead_ms)));
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
			// #38: with the gun in the off hand (or dropped) the gun hand's swing is no strike: the knife the game
			// draws for it would end the hold (a reach for the handguard counted as a swing on the monitor).
			const bool busy = grenade_in_hand() || reload_owns_off_hand() || ui::vr_panel_view().open || now - last_in_holster < after_holster_ms
				|| off_hand_now.load() || dropped_now.load();
			if (melee_enabled.load(std::memory_order_relaxed) && head_sample.valid
				&& melee.update(stereo::eye_math::centre_pose(head_sample), hands.grip[1].position, hands.valid[1], busy, seconds))
			{
				// #70: the game's aim to where the head looks first, the stab after the lead (advance).
				const bool aimed = action_aim_on.load(std::memory_order_relaxed);
				action_aim& stab = aimed ? melee_aim : melee_plain;
				if (stab.start(now, melee_pulse_ms) && aimed)
				{
					head_aim_now = true; // as for the order: the aim before the stab even at a low frame rate
				}
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
		// #54: a weapon on the ground taken by either hand (R held). While it holds R the off hand's use reach
		// (E) stays off: both read the off-hand grip near the ground.
		bool picking = false;
		{
			static int pickup_last_slot = -1;
			static char pickup_last_name[48]{};
			const bool weapon_changed = held.valid
				&& (held.current != pickup_last_slot || std::strncmp(held.name, pickup_last_name, sizeof(pickup_last_name)) != 0);
			pickup_last_slot = held.current;
			std::memcpy(pickup_last_name, held.name, sizeof(pickup_last_name));
			const bool panel = ui::vr_panel_view().open;
			const bool gun_out = gun.valid && gun.current >= 0 && gun.current <= 1 && std::strstr(gun.name, "Knife") == nullptr;
			const bool busy[2]{
				grenade_in_hand() || reload_owns_off_hand() || off_hand_on_gun() || off_grip_claimed.load() || signal_raised.load() || panel
					|| hooks::game_pointer_active(),
				grenade_in_hand() || grip_claimed.load() || reload_claims_free_hand() || panel
					|| (gun_out && !off_hand_now.load() && (!free_hands_on.load(std::memory_order_relaxed) || !gun_dropped())),
			};
			const float head_at[3]{ head.x, head.y, head.z };
			const ULONGLONG buttons = input::latest().buttons;
			picking = update_weapon_pickup(points, now - points.at < points_max_age_ms, head_valid, head_at, (buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0,
				(buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0, busy, weapon_changed, seconds);
		}
		if (use_enabled.load(std::memory_order_relaxed) && !picking)
		{
			use_frame reach;
			reach.head_valid = head_valid;
			reach.head = head;
			reach.head_yaw = head_yaw;
			const bool fresh = now - points.at < points_max_age_ms;
			reach.hand_valid = points.hand_valid[0] && fresh;
			// Measured at the palms (the controllers' grip points), as every grab zone is: the drawn wrist sits
			// ~8 cm behind, so a natural reach of the controller fell short (headset 2026-11-09, #34).
			reach.off_hand = { points.palm[0][0], points.palm[0][1], points.palm[0][2] };
			// A dropped or slung gun (free hands) leaves no handguard near the gun hand to keep clear of.
			reach.gun_hand_valid = points.hand_valid[1] && fresh && !gun_dropped();
			reach.gun_hand = { points.palm[1][0], points.palm[1][1], points.palm[1][2] };
			reach.gun_line_valid = points.has_support && fresh && !gun_dropped();
			reach.gun_grip = { points.grip[0], points.grip[1], points.grip[2] };
			reach.gun_support = { points.support[0], points.support[1], points.support[2] };
			reach.off_grip = (input::latest().buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
			// A menu the laser points at (the gadget stash) is no use target: the grip there sent E again and again.
			reach.busy = grenade_in_hand() || reload_owns_off_hand() || off_hand_on_gun() || signal_raised.load() || ui::vr_panel_view().open
				|| hooks::game_pointer_active();
			const use_result used = user.update(reach);
			use_reaching = used.reaching;
			if (reach.head_valid && reach.hand_valid)
			{
				const float s = std::sin(head_yaw), c = std::cos(head_yaw);
				const v3 d{ reach.off_hand.x - head.x, reach.off_hand.y - head.y, reach.off_hand.z - head.z };
				use_offset[0] = -d.x * s - d.z * c; // forward
				use_offset[1] = d.x * c - d.z * s;  // side
				use_offset[2] = d.y;               // up
			}
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
			drop.gun_in_hand = gun.valid && gun.current >= 0 && gun.current <= 1 && std::strstr(gun.name, "Knife") == nullptr;
			drop.weapon_changed = changed;
			drop.hand_valid = points.hand_valid[1] && now - points.at < points_max_age_ms && points.has_floor;
			drop.gun_hand = { points.palm[1][0], points.palm[1][1], points.palm[1][2] };
			drop.grip = (input::latest().buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
			// The grip pulling a grenade pin, drawing from a holster or clicking the panel is not a release.
			// #49: a grip at the selected weapon's own holster takes that weapon (the claim is its own).
			drop.holster_take = holster_take;
			holster_take = false;
			drop.busy = grenade_in_hand() || (grip_claimed.load() && !drop.holster_take) || ui::vr_panel_view().open || reload_claims_free_hand();
			drop.floor_y = points.floor_y;
			// #38: the off hand on the handguard keeps the gun when the gun hand lets go.
			drop.off_holds = off_hand_holds_gun();
			drop.off_grip = (input::latest().buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
			drop.off_valid = points.hand_valid[0] && now - points.at < points_max_age_ms;
			drop.off_hand = { points.palm[0][0], points.palm[0][1], points.palm[0][2] };
			const float* o = points.hand_rotation[0];
			drop.off_yaw = std::atan2(2.0f * (o[0] * o[2] + o[1] * o[3]), 1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1]));
			drop.has_grip_point = points.has_support;
			drop.grip_point = { points.grip[0], points.grip[1], points.grip[2] };
			drop.sling = sling_on.load(std::memory_order_relaxed);
			drop.off_busy = grenade_in_hand() || reload_owns_off_hand() || off_grip_claimed.load() || ui::vr_panel_view().open;
			drop.sling_valid = body_rig_point(sling_point, drop.sling_at) && body_yaw(drop.sling_yaw);
			const float* q = points.hand_rotation[1];
			drop.hand_yaw = std::atan2(2.0f * (q[0] * q[2] + q[1] * q[3]), 1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
			dropper.set_release_seconds(free_hands_release.load(std::memory_order_relaxed));
			const free_hands_result dropped = dropper.update(drop);
			last_grip_point[0] = drop.grip_point.x;
			last_grip_point[1] = drop.grip_point.y;
			last_grip_point[2] = drop.grip_point.z;
			last_grip_in_reach = dropped.grip_in_reach;
			last_dropped_at[0] = dropped.position.x;
			last_dropped_at[1] = dropped.position.y;
			last_dropped_at[2] = dropped.position.z;
			last_slung = dropped.slung;
			if (dropped.event == free_hands_event::dropped)
			{
				++drops;
				input::haptic(1, input::pulses::tick);
			}
			else if (dropped.event == free_hands_event::picked_up || dropped.event == free_hands_event::taken_back)
			{
				++pickups;
				input::haptic(1, input::pulses::grab);
			}
			else if (dropped.event == free_hands_event::picked_up_off)
			{
				++pickups;
				input::haptic(0, input::pulses::grab);
			}
			else if (dropped.event == free_hands_event::handed_to_off)
			{
				++hand_overs;
				input::haptic(0, input::pulses::grab);
				input::haptic(1, input::pulses::tick);
			}
			dropped_now = dropped.dropped;
			off_hand_now = dropped.in_off_hand || dropped.in_off_grip;
			off_grip_now = dropped.in_off_grip;
			body::set_gun_in_off_hand(dropped.in_off_hand || dropped.in_off_grip, dropped.in_off_grip);
			const float lying[3]{ dropped.position.x, dropped.position.y, dropped.position.z };
			body::set_dropped_gun(dropped.dropped, lying, dropped.yaw, dropped.slung);
			if (dropped.dropped)
			{
				publish_grab_zone(grab_slot::dropped_gun, lying, dropped.slung ? dropper.config().sling_reach : dropper.config().pickup_reach, dropped.in_reach || dropped.off_in_reach);
			}
			else if (dropped.in_off_hand && drop.has_grip_point)
			{
				// The pistol grip, where the free gun hand takes the gun back.
				const float grip_at[3]{ drop.grip_point.x, drop.grip_point.y, drop.grip_point.z };
				publish_grab_zone(grab_slot::dropped_gun, grip_at, dropper.config().regrip_reach, dropped.grip_in_reach);
			}
			else
			{
				clear_grab_zone(grab_slot::dropped_gun);
			}
		}
		// #136: no holsters while the game has taken the weapons (its unarmed item): nothing to draw, no grip claimed.
		if (!enabled.load(std::memory_order_relaxed) || unarmed)
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
		{
			// The holsters hang from the level head, as the pouches (body_rig_point): looking down does not move them.
			const stereo::head_sample sample = stereo::latest_head_sample();
			const float* q = sample.eyes[0].orientation;
			frame.head = sample.valid ? level_head(head, { q[0], q[1], q[2], q[3] }) : head;
		}
		frame.head_yaw = head_yaw;
		frame.hand_valid = points.hand_valid[1] && now - points.at < points_max_age_ms;
		frame.gun_hand = { points.palm[1][0], points.palm[1][1], points.palm[1][2] };
		// While a grenade is in the off hand, the gun hand's grip pulls its pin, never draws.
		frame.gun_grip = (input::latest().buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0 && !grenade_in_hand();
		frame.left_handed = input::left_handed();
		frame.size = body_size_now.load(std::memory_order_relaxed);
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
		// #41: the knife at its holster (the game's own knife, once captured) while it is not the weapon; its
		// ring is left out while that is drawn.
		{
			constexpr int knife_zone = static_cast<int>(holster::knife) - static_cast<int>(holster::primary);
			const v3 model = slot_model_point(result.centres[knife_zone], zones.body_yaw(), slot_models_ahead());
			const float at[3]{ model.x, model.y, model.z };
			const bool shown = result.has_centres && !knife_out && zones.body_known();
			body::set_holster_knife(shown, at, zones.body_yaw());
		}
		if (grab_debug_enabled() && result.has_centres)
		{
			for (int i = 0; i < 5; ++i)
			{
				if (static_cast<int>(holster::primary) + i == static_cast<int>(holster::knife) && stereo::extra_mesh::item_drawn(stereo::extra_mesh::captured::knife))
				{
					clear_grab_zone(grab_slot::knife);
					continue;
				}
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
			const slot_state slot = read_slot();
			switch (draw_for(slot.valid && already(result.draw, slot), dropped_now.load() && last_slung))
			{
			case draw_action::switch_weapon:
				request(result.draw, now);
				break;
			case draw_action::take_into_hand:
				holster_take = true;
				last_result = "taken from its holster";
				log::write("holsters: %s taken from its holster (it hung on the sling)", holster_name(result.draw));
				break;
			case draw_action::none:
				// The selected weapon: in the hand, or (free hands) on the floor, picked up where it lies.
				last_result = dropped_now.load() ? "on the floor (pick it up there)" : "already in hand";
				break;
			}
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
		float yaw{};
		return body_rig_point(offset, point, yaw);
	}

	bool body_rig_point(const v3& offset, v3& point, float& yaw)
	{
		const stereo::head_sample head = stereo::latest_head_sample();
		if (!head.valid)
		{
			return false;
		}
		const float* l = head.eyes[0].position;
		const float* r = head.eyes[1].position;
		const float* q = head.eyes[0].orientation;
		const v3 centre = level_head({ 0.5f * (l[0] + r[0]), 0.5f * (l[1] + r[1]), 0.5f * (l[2] + r[2]) }, { q[0], q[1], q[2], q[3] });
		if (!body_yaw(yaw))
		{
			const float* o = head.eyes[0].orientation;
			yaw = std::atan2(2.0f * (o[0] * o[2] + o[1] * o[3]), 1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1]));
		}
		point = body_point(centre, yaw, offset, input::left_handed(), body_size_now.load(std::memory_order_relaxed));
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
		if (melee_down.load(std::memory_order_relaxed))
		{
			out.buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
			out.connected = true;
		}
		if (off_grip_now.load(std::memory_order_relaxed))
		{
			// #38: the off hand holds the pistol grip: its trigger fires (the latched controller value, not the
			// game's left trigger, which sight_aim may hold for ADS).
			out.right_trigger = input::latest().left_trigger;
		}
		else if (dropped_now.load(std::memory_order_relaxed) || off_hand_now.load(std::memory_order_relaxed))
		{
			// The gun lies on the floor, hangs on the sling or from the off hand (free hands, #38). #148: say so,
			// a held gun that does not fire looked broken.
			if (out.right_trigger >= 128)
			{
				ui::flash_notice(dropped_now.load(std::memory_order_relaxed) ? "Your hand is empty: grip the gun to fire"
					: "The off hand has the gun: grip its pistol grip to fire");
			}
			out.right_trigger = 0;
		}
		if (capture_active.load(std::memory_order_relaxed))
		{
			out.right_trigger = 0;
			out.left_trigger = 0;
		}
		return out;
	}

	void set_slot_models_ahead(float metres)
	{
		slot_models_ahead_m = std::isfinite(metres) ? std::fmax(0.0f, std::fmin(0.3f, metres)) : 0.0f;
	}

	float slot_models_ahead()
	{
		return slot_models_ahead_m.load(std::memory_order_relaxed);
	}

	void set_free_hands(bool on)
	{
		free_hands_on = on;
	}

	void set_free_hands_release(float seconds)
	{
		free_hands_release = seconds;
	}

	void set_sling(bool on)
	{
		sling_on = on;
	}

	void set_sling_show_primary(bool on)
	{
		sling_primary_on = on;
	}

	void set_slot_models_at_start(bool on)
	{
		slot_models_at_start = on;
	}

	bool gun_dropped()
	{
		// #136: no gun in the gun hand at all (the game's unarmed item) counts as a dropped one for every caller: no
		// two-handed hold, the grenade from either hand, no reload, no drawn shot line.
		return dropped_now.load(std::memory_order_relaxed) || unarmed_now.load(std::memory_order_relaxed);
	}

	bool gun_in_off_hand()
	{
		return off_hand_now.load(std::memory_order_relaxed);
	}

	bool gun_in_off_grip()
	{
		return off_grip_now.load(std::memory_order_relaxed);
	}

	bool action_aim_direction(float (&direction)[3])
	{
		if (grenade_throw_aim(direction))
		{
			return true; // #85: a grenade just thrown: the game's aim along it until the game has thrown it
		}
		const ULONGLONG now = GetTickCount64();
		const bool c4 = now < c4_aim_until.load(std::memory_order_relaxed);
		if (!c4 && !head_aim_now.load(std::memory_order_relaxed))
		{
			return false;
		}
		float q[4];
		if (c4)
		{
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			if (!hands.valid[0])
			{
				return false;
			}
			std::copy(std::begin(hands.aim[0].orientation), std::end(hands.aim[0].orientation), q);
		}
		else
		{
			const stereo::head_sample head = stereo::latest_head_sample();
			if (!head.valid)
			{
				return false;
			}
			const stereo::pose centre = stereo::eye_math::centre_pose(head);
			std::copy(std::begin(centre.orientation), std::end(centre.orientation), q);
		}
		// -z of the rotation (OpenXR: forward).
		direction[0] = -2.0f * (q[0] * q[2] + q[1] * q[3]);
		direction[1] = -2.0f * (q[1] * q[2] - q[0] * q[3]);
		direction[2] = -(1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
		return true;
	}

	void set_action_aim(bool on)
	{
		action_aim_on = on;
	}

	bool use_reach_direction(float (&direction)[3])
	{
		// Only while using (the grip squeezed in the reach pose, E held): with the hand merely in the pose the
		// game's aim, and so the walking direction, followed a resting off hand (headset 2026-11-10: pushing
		// the stick forward ran left, once the pose had widened for the stash).
		if (!use_reaching.load(std::memory_order_relaxed) || !use_down)
		{
			return false;
		}
		const stereo::head_sample head = stereo::latest_head_sample();
		const body::weapon_points points = body::latest_weapon_points();
		if (!head.valid || !points.hand_valid[0])
		{
			return false;
		}
		const float* l = head.eyes[0].position;
		const float* r = head.eyes[1].position;
		float d[3];
		float length = 0.0f;
		for (int axis = 0; axis < 3; ++axis)
		{
			d[axis] = points.palm[0][axis] - 0.5f * (l[axis] + r[axis]);
			length += d[axis] * d[axis];
		}
		length = std::sqrt(length);
		if (length < 0.1f)
		{
			return false;
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			direction[axis] = d[axis] / length;
		}
		return true;
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
			if (argument == "slingtest on" || argument == "slingtest off")
			{
				sling_test_show = argument == "slingtest on";
				return std::string("sling copy test: ") + (sling_test_show ? "shown with the primary in hand too" : "off");
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
				return "error: usage: holster [primary|secondary|knife|gadget1|gadget2|order|slingtest on|slingtest off]";
			}
			request(which, GetTickCount64());
		}
		const slot_state slot = read_slot();
		char text[1024];
		std::snprintf(text, sizeof(text),
			"holsters=%d slot=%d in_hand=%d weapon=%s zone=%s pending=%s switches=%llu failed=%llu last=%s body_yaw=%.2f\nsquad_signal=%d raised=%d orders=%llu\nmelee_gesture=%d strikes=%llu forward_speed=%.2f\n"
			"use_reach=%d reaching=%d using=%d uses=%llu palm_forward=%.2f side=%.2f up=%.2f\nfree_hands=%d dropped=%d unarmed=%d in_off_hand=%d in_off_grip=%d drops=%llu pickups=%llu hand_overs=%llu floor_y=%.2f grip=(%.3f,%.3f,%.3f) grip_in_reach=%d slung=%d at=(%.3f,%.3f,%.3f)",
			enabled ? 1 : 0, slot.current, in_hand_slot.load(), slot.name, holster_name(last_zone), holster_name(pending), static_cast<unsigned long long>(switches),
			static_cast<unsigned long long>(failed), last_result, zones.body_yaw(), signal_enabled ? 1 : 0, signal_raised ? 1 : 0,
			static_cast<unsigned long long>(orders), melee_enabled ? 1 : 0, static_cast<unsigned long long>(strikes), melee.forward_speed(),
			use_enabled ? 1 : 0, use_reaching ? 1 : 0, use_down ? 1 : 0, static_cast<unsigned long long>(uses), use_offset[0], use_offset[1], use_offset[2],
			free_hands_on ? 1 : 0, dropped_now ? 1 : 0, unarmed_now ? 1 : 0, off_hand_now ? 1 : 0, off_grip_now ? 1 : 0, static_cast<unsigned long long>(drops), static_cast<unsigned long long>(pickups), static_cast<unsigned long long>(hand_overs),
			body::latest_weapon_points().floor_y, last_grip_point[0], last_grip_point[1], last_grip_point[2], last_grip_in_reach ? 1 : 0, last_slung ? 1 : 0, last_dropped_at[0], last_dropped_at[1], last_dropped_at[2]);
		float aim_yaw{}, aim_pitch{};
		const bool aim_known = input::aim_angles(aim_yaw, aim_pitch);
		const ULONGLONG tick = GetTickCount64();
		char aim[160];
		std::snprintf(aim, sizeof(aim), "\naction_aim=%d head_aim=%d c4_aim=%d game_aim_yaw_deg=%.1f pitch_deg=%.1f%s", action_aim_on.load() ? 1 : 0,
			head_aim_now.load() ? 1 : 0, tick < c4_aim_until.load() ? 1 : 0, aim_yaw * 57.29578f, aim_pitch * 57.29578f, aim_known ? "" : " (unknown)");
		return std::string(text) + aim + "\ngrenade_capture stage=" + std::to_string(static_cast<int>(grenade_capture)) + " last=" + capture_result;
	}

	void request_grenade_capture()
	{
		grenade_capture_requested = true;
	}
}
