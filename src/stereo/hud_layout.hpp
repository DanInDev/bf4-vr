#pragma once

#include "stereo/tracking.hpp"

// Where pieces of the captured HUD texture go (docs/tasks/hud-gestures.md, #26/#27), as quads in
// OpenXR LOCAL space. Engine-free; tested by tests/eye_math_test.cpp.
namespace bfvr::stereo::hud_layout
{
	struct uv_rect
	{
		float u0{}, v0{}, u1{ 1 }, v1{ 1 }; // (0, 0) = top left of the texture
	};

	struct quad
	{
		pose placement;      // centre; the quad faces +z
		float width{};
		float height{};
		uv_rect rect;
	};

	// The HUD as the game drew it: projected like the eye image of the same frame (view pose, rendered
	// FOV) onto a plane distance metres ahead, so markers lie on the directions of their targets.
	// Returns the part of that plane the rect covers.
	quad projected(const pose& view, const fov& rendered, float distance, const uv_rect& rect);

	// A piece of the HUD on the body: centred at anchor * offset (anchor axes), turned to face eye,
	// width metres wide, height from the piece's pixel aspect (texture_aspect = texture width / height).
	quad on_body(const pose& anchor, const float (&offset)[3], const float (&eye)[3], float width, const uv_rect& rect, float texture_aspect);

	// Orientation whose +z points from position to target, x horizontal (y up as far as possible).
	pose facing(const float (&position)[3], const float (&target)[3]);

	void rotate(const float (&q)[4], const float (&v)[3], float (&out)[3]);
}
