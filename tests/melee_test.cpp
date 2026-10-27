// Checks the melee gesture (src/weapon/melee_gesture): a forward thrust strikes once, slow moves,
// sideways and backward swings and a busy hand do not, the cooldown and re-arm rules hold.
#include <cmath>
#include <cstdio>

#include "weapon/melee_gesture.hpp"

using namespace bfvr;
using namespace bfvr::weapon;

namespace
{
	int failures = 0;
	constexpr float frame = 1.0f / 90.0f;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	stereo::pose head_facing(float yaw)
	{
		stereo::pose head;
		head.orientation[1] = std::sin(0.5f * yaw);
		head.orientation[3] = std::cos(0.5f * yaw);
		return head;
	}

	// Moves the hand at a constant velocity (m/s, LOCAL) for some time; counts strikes.
	int move(melee_gesture& gesture, const stereo::pose& head, float (&hand)[3], const float (&velocity)[3], float seconds, bool busy = false)
	{
		int strikes = 0;
		for (float t = 0; t < seconds - 1e-6f; t += frame)
		{
			for (int axis = 0; axis < 3; ++axis)
			{
				hand[axis] += velocity[axis] * frame;
			}
			strikes += gesture.update(head, hand, true, busy, frame) ? 1 : 0;
		}
		return strikes;
	}

	const float still[3]{ 0, 0, 0 };

	void test_thrust()
	{
		melee_gesture gesture;
		const stereo::pose head = head_facing(0);
		float hand[3]{ 0.2f, -0.3f, -0.3f };
		move(gesture, head, hand, still, 0.2f);
		const float forward[3]{ 0, 0, -3.5f };
		expect(move(gesture, head, hand, forward, 0.15f) == 1, "fast thrust forward strikes once");
		expect(move(gesture, head, hand, still, 0.3f) == 0, "stopping does not strike again");

		// Within the cooldown even after slowing down: no second strike.
		expect(move(gesture, head, hand, forward, 0.15f) == 0, "second thrust inside the cooldown");
		move(gesture, head, hand, still, 1.0f);
		expect(move(gesture, head, hand, forward, 0.15f) == 1, "after the cooldown it strikes again");
	}

	void test_refusals()
	{
		const stereo::pose head = head_facing(0);
		melee_gesture gesture;
		float hand[3]{ 0.2f, -0.3f, -0.3f };
		move(gesture, head, hand, still, 0.2f);
		const float slow[3]{ 0, 0, -1.5f };
		expect(move(gesture, head, hand, slow, 0.4f) == 0, "a slow reach does not strike");
		move(gesture, head, hand, still, 0.2f);
		const float sideways[3]{ 4.0f, 0, 0 };
		expect(move(gesture, head, hand, sideways, 0.1f) == 0, "a fast sideways swing does not strike");
		move(gesture, head, hand, still, 0.2f);
		const float back[3]{ 0, 0, 4.0f };
		expect(move(gesture, head, hand, back, 0.1f) == 0, "pulling the gun back does not strike");
		move(gesture, head, hand, still, 0.2f);
		const float forward[3]{ 0, 0, -3.5f };
		expect(move(gesture, head, hand, forward, 0.15f, true) == 0, "a busy hand (grenade, reload) does not strike");

		// Tracking jumps: a lost and refound hand is not a velocity.
		melee_gesture jumpy;
		float at[3]{ 0, 0, 0 };
		jumpy.update(head, at, true, false, frame);
		jumpy.update(head, at, false, false, frame);
		float far[3]{ 0, 0, -1.0f };
		expect(!jumpy.update(head, far, true, false, frame), "tracking gap: no strike from the jump");
	}

	void test_direction_follows_the_head()
	{
		// Facing left (+90 degrees about y): forward is -x.
		const stereo::pose head = head_facing(1.5707963f);
		melee_gesture gesture;
		float hand[3]{ 0, -0.3f, 0 };
		move(gesture, head, hand, still, 0.2f);
		const float old_forward[3]{ 0, 0, -3.5f };
		expect(move(gesture, head, hand, old_forward, 0.15f) == 0, "thrust towards the old forward: sideways now");
		move(gesture, head, hand, still, 1.0f);
		const float left[3]{ -3.5f, 0, 0 };
		expect(move(gesture, head, hand, left, 0.15f) == 1, "thrust where the head faces strikes");

		// Looking down while thrusting forward still counts (level forward).
		stereo::pose down = head_facing(0);
		const float pitch = -0.6f;
		down.orientation[0] = std::sin(0.5f * pitch);
		down.orientation[3] = std::cos(0.5f * pitch);
		melee_gesture pitched;
		float hand2[3]{ 0, -0.3f, -0.3f };
		move(pitched, down, hand2, still, 0.2f);
		const float forward[3]{ 0, -0.5f, -3.5f };
		expect(move(pitched, down, hand2, forward, 0.15f) == 1, "head pitched down: a level thrust strikes");
	}
}

int main()
{
	test_thrust();
	test_refusals();
	test_direction_follows_the_head();
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("melee: all checks passed\n");
	return 0;
}
