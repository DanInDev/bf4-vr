#pragma once

#include <string>

#include "input/gamepad.hpp"

// Physical grenades in the game (docs/tasks/grenades.md, grenade = physical): gestures from
// weapon/grenade_hands; the throw is the game's own quick throw (pad LB) with its launch data
// rewritten for that one throw (re/notes/weapon.md "Grenades"): start at the hand, the hand's
// velocity, no spawn or hold delay. BF4 build 179547, x64 only.
namespace bfvr::weapon
{
	void set_physical_grenades(bool enabled);
	void set_grenade_throw_scale(float scale);

	// Present thread, once per frame, after update_physical_reload.
	void update_physical_grenades();

	// A grenade is in the off hand: the reload and accessory gestures leave the grip alone.
	bool grenade_in_hand();

	// XInput hook thread: LB is ours in physical mode (our throw pulse only); the gun-hand grip
	// while it holds the pin.
	input::gamepad_state filter_grenade_pad(const input::gamepad_state& pad);

	// Pipe "grenade": state. "grenade throw <yaw> <pitch> <speed> [safe]" (research): a throw from
	// the off hand (or 20 cm left-low-ahead without tracking), yaw right / pitch up in degrees from
	// the view, m/s; safe = the grenade does not explode (for tests indoors).
	std::string grenade_pipe_command(const std::string& argument);
}
