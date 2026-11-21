#pragma once

// #92: the renderer's re-basing of first-person lights (a weapon's flashlight and laser light), as BF4's
// FUN_140CC9C50 computes it for every light with the first-person flag (+0xDA): the light's world matrix L
// (rows right, up, forward, position; LocalLightEntity +0x50) is taken into the frame of one camera and out
// through another, drawn = L x inverse(from) x to. In the flat game both cameras are the same view; the mod
// turns the drawn view by the head, so the light was turned with the head away from the drawn gun.
// Engine-free (tests/light_rebase_test.cpp).
namespace bfvr::stereo::light_rebase
{
	// 4x4 row-major matrices, rows right, up, forward, position (w 0, 0, 0, 1). from may carry a uniform scale
	// (the game normalises by the first row's length, as here).
	void rebase(const float (&light)[16], const float (&from)[16], const float (&to)[16], float (&out)[16]);

	// The largest difference between two matrices' rotation rows and, separately, their positions (metres):
	// how far the game's re-basing moves a light.
	float rotation_difference(const float (&a)[16], const float (&b)[16]);
	float position_difference(const float (&a)[16], const float (&b)[16]);

	// Angle in degrees between the two lights' forward rows (row 2).
	float forward_angle_degrees(const float (&a)[16], const float (&b)[16]);
}
