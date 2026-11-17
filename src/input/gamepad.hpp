#pragma once

#include <cstdint>
#include <string>

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

	// The published pad goes through the release gate (input/release_gate, #72): what latest() returns.
	void publish(const gamepad_state& state);
	gamepad_state latest();

	// The XR loop: false while a system menu has the controllers or their stream froze.
	void set_controllers_ours(bool ours);
	void set_focus_release(bool enabled); // setting focus_release
	// Pipe fakefocus lost|back|off: simulates the runtime taking the controllers (monitor test of #72).
	std::string fake_focus(const std::string& argument);
	std::string focus_release_report();
	// #73: a death: every trigger, grip, button and stick held now waits for a release (setting death_release).
	void close_held_inputs();
}
