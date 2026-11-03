#pragma once

#include <string>

#include "input/gamepad.hpp"

// Two-handed weapon in the game (issue #29, two_handed = 1): the off hand grabs the handguard
// (grip press there) and the gun then points along the line between the hands (weapon/two_hand).
// The drawn gun (arm IK) and, with aim = controller, the shots use that rotation; shots converge on
// the barrel line at aim_convergence metres, because bullets leave the game camera, not the gun.
// While holding, the off-hand trigger is the game's accessory toggle (accessory_toggle = trigger).
namespace bfvr::weapon
{
	void set_two_handed(bool enabled);
	void set_aim_convergence(float metres);
	// shot_line: aim along the gun as drawn (true: the optic, else Wep_Root, along its barrel) or the
	// controller's aim ray (false). Bullets and the reticle follow it.
	void set_shot_from_gun(bool gun);
	// two_hand_mode: lock (barrel on the line between the hands) or soft (the hand line steers).
	void set_two_hand_lock(bool lock);
	// accessory_toggle = trigger: off-hand trigger while holding the gun = D-pad up (laser, light, magnifier).
	void set_accessory_trigger(bool enabled);
	// Present thread, once per frame, after update_physical_reload and update_physical_grenades.
	void update_two_handed();
	// XInput hook thread: the off-hand grip (pad LB, the game's grenade) and trigger (LT, zoom) while it
	// holds the gun, plus the accessory toggle's D-pad up tap.
	input::gamepad_state filter_two_hand_pad(const input::gamepad_state& pad);
	// Where the shots converge (OpenXR LOCAL): the gun hand's grip plus aim_convergence metres down the
	// drawn barrel; false without a tracked gun hand. For the aim reticle.
	bool aim_point(float (&point)[3]);
	// The off hand holds the gun or is on its handguard (its grip is the two-handed grab's).
	bool off_hand_on_gun();

	// Pipe "twohand": state, weight, barrel and shot directions, grab count.
	std::string two_hand_report();

	// sight_ads: holding the gun's sight to an eye aims down it in the game (two_hand eye_behind_sight).
	void set_sight_ads(bool on);
}
