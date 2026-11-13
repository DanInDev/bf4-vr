// Checks the BF3-style lens's CPU math (src/stereo/lens_math): the reticle's angular span from the game's
// material size, and the gun's axes square to the sight line (so the reticle turns with the gun, not the head).
#include <cmath>
#include <cstdio>

#include "stereo/lens_math.hpp"

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

	bool near(float a, float b, float tolerance = 1e-3f)
	{
		return std::fabs(a - b) <= tolerance;
	}
}

int main()
{
	// EOTech, live 2026-11-13: size 0.038 of the screen height, ring 70 % of the art; at the game's 40 degree ADS
	// view the ring is about 1.1 degrees, as the real 65 MOA ring.
	const float span = reticle_span(0.038f, 40.0f);
	expect(near(span, 0.02766f, 1e-4f), "EOTech span at 40 degrees");
	const float ring_degrees = 2.0f * std::atan(0.5f * 0.70f * span) * 180.0f / 3.14159265f;
	expect(ring_degrees > 1.0f && ring_degrees < 1.25f, "the EOTech ring about 1.1 degrees, as the real sight");
	expect(reticle_span(0.040f, 40.0f) > span, "the PK-AS's larger art is wider");

	float right[3]{}, up[3]{};
	{
		// Straight ahead, upright: right is +x, up is +y.
		const float along[3]{ 0.0f, 0.0f, -1.0f }, gun_up[3]{ 0.0f, 1.0f, 0.0f };
		expect(lens_axes(along, gun_up, right, up), "upright axes");
		expect(near(right[0], 1.0f) && near(up[1], 1.0f), "upright: right +x, up +y");
	}
	{
		// The gun canted 30 degrees right (head level): the axes turn with the gun.
		const float s = std::sin(0.5235988f), c = std::cos(0.5235988f);
		const float along[3]{ 0.0f, 0.0f, -1.0f }, gun_up[3]{ s, c, 0.0f };
		expect(lens_axes(along, gun_up, right, up), "canted axes");
		expect(near(up[0], s) && near(up[1], c) && near(right[0], c) && near(right[1], -s), "canted 30: right and up turn with the gun");
	}
	{
		// An up not quite square to the line (the sight 2 degrees below the bore): made square.
		const float along[3]{ 0.0f, -0.0349f, -0.9994f }, gun_up[3]{ 0.0f, 1.0f, 0.0f };
		expect(lens_axes(along, gun_up, right, up), "nearly square axes");
		const float dot = up[0] * along[0] + up[1] * along[1] + up[2] * along[2];
		expect(near(dot, 0.0f), "up square to the line");
	}
	{
		const float along[3]{ 0.0f, 1.0f, 0.0f }, gun_up[3]{ 0.0f, 1.0f, 0.0f };
		expect(!lens_axes(along, gun_up, right, up), "up along the line: no axes");
	}

	if (failures == 0)
	{
		std::printf("lens_math: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
