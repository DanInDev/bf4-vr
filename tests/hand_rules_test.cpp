// Checks which hand pose rule applies (src/body/hand_rules, #68).
#include <cstdio>

#include "body/hand_rules.hpp"

using namespace bfvr::body::hand_rules;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}
}

int main()
{
	// hand, carrier, gun_hand_carries, item, neutral, has_grip
	expect(gun_hand_controller_grip(1, 0, false, false, true, true), "the off hand carries the gun: the gun hand takes the controller grip");
	expect(gun_hand_controller_grip(1, 1, true, true, true, true), "a grenade or the C4 detonator in the gun hand: the controller grip");
	expect(!gun_hand_controller_grip(1, 1, true, false, true, true), "a gun (or a launcher such as the M320) in the gun hand: its own hold");
	expect(!gun_hand_controller_grip(1, 1, false, true, true, true), "the item flag alone, without the gun hand carrying the weapon bone: no change");
	expect(!gun_hand_controller_grip(0, 0, false, false, true, true), "never the off hand");
	expect(!gun_hand_controller_grip(1, 0, false, false, false, true), "gun_hand_pose animated: the game's hold");
	expect(!gun_hand_controller_grip(1, 0, false, false, true, false), "no grip measured yet: the game's hold");
	if (failures == 0)
	{
		std::printf("hand_rules: all tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}
