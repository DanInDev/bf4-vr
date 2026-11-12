// Checks sprint and vault toward the view (src/input/move_steer, #52): the stick is turned from the head
// into the soldier's frame, a sprint (click with a forward push) or a forward jump asks the soldier to
// face the move direction, and once it does the game sees a straight forward push.
#include <cmath>
#include <cstdio>

#include "input/move_steer.hpp"

using namespace bfvr::input;

namespace
{
	int failures = 0;
	constexpr float deg = 3.14159265f / 180.0f;
	constexpr float frame = 1.0f / 90.0f;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance = 1e-3f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	steer_input push(float x, float y, float head, float soldier)
	{
		steer_input in;
		in.seconds = frame;
		in.x = x;
		in.y = y;
		in.head_yaw = head;
		in.soldier_yaw = soldier;
		return in;
	}
}

int main()
{
	expect(near(wrap_angle(3.5f * 3.14159265f), -0.5f * 3.14159265f) && near(wrap_angle(-0.25f), -0.25f), "angles wrap into -pi..pi");

	// Walking: the head 30 degrees left of the soldier; forward goes where the head looks.
	{
		move_steer steer;
		const steer_output out = steer.update(push(0.0f, 1.0f, 30 * deg, 0.0f));
		expect(!out.steer && !out.sprinting, "walking forward: the soldier is not turned");
		expect(near(out.x, -std::sin(30 * deg)) && near(out.y, std::cos(30 * deg)), "the push reaches the game turned 30 degrees left (a diagonal: no sprint)");
		const steer_output same = steer.update(push(0.3f, 0.8f, 0.0f, 0.0f));
		expect(near(same.x, 0.3f) && near(same.y, 0.8f), "head and soldier aligned: the stick as it is");
	}

	// Sprint: click with a forward push; the soldier is asked to face the head; once it does, straight ahead.
	{
		move_steer steer;
		steer_input in = push(0.0f, 1.0f, 30 * deg, 0.0f);
		in.sprint_click = true;
		steer_output out = steer.update(in);
		expect(out.steer && out.sprinting && near(out.yaw, 30 * deg), "click + forward: sprint, face 30 degrees left");
		in.sprint_click = false;
		in.soldier_yaw = 30 * deg; // the game turned its soldier
		out = steer.update(in);
		expect(out.steer && near(out.x, 0.0f) && near(out.y, 1.0f), "soldier facing the head: a straight forward push (sprint holds)");
		in.x = -std::sin(20 * deg);
		in.y = std::cos(20 * deg);
		out = steer.update(in);
		expect(out.steer && near(out.yaw, 50 * deg), "steering 20 degrees further left while sprinting: the soldier follows");
		in.x = 0.0f;
		in.y = 0.1f;
		expect(!steer.update(in).steer, "letting go of the stick ends the sprint");
	}

	// The sprint ends on a sideways push or the fire trigger; a click without a forward push starts nothing.
	{
		move_steer steer;
		steer_input in = push(0.0f, 1.0f, 0.0f, 0.0f);
		in.sprint_click = true;
		steer.update(in);
		in.x = 1.0f;
		in.y = 0.0f;
		expect(!steer.update(in).sprinting, "a push swung 90 degrees aside ends the sprint");
		move_steer firing;
		in = push(0.0f, 1.0f, 0.0f, 0.0f);
		in.sprint_click = true;
		firing.update(in);
		in.fire = true;
		expect(!firing.update(in).sprinting, "firing ends the sprint (as in the game)");
		move_steer idle;
		steer_input still = push(0.0f, 0.0f, 0.0f, 0.0f);
		still.sprint_click = true;
		expect(!idle.update(still).sprinting, "a click with the stick at rest: no sprint");
		still.y = 1.0f;
		expect(!idle.update(still).sprinting, "a click already held does not count again once the stick moves");
		move_steer sideways;
		steer_input side = push(1.0f, 0.0f, 0.0f, 0.0f);
		side.sprint_click = true;
		expect(!sideways.update(side).steer, "click with a sideways push: no sprint");
	}

	// Vault: a forward jump faces the soldier for 0.7 s.
	{
		move_steer steer;
		steer_input in = push(0.0f, 1.0f, -40 * deg, 0.0f);
		in.jump = true;
		const steer_output out = steer.update(in);
		expect(out.steer && !out.sprinting && near(out.yaw, -40 * deg), "jump with a forward push: face the head (40 degrees right) to vault");
		in.jump = false;
		float held = 0.0f;
		while (steer.update(in).steer && held < 2.0f)
		{
			held += frame;
		}
		expect(held > 0.6f && held < 0.75f, "the vault turn lasts about 0.7 s");
		move_steer back;
		steer_input backwards = push(0.0f, -1.0f, 0.0f, 0.0f);
		backwards.jump = true;
		expect(!back.update(backwards).steer, "jumping backwards: no turn");
	}

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("move_steer: all checks passed\n");
	return 0;
}
