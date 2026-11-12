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

// Empty hands (#38): the off hand on the handguard keeps the gun when the gun hand lets go; the gun
// hand takes it back at the pistol grip; the off hand letting go drops it.
void test_hand_over()
{
	free_hands hands;
	free_hands_frame f = holding();
	f.off_valid = true;
	f.off_holds = true;
	f.off_grip = true;
	f.off_hand = { -0.1f, -0.25f, -0.6f };
	f.off_yaw = 0.2f;
	f.has_grip_point = true;
	f.grip_point = { 0.2f, -0.3f, -0.3f };
	hands.update(f);
	f.grip = false;
	free_hands_result r;
	int steps = 0;
	for (; steps < 40 && r.event != free_hands_event::handed_to_off; ++steps)
	{
		r = hands.update(f);
	}
	expect(r.event == free_hands_event::handed_to_off && r.in_off_hand && r.block_fire && !r.dropped, "the gun hand lets go: the off hand keeps the gun");
	expect(steps * 0.05f + 1e-3f >= hands.config().release_seconds, "only after the release time (a quick regrip is free)");
	// The free gun hand goes off to rack, grips there: no take-back away from the pistol grip.
	f.gun_hand = { 0.25f, -0.15f, -0.55f };
	f.grip = true;
	r = hands.update(f);
	expect(r.in_off_hand && r.event == free_hands_event::none && !r.grip_in_reach, "gripping away from the pistol grip (racking) keeps it in the off hand");
	f.grip = false;
	hands.update(f);
	// Back to the pistol grip and grip: taken back.
	f.gun_hand = { 0.22f, -0.3f, -0.3f };
	r = hands.update(f);
	expect(r.grip_in_reach && r.in_off_hand, "at the pistol grip it is in reach");
	f.grip = true;
	r = hands.update(f);
	expect(r.event == free_hands_event::taken_back && !r.in_off_hand && !r.block_fire, "gripping at the pistol grip takes it back");
	// Handed over again, then the off hand lets go: it drops below the off hand.
	f.grip = false;
	for (int i = 0; i < 40 && !r.in_off_hand; ++i)
	{
		r = hands.update(f);
	}
	expect(r.in_off_hand, "handed over again");
	f.off_grip = false;
	f.off_holds = false;
	for (int i = 0; i < 40 && r.event != free_hands_event::dropped; ++i)
	{
		r = hands.update(f);
	}
	expect(r.event == free_hands_event::dropped && r.dropped && near(r.position.x, -0.1f) && near(r.position.z, -0.6f) && near(r.yaw, 0.2f),
		"the off hand letting go drops it below the off hand");
	// Without the off hand on the handguard, letting go drops it as before.
	free_hands plain;
	free_hands_frame g = holding();
	g.off_valid = true;
	g.off_grip = true; // gripping, but not on the handguard
	expect(steps_to_drop(plain, g) > 0, "an off hand gripping elsewhere does not take the gun");
	// Lost off-hand tracking never drops it.
	free_hands lost;
	free_hands_frame h = f;
	h.off_holds = true;
	h.off_grip = true;
	lost.update(h);
	h.grip = false;
	for (int i = 0; i < 40; ++i)
	{
		r = lost.update(h);
	}
	h.off_valid = false;
	h.off_grip = false;
	for (int i = 0; i < 40; ++i)
	{
		r = lost.update(h);
	}
	expect(r.in_off_hand && !r.dropped, "lost off-hand tracking keeps it in the off hand");
}

// The sling (#38): a gun let go with no other hand on it hangs at the sling point, follows the body,
// and is grabbed back there.
void test_sling()
{
	free_hands hands;
	free_hands_frame f = holding();
	f.sling = true;
	f.sling_valid = true;
	f.sling_at = { 0.1f, -0.5f, -0.12f };
	f.sling_yaw = 0.0f;
	hands.update(f);
	f.grip = false;
	free_hands_result r;
	for (int i = 0; i < 40 && r.event != free_hands_event::dropped; ++i)
	{
		r = hands.update(f);
	}
	expect(r.dropped && r.slung && near(r.position.y, -0.5f) && near(r.position.x, 0.1f), "let go: it hangs on the sling, not on the floor");
	f.sling_at = { 0.6f, -0.4f, 0.3f }; // the player walked and turned
	f.sling_yaw = 1.0f;
	r = hands.update(f);
	expect(r.slung && near(r.position.x, 0.6f) && near(r.position.z, 0.3f) && near(r.yaw, 1.0f), "the slung gun follows the body");
	f.gun_hand = { 0.6f, -0.18f, 0.3f }; // 22 cm above it (where the grenade pouch sits): not in reach
	r = hands.update(f);
	expect(!r.in_reach, "the grenade pouch above the sling is not the sling");
	f.gun_hand = { 0.62f, -0.38f, 0.28f };
	r = hands.update(f);
	expect(r.in_reach, "the hand at the sling is in reach");
	f.grip = true;
	r = hands.update(f);
	expect(r.event == free_hands_event::picked_up && !r.dropped && !r.slung, "gripping at the sling takes it");
	// Sling off: the floor as before.
	free_hands floor;
	free_hands_frame g = holding();
	g.sling = false;
	g.sling_valid = true;
	g.sling_at = { 0.1f, -0.5f, -0.12f };
	floor.update(g);
	g.grip = false;
	for (int i = 0; i < 40 && !r.dropped; ++i)
	{
		r = floor.update(g);
	}
	r = floor.update(g);
	expect(r.dropped && !r.slung && near(r.position.y, g.floor_y + floor.config().lie_height), "sling off: on the floor");
}

// #49: with the hand empty (the selected gun on the sling or the floor), gripping at that gun's own holster
// takes it into the hand there and then, wherever the gun hangs; held, it stays.
void test_holster_take()
{
	free_hands hands;
	free_hands_frame f = holding();
	f.sling = true;
	f.sling_valid = true;
	f.sling_at = { 0.1f, -0.5f, -0.12f };
	hands.update(f);
	f.grip = false;
	free_hands_result r;
	for (int i = 0; i < 40 && r.event != free_hands_event::dropped; ++i)
	{
		r = hands.update(f);
	}
	expect(r.dropped && r.slung, "let go: on the sling");
	f.gun_hand = { 0.2f, 0.05f, 0.15f }; // over the shoulder, far from the sling
	f.grip = true;
	r = hands.update(f);
	expect(r.dropped && r.event == free_hands_event::none, "a grip far from the sling alone does not take it");
	f.grip = false;
	hands.update(f);
	f.grip = true;
	f.holster_take = true;
	r = hands.update(f);
	expect(r.event == free_hands_event::picked_up && !r.dropped && !r.slung && !r.block_fire, "a grip at its holster takes it into the hand");
	f.holster_take = false;
	f.gun_hand = { 0.2f, -0.3f, -0.3f }; // pulled out in front, grip held
	for (int i = 0; i < 20; ++i)
	{
		r = hands.update(f);
	}
	expect(!r.dropped && r.event == free_hands_event::none, "held while the grip stays closed");
	// Letting go after the take puts it back on the sling, as any held gun.
	expect(steps_to_drop(hands, f) > 0 && hands.update(f).slung, "let go after the take: back on the sling");
	// A busy grip (a grenade pin, the panel) never takes it.
	f.grip = true;
	f.holster_take = true;
	f.busy = true;
	expect(hands.update(f).dropped, "a busy grip does not take it");
	// Only off the sling: a gun lying on the floor (sling off) stays there; it is picked up where it lies.
	free_hands floor;
	free_hands_frame g = holding();
	floor.update(g);
	g.grip = false;
	for (int i = 0; i < 40 && !floor.update(g).dropped; ++i)
	{
	}
	g.grip = true;
	g.holster_take = true;
	r = floor.update(g);
	expect(r.dropped && r.event == free_hands_event::none, "a gun on the floor is not recalled by its holster");
}

// #49: drawing another weapon in one held grab. The grip is pressed at the holster (claimed there: busy),
// the hand leaves the zone with the grip still closed, and the game's switch lands ~0.5 s later.
void test_held_through_switch()
{
	for (int pass = 0; pass < 2; ++pass)
	{
		const bool start_empty = pass == 1;
		free_hands hands;
		free_hands_frame f = holding();
		hands.update(f);
		if (start_empty)
		{
			steps_to_drop(hands, f); // the hand was empty: the old gun on the floor
		}
		f.grip = false;
		hands.update(f);
		f.grip = true;
		f.busy = true; // at the holster: the press belongs to the draw
		for (int i = 0; i < 4; ++i)
		{
			hands.update(f);
		}
		f.busy = false; // out of the zone, grip still held, the switch not yet done
		for (int i = 0; i < 6; ++i)
		{
			hands.update(f);
		}
		f.weapon_changed = true;
		free_hands_result r = hands.update(f);
		f.weapon_changed = false;
		for (int i = 0; i < 20; ++i)
		{
			r = hands.update(f);
		}
		expect(!r.dropped && r.event == free_hands_event::none,
			start_empty ? "empty hand: the drawn weapon stays while the grip is held" : "gun in hand: the drawn weapon stays while the grip is held");
	}
}

// The off hand picks the dropped gun up by its pistol grip (#38): it holds and fires it; letting go drops
// it again, and the gun hand can pick it up from there.
void test_off_hand_pickup()
{
	free_hands hands;
	free_hands_frame f = holding();
	f.off_valid = true;
	f.off_hand = { -0.3f, -0.4f, -0.2f };
	f.sling = true;
	f.sling_valid = true;
	f.sling_at = { 0.1f, -0.5f, -0.12f };
	hands.update(f);
	f.grip = false;
	free_hands_result r;
	for (int i = 0; i < 40 && !r.dropped; ++i)
	{
		r = hands.update(f);
	}
	expect(r.dropped && r.slung, "let go: on the sling");
	f.off_hand = { 0.12f, -0.48f, -0.1f };
	r = hands.update(f);
	expect(r.off_in_reach, "the off hand at the sling is in reach");
	f.off_busy = true;
	f.off_grip = true;
	r = hands.update(f);
	expect(r.dropped && !r.in_off_grip, "a busy off-hand grip (a grenade) does not take it");
	f.off_grip = false;
	f.off_busy = false;
	hands.update(f);
	f.off_grip = true;
	r = hands.update(f);
	expect(r.event == free_hands_event::picked_up_off && r.in_off_grip && !r.dropped && !r.block_fire, "the off hand picks it up by the grip, free to fire");
	f.off_hand = { -0.2f, -0.3f, -0.35f };
	r = hands.update(f);
	expect(r.in_off_grip && r.event == free_hands_event::none, "held in the off hand while it moves");
	f.off_grip = false;
	for (int i = 0; i < 40 && r.event != free_hands_event::dropped; ++i)
	{
		r = hands.update(f);
	}
	expect(r.event == free_hands_event::dropped && r.slung && !r.in_off_grip, "the off hand lets go: back on the sling");
	f.gun_hand = { 0.11f, -0.49f, -0.12f };
	f.grip = true;
	r = hands.update(f);
	expect(r.event == free_hands_event::picked_up && !r.dropped, "the gun hand picks it up from the sling");
}

int main()
{
	test_sling();
	test_off_hand_pickup();
	test_holster_take();
	test_held_through_switch();
	test_drop_and_pick_up();
	test_regrip_and_busy();
	test_reset_cases();
	test_hand_over();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("free_hands: all checks passed\n");
	return 0;
}
