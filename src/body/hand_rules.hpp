#pragma once

// Which hand pose rule applies, engine-free (body/arm_pose takes the result). Tested by tests/hand_rules_test.cpp.
namespace bfvr::body::hand_rules
{
	// #68: the gun hand (hand 1) takes the gun grip as you hold the controller (gun_hand_pose neutral) while the off
	// hand carries the gun (carrier 0), or while it holds the hand grenade or the C4 detonator (item, with the gun
	// hand carrying the weapon bone). Needs the grip measured once (has_grip).
	inline bool gun_hand_controller_grip(int hand, int carrier, bool gun_hand_carries, bool item, bool neutral, bool has_grip)
	{
		return hand == 1 && neutral && has_grip && (carrier == 0 || (gun_hand_carries && item));
	}
}
