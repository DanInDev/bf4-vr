#include "input/vr_controls.hpp"

#include <windows.h>
#include <xinput.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <mutex>

#include "common/config.hpp"
#include "common/log.hpp"
#include "input/move_steer.hpp"
#include "input/right_stick.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::input
{
	namespace
	{
		std::mutex direction_lock;
		bool direction_valid{};
		float shot_direction[3]{};
	}

	void set_aim_direction(bool valid, const float (&direction)[3])
	{
		const std::lock_guard guard(direction_lock);
		direction_valid = valid;
		std::copy(std::begin(direction), std::end(direction), shot_direction);
	}

	bool aim_direction(float (&direction)[3])
	{
		const std::lock_guard guard(direction_lock);
		std::copy(std::begin(shot_direction), std::end(shot_direction), direction);
		return direction_valid;
	}

	namespace
	{
		std::mutex line_lock;
		bool line_valid{};
		float line_start[3]{};
		float line_direction[3]{};
		ULONGLONG line_at{};
	}

	void set_shot_line(bool valid, const float (&start)[3], const float (&direction)[3])
	{
		const std::lock_guard guard(line_lock);
		line_valid = valid;
		std::copy(std::begin(start), std::end(start), line_start);
		std::copy(std::begin(direction), std::end(direction), line_direction);
		line_at = GetTickCount64();
	}

	bool shot_line(float (&start)[3], float (&direction)[3])
	{
		const std::lock_guard guard(line_lock);
		std::copy(std::begin(line_start), std::end(line_start), start);
		std::copy(std::begin(line_direction), std::end(line_direction), direction);
		return line_valid && GetTickCount64() - line_at < 250;
	}

#if defined(_M_X64)
	namespace
	{
		// BF4 179547 (re/notes/aim.md, re/symbols/bf4-179547.md).
		constexpr std::uintptr_t local_soldier_global = 0x14230F260;
		constexpr std::size_t soldier_weapons = 0x570;
		constexpr std::size_t soldier_stance = 0x4F0; // u32: 0 stand, 1 crouch, 2 prone (tools/pose_probe.py)
		constexpr std::size_t weapons_flags = 0x28;
		constexpr std::size_t weapons_index_a = 0x8D8;
		constexpr std::size_t weapons_index_b = 0x8E0;
		constexpr std::size_t weapons_begin = 0x890;
		constexpr std::size_t weapons_end = 0x898;
		constexpr std::size_t weapons_fallback_aiming = 0x8F8;
		constexpr std::size_t weapon_aiming = 0x4988;
		constexpr std::size_t aiming_aimer = 0x10;
		constexpr std::size_t aimer_yaw = 0x14;
		constexpr std::size_t aimer_pitch = 0x18;
		// The render path's aim prediction (0x140FBA340): yaw/pitch at +0x60/+0x64, frame time at +0x68.
		// While that time is positive and the game has seen pad input, the next aimer update
		// (0x140FB9610) starts from the prediction, not from +0x14/+0x18 (re/notes/aim.md).
		constexpr std::size_t aimer_predicted = 0x60 - aimer_yaw;
		constexpr std::size_t aimer_predicted_time = 0x68;

		// Button presses for a stance change (pad crouch = B: tap toggles crouch, hold goes prone;
		// A = jump, stands up). Verified with tools/pose_probe.py 2026-10-26.
		constexpr ULONGLONG tap_ms = 120;
		constexpr ULONGLONG hold_ms = 900;

		std::atomic<bool> stick_turns{};
		std::atomic<bool> grips_to_game{};
		std::atomic<bool> ads_trigger{};
		std::atomic<bool> item_trigger{};
		std::atomic<bool> sight_aim{}; // an eye behind the gun's sight: the game aims down it (LT held)
		std::atomic<bool> game_aiming_now{}; // the game got its aim button (LT) this poll
		std::atomic<bool> x_menu{ true };
		std::mutex lock;
		right_stick stick;
		LARGE_INTEGER last_poll{};
		std::uint16_t pulse_buttons{};
		ULONGLONG pulse_until{};
		// Physical stance: the stance to reach (-1 = none) and until when to keep trying.
		std::atomic<int> physical_target{ -1 };
		ULONGLONG physical_until{};
		constexpr ULONGLONG stance_settle_ms = 350;
		std::atomic<float> pending_yaw{};
		std::atomic<bool> pitch_neutral{};
		// Controller aim (#12): the view turns with our own body yaw (camera yaw convention:
		// atan2(look.x, look.z)), the aimer follows the controller.
		std::atomic<bool> aim_by_controller{};
		bool body_yaw_known{};
		float body_yaw{};
		std::atomic<std::uint64_t> turns{};
		std::atomic<move_reference> move_from{ move_reference::head };
		std::atomic<float> move_rotation{};
		// #52: sprint and vault toward the view (XInput thread writes, game thread reads).
		std::atomic<bool> sprint_steer_on{ true };
		move_steer steer;
		LARGE_INTEGER steer_last{};
		std::atomic<bool> steering{};
		std::atomic<float> steering_yaw{};

		// Yaw of a pose's forward (-z) in LOCAL, positive to the left; 0 when it points straight up/down.
		float yaw_left(const stereo::pose& p)
		{
			const float* q = p.orientation;
			const float x = -2 * (q[0] * q[2] + q[1] * q[3]);
			const float z = -(1 - 2 * (q[0] * q[0] + q[1] * q[1]));
			return x * x + z * z > 1e-6f ? std::atan2(-x, -z) : 0.0f;
		}
		std::atomic<std::uint64_t> stance_commands{};

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), nullptr);
		}

		// Sets the aimer's yaw (aimer_yaw) or pitch (aimer_pitch), and the pending prediction the next
		// update would start from, so the game keeps the value.
		void write_aim(std::uintptr_t aimer, std::size_t angle, float value)
		{
			WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(aimer + angle), &value, sizeof(value), nullptr);
			float predicted_time{};
			if (read(aimer + aimer_predicted_time, predicted_time) && predicted_time > 0.0f)
			{
				WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(aimer + angle + aimer_predicted), &value, sizeof(value), nullptr);
			}
		}

		std::uintptr_t local_soldier()
		{
			std::uintptr_t soldier{};
			return read(local_soldier_global, soldier) ? soldier : 0;
		}

		// The local soldier's weapons component and current ClientSoldierWeapon (0 when unreadable).
		std::uintptr_t current_soldier_weapon(std::uintptr_t& weapons)
		{
			weapons = 0;
			const std::uintptr_t soldier = local_soldier();
			if (!read(soldier + soldier_weapons, weapons) || !weapons)
			{
				weapons = 0;
				return 0;
			}
			std::uint32_t flags{};
			read(weapons + weapons_flags, flags);
			std::uintptr_t index_holder{};
			std::uint32_t index{};
			std::uintptr_t begin{}, end{};
			std::uintptr_t weapon{};
			if (read(weapons + ((flags & 4) ? weapons_index_b : weapons_index_a), index_holder) && read(index_holder + 0x8, index)
				&& read(weapons + weapons_begin, begin) && read(weapons + weapons_end, end) && begin && index < (end - begin) / 8
				&& read(begin + index * 8, weapon))
			{
				return weapon;
			}
			return 0;
		}

		// The aimer of the current weapon (or the fallback aiming), 0 when the chain is not readable.
		std::uintptr_t current_aimer()
		{
			std::uintptr_t weapons{};
			const std::uintptr_t weapon = current_soldier_weapon(weapons);
			if (!weapons)
			{
				return 0;
			}
			std::uintptr_t aiming{};
			if (weapon)
			{
				read(weapon + weapon_aiming, aiming);
			}
			if (!aiming)
			{
				read(weapons + weapons_fallback_aiming, aiming);
			}
			std::uintptr_t aimer{};
			return aiming && read(aiming + aiming_aimer, aimer) ? aimer : 0;
		}

		void start_stance_change(stance_command command)
		{
			std::uint32_t current{};
			if (!read(local_soldier() + soldier_stance, current) || current > 2)
			{
				return;
			}
			std::uint16_t buttons{};
			ULONGLONG duration{};
			if (command == stance_command::down)
			{
				buttons = XINPUT_GAMEPAD_B;
				duration = current == 0 ? tap_ms : hold_ms; // stand -> crouch, crouch -> prone
				if (current == 2)
				{
					return;
				}
			}
			else
			{
				if (current == 0)
				{
					return;
				}
				buttons = current == 2 ? XINPUT_GAMEPAD_B : XINPUT_GAMEPAD_A; // prone -> crouch, crouch -> stand
				duration = tap_ms;
			}
			pulse_buttons = buttons;
			pulse_until = GetTickCount64() + duration;
			++stance_commands;
		}
	}

	void configure_turn(const std::string& turn, int snap_degrees, bool stance_flicks)
	{
		right_stick_config config;
		config.turn = turn == "smooth" ? turn_mode::smooth : turn == "off" ? turn_mode::off : turn_mode::snap;
		if (snap_degrees > 0 && snap_degrees <= 90)
		{
			config.snap_angle = static_cast<float>(snap_degrees) * 3.14159265f / 180.0f;
		}
		config.stance_flicks = stance_flicks;
		const std::lock_guard guard(lock);
		stick.configure(config);
	}

	void install_vr_controls()
	{
		configure_turn(config::get("turn", "snap"), std::atoi(config::get("snap_turn_degrees", "30").c_str()), config::get_bool("stance_flicks", true));
		stick_turns = config::get("right_stick", "turn") == "turn";
		log::write("controls: right_stick=%s", stick_turns.load() ? "turn" : "look");
	}

	gamepad_state for_game(const gamepad_state& raw)
	{
		// The grips hold the gun and drive the mod's gestures (reload, grenades, holsters, signals);
		// as LB/RB they would throw grenades and fire. The left trigger (ADS) zooms the view, which in
		// VR changes the field of view under the player. Both stay out of the game unless enabled.
		gamepad_state pad = raw;
		if (!grips_to_game.load(std::memory_order_relaxed))
		{
			pad.buttons &= static_cast<std::uint16_t>(~(XINPUT_GAMEPAD_LEFT_SHOULDER | XINPUT_GAMEPAD_RIGHT_SHOULDER));
		}
		if (!ads_trigger.load(std::memory_order_relaxed) && !item_trigger.load(std::memory_order_relaxed))
		{
			pad.left_trigger = 0;
		}
		// Left X opens the game's menu (Start); the reload is the right B (weapon/physical_reload).
		if (x_menu.load(std::memory_order_relaxed) && (pad.buttons & XINPUT_GAMEPAD_X))
		{
			pad.buttons = static_cast<std::uint16_t>((pad.buttons & ~XINPUT_GAMEPAD_X) | XINPUT_GAMEPAD_START);
		}
		const move_reference reference = move_from.load(std::memory_order_relaxed);
		float angle = 0.0f;
		bool steered = false;
		if (reference != move_reference::body)
		{
			const stereo::head_sample head = stereo::latest_head_sample();
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			float body{};
			if (reference == move_reference::head && head.valid && sprint_steer_on.load(std::memory_order_relaxed) && body_yaw_now(body))
			{
				// #52: the stick turned from the head into the soldier's frame (its aimer), and the soldier
				// asked to face the move direction while sprinting or vaulting.
				LARGE_INTEGER counter{}, frequency{};
				QueryPerformanceCounter(&counter);
				QueryPerformanceFrequency(&frequency);
				steer_input in;
				in.seconds = steer_last.QuadPart ? std::fmin(static_cast<float>(counter.QuadPart - steer_last.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
				steer_last = counter;
				in.x = pad.left_x / 32767.0f;
				in.y = pad.left_y / 32767.0f;
				in.head_yaw = body + yaw_left(head.eyes[0]);
				float aimer_yaw_now{}, aimer_pitch_now{};
				in.soldier_yaw = aim_angles(aimer_yaw_now, aimer_pitch_now) ? -aimer_yaw_now : body; // camera yaw = -aimer yaw
				in.sprint_click = (pad.buttons & XINPUT_GAMEPAD_LEFT_THUMB) != 0;
				in.jump = (pad.buttons & XINPUT_GAMEPAD_A) != 0;
				in.fire = pad.right_trigger > 128;
				const steer_output out = steer.update(in);
				const auto axis = [](float v) { return static_cast<std::int16_t>(std::lround(std::fmax(-1.0f, std::fmin(1.0f, v)) * 32767.0f)); };
				pad.left_x = axis(out.x);
				pad.left_y = axis(out.y);
				steering_yaw = out.yaw;
				if (steering.exchange(out.steer) != out.steer)
				{
					log::write("sprint steer: %s (yaw %.1f deg, %s)", out.steer ? "on" : "off", out.yaw * 57.29578f, out.sprinting ? "sprint" : out.steer ? "vault" : "-");
				}
				move_rotation = wrap_angle(in.head_yaw - in.soldier_yaw);
				steered = true;
			}
			else if (reference == move_reference::head && head.valid)
			{
				angle = yaw_left(head.eyes[0]);
			}
			else if (reference == move_reference::gun && hands.valid[1])
			{
				angle = yaw_left(hands.aim[1]);
			}
		}
		if (!steered)
		{
			move_rotation = angle;
			if (steering.exchange(false))
			{
				steer.reset();
				log::write("sprint steer: off (not steering)");
			}
		}
		if (angle != 0.0f)
		{
			// Stick x right, y forward: turning the frame left by angle turns the stick vector the same way.
			const float x = pad.left_x / 32767.0f, y = pad.left_y / 32767.0f;
			const float c = std::cos(angle), s = std::sin(angle);
			const auto axis = [](float v) { return static_cast<std::int16_t>(std::lround(std::fmax(-1.0f, std::fmin(1.0f, v)) * 32767.0f)); };
			pad.left_x = axis(x * c - y * s);
			pad.left_y = axis(y * c + x * s);
		}
		// Physical stance (request_stance): one press at a time towards the stance the body asked for,
		// each after the previous one has finished and the game had time to take it.
		if (physical_target.load(std::memory_order_relaxed) >= 0)
		{
			const std::lock_guard guard(lock);
			const ULONGLONG tick = GetTickCount64();
			std::uint32_t current{ 99 };
			if (tick >= physical_until || !read(local_soldier() + soldier_stance, current) || current == static_cast<std::uint32_t>(physical_target.load()))
			{
				physical_target = -1;
			}
			else if (tick >= pulse_until + stance_settle_ms)
			{
				start_stance_change(static_cast<int>(current) < physical_target.load() ? stance_command::down : stance_command::up);
			}
			if (tick < pulse_until)
			{
				pad.buttons |= pulse_buttons;
				pad.connected = true; // the game ignores a pad that is not connected (no controllers yet)
			}
		}
		// With controller aim the stick cannot aim (the aimer follows the controller every frame), so
		// it turns even in look mode; otherwise it would do nothing at all.
		if (!stick_turns.load(std::memory_order_relaxed) && !aim_by_controller.load(std::memory_order_relaxed))
		{
			return pad;
		}
		gamepad_state out = pad;
		const std::lock_guard guard(lock);
		LARGE_INTEGER now{}, frequency{};
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_poll.QuadPart ? static_cast<float>(now.QuadPart - last_poll.QuadPart) / static_cast<float>(frequency.QuadPart) : 0.0f;
		last_poll = now;
		const right_stick_output result = stick.update(pad.right_x / 32767.0f, pad.right_y / 32767.0f, std::fmin(seconds, 0.1f));
		if (result.turn != 0.0f)
		{
			pending_yaw = pending_yaw.load() + result.turn;
			++turns;
		}
		if (result.stance != stance_command::none)
		{
			start_stance_change(result.stance);
		}
		out.right_x = 0;
		out.right_y = 0;
		if (GetTickCount64() < pulse_until)
		{
			out.buttons |= pulse_buttons;
		}
		return out;
	}

	void apply_view_turn()
	{
		const float turn = pending_yaw.exchange(0.0f);
		const bool level = pitch_neutral.load(std::memory_order_relaxed);
		if (turn == 0.0f && !level)
		{
			return;
		}
		const bool controller = aim_by_controller.load(std::memory_order_relaxed);
		if (turn != 0.0f && controller)
		{
			body_yaw -= turn; // turning right lowers the camera yaw (camera yaw = -aimer yaw); needs no aimer
		}
		const std::uintptr_t aimer = current_aimer();
		float yaw{};
		if (!read(aimer + aimer_yaw, yaw) || !std::isfinite(yaw))
		{
			return; // scripted sections without an aimer: a head-aim turn is dropped
		}
		if (turn != 0.0f && !controller)
		{
			write_aim(aimer, aimer_yaw, yaw + turn);
		}
		if (level)
		{
			// neutral_pitch: the head alone pitches the view, the horizon stays level (#5).
			write_aim(aimer, aimer_pitch, 0.0f);
		}
	}


	void set_controller_aim(bool enabled)
	{
		if (!enabled)
		{
			body_yaw_known = false;
		}
		aim_by_controller = enabled;
	}

	bool controller_aim_active()
	{
		return aim_by_controller.load(std::memory_order_relaxed);
	}

	float body_camera_yaw(float game_yaw)
	{
		if (!body_yaw_known)
		{
			body_yaw = game_yaw;
			body_yaw_known = true;
		}
		return body_yaw;
	}

	bool body_yaw_now(float& yaw)
	{
		yaw = body_yaw;
		return aim_by_controller.load(std::memory_order_relaxed) && body_yaw_known && std::isfinite(yaw);
	}

	std::uintptr_t current_client_weapon()
	{
		// ClientSoldierWeapon +0x49A8: its ClientWeapon (vtable checked live, re/notes/weapon.md).
		constexpr std::size_t soldier_weapon_client = 0x49A8;
		std::uintptr_t weapons{};
		const std::uintptr_t weapon = current_soldier_weapon(weapons);
		std::uintptr_t client{};
		return weapon && read(weapon + soldier_weapon_client, client) ? client : 0;
	}

	void aim_along(const float (&direction)[3])
	{
		const float length = std::sqrt(direction[0] * direction[0] + direction[1] * direction[1] + direction[2] * direction[2]);
		const std::uintptr_t aimer = current_aimer();
		float yaw{};
		if (length < 1e-6f || !read(aimer + aimer_yaw, yaw) || !std::isfinite(yaw))
		{
			return;
		}
		// Camera yaw = -aimer yaw (re/notes/aim.md); keep the aimer continuous across the wrap.
		const float target = -std::atan2(direction[0], direction[2]);
		float delta = std::fmod(target - yaw, 6.2831853f);
		delta += delta > 3.14159265f ? -6.2831853f : delta < -3.14159265f ? 6.2831853f : 0.0f;
		yaw += delta;
		const float pitch = std::asin(std::fmax(-1.0f, std::fmin(1.0f, direction[1] / length)));
		write_aim(aimer, aimer_yaw, yaw);
		write_aim(aimer, aimer_pitch, pitch);
	}

	void request_stance(int stance)
	{
		const std::lock_guard guard(lock);
		physical_until = GetTickCount64() + 3000;
		physical_target = stance >= 0 && stance <= 2 ? stance : -1;
	}

	bool stance_press_active()
	{
		const std::lock_guard guard(lock);
		return GetTickCount64() < pulse_until;
	}

	int game_stance()
	{
		std::uint32_t stance{ 99 };
		return read(local_soldier() + soldier_stance, stance) && stance <= 2 ? static_cast<int>(stance) : -1;
	}

	bool aim_angles(float& yaw, float& pitch)
	{
		const std::uintptr_t aimer = current_aimer();
		return aimer && read(aimer + aimer_yaw, yaw) && read(aimer + aimer_pitch, pitch) && std::isfinite(yaw) && std::isfinite(pitch);
	}

	bool soldier_health(float& health)
	{
		// ClientSoldierEntity +0x140 BFClientSoldierHealthComponent, +0x20 health (re/symbols/bf4-179547.md).
		std::uintptr_t component{};
		return read(local_soldier() + 0x140, component) && component && read(component + 0x20, health) && std::isfinite(health);
	}

	void set_sprint_steer(bool enabled)
	{
		sprint_steer_on = enabled;
	}

	bool sprint_steer_yaw(float& yaw)
	{
		yaw = steering_yaw.load(std::memory_order_relaxed);
		return sprint_steer_on.load(std::memory_order_relaxed) && steering.load(std::memory_order_relaxed) && std::isfinite(yaw);
	}

	void set_move_reference(move_reference reference)
	{
		move_from = reference;
	}

	void set_neutral_pitch(bool enabled)
	{
		pitch_neutral = enabled;
	}

	void set_game_buttons(bool grips, bool ads, bool x_opens_menu)
	{
		grips_to_game = grips;
		ads_trigger = ads;
		x_menu = x_opens_menu;
	}

	void set_item_left_trigger(bool passes)
	{
		item_trigger = passes;
	}

	void set_sight_aim(bool aiming)
	{
		sight_aim = aiming;
	}

	gamepad_state with_sight_aim(const gamepad_state& pad)
	{
		gamepad_state out = pad;
		if (sight_aim.load(std::memory_order_relaxed))
		{
			out.left_trigger = 255;
			out.connected = true;
		}
		return out;
	}

	void note_game_pad(const gamepad_state& pad)
	{
		game_aiming_now = !item_trigger.load(std::memory_order_relaxed) && pad.left_trigger > 128;
	}

	bool game_aiming()
	{
		return game_aiming_now.load(std::memory_order_relaxed);
	}

	std::string set_right_stick_mode(const std::string& mode)
	{
		if (mode == "turn" || mode == "look")
		{
			stick_turns = mode == "turn";
		}
		std::uint32_t stance{ 99 };
		read(local_soldier() + soldier_stance, stance);
		float aim[2]{ NAN, NAN };
		if (const std::uintptr_t aimer = current_aimer())
		{
			read(aimer + aimer_yaw, aim);
		}
		char angles[64];
		std::snprintf(angles, sizeof(angles), " aimer_yaw=%.1f aimer_pitch=%.1f", aim[0] * 57.29578f, aim[1] * 57.29578f);
		return std::string("right_stick=") + (stick_turns.load() ? "turn" : "look") + (aim_by_controller.load() ? " aim=controller (stick turns)" : "") + " move_rotation=" + std::to_string(move_rotation.load() * 57.29578f) + " turns=" + std::to_string(turns.load())
			+ " stance_commands=" + std::to_string(stance_commands.load()) + " stance=" + std::to_string(stance) + angles;
	}
#else
	// BF3 (x86) has no mapped aimer or stance: the right stick stays a look stick.
	void set_sprint_steer(bool) {}
	bool sprint_steer_yaw(float&) { return false; }
	void set_game_buttons(bool, bool, bool) {}
	void set_item_left_trigger(bool) {}
	void set_sight_aim(bool) {}
	gamepad_state with_sight_aim(const gamepad_state& pad) { return pad; }
	void note_game_pad(const gamepad_state&) {}
	bool game_aiming() { return false; }
	void install_vr_controls() {}
	void configure_turn(const std::string&, int, bool) {}
	void set_neutral_pitch(bool) {}
	void set_move_reference(move_reference) {}
	void set_controller_aim(bool) {}
	bool controller_aim_active() { return false; }
	float body_camera_yaw(float game_yaw) { return game_yaw; }
	bool body_yaw_now(float&) { return false; }
	std::uintptr_t current_client_weapon() { return 0; }
	void aim_along(const float (&)[3]) {}
	bool aim_angles(float&, float&) { return false; }
	void request_stance(int) {}
	int game_stance() { return -1; }
	bool stance_press_active() { return false; }
	bool soldier_health(float&) { return false; }
	gamepad_state for_game(const gamepad_state& pad) { return pad; }
	void apply_view_turn() {}
	std::string set_right_stick_mode(const std::string&) { return "right_stick=look (x64 only)"; }
#endif
}
