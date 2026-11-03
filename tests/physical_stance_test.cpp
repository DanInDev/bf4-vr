// Checks the physical stance (src/input/physical_stance): standing is the highest head height, drops
// past the thresholds held for a moment crouch and go prone, with hysteresis on the way back, and
// only changes are reported.
#include <cstdio>

#include "input/physical_stance.hpp"

using namespace bfvr::input;

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

	// Holds the head at a height for a while; returns how many changes were reported.
	int hold(physical_stance& body, float height, float seconds)
	{
		int changes = 0;
		for (float t = 0.0f; t < seconds; t += 0.011f)
		{
			changes += body.update(height, 0.011f).changed ? 1 : 0;
		}
		return changes;
	}

	void test_crouch_and_prone()
	{
		physical_stance body;
		expect(hold(body, 0.0f, 1.0f) == 0 && body.stance() == 0, "standing at the start: stand, nothing reported");
		expect(hold(body, -0.1f, 1.0f) == 0 && body.stance() == 0, "a small dip (looking down, leaning) stays standing");
		expect(hold(body, -0.45f, 0.1f) == 0 && body.stance() == 0, "a quick duck is not yet a crouch");
		expect(hold(body, -0.45f, 0.3f) == 1 && body.stance() == 1, "held 45 cm lower: crouch, reported once");
		expect(hold(body, -0.25f, 1.0f) == 0 && body.stance() == 1, "rising a little stays crouched (hysteresis)");
		expect(hold(body, -1.1f, 0.5f) == 1 && body.stance() == 2, "lying down: prone");
		expect(hold(body, -0.8f, 1.0f) == 0 && body.stance() == 2, "propped up on the elbows stays prone");
		expect(hold(body, -0.5f, 0.5f) == 1 && body.stance() == 1, "up on the knees: crouch");
		expect(hold(body, -0.05f, 0.5f) == 1 && body.stance() == 0, "standing again: stand");
	}

	void test_reference()
	{
		physical_stance body;
		hold(body, 0.0f, 0.5f);
		hold(body, 0.2f, 0.5f); // stood up straighter than at the start
		expect(hold(body, -0.15f, 0.5f) == 1 && body.stance() == 1, "the standing reference is the highest head seen");
		body.reset();
		expect(hold(body, -0.15f, 0.5f) == 0 && body.stance() == 0, "after a reset the current height is standing");
		physical_stance straight;
		expect(hold(straight, 0.0f, 0.2f) == 0 && hold(straight, -1.1f, 0.5f) == 1 && straight.stance() == 2, "straight down from standing to prone");
	}
}

int main()
{
	test_crouch_and_prone();
	test_reference();
	if (failures == 0)
	{
		std::printf("physical_stance: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
