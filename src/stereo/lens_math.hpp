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

	// #139 scope_glass (Great Destroyer's lens: what is seen at a slope off the sight line through an optic of this power
	// is the world at slope / power): the scope picture's uv per unit of slope through the glass, for a picture whose
	// disc edge is `lens_tan` (world tangent) from its centre, the sight line; the shader takes
	// uv = 0.5 + (slope.x, -slope.y) * scale. 0 when unusable (power below 1, no disc).
	float glass_picture_scale(float power, float lens_tan);

	// #139: Great Destroyer's field stop (0.38 off the line, scopezoom.cpp) for an eye on the sight line, closing as the
	// eye moves off it (metres), shut at 2.5 cm: a scope's eye box, so the picture darkens from the edge.
	constexpr float glass_field_stop_on_line = 0.38f;
	constexpr float glass_eye_box = 0.025f;
	float glass_field_stop(float eye_off_line_metres);
}
