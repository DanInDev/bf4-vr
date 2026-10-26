#include "input/vr_controls.hpp"

#include <windows.h>
#include <xinput.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <mutex>

#include "common/config.hpp"
#include "common/log.hpp"
#include "input/right_stick.hpp"

namespace bfvr::input
{
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

		// Button presses for a stance change (pad crouch = B: tap toggles crouch, hold goes prone;
		// A = jump, stands up). Verified with tools/pose_probe.py 2026-10-26.
		constexpr ULONGLONG tap_ms = 120;
		constexpr ULONGLONG hold_ms = 900;

		std::atomic<bool> stick_turns{};
		std::mutex lock;
		right_stick stick;
		LARGE_INTEGER last_poll{};
		std::uint16_t pulse_buttons{};
		ULONGLONG pulse_until{};
		std::atomic<float> pending_yaw{};
		std::atomic<std::uint64_t> turns{};
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

	void install_vr_controls()
	{
		right_stick_config config;
		const std::string turn = config::get("turn", "snap");
		config.turn = turn == "smooth" ? turn_mode::smooth : turn == "off" ? turn_mode::off : turn_mode::snap;
		const float degrees = static_cast<float>(std::atof(config::get("snap_turn_degrees", "30").c_str()));
		if (degrees > 0.0f && degrees <= 90.0f)
		{
			config.snap_angle = degrees * 3.14159265f / 180.0f;
		}
		config.stance_flicks = config::get_bool("stance_flicks", true);
		stick.configure(config);
		stick_turns = config::get("right_stick", "look") == "turn";
		log::write("controls: right_stick=%s turn=%s snap=%.0f deg stance_flicks=%d", stick_turns.load() ? "turn" : "look", turn.c_str(),
			degrees, config.stance_flicks ? 1 : 0);
	}

	gamepad_state for_game(const gamepad_state& pad)
	{
		if (!stick_turns.load(std::memory_order_relaxed))
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
		if (turn == 0.0f)
		{
			return;
		}
		const std::uintptr_t aimer = current_aimer();
		float yaw{};
		if (!read(aimer + aimer_yaw, yaw) || !std::isfinite(yaw))
		{
			return; // scripted sections without an aimer: the turn is dropped
		}
		yaw += turn;
		WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(aimer + aimer_yaw), &yaw, sizeof(yaw), nullptr);
	}

	std::string set_right_stick_mode(const std::string& mode)
	{
		if (mode == "turn" || mode == "look")
		{
			stick_turns = mode == "turn";
		}
		std::uint32_t stance{ 99 };
		read(local_soldier() + soldier_stance, stance);
		return std::string("right_stick=") + (stick_turns.load() ? "turn" : "look") + " turns=" + std::to_string(turns.load())
			+ " stance_commands=" + std::to_string(stance_commands.load()) + " stance=" + std::to_string(stance);
	}
#else
	// BF3 (x86) has no mapped aimer or stance: the right stick stays a look stick.
	void install_vr_controls() {}
	gamepad_state for_game(const gamepad_state& pad) { return pad; }
	void apply_view_turn() {}
	std::string set_right_stick_mode(const std::string&) { return "right_stick=look (x64 only)"; }
#endif
}
