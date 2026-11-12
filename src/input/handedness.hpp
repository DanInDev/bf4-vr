#pragma once

#include "input/gamepad.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::input
{
	// Left-handed play (#9, dominant_hand = left): the controllers swap roles. The pad is mirrored
	// (sticks, triggers, grips; A/B with X/Y and the stick clicks before the controller mapping, by
	// input::mirror_held; Start stays on the left menu button, the right one belongs to the runtime)
	// and hand index 1 becomes the dominant hand, so
	// everything that means "gun hand" (arm IK, controller aim, ammo gesture, pointer default)
	// follows the left controller and the off-hand features (minimap gesture) the right one.
	void set_left_handed(bool enabled);
	bool left_handed();

	gamepad_state mirrored(const gamepad_state& pad);
	stereo::hand_sample swapped(const stereo::hand_sample& hands);
}
