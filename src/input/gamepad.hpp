#pragma once

#include <cstdint>

namespace bfvr::input
{
	// An Xbox-style pad built from the VR controllers. Written by the XR frame loop,
	// read by the XInput hook on the game's input thread.
	struct gamepad_state
	{
		bool connected{};
		std::uint16_t buttons{}; // XINPUT_GAMEPAD_* bits
		std::uint8_t left_trigger{};
		std::uint8_t right_trigger{};
		std::int16_t left_x{};
		std::int16_t left_y{};
		std::int16_t right_x{};
		std::int16_t right_y{};
	};

	void publish(const gamepad_state& state);
	gamepad_state latest();
}
