#pragma once

#include "stereo/tracking.hpp"

// Laser pointer for menus (docs/tasks/input-locomotion.md, issue #23): where a controller's aim ray
// hits a quad layer. Engine-free; tested by tests/gestures_test.cpp.
namespace bfvr::input
{
	// An OpenXR quad layer in the same space as the ray: centred on pose, in its x/y plane, visible
	// from +z. A head-locked quad (view space) must be composed with the head pose first.
	struct quad_target
	{
		stereo::pose pose;
		float width{};
		float height{};
	};

	struct pointer_hit
	{
		bool hit{};
		float distance{};   // metres along the ray
		float u{}, v{};     // texture coordinates, (0, 0) top left
		float point[3]{};
	};

	// aim: the controller's aim pose (ray from its position along -z). Hits only the front face,
	// within max_distance and inside the quad.
	pointer_hit point_at(const stereo::pose& aim, const quad_target& quad, float max_distance = 100.0f);

	// The point on the quad at texture coordinates u, v ((0, 0) top left): where point_at would report them.
	void point_on_quad(const quad_target& quad, float u, float v, float (&point)[3]);

	// A screen distance metres ahead of the head, level and facing it (head yaw only), at head height:
	// menus over a level appear where the player looks, not where tracking started (crates, 2026-11-05).
	stereo::pose screen_in_front(const stereo::pose& head, float distance);
}
