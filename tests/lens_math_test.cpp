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

	// #139 scope_glass: the scope picture's uv per unit of slope through the glass.
	{
		// A 4x scope; the picture's disc edge 0.1 off the line in the world (0.4 through the glass).
		const float scale = glass_picture_scale(4.0f, 0.1f);
		expect(near(scale, 1.25f), "4x, disc 0.1: 1.25 uv per slope");
		expect(near(0.5f + 0.4f * scale, 1.0f), "the glass's 0.4 off the line reaches the disc edge");
		// The same line through a 1.73x magnifier: what is 0.1 off in the world is 0.173 off through the glass.
		expect(near(0.5f + 0.173f * glass_picture_scale(1.73f, 0.1f), 1.0f, 2e-3f), "magnifier: 0.173 through the glass is the disc edge");
		// A stronger optic shows less of the world per slope.
		expect(glass_picture_scale(6.0f, 0.1f) < scale, "more power, less world per slope");
		expect(glass_picture_scale(0.9f, 0.1f) == 0.0f, "below 1x: unusable");
		expect(glass_picture_scale(4.0f, 0.0f) == 0.0f, "no disc: unusable");
	}
	{
		// The field stop: Great Destroyer's 0.38 on the line, closing as the eye leaves it, shut at the eye box.
		expect(near(glass_field_stop(0.0f), 0.38f), "on the line: 0.38");
		expect(glass_field_stop(0.015f) < 0.2f && glass_field_stop(0.015f) > 0.1f, "1.5 cm off: darkened from the edge, not shut");
		expect(glass_field_stop(0.03f) == 0.0f, "past the eye box: dark");
		expect(near(glass_field_stop(-0.01f), glass_field_stop(0.01f)), "either side alike");
	}

	if (failures == 0)
	{
		std::printf("lens_math: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
