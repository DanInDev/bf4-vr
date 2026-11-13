// Checks the weapon pickup grab (src/weapon/weapon_pickup_math): a fresh squeeze of a free hand near a gun
// on the ground holds R until the weapon changes, the hand lets go or the hold runs out; never otherwise.
#include <cstdio>

#include "weapon/weapon_pickup.hpp"

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

	// Standing: head 1.6 m over the floor at the origin, the AK-12 on the floor 0.7 m ahead (live 2026-11-13: 0.69 m).
	pickup_frame standing(const pickup_point* pickups, int count)
	{
		pickup_frame f;
		f.seconds = 0.011f;
		f.head_valid = true;
		f.head[1] = 1.6f;
		f.pickups = pickups;
		f.count = count;
		for (pickup_hand& h : f.hands)
		{
			h.valid = true;
		}
		f.hands[0].palm[0] = -0.2f;
		f.hands[0].palm[1] = 1.25f; // at the waist: not lowered over anything
		f.hands[0].palm[2] = -0.3f;
		f.hands[1].palm[0] = 0.2f;
		f.hands[1].palm[1] = 1.25f;
		f.hands[1].palm[2] = -0.3f;
		return f;
	}

	void place(pickup_hand& hand, float x, float y, float z)
	{
		hand.palm[0] = x;
		hand.palm[1] = y;
		hand.palm[2] = z;
	}
}

int main()
{
	const pickup_point ak{ { 0.0f, 0.05f, -0.7f }, 2.5f };
	const pickup_config config;

	{
		pickup_grab grab;
		pickup_frame f = standing(&ak, 1);
		place(f.hands[0], 0.05f, 0.2f, -0.6f); // the off hand down at the gun
		expect(!grab.update(f).hold, "no squeeze, no R");
		f.hands[0].grip = true;
		const pickup_result r = grab.update(f);
		expect(r.hold && r.hand == 0 && r.target == 0 && r.started, "a fresh squeeze at the gun holds R");
		expect(grab.update(f).hold, "R stays down while the grip is held");
		f.weapon_changed = true;
		expect(!grab.update(f).hold, "the weapon changed: R goes up");
		f.weapon_changed = false;
		expect(!grab.update(f).hold, "the grip still held after the take does not take again");
	}
	{
		pickup_grab grab;
		pickup_frame f = standing(&ak, 1);
		place(f.hands[1], 0.1f, 0.9f, -0.6f); // the gun hand lowered over it, 85 cm above
		f.hands[1].grip = true;
		expect(grab.update(f).hand == 1, "a hand lowered over the gun takes it (the column)");
	}
	{
		pickup_grab grab;
		pickup_frame f = standing(&ak, 1);
		place(f.hands[0], 0.05f, 1.3f, -0.6f); // at chest height over it: not lowered
		f.hands[0].grip = true;
		expect(!grab.update(f).hold, "a hand at the chest over the gun does not take (a holster or pouch grab)");
	}
	{
		pickup_grab grab;
		pickup_frame f = standing(&ak, 1);
		f.hands[0].grip = true; // gripped somewhere else first ...
		grab.update(f);
		place(f.hands[0], 0.05f, 0.2f, -0.6f); // ... then brought down still gripping
		expect(!grab.update(f).hold, "a grip carried down is no fresh squeeze");
	}
	{
		pickup_grab grab;
		pickup_frame f = standing(&ak, 1);
		place(f.hands[1], 0.05f, 0.2f, -0.6f);
		f.hands[1].grip = true;
		f.hands[1].busy = true;
		expect(!grab.update(f).hold, "a busy hand (holding the gun) never takes");
	}
	{
		const pickup_point far{ { 0.0f, 0.05f, -3.0f }, 2.5f };
		pickup_grab grab;
		pickup_frame f = standing(&far, 1);
		place(f.hands[0], 0.0f, 0.2f, -2.9f); // a reach to 2.9 m (impossible, but the radius check is what is tested)
		f.hands[0].grip = true;
		expect(!grab.update(f).hold, "a pickup outside its radius of the soldier is not offered: no R (it would reload)");
	}
	{
		pickup_grab grab;
		pickup_frame f = standing(&ak, 1);
		place(f.hands[0], 0.05f, 0.2f, -0.6f);
		f.hands[0].grip = true;
		bool held = true;
		for (int frame = 0; frame < 300 && held; ++frame)
		{
			held = grab.update(f).hold;
		}
		expect(!held, "R comes up after max_hold even with the grip held (the game did not take it)");
	}
	{
		const pickup_point two[]{ { { 0.3f, 0.05f, -0.7f }, 2.5f }, { { 0.0f, 0.05f, -0.7f }, 2.5f } };
		const float palm[3]{ 0.05f, 0.2f, -0.65f };
		const float head[3]{ 0.0f, 1.6f, 0.0f };
		expect(pickup_in_reach(config, palm, true, head, two, 2) == 1, "the nearer of two guns");
		expect(pickup_in_reach(config, palm, false, head, two, 2) == -1, "no head, no take");
	}
	{
		pickup_grab grab;
		pickup_frame f = standing(nullptr, 0);
		place(f.hands[0], 0.05f, 0.2f, -0.6f);
		f.hands[0].grip = true;
		expect(!grab.update(f).hold, "no pickups: no R");
	}

	if (failures == 0)
	{
		std::printf("weapon_pickup: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
