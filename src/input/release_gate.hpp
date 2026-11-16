#pragma once

#include <cstdint>

#include "input/gamepad.hpp"

// #72: a system menu (the SteamVR dashboard, the Quest menu through Steam Link) takes the controllers. Presses
// made there must not reach the game, and a release the runtime swallowed must not leave a trigger or grip
// held: while the runtime has the controllers the pad is neutral, and afterwards every input that still reads
// pressed stays closed until it has been seen released once. Engine-free; tested by tests/release_gate_test.cpp.
namespace bfvr::input
{
	constexpr std::uint8_t gate_trigger_released = 30; // of 255: a resting finger reads more on the Quest, but the latch has already cut it
	constexpr int gate_stick_released = 8000;          // of 32767: inside the game's own dead zone

	class release_gate
	{
	public:
		// controllers_ours: the session is FOCUSED and the controller stream is alive.
		gamepad_state apply(const gamepad_state& raw, bool controllers_ours);

		// Inputs still closed after the controllers came back.
		bool holding() const;
		std::uint16_t closed_buttons() const { return buttons_; }
		bool closed_left_trigger() const { return left_trigger_; }
		bool closed_right_trigger() const { return right_trigger_; }
		bool closed_left_stick() const { return left_stick_; }
		bool closed_right_stick() const { return right_stick_; }
		std::uint32_t times_closed() const { return closures_; }

	private:
		std::uint16_t buttons_{};
		bool left_trigger_{};
		bool right_trigger_{};
		bool left_stick_{};
		bool right_stick_{};
		bool was_ours_{ true };
		std::uint32_t closures_{};
	};

	// A controller stream that stopped: both controllers report the same pose, bit for bit, for `after` seconds.
	// Real tracking never repeats a float exactly frame after frame; Steam Link repeats the last sample while the
	// Quest menu is open (the session stays FOCUSED, so the runtime never says so).
	class freeze_watch
	{
	public:
		explicit freeze_watch(float after = 0.5f) : after_(after) {}
		// tracked: at least one controller reports a pose; changed: any of the poses differs from the last call.
		bool update(bool tracked, bool changed, float seconds);
		bool frozen() const { return frozen_; }

	private:
		float after_;
		float still_{};
		bool frozen_{};
	};
}
