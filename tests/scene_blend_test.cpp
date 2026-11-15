// Checks the cutscene blend (src/body/scene_blend): the mod's pose eases out in a scene and back in after,
// and the per-bone blend between the edited and the animated pose.
#include <cmath>
#include <cstdio>

#include "body/scene_blend.hpp"

using namespace bfvr::body;

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
}

static void test_weight()
{
	scene_blend blend;
	expect(blend.weight() == 1.0f, "weight: play draws the mod's pose");
	expect(blend.update(false, 0.016f) == 1.0f, "weight: stays 1 in play");
	const float first = blend.update(true, 0.15f);
	expect(first > 0.3f && first < 0.4f, "weight: one time constant into a scene is ~37 % left");
	for (int frame = 0; frame < 60; ++frame)
	{
		blend.update(true, 0.016f);
	}
	expect(blend.weight() == 0.0f, "weight: settles at 0 (the game's animation alone)");
	blend.update(false, 0.016f);
	expect(blend.weight() > 0.0f && blend.weight() < 0.2f, "weight: eases back in after the scene");
	expect(scene_blend{}.update(true, 0.0f, 0.0f) == 0.0f, "weight: no time constant switches at once");
	scene_blend paused;
	expect(paused.update(true, -1.0f) == 1.0f, "weight: a negative frame time changes nothing");
}

static void test_pose()
{
	ik::bone_transform animated[2]{};
	animated[0].position = { 0, 0, 0 };
	animated[0].rotation = { 0, 0, 0, 1 };
	animated[1].position = { 1, 2, 3 };
	animated[1].rotation = { 0, 0, std::sin(0.25f), std::cos(0.25f) };
	ik::bone_transform edited[2]{ animated[0], animated[1] };
	edited[0].position = { 0.2f, 0, 0 };
	edited[0].rotation = { std::sin(0.5f), 0, 0, std::cos(0.5f) }; // 60 degrees about x
	ik::bone_transform kept[2]{ edited[0], edited[1] };
	blend_pose(kept, animated, 2, 1.0f);
	expect(kept[0].position.x == 0.2f && kept[0].rotation.x == edited[0].rotation.x, "pose: weight 1 keeps the edit");
	ik::bone_transform none[2]{ edited[0], edited[1] };
	blend_pose(none, animated, 2, 0.0f);
	expect(near(none[0].position.x, 0.0f) && near(none[0].rotation.x, 0.0f) && near(none[0].rotation.w, 1.0f), "pose: weight 0 gives the animation");
	expect(near(none[1].position.y, 2.0f) && near(none[1].rotation.z, animated[1].rotation.z), "pose: an unedited bone stays");
	ik::bone_transform half[2]{ edited[0], edited[1] };
	blend_pose(half, animated, 2, 0.5f);
	expect(near(half[0].position.x, 0.1f), "pose: half way in position");
	const float angle = 2.0f * std::atan2(half[0].rotation.x, half[0].rotation.w);
	expect(near(angle, 0.5f, 0.02f), "pose: about half way in rotation (nlerp)");
	// The same rotation stored with the opposite sign blends the short way.
	ik::bone_transform flipped[1]{ animated[1] };
	flipped[0].rotation = { -animated[1].rotation.x, -animated[1].rotation.y, -animated[1].rotation.z, -animated[1].rotation.w };
	blend_pose(flipped, &animated[1], 1, 0.5f);
	const float d = std::fabs(flipped[0].rotation.z * animated[1].rotation.z + flipped[0].rotation.w * animated[1].rotation.w);
	expect(near(d, 1.0f, 1e-3f), "pose: q and -q blend to the same rotation");
	blend_pose(nullptr, animated, 2, 0.5f); // no crash
}

int main()
{
	test_weight();
	test_pose();
	if (failures == 0)
	{
		std::printf("scene_blend: all tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}
