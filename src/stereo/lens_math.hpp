#pragma once

#include <cstdint>

// sight_lens (#39): the CPU side of the BF3-style lens (stereo/eye_pass). Engine-free; tests/lens_math_test.cpp.
namespace bfvr::stereo
{
	// The reticle art's span as a slope (tangent units across the whole art): the material's size as a fraction
	// of the screen height, at the game's ADS view of ads_fov_degrees (vertical). EOTech: 0.038 at 40 degrees =
	// 0.0277, its ring (70 % of the art) 1.1 degrees across, as the real sight's 65 MOA.
	float reticle_span(float size_fraction, float ads_fov_degrees);

	// #140: the aiming view the reticle art is sized for: the setting sight_lens_fov when 10 degrees or more (an
	// override), else the reticle's own zoom level FOV (reticle_level_fov), else 40 (the EOTech's measured view).
	float reticle_fov(float override_degrees, float level_fov_degrees);
	// The zoom level whose view the reticle art belongs to, degrees (0 when unknown). A scope's or red dot's own selected
	// level (ACOG 17.2, SRS 8.7, red dot / holo 40). A flipped magnifier only magnifies the red dot or holo behind it: its
	// reticle keeps that sight's own level, the one below (27 -> 40), so in the glass it grows with the power, as
	// through a real magnifier (owner, #140), not by the game's 1.14x.
	float reticle_level_fov(const float* level_fov, int levels, int capacity, int selected, bool magnifier);
	// Seen through a glass of this power the art's world span is magnified with the world (0 below 1x).
	float glass_reticle_span(float span, float power);

	// The gun's right and up square to its sight line (all in one view space: x right, y up, -z ahead). False
	// when up runs along the line.
	bool lens_axes(const float (&along)[3], const float (&up)[3], float (&right_out)[3], float (&up_out)[3]);

	// #139 scope_glass (Great Destroyer's lens: what is seen at a slope off the sight line through an optic of this power
	// is the world at slope / power). The scope picture is rendered with the eye's own orientation (measured: the gun's
	// rotation does not reach the scope render, re/notes/weapon.md) as a disc `lens_tan` (world tangent) around the
	// view's axis. glass_picture_uv: where the glass's view at `slope` (in the gun's right / up, all in the eye's view
	// space, -z ahead) is in that picture (uv, v down); false outside the disc or behind. The shader (ps_glass) does the
	// same per pixel.
	bool glass_picture_uv(const float (&along)[3], const float (&right)[3], const float (&up)[3], const float (&slope)[2], float power, float lens_tan,
		float (&uv)[2]);
	// The picture's reach (world tangent from the view's axis) the glass needs: the sight line's own tangent off the axis
	// plus the field stop over the power, with 15 % room. 0 when the line does not point ahead.
	float glass_picture_reach(const float (&along)[3], float power);

	// #139: Great Destroyer's field stop (0.38 off the line, scopezoom.cpp) for an eye on the sight line, closing as the
	// eye moves off it (metres), shut at 2 cm: a scope's eye box, so the picture darkens from the edge (a real ACOG's
	// exit pupil is ~8 mm; 2 cm leaves room for the headset's own motion).
	constexpr float glass_field_stop_on_line = 0.38f;
	constexpr float glass_eye_box = 0.02f;
	float glass_field_stop(float eye_off_line_metres);

	// #139: which of this eye render's lens draws are the optic's glass. A lens draw is the glass when the sight line
	// passes through it (`on_line` pixels at the line's point > 0); all such draws together (the ACOG's lie at two
	// depths). The choice is kept while the eye leaves the line (no draw on it any more), as long as the optic is the
	// same: the kept draws together within half to double their size then. Returns the draws' bits (bit k = draw k), 0
	// for no glass.
	constexpr int glass_max_draws = 8;
	struct glass_choice
	{
		int smallest{ -1 };               // the smallest glass draw when the line last passed through one
		std::uint64_t area{};             // the glass draws' pixels together then
		unsigned bits{};
	};
	unsigned choose_glass(glass_choice& kept, const std::uint64_t* area, const std::uint64_t* on_line, int draws);
}
