#pragma once

// sight_lens (#39): the CPU side of the BF3-style lens (stereo/eye_pass). Engine-free; tests/lens_math_test.cpp.
namespace bfvr::stereo
{
	// The reticle art's span as a slope (tangent units across the whole art): the material's size as a fraction
	// of the screen height, at the game's ADS view of ads_fov_degrees (vertical). EOTech: 0.038 at 40 degrees =
	// 0.0277, its ring (70 % of the art) 1.1 degrees across, as the real sight's 65 MOA.
	float reticle_span(float size_fraction, float ads_fov_degrees);

	// The gun's right and up square to its sight line (all in one view space: x right, y up, -z ahead). False
	// when up runs along the line.
	bool lens_axes(const float (&along)[3], const float (&up)[3], float (&right_out)[3], float (&up_out)[3]);
}
