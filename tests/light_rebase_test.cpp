// Checks the first-person light re-basing (src/stereo/light_rebase, #92), the math of BF4's FUN_140CC9C50: from the same
// camera to the same camera leaves the light where it is (the mod's fix); from the game's camera to a view turned by the
// head turns the light about the camera by that turn (the bug the owner saw: the spot off to the side the head looks);
// a scaled "from" camera is normalised; and the angle and position readouts.
#include <cmath>
#include <cstdio>

#include "stereo/light_rebase.hpp"

using namespace bfvr::stereo::light_rebase;

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

	// A camera or light turned yaw degrees about +y (rows right, up, forward, position).
	void yawed(float degrees, float x, float y, float z, float (&m)[16])
	{
		const float r = degrees * 3.14159265f / 180.0f, c = std::cos(r), s = std::sin(r);
		const float rows[16] = { c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, x, y, z, 1 };
		for (int i = 0; i < 16; ++i)
		{
			m[i] = rows[i];
		}
	}
}

int main()
{
	// The flashlight 0.66 m ahead of a gun turned 20 degrees, in a room at (348, 278, -51).
	float light[16];
	yawed(20.0f, 348.2f, 278.0f, -50.4f, light);
	float camera[16];
	yawed(0.0f, 349.0f, 278.1f, -51.2f, camera);

	float same[16];
	rebase(light, camera, camera, same);
	expect(rotation_difference(light, same) < 1e-5f && position_difference(light, same) < 1e-4f, "the same camera both ways leaves the light in place");
	expect(near(forward_angle_degrees(light, same), 0.0f, 0.05f), "and its direction");

	// The drawn view: the same camera turned 30 degrees by the head (about the camera, same place).
	float head_turned[16];
	yawed(30.0f, 349.0f, 278.1f, -51.2f, head_turned);
	float turned[16];
	rebase(light, camera, head_turned, turned);
	expect(near(forward_angle_degrees(light, turned), 30.0f, 0.05f), "from the game's camera into the head-turned view the light turns by the head's 30 degrees");
	// It turns about the camera: its distance to the camera stays.
	const float before = std::sqrt((light[12] - 349.0f) * (light[12] - 349.0f) + (light[14] + 51.2f) * (light[14] + 51.2f));
	const float after = std::sqrt((turned[12] - 349.0f) * (turned[12] - 349.0f) + (turned[14] + 51.2f) * (turned[14] + 51.2f));
	expect(near(before, after, 1e-3f) && position_difference(light, turned) > 0.3f, "about the camera, so its place swings too");

	// The fix: re-based from the drawn view itself, the head-turned view leaves it where the gun put it.
	float fixed[16];
	rebase(light, head_turned, head_turned, fixed);
	expect(rotation_difference(light, fixed) < 1e-5f && position_difference(light, fixed) < 1e-4f, "re-based from the drawn view: the light stays at the gun");

	// A "from" camera with a uniform scale (the game divides by the first row's squared length).
	float scaled[16];
	yawed(0.0f, 349.0f, 278.1f, -51.2f, scaled);
	for (int i = 0; i < 12; ++i)
	{
		scaled[i] *= 2.0f;
	}
	float unscaled_back[16];
	rebase(light, scaled, scaled, unscaled_back);
	expect(rotation_difference(light, unscaled_back) < 1e-4f && position_difference(light, unscaled_back) < 1e-3f, "a scaled camera both ways is still the identity");

	// The readouts.
	float pitched[16] = { 1, 0, 0, 0, 0, 0.8660254f, -0.5f, 0, 0, 0.5f, 0.8660254f, 0, 0, 0, 0, 1 };
	float level[16] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 3, 4, 0, 1 };
	expect(near(forward_angle_degrees(level, pitched), 30.0f, 0.05f), "the forward rows' angle");
	expect(near(position_difference(level, pitched), 5.0f), "the positions' distance");

	if (failures == 0)
	{
		std::printf("light_rebase: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
