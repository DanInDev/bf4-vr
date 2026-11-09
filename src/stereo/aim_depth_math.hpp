#pragma once

// Where the shot line meets the scene, from the scene depth (stereo/aim_depth). Engine-free; tested by
// tests/aim_depth_test.cpp. OpenXR LOCAL metres; eye axes x right, y up, -z forward.
namespace bfvr::stereo::aim_depth_math
{
	constexpr int samples = 32;
	constexpr float first_distance = 0.75f; // closer than this the line is still inside the gun
	constexpr float last_distance = 80.0f;
	constexpr float min_scene_distance = 0.8f; // nearer surfaces are the drawn gun and arms, not the world

	struct projection
	{
		float sx{}, sy{};   // x and y scale (cot of the half angles)
		float a{}, b{};     // depth = -a + b / d for a point d in front (the game's: a = -1, b = -near)
		float width{}, height{};
	};

	struct sample_set
	{
		float texel[samples][2]{}; // pixel to read
		bool on_screen[samples]{};
		float along[samples]{};    // distance along the shot line
		float view_depth[samples]{}; // the point's distance in front of the eye (along -z)
	};

	// Points along start + t * direction (t from first_distance to last_distance, spaced geometrically),
	// projected into the eye at eye_position with rotation eye_orientation (x, y, z, w).
	sample_set make_samples(const float (&start)[3], const float (&direction)[3], const float (&eye_position)[3], const float (&eye_orientation)[4],
		const projection& p);

	// The scene's distance in front of the eye for a depth value; negative when the value is invalid.
	float scene_distance(float depth, const projection& p);

	// The distance along the line to the first surface it meets, or a negative value when it meets none.
	// depths: the scene depth read at each sample's texel (ignored where not on screen).
	float hit_distance(const sample_set& set, const float (&depths)[samples], const projection& p);
}
