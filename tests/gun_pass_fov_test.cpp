// Checks the gun pass FOV for a life-size gun (src/stereo/gun_pass_fov_math): with that FOV, a point in the
// gun pass lands where the world camera would draw it, times gun_scale.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "stereo/gun_pass_fov_math.hpp"

using namespace bfvr::stereo;

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

	bool near(float a, float b, float tolerance)
	{
		return std::fabs(a - b) <= tolerance;
	}
}

int main()
{
	constexpr float degree = 3.14159265f / 180.0f;
	const float world = 39.17f * degree; // the headset's half vertical FOV (78.35 degrees)
	const float life = gun_pass_fov_math::life_size_half_fov(world, 1.0f);
	expect(near(life, world, 1e-5f), "gun_scale 1: the gun pass draws at the world's FOV");

	// A point 0.3 m ahead, 0.1 m up: its y on screen is y / (d tan(half)).
	for (const float scale : { 0.8f, 1.1f, 1.5f })
	{
		const float gun = gun_pass_fov_math::life_size_half_fov(world, scale);
		const float drawn = 0.1f / (0.3f * std::tan(gun));
		const float wanted = scale * 0.1f / (0.3f * std::tan(world));
		expect(near(drawn, wanted, 1e-4f), "a point in the gun pass lands gun_scale times where the world would draw it");
	}
	expect(gun_pass_fov_math::life_size_half_fov(world, 1.1f) < world, "a larger gun: a narrower gun pass");
	expect(near(gun_pass_fov_math::degrees_of(22.5f * degree), 45.0f, 1e-3f), "the game stores the full angle in degrees (45 by default)");
	expect(gun_pass_fov_math::life_size_half_fov(1.5f, 0.01f) <= 85.0f * degree + 1e-6f, "clamped below 170 degrees");
	expect(gun_pass_fov_math::plausible_degrees(45.0f) && gun_pass_fov_math::plausible_degrees(17.0f), "the game's values are plausible");
	expect(!gun_pass_fov_math::plausible_degrees(0.0f) && !gun_pass_fov_math::plausible_degrees(NAN) && !gun_pass_fov_math::plausible_degrees(1e20f),
		"garbage is not");
	std::printf(failures ? "gun_pass_fov_test: %d failures\n" : "gun_pass_fov_test: all passed\n", failures);
	return failures ? 1 : 0;
}
