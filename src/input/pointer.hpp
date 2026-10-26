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
}
