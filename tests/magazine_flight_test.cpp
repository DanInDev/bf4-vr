// Checks a discarded magazine's flight (src/weapon/magazine_flight, #47): it keeps its release velocity,
// falls under gravity, bounces softly on the floor, rests there and goes; without a floor it falls briefly.
#include <cmath>
#include <cstdio>

#include "weapon/magazine_flight.hpp"

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

	bool near(float a, float b, float tolerance)
	{
		return std::fabs(a - b) <= tolerance;
	}

	// Advances until the magazine rests or goes; returns the seconds it took.
	float run(magazine_flight& flight, float limit, bool until_rest)
	{
		float t = 0.0f;
		while (t < limit && flight.active() && !(until_rest && flight.resting()))
		{
			flight.update(frame);
			t += frame;
		}
		return t;
	}
}

int main()
{
	const float floor_y = -1.6f;
	const v3 hand{ 0.0f, -0.6f, -0.3f }; // a hand at the chest, 1.0 m above the floor

	// Tossed sideways at 2 m/s: lands about 2 m/s * fall time away, and rests on the floor.
	{
		magazine_flight flight;
		flight.start(hand, q4{}, v3{ 2.0f, 0.0f, 0.0f }, true, floor_y);
		flight.update(0.1f);
		expect(near(flight.position().x, 0.2f, 0.01f) && flight.position().y < hand.y, "after 0.1 s: 20 cm sideways and falling");
		run(flight, 5.0f, true);
		const float fall = std::sqrt(2.0f * (1.0f - 0.08f) / 9.81f); // to the resting height
		expect(flight.resting(), "comes to rest");
		expect(near(flight.position().y, floor_y + 0.08f, 0.001f), "rests on the floor (its centre 8 cm up)");
		expect(flight.position().x > 2.0f * fall && flight.position().x < 2.0f * fall + 0.6f, "landed past the first impact point, slid a little");
		const float rested = run(flight, 10.0f, false);
		expect(!flight.active() && rested > 3.9f && rested < 4.2f, "lies there about 4 s, then goes");
	}

	// Let go without moving: straight down, no tumble, rests under the hand.
	{
		magazine_flight flight;
		flight.start(hand, q4{}, v3{}, true, floor_y);
		run(flight, 5.0f, true);
		expect(flight.resting() && near(flight.position().x, 0.0f, 1e-4f) && near(flight.position().z, -0.3f, 1e-4f), "dropped still: straight down");
		expect(near(flight.rotation().w, 1.0f, 1e-4f), "no tumble without a throw");
	}

	// A tracking jump (40 m/s) is clamped to 8 m/s.
	{
		magazine_flight flight;
		flight.start(hand, q4{}, v3{ 40.0f, 0.0f, 0.0f }, true, floor_y);
		expect(near(flight.velocity().x, 8.0f, 1e-3f), "release speed clamped");
	}

	// A throw tumbles end over end.
	{
		magazine_flight flight;
		flight.start(hand, q4{}, v3{ 0.0f, 1.0f, -2.0f }, true, floor_y);
		flight.update(0.1f);
		expect(flight.rotation().w < 0.999f, "tumbles in flight");
	}

	// No floor known: falls 0.6 s with its velocity, then goes (the old behaviour, without a STAGE space).
	{
		magazine_flight flight;
		flight.start(hand, q4{}, v3{ 1.0f, 0.0f, 0.0f }, false, 0.0f);
		const float t = run(flight, 5.0f, false);
		expect(!flight.active() && t > 0.55f && t < 0.65f, "no floor: gone after 0.6 s");
	}

	// A floor above the release point (bad data) counts as unknown.
	{
		magazine_flight flight;
		flight.start(hand, q4{}, v3{}, true, 0.5f);
		const float t = run(flight, 5.0f, false);
		expect(t < 0.65f, "a floor above the hand is ignored");
	}

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("magazine_flight: all checks passed\n");
	return 0;
}
