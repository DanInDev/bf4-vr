#pragma once

#include <string>

#include "input/gamepad.hpp"

// Two-handed weapon in the game (issue #29, two_handed = 1): the off hand grabs the handguard
// (grip press there) and the gun then points along the line between the hands (weapon/two_hand).
// The drawn gun (arm IK) and, with aim = controller, the shots use that rotation; shots converge on
// the barrel line at aim_convergence metres, because bullets leave the game camera, not the gun.
namespace bfvr::weapon
{
	void set_two_handed(bool enabled);
	void set_aim_convergence(float metres);
	// Present thread, once per frame, after update_physical_reload and update_physical_grenades.
	void update_two_handed();
	// XInput hook thread: the off-hand grip (pad LB, the game's grenade) while it holds the gun.
	input::gamepad_state filter_two_hand_pad(const input::gamepad_state& pad);
	// Pipe "twohand": state, weight, barrel and shot directions, grab count.
	std::string two_hand_report();
}
