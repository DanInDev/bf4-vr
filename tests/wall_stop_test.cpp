// Checks the drawn gun stopping at walls (src/weapon/wall_stop, #63): pulled back by what is inside the wall plus
// a margin, quickly; back to the hand when clear; never stuck: past max_push it lets go, and it keeps no state
// that could hold the gun once the real barrel is out.
#include <cmath>
#include <cstdio>

#include "weapon/wall_stop.hpp"

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

	float run(wall_stop& stop, bool known, float inside, int frames)
	{
		float push = 0.0f;
		for (int i = 0; i < frames; ++i)
		{
			push = stop.update(frame, known, inside);
		}
		return push;
	}
}

int main()
{
	wall_stop stop;
	expect(run(stop, true, -0.5f, 10) == 0.0f, "clear of walls: no push");
	expect(std::fabs(run(stop, true, 0.10f, 9) - 0.13f) < 0.005f, "10 cm inside: pulled back 13 cm within 0.1 s");
	expect(std::fabs(run(stop, true, -0.02f, 27) - 0.01f) < 0.003f, "2 cm short of the wall: within the margin, 1 cm back");
	float after = run(stop, true, -0.5f, 27);
	expect(after < 0.002f, "out of the wall: back in the hand within 0.3 s");
	run(stop, true, 0.20f, 20);
	after = run(stop, true, 0.40f, 30);
	expect(stop.let_go() && after < 0.005f, "40 cm inside (more than 30 cm): lets go, the barrel goes in");
	run(stop, true, 0.10f, 20);
	after = run(stop, false, 0.10f, 30);
	expect(after < 0.002f, "no answer for the gun now: lets go");
	expect(run(stop, true, NAN, 30) == 0.0f, "a broken answer: nothing");
	if (failures)
	{
		std::printf("wall_stop: %d failed\n", failures);
		return 1;
	}
	std::printf("wall_stop: all checks passed\n");
	return 0;
}
