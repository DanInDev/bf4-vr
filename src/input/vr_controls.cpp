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

		// Button presses for a stance change (pad crouch = B: tap toggles crouch, hold goes prone;
		// A = jump, stands up). Verified with tools/pose_probe.py 2026-10-26.
		constexpr ULONGLONG tap_ms = 120;
		constexpr ULONGLONG hold_ms = 900;

		std::atomic<bool> stick_turns{};
		std::atomic<bool> grips_to_game{};
		std::atomic<bool> ads_trigger{};
		std::atomic<bool> x_menu{ true };
		std::mutex lock;
		right_stick stick;
		LARGE_INTEGER last_poll{};
		std::uint16_t pulse_buttons{};
		ULONGLONG pulse_until{};
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

		std::uintptr_t local_soldier()
		{
			std::uintptr_t soldier{};
			return read(local_soldier_global, soldier) ? soldier : 0;
		}

		// The aimer of the current weapon (or the fallback aiming), 0 when the chain is not readable.
		std::uintptr_t current_aimer()
		{
			const std::uintptr_t soldier = local_soldier();
			std::uintptr_t weapons{};
			if (!read(soldier + soldier_weapons, weapons) || !weapons)
			{
				return 0;
			}
			std::uint32_t flags{};
			read(weapons + weapons_flags, flags);
			std::uintptr_t index_holder{};
			std::uint32_t index{};
			std::uintptr_t begin{}, end{};
			std::uintptr_t aiming{};
			if (read(weapons + ((flags & 4) ? weapons_index_b : weapons_index_a), index_holder) && read(index_holder + 0x8, index)
				&& read(weapons + weapons_begin, begin) && read(weapons + weapons_end, end) && begin && index < (end - begin) / 8)
			{
				std::uintptr_t weapon{};
				if (read(begin + index * 8, weapon) && weapon)
				{
					read(weapon + weapon_aiming, aiming);
				}
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
		if (!ads_trigger.load(std::memory_order_relaxed))
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
		if (reference != move_reference::body)
		{
			const stereo::head_sample head = stereo::latest_head_sample();
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			if (reference == move_reference::head && head.valid)
			{
				angle = yaw_left(head.eyes[0]);
			}
			else if (reference == move_reference::gun && hands.valid[1])
			{
				angle = yaw_left(hands.aim[1]);
			}
		}
		move_rotation = angle;
		if (angle != 0.0f)
		{
			// Stick x right, y forward: turning the frame left by angle turns the stick vector the same way.
			const float x = pad.left_x / 32767.0f, y = pad.left_y / 32767.0f;
			const float c = std::cos(angle), s = std::sin(angle);
			const auto axis = [](float v) { return static_cast<std::int16_t>(std::lround(std::fmax(-1.0f, std::fmin(1.0f, v)) * 32767.0f)); };
			pad.left_x = axis(x * c - y * s);
			pad.left_y = axis(y * c + x * s);
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
			yaw += turn;
			WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(aimer + aimer_yaw), &yaw, sizeof(yaw), nullptr);
		}
		if (level)
		{
			// neutral_pitch: the head alone pitches the view, the horizon stays level (#5).
			const float zero = 0.0f;
			WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(aimer + aimer_pitch), &zero, sizeof(zero), nullptr);
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
		WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(aimer + aimer_yaw), &yaw, sizeof(yaw), nullptr);
		WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(aimer + aimer_pitch), &pitch, sizeof(pitch), nullptr);
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
	void set_game_buttons(bool, bool, bool) {}
	void install_vr_controls() {}
	void configure_turn(const std::string&, int, bool) {}
	void set_neutral_pitch(bool) {}
	void set_move_reference(move_reference) {}
	void set_controller_aim(bool) {}
	bool controller_aim_active() { return false; }
	float body_camera_yaw(float game_yaw) { return game_yaw; }
	void aim_along(const float (&)[3]) {}
	gamepad_state for_game(const gamepad_state& pad) { return pad; }
	void apply_view_turn() {}
	std::string set_right_stick_mode(const std::string&) { return "right_stick=look (x64 only)"; }
#endif
}
