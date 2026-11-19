#include "weapon/physical_grenade.hpp"

#include "common/controlled.hpp"

#include <windows.h>
#include <Xinput.h>

#include <algorithm>
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
#include "input/haptics.hpp"
#include "stereo/extra_mesh.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/tracking.hpp"
#include "weapon/body_rig.hpp"
#include "weapon/game_reload_sounds.hpp"
#include "weapon/grab_debug.hpp"
#include "weapon/grenade_hands.hpp"
#include "weapon/reload_sounds.hpp"
#include "weapon/server_soldier.hpp"
#include "weapon/weapon_swap.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// BF4 179547 (re/notes/weapon.md "Grenades"). Client weapon slots as for the reload.
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_unlock = 0x4850;
		constexpr std::uintptr_t unlock_name = 0x20;
		constexpr std::uintptr_t weapon_data = 0x30;
		constexpr std::uintptr_t data_firing = 0x90;
		constexpr std::uintptr_t firing_primary = 0x10;
		constexpr std::uintptr_t server_soldier_weapons = 0x7c0;
		constexpr std::uintptr_t server_weapon_slots = 0x828;
		constexpr std::uintptr_t server_firing = 0x200;
		constexpr std::uintptr_t firing_magazine = 0x1a0;
		// FiringFunctionData: Shot (ShotConfigData) at +0x60, FireLogic.HoldAndRelease at +0x168.
		constexpr std::uintptr_t shot_position = 0x60;
		constexpr std::uintptr_t shot_direction = 0x70;
		constexpr std::uintptr_t shot_speed = 0x80;
		constexpr std::uintptr_t shot_projectile = 0xb0;
		constexpr std::uintptr_t shot_spawn_delay = 0xd0;
		constexpr std::uintptr_t shot_force_to_camera = 0xe5;
		constexpr std::uintptr_t hold_delay = 0x178;
		constexpr std::uintptr_t projectile_detonate_on_timeout = 0xd8;
		constexpr int max_slots = 16;
		constexpr ULONGLONG pulse_ms = 80;
		constexpr ULONGLONG restore_ms = 400;
		constexpr ULONGLONG safe_restore_ms = 6000;
		constexpr ULONGLONG points_max_age_ms = 250;
		constexpr float max_speed = 30.0f;

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

		bool is_grenade(const char* name)
		{
			for (const char* prefix : { "U_M67", "U_V40", "U_M34", "U_RGO", "U_Flashbang", "U_Smoke" })
			{
				if (std::strncmp(name, prefix, std::strlen(prefix)) == 0)
				{
					return true;
				}
			}
			return false;
		}

		struct grenade_weapon
		{
			bool valid{};
			char name[48]{};
			std::uintptr_t function{}; // FiringFunctionData (shared data: write only around our throw)
			int count{};               // ready + spare
		};

		char captured_for[48]{}; // the grenade whose draws stereo/extra_mesh holds (update thread)

		grenade_weapon find_grenade_weapon()
		{
			grenade_weapon found;
			std::uintptr_t soldier{}, weapons{}, begin{}, end{}, server_soldier{}, server_weapons{};
			if (!(soldier = game::local_soldier()) != 0 || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_begin, begin)
				|| !read(weapons + weapons_end, end) || !begin || end <= begin || (server_soldier = player_server_soldier()) == 0
				|| !read(server_soldier + server_soldier_weapons, server_weapons))
			{
				return found;
			}
			const int slots = static_cast<int>((end - begin) / 8);
			for (int slot = 0; slot < slots && slot < max_slots; ++slot)
			{
				std::uintptr_t client{}, unlock{}, name{};
				if (!read(begin + slot * 8, client) || !client || !read(client + weapon_unlock, unlock) || !read(unlock + unlock_name, name)
					|| !read(name, found.name))
				{
					continue;
				}
				found.name[sizeof(found.name) - 1] = 0;
				if (!is_grenade(found.name))
				{
					continue;
				}
				std::uintptr_t data{}, firing{}, server_weapon{}, server_ammo{};
				int ammo[2]{};
				if (!read(client + weapon_data, data) || !read(data + data_firing, firing) || !read(firing + firing_primary, found.function)
					|| !read(server_weapons + server_weapon_slots + slot * 8, server_weapon) || !read(server_weapon + server_firing, server_ammo)
					|| !read(server_ammo + firing_magazine, ammo))
				{
					continue;
				}
				found.count = (ammo[0] > 0 ? ammo[0] : 0) + (ammo[1] > 0 ? ammo[1] : 0);
				found.valid = found.function != 0;
				return found;
			}
			return found;
		}

		// The launch data of one throw, written into the shared FiringFunctionData and restored.
		struct launch_backup
		{
			std::uintptr_t function{};
			std::uintptr_t projectile{};
			float position[3]{}, direction[3]{}, speed[3]{};
			float spawn_delay{}, hold{};
			std::uint8_t force_to_camera{}, detonate{};
			bool safe{};
		};

		std::atomic<bool> physical{};
		std::atomic<float> throw_scale{ 1.5f };
		std::atomic<bool> holding{};
		std::atomic<int> holder{}; // #38: 0 = off hand, 1 = gun hand
		std::atomic<bool> armed{};
		std::atomic<bool> active{};
		std::atomic<bool> gun_busy{};
		std::atomic<bool> at_belt{};
		std::atomic<ULONGLONG> pulse_until{};

		std::mutex lock;
		grenade_hands hands;
		launch_backup backup;
		bool pending{};
		// #83: how long other modules ignore the quick throw's switch to the grenade. Measured: slot 6 from ~100 to ~450 ms
		// after the throw, longer than restore_ms, so this has its own window.
		constexpr ULONGLONG quick_throw_switch_ms = 1000;
		std::atomic<ULONGLONG> switch_window_until{};
		// #85: the game's aim along the throw until the game has thrown it (quick throw: up to ~450 ms).
		constexpr ULONGLONG throw_aim_ms = 700;
		std::atomic<bool> throw_aim_on{ true };
		std::atomic<ULONGLONG> throw_aim_until{};
		float throw_aim_direction[3]{ 0.0f, 0.0f, -1.0f }; // written under lock, read by grenade_throw_aim
		ULONGLONG restore_at{};
		LARGE_INTEGER last_update{};
		std::uint64_t throws{};
		const char* last_event{ "none" };
		float last_velocity[3]{};

		void restore(const launch_backup& b)
		{
			write(b.function + shot_position, b.position);
			write(b.function + shot_direction, b.direction);
			write(b.function + shot_speed, b.speed);
			write(b.function + shot_spawn_delay, b.spawn_delay);
			write(b.function + shot_force_to_camera, b.force_to_camera);
			write(b.function + hold_delay, b.hold);
			if (b.safe)
			{
				write(b.projectile + projectile_detonate_on_timeout, b.detonate);
			}
		}

		// Called with lock held. position and velocity: OpenXR axes relative to the first-person camera
		// (x right, y up, -z forward); the shot frame is x left, y up, z forward, pitched with the aim.
		bool launch(const grenade_weapon& weapon, const float (&position)[3], const float (&velocity)[3], bool safe)
		{
			if (pending || !weapon.valid)
			{
				return false;
			}
			launch_backup saved;
			saved.function = weapon.function;
			saved.safe = safe;
			if (!read(weapon.function + shot_position, saved.position) || !read(weapon.function + shot_direction, saved.direction)
				|| !read(weapon.function + shot_speed, saved.speed) || !read(weapon.function + shot_spawn_delay, saved.spawn_delay)
				|| !read(weapon.function + shot_force_to_camera, saved.force_to_camera) || !read(weapon.function + hold_delay, saved.hold)
				|| !read(weapon.function + shot_projectile, saved.projectile)
				|| (safe && !read(saved.projectile + projectile_detonate_on_timeout, saved.detonate)))
			{
				return false;
			}
			float speed[3] = { -velocity[0], velocity[1], -velocity[2] };
			const float length = std::sqrt(speed[0] * speed[0] + speed[1] * speed[1] + speed[2] * speed[2]);
			if (length > max_speed)
			{
				for (float& s : speed)
				{
					s *= max_speed / length;
				}
			}
			const float local_position[3] = { -position[0], position[1], -position[2] };
			const float forward[3] = { 0.0f, 0.0f, 1.0f };
			const float zero = 0.0f;
			const std::uint8_t off = 0;
			write(weapon.function + shot_position, local_position);
			write(weapon.function + shot_direction, forward);
			write(weapon.function + shot_speed, speed);
			write(weapon.function + shot_spawn_delay, zero);
			write(weapon.function + shot_force_to_camera, off);
			write(weapon.function + hold_delay, zero);
			if (safe)
			{
				write(saved.projectile + projectile_detonate_on_timeout, off);
			}
			backup = saved;
			pending = true;
			const ULONGLONG now = GetTickCount64();
			restore_at = now + (safe ? safe_restore_ms : restore_ms);
			pulse_until = now + pulse_ms;
			switch_window_until = now + quick_throw_switch_ms;
			++throws;
			last_velocity[0] = velocity[0];
			last_velocity[1] = velocity[1];
			last_velocity[2] = velocity[2];
			return true;
		}

		// A throw or drop from the hand (LOCAL position and velocity, already scaled). Called with lock held.
		void launch_from_hand(const grenade_weapon& weapon, bool dropped, weapon::v3 hand, weapon::v3 hand_velocity, ULONGLONG now, bool safe = false)
		{
			float position[3] = { hand.x, hand.y, hand.z };
			// A drop still needs a direction: a slow forward push.
			float velocity[3] = { hand_velocity.x, hand_velocity.y, dropped ? -0.3f : hand_velocity.z };
			// #85: in the game's frame: its aim turned along the throw, the start taken from the eyes.
			const stereo::head_sample head = stereo::latest_head_sample();
			if (throw_aim_on.load(std::memory_order_relaxed) && head.valid)
			{
				const stereo::pose eyes = stereo::eye_math::centre_pose(head);
				weapon::v3 motion{ velocity[0], velocity[1], velocity[2] };
				if (dropped)
				{
					// A drop: a slow push along the head's level forward.
					const float* o = eyes.orientation;
					const float ahead_x = -2.0f * (o[0] * o[2] + o[3] * o[1]);
					const float ahead_z = -(1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1]));
					const float level = std::sqrt(ahead_x * ahead_x + ahead_z * ahead_z);
					motion = level > 1e-3f ? weapon::v3{ 0.3f * ahead_x / level, 0.0f, 0.3f * ahead_z / level } : weapon::v3{ 0.0f, 0.0f, -0.3f };
				}
				const throw_in_frame t = throw_along_aim({ hand.x - eyes.position[0], hand.y - eyes.position[1], hand.z - eyes.position[2] }, motion);
				if (t.valid)
				{
					// launch() takes x right, y up, -z forward and turns them into the shot frame's x left, z forward.
					position[0] = -t.position.x;
					position[1] = t.position.y;
					position[2] = -t.position.z;
					velocity[0] = -t.speed.x;
					velocity[1] = t.speed.y;
					velocity[2] = -t.speed.z;
					throw_aim_direction[0] = t.aim.x;
					throw_aim_direction[1] = t.aim.y;
					throw_aim_direction[2] = t.aim.z;
					throw_aim_until = now + throw_aim_ms;
				}
			}
			if (!launch(weapon, position, velocity, safe))
			{
				log::write("grenade: %s not launched (no grenade weapon, or a throw still pending)", last_event);
				throw_aim_until = 0;
				return;
			}
			log::write("grenade: %s, %.2f m/s along (%.2f, %.2f, %.2f) LOCAL, start %.2f left %.2f up %.2f forward of the eyes", dropped ? "dropped" : "thrown",
				std::sqrt(velocity[0] * velocity[0] + velocity[1] * velocity[1] + velocity[2] * velocity[2]), throw_aim_direction[0], throw_aim_direction[1],
				throw_aim_direction[2], -position[0], position[1], -position[2]);
		}

		// The grenade pouch: 70 cm below the head, 10 cm ahead along the head's yaw.
		bool belt_point(weapon::v3& belt)
		{
			return weapon::body_rig_point(weapon::grenade_pouch, belt);
		}

		const char* event_name(grenade_event event)
		{
			switch (event)
			{
			case grenade_event::none: return "none";
			case grenade_event::taken: return "taken";
			case grenade_event::pin_pulled: return "pin_pulled";
			case grenade_event::thrown: return "thrown";
			case grenade_event::dropped: return "dropped";
			case grenade_event::put_back: return "put_back";
			}
			return "?";
		}
	}

	void set_physical_grenades(bool enabled)
	{
		physical = enabled;
	}

	void set_grenade_throw_scale(float scale)
	{
		throw_scale = scale;
	}

	bool grenade_in_hand()
	{
		return holding.load(std::memory_order_relaxed);
	}

	int grenade_hand()
	{
		return holder.load(std::memory_order_relaxed);
	}

	void set_grenade_throw_aim(bool on)
	{
		throw_aim_on = on;
	}

	bool grenade_throw_aim(float (&direction)[3])
	{
		if (GetTickCount64() >= throw_aim_until.load(std::memory_order_relaxed))
		{
			return false;
		}
		const std::lock_guard guard(lock);
		std::copy(std::begin(throw_aim_direction), std::end(throw_aim_direction), direction);
		return true;
	}

	bool grenade_throw_pending()
	{
		return GetTickCount64() < switch_window_until.load(std::memory_order_relaxed);
	}

	bool grenade_armed()
	{
		return armed.load(std::memory_order_relaxed);
	}

	bool off_hand_at_belt()
	{
		return at_belt.load(std::memory_order_relaxed);
	}

	void update_physical_grenades()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock);
		const ULONGLONG now = GetTickCount64();
		if (pending && now >= restore_at)
		{
			restore(backup);
			pending = false;
		}
		LARGE_INTEGER counter{}, frequency{};
		QueryPerformanceCounter(&counter);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_update.QuadPart ? std::fmin(static_cast<float>(counter.QuadPart - last_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last_update = counter;

		if (!physical.load(std::memory_order_relaxed))
		{
			hands.reset();
			holding = false;
			armed = false;
			at_belt = false;
			active = false;
			gun_busy = false;
			return;
		}
		const grenade_weapon weapon = find_grenade_weapon();
		const body::weapon_points points = body::latest_weapon_points();
		const input::gamepad_state pad = input::latest();
		grenade_frame frame;
		frame.seconds = seconds;
		frame.hands_valid = points.hand_valid[0] && points.hand_valid[1] && now - points.at < points_max_age_ms;
		// The palms (the controllers' grip points), where every grab zone is measured: the drawn wrists sit
		// ~8 cm behind and kept two hands apart around a held grenade (headset 2026-11-10: no pin pull).
		frame.off_hand = { points.palm[0][0], points.palm[0][1], points.palm[0][2] };
		frame.gun_hand = { points.palm[1][0], points.palm[1][1], points.palm[1][2] };
		if (!belt_point(frame.belt))
		{
			frame.belt = { 0.0f, -0.70f, -0.10f };
		}
		frame.off_grip = (pad.buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
		frame.gun_grip = (pad.buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
		frame.have_grenade = weapon.valid && weapon.count > 0;
		frame.either_hand = gun_dropped(); // #38: the gun on the floor or the sling frees the gun hand
		{
			// The mouth for the teeth pin pull: 8 cm below and 9 cm ahead of the eyes.
			const stereo::head_sample head = stereo::latest_head_sample();
			if (head.valid)
			{
				const stereo::pose centre = stereo::eye_math::centre_pose(head);
				const float* o = centre.orientation;
				const float x = o[0], y = o[1], z = o[2], w = o[3];
				const float down[3]{ -2.0f * (x * y - w * z), -(1.0f - 2.0f * (x * x + z * z)), -2.0f * (y * z + w * x) }; // -(head up)
				const float ahead[3]{ -2.0f * (x * z + w * y), -2.0f * (y * z - w * x), -(1.0f - 2.0f * (x * x + y * y)) };  // head -z
				frame.has_mouth = true;
				frame.mouth = { centre.position[0] + 0.08f * down[0] + 0.09f * ahead[0], centre.position[1] + 0.08f * down[1] + 0.09f * ahead[1],
					centre.position[2] + 0.08f * down[2] + 0.09f * ahead[2] };
			}
		}
		const grenade_result result = hands.update(frame);
		// #41: the game's own grenade stands at the pouch while none is held (once captured); the ring only
		// stands in until then (grab_debug: the pouch had no ring and could not be found, headset 2026-11-04).
		{
			float yaw{};
			weapon::v3 at;
			const bool shown = frame.hands_valid && frame.have_grenade && !result.holding && body_rig_point(weapon::grenade_pouch, at, yaw);
			at = weapon::slot_model_point(at, yaw, weapon::slot_models_ahead());
			const float position[3] = { at.x, at.y, at.z };
			body::set_pouch_grenade(shown, position, yaw);
		}
		const bool pouch_model = stereo::extra_mesh::grenade_drawn();
		if (frame.hands_valid && frame.have_grenade && !result.holding && !pouch_model)
		{
			const float pouch[3] = { frame.belt.x, frame.belt.y, frame.belt.z };
			publish_grab_zone(grab_slot::grenade, pouch, grenade_config{}.reach_belt, result.off_busy);
		}
		else
		{
			clear_grab_zone(grab_slot::grenade);
		}
		// The pin: a ring on the held grenade until it is out (the other hand grips there and pulls away; with
		// free hands a quick re-squeeze of the gun hand keeps the gun).
		if (frame.hands_valid && result.holding && !result.armed)
		{
			const v3 at = result.hand == 1 ? frame.gun_hand : frame.off_hand;
			const float pin[3] = { at.x, at.y, at.z };
			publish_grab_zone(grab_slot::grenade_pin, pin, grenade_config{}.reach_pin, result.hand == 1 ? result.off_busy : result.gun_busy);
		}
		else
		{
			clear_grab_zone(grab_slot::grenade_pin);
		}
		if (result.event != grenade_event::none)
		{
			last_event = event_name(result.event);
		}
		// The grenade is in result.hand (the off hand, or the free gun hand, #38); the other pulls the pin.
		const int held_by = result.hand;
		const int pin_hand = 1 - held_by;
		if (result.event == grenade_event::taken && weapon.valid
			&& (!stereo::extra_mesh::grenade_captured() || (captured_for[0] && std::strcmp(captured_for, weapon.name) != 0)))
		{
			// The game's own grenade for the hand (#36): once, and again for another kind of grenade. One captured at
			// level start (#41, slot capture) counts: capturing again meant 4 s with the game's grenade selected, when a
			// trigger threw it (headset 2026-11-14).
			std::memcpy(captured_for, weapon.name, sizeof(captured_for));
			request_grenade_capture();
		}
		else if (result.event == grenade_event::taken && weapon.valid && !captured_for[0])
		{
			std::memcpy(captured_for, weapon.name, sizeof(captured_for)); // the level start's capture was of this one
		}
		if (result.event == grenade_event::taken || result.event == grenade_event::put_back)
		{
			// The gun's own pouch foley, as the magazine pouch uses; the synthesised rustle sounded wrong
			// (headset 2026-11-04 for magazines, 2026-11-10 for grenades). A gun whose set has no pouch foley (the
			// shotguns, the UTAS) plays the game's weapon-switch cloth where tools/game_cues.py installed it, else nothing.
			if (!play_game_reload_sound(reload_event::magazine_taken) && has_file(cue::cloth))
			{
				play(cue::cloth);
			}
			input::haptic(held_by, result.event == grenade_event::taken ? input::pulses::grab : input::pulses::tick);
		}
		if (result.bite_now)
		{
			input::haptic(held_by, input::pulses::tick);
		}
		if (result.event == grenade_event::pin_pulled)
		{
			play(cue::pin);
			input::haptic(pin_hand, input::pulses::grab);
			input::haptic(held_by, input::pulses::tick);
		}
		else if (result.event == grenade_event::thrown)
		{
			input::haptic(held_by, input::pulses::strike);
		}
		if (result.event == grenade_event::thrown || result.event == grenade_event::dropped)
		{
			const float scale = result.event == grenade_event::thrown ? throw_scale.load(std::memory_order_relaxed) : 0.0f;
			launch_from_hand(weapon, result.event == grenade_event::dropped, result.position,
				{ result.velocity.x * scale, result.velocity.y * scale, result.velocity.z * scale }, now);
		}
		holding = result.holding;
		holder = result.hand;
		body::set_gun_hand_fist(result.holding && result.hand == 1);
		body::set_held_grenade(result.holding, result.hand, result.armed);
		armed = result.armed;
		gun_busy = result.gun_busy;
		// off_busy without a grenade in hand: the hand is at the belt.
		at_belt = result.off_busy && !result.holding && frame.have_grenade && !frame.off_grip;
		active = weapon.valid;
#endif
	}

	input::gamepad_state filter_grenade_pad(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (active.load(std::memory_order_relaxed))
		{
			// The game's quick throw is ours: only the pulse of a physical throw reaches it.
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
			if (gun_busy.load(std::memory_order_relaxed))
			{
				out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_RIGHT_SHOULDER);
			}
		}
		return out;
	}

	input::gamepad_state add_grenade_pulse(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (GetTickCount64() < pulse_until.load(std::memory_order_relaxed))
		{
			out.buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
			out.connected = true; // also without a headset session (pipe tests)
		}
		return out;
	}

	std::string grenade_pipe_command(const std::string& argument)
	{
		const std::lock_guard guard(lock);
		const grenade_weapon weapon = find_grenade_weapon();
		if (argument.rfind("throwhand", 0) == 0)
		{
			// #85: a throw through the physical path (the game's aim along it, start from the eyes) with a LOCAL velocity from
			// the off hand's palm, as if the hand had let go.
			if (!config::research_enabled())
			{
				return "error: 'grenade throwhand' needs research = 1 in bfvr.ini";
			}
			float vx{}, vy{}, vz{};
			char safe_word[8]{};
			if (sscanf_s(argument.c_str() + 9, "%f %f %f %7s", &vx, &vy, &vz, safe_word, static_cast<unsigned>(sizeof(safe_word))) < 3)
			{
				return "error: usage: grenade throwhand <vx> <vy> <vz> [safe]  (m/s, OpenXR LOCAL: x right, y up, z back)";
			}
			const body::weapon_points points = body::latest_weapon_points();
			const weapon::v3 hand = points.hand_valid[0] ? weapon::v3{ points.palm[0][0], points.palm[0][1], points.palm[0][2] } : weapon::v3{ -0.2f, 1.2f, -0.3f };
			last_event = "thrown (pipe, hand path)";
			launch_from_hand(weapon, false, hand, { vx, vy, vz }, GetTickCount64(), std::strcmp(safe_word, "safe") == 0);
			return std::string("thrown through the hand path, hand ") + (points.hand_valid[0] ? "tracked" : "not tracked (placeholder position)");
		}
				if (argument == "pinsound")
		{
			play(cue::pin); // #82: the pin pull's sound alone, for recordings
			return "pin sound played";
		}
		if (argument == "capture")
		{
			request_grenade_capture();
			return "grenade capture requested (pipe holster shows its stage, extramesh the result)";
		}
		if (argument.rfind("throw", 0) == 0)
		{
			if (!config::research_enabled())
			{
				return "error: 'grenade throw' needs research = 1 in bfvr.ini";
			}
			float yaw = 0.0f, pitch = 0.0f, speed = 0.0f;
			char safe_word[8]{};
			if (sscanf_s(argument.c_str() + 5, "%f %f %f %7s", &yaw, &pitch, &speed, safe_word, static_cast<unsigned>(sizeof(safe_word))) < 3)
			{
				return "error: usage: grenade throw <yaw right deg> <pitch up deg> <m/s> [safe]";
			}
			const body::weapon_points points = body::latest_weapon_points();
			float position[3] = { -0.2f, -0.2f, -0.3f };
			if (points.hand_valid[0])
			{
				std::memcpy(position, points.hand[0], sizeof(position));
			}
			constexpr float radians = 3.14159265f / 180.0f;
			const float velocity[3] = { speed * std::sin(yaw * radians) * std::cos(pitch * radians), speed * std::sin(pitch * radians),
				-speed * std::cos(yaw * radians) * std::cos(pitch * radians) };
			if (!launch(weapon, position, velocity, std::strcmp(safe_word, "safe") == 0))
			{
				return "error: no grenade weapon, or a throw still pending";
			}
			last_event = "thrown (pipe)";
		}
		char text[512];
		std::snprintf(text, sizeof(text),
			"grenade=%s weapon=%s count=%d holding=%d hand=%s pending=%d throws=%llu last_event=%s last_velocity=(%.2f,%.2f,%.2f) scale=%.2f",
			physical ? "physical" : "button", weapon.valid ? weapon.name : "-", weapon.count, holding ? 1 : 0, holder.load() == 1 ? "gun" : "off", pending ? 1 : 0,
			static_cast<unsigned long long>(throws), last_event, last_velocity[0], last_velocity[1], last_velocity[2], throw_scale.load());
		std::string out = text;
		if (weapon::v3 belt; belt_point(belt))
		{
			char line[96];
			std::snprintf(line, sizeof(line), " belt=(%.3f,%.3f,%.3f) armed=%d", belt.x, belt.y, belt.z, armed ? 1 : 0);
			out += line;
		}
		return out;
	}
}
