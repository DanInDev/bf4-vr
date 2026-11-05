// Free hands (#35): the gun drops when the grip stays open, lies on the floor below the hand, and is
// picked up again by gripping near it; regrips, busy grips, weapon changes and the setting are safe.

#include <cmath>
#include <cstdio>

#include "weapon/free_hands.hpp"

using namespace bfvr::weapon;

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

	bool near(float a, float b, float tolerance = 1e-4f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	free_hands_frame holding()
	{
		free_hands_frame f;
		f.seconds = 0.05f;
		f.enabled = true;
		f.gun_in_hand = true;
		f.hand_valid = true;
		f.gun_hand = { 0.2f, -0.3f, -0.3f };
		f.grip = true;
		f.floor_y = -1.6f;
		f.hand_yaw = 0.3f;
		return f;
	}

	// Steps until the gun drops with the grip open; -1 when it does not within two seconds.
	int steps_to_drop(free_hands& hands, free_hands_frame f)
	{
		f.grip = false;
		for (int i = 0; i < 40; ++i)
		{
			if (hands.update(f).event == free_hands_event::dropped)
			{
				return i + 1;
			}
		}
		return -1;
	}

	void test_drop_and_pick_up()
	{
		free_hands hands;
		free_hands_frame f = holding();
		expect(!hands.update(f).dropped, "held while gripping");
		const int steps = steps_to_drop(hands, f);
		expect(steps == 7, "drops after 0.35 s with the grip open (7 steps of 50 ms)");

		f.grip = false;
		free_hands_result r = hands.update(f);
		expect(r.dropped && r.block_fire, "dropped: the trigger is blocked");
		expect(near(r.position.x, 0.2f) && near(r.position.z, -0.3f) && near(r.position.y, -1.6f + hands.config().lie_height),
			"lies on the floor below where the hand was");
		expect(near(r.yaw, 0.3f), "its barrel along the hand's yaw");
		expect(!r.in_reach, "the hand at the chest is out of reach of the floor");

		// Gripping far away does nothing.
		f.grip = true;
		r = hands.update(f);
		expect(r.dropped && r.event == free_hands_event::none, "a grip far from the gun does not pick it up");
		f.grip = false;
		hands.update(f);

		// Reach down and grip.
		f.gun_hand = { 0.25f, -1.45f, -0.25f };
		r = hands.update(f);
		expect(r.dropped && r.in_reach, "the palm 17 cm from the dropped gun is in reach");
		f.grip = true;
		r = hands.update(f);
		expect(r.event == free_hands_event::picked_up && !r.dropped && !r.block_fire, "grip in reach picks it up");
		f.gun_hand = { 0.2f, -0.3f, -0.3f };
		expect(!hands.update(f).dropped, "held again while gripping");
	}

	void test_regrip_and_busy()
	{
		free_hands hands;
		free_hands_frame f = holding();
		hands.update(f);
		f.grip = false;
		for (int i = 0; i < 5; ++i)
		{
			hands.update(f);
		}
		f.grip = true;
		expect(!hands.update(f).dropped, "a regrip within 0.35 s keeps the gun");
		expect(steps_to_drop(hands, f) == 7, "the timer starts over after a regrip");

		free_hands busy_hands;
		f = holding();
		f.busy = true;
		f.grip = false;
		for (int i = 0; i < 40; ++i)
		{
			expect(!busy_hands.update(f).dropped, "never drops while the grip is busy (holster, pin, panel)");
		}
		f = holding();
		f.hand_valid = false;
		f.grip = false;
		free_hands lost;
		for (int i = 0; i < 40; ++i)
		{
			expect(!lost.update(f).dropped, "never drops while the hand is not tracked");
		}
	}

	void test_reset_cases()
	{
		free_hands hands;
		free_hands_frame f = holding();
		steps_to_drop(hands, f);
		f.grip = false;
		expect(hands.update(f).dropped, "dropped");
		f.weapon_changed = true;
		expect(!hands.update(f).dropped, "a new weapon in hand ends the drop");
		f.weapon_changed = false;
		f.grip = true;
		expect(!hands.update(f).dropped, "and it is held");

		steps_to_drop(hands, f);
		f.grip = false;
		f.enabled = false;
		expect(!hands.update(f).dropped, "switching the setting off puts the gun back in hand");
		f.enabled = true;
		f.gun_in_hand = false;
		for (int i = 0; i < 40; ++i)
		{
			expect(!hands.update(f).dropped, "a gadget or the knife never drops");
		}

		// The drop's own frame does not also pick it up, even with the hand on the floor.
		free_hands low;
		f = holding();
		f.gun_hand = { 0.0f, -1.5f, 0.0f };
		low.update(f);
		f.grip = false;
		free_hands_result r{};
		for (int i = 0; i < 7; ++i)
		{
			r = low.update(f);
		}
		expect(r.event == free_hands_event::dropped && r.dropped, "drops even with the hand already low");
	}
}

int main()
{
	test_drop_and_pick_up();
	test_regrip_and_busy();
	test_reset_cases();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("free_hands: all checks passed\n");
	return 0;
}
