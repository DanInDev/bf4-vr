// Checks where the shot line meets the scene from the depth (src/stereo/aim_depth_math): a wall ahead,
// the sky, the gun covering near pixels, and something nearer hiding the line.
#include <cmath>
#include <cstdio>

#include "stereo/aim_depth_math.hpp"

using namespace bfvr::stereo::aim_depth_math;

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

	// The game's projection (live 2026-11-09): sx 0.9324, sy 1.2272, depth = 1 - 0.06 / d.
	projection game()
	{
		projection p;
		p.sx = 0.9324f;
		p.sy = 1.2272f;
		p.a = -1.0f;
		p.b = -0.06f;
		p.width = 2160.0f;
		p.height = 2160.0f;
		return p;
	}

	float depth_at(float distance)
	{
		return 1.0f - 0.06f / distance;
	}
}

int main()
{
	const projection p = game();
	expect(std::fabs(scene_distance(depth_at(7.5f), p) - 7.5f) < 1e-2f, "depth back to distance");
	expect(scene_distance(1.0f, p) < 0.0f, "the sky (depth 1) has no distance");

	// The eye at the origin looking along -z; the gun 20 cm right, 25 cm down, 30 cm ahead, pointing ahead.
	const float eye[3]{ 0.0f, 0.0f, 0.0f };
	const float facing[4]{ 0.0f, 0.0f, 0.0f, 1.0f };
	const float start[3]{ 0.2f, -0.25f, -0.3f };
	const float ahead[3]{ 0.0f, 0.0f, -1.0f };
	const sample_set set = make_samples(start, ahead, eye, facing, p);
	int visible = 0;
	for (int i = 0; i < samples; ++i)
	{
		visible += set.on_screen[i] ? 1 : 0;
	}
	expect(visible >= samples - 2, "the line ahead stays on screen");

	// A wall 5 m ahead everywhere: the line meets it 4.7 m along (it starts 0.3 m ahead of the eye).
	float wall[samples];
	for (float& d : wall)
	{
		d = depth_at(5.0f);
	}
	const float hit = hit_distance(set, wall, p);
	expect(std::fabs(hit - 4.7f) < 0.15f, "a wall at 5 m is met 4.7 m along the line");

	// Sky: no hit.
	float sky[samples];
	for (float& d : sky)
	{
		d = 1.0f;
	}
	expect(hit_distance(set, sky, p) < 0.0f, "the sky meets nothing");

	// The gun covers the first samples (surfaces at 0.4 m), the wall is at 12 m.
	float gun[samples];
	for (int i = 0; i < samples; ++i)
	{
		gun[i] = set.along[i] < 1.5f ? depth_at(0.4f) : depth_at(12.0f);
	}
	const float past_gun = hit_distance(set, gun, p);
	expect(std::fabs(past_gun - 11.7f) < 0.5f, "the gun's own pixels are skipped, the wall behind is found");

	// A pillar at 2 m hides the line's points from 6 m on (they are far behind it), the wall is at 20 m.
	float pillar[samples];
	for (int i = 0; i < samples; ++i)
	{
		pillar[i] = set.along[i] > 6.0f && set.along[i] < 10.0f ? depth_at(2.0f) : depth_at(20.0f);
	}
	const float behind_pillar = hit_distance(set, pillar, p);
	expect(std::fabs(behind_pillar - 19.7f) < 1.5f, "a nearer surface hiding the line is no hit");

	// The gun turned 30 degrees right: still meets the wall where the slanted line reaches 5 m depth.
	const float right[3]{ std::sin(0.5236f), 0.0f, -std::cos(0.5236f) };
	const sample_set slanted = make_samples(start, right, eye, facing, p);
	const float slanted_hit = hit_distance(slanted, wall, p);
	expect(std::fabs(slanted_hit * std::cos(0.5236f) + 0.3f - 5.0f) < 0.25f, "a slanted line meets the wall at the right distance");

	std::printf(failures ? "aim_depth_test: %d failures\n" : "aim_depth_test: all passed\n", failures);
	return failures ? 1 : 0;
}
