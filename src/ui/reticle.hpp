#pragma once

#include <cstdint>
#include <vector>

#include "stereo/tracking.hpp"

// Aim reticle for controller aim (issues #3, #12): a dot where the shots converge (weapon/two_hand
// converged_aim), drawn as a quad facing the head with a constant angular size. Engine-free; tested
// by tests/vignette_test.cpp (same overlay family).
namespace bfvr::ui
{
	// Premultiplied BGRA: a bright dot with a dark ring so it reads on light and dark backgrounds.
	void reticle_texture(int size, std::vector<std::uint32_t>& pixels);

	struct reticle_quad
	{
		stereo::pose pose; // OpenXR LOCAL, +z towards the eye
		float size{};      // metres (square)
	};
	// The quad at point, facing eye, angular_size radians wide.
	reticle_quad place_reticle(const float (&point)[3], const float (&eye)[3], float angular_size);
}
