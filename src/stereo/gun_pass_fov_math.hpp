#pragma once

// The gun pass's own field of view for a life-size gun (#39). Engine-free; tested by tests/gun_pass_fov_test.cpp.
namespace bfvr::stereo::gun_pass_fov_math
{
	// Half vertical FOV (radians) the gun pass must have so one game metre in it is gun_scale metres in the
	// world drawn at world_half (radians): tan(gun) = tan(world) / gun_scale. gun_scale is clamped to
	// 0.05..20; the result to 1..85 degrees.
	float life_size_half_fov(float world_half, float gun_scale);

	// The value the game stores for it: full vertical FOV in degrees.
	float degrees_of(float half_fov);

	// A stored FOV that can be the game's own (not garbage, not ours): 1..170 degrees.
	bool plausible_degrees(float degrees);
}
