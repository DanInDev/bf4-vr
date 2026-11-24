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

	// Owner 2026-11-24: every optic's own reticle as the game has it. A reticle quad's material (its ps cb1, 20 floats):
	// float 8 the art's height as a fraction of the screen height (at its zoom level's view), float 12 the quad's aspect
	// (width / height: the ACOG's 512x256 lines 2, its 128x128 chevron 0.65), floats 16 and 17 the art's point (uv) that
	// sits on the sight line (the ACOG's lines 0.5 0.11: the bar line under the chevron). Measured on the ACOG's quads drawn
	// by the game's own shader (2026-11-24, re/notes/weapon.md "Every optic's own reticle").
	struct reticle_material
	{
		float size{ 0.038f };
		float aspect{ 1.0f };
		float anchor[2]{ 0.5f, 0.5f };
		bool known{}; // the block has the reticle material's layout (else size only, as before: aspect 1, centred)
	};
	reticle_material read_reticle_material(const float (&block)[20]);
	// Where the art is seen at `slope` off the sight line (the gun's right / up, tangents): uv (v down) for an art spanning
	// `span` (slope) in height. False outside the art.
	bool reticle_art_uv(const reticle_material& art, float span, const float (&slope)[2], float (&uv)[2]);

	// A scope's reticle (the SRS's rifle, PKS-07 and ballistic scopes) is another shader with no material: a quad of the
	// scope's model (vertex: position as 4 halves at byte 0, uv as 2 halves at byte 24; model +x left, +y up) whose origin
	// is where the sight line crosses it (the ACOG's line quad puts the origin exactly on its material anchor, 0.5 0.11).
	// From its 4 corners: the aspect (width / height) and the origin's uv. False when the corners are no flat quad round
	// the origin of at most 20 cm, or the origin is at its edge (a quad the ACOG draws with its origin at a corner).
	float half_to_float(std::uint16_t half);
	bool scope_art_from_quad(const float (&position)[4][2], const float (&uv)[4][2], reticle_material& art);
	// Its height as a slope at its zoom level's view: in the game's own scope view the art's width is about the scope's
	// circle, 0.8 of the screen height (BF4 PKS-07 and 8x pictures, owner 2026-11-24).
	constexpr float scope_art_width_of_screen = 0.8f;
	float scope_art_span(float aspect, float level_fov_degrees);

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
	// #158 (owner 2026-11-24, measured with a turned, pitched and rolled fake head): the picture is rendered along the scope
	// camera, i.e. along the gun's sight line, not the eye's axis. `picture` is that camera's orientation in the eye's view
	// space (x y z w; scope_request::relative): the world direction is read on the picture's own axes.
	bool glass_picture_uv(const float (&along)[3], const float (&right)[3], const float (&up)[3], const float (&slope)[2], float power, float lens_tan,
		const float (&picture)[4], float (&uv)[2]);
	// The picture's reach (world tangent from the view's axis) the glass needs: the sight line's own tangent off the axis
	// plus the field stop over the power, with 15 % room. 0 when the line does not point ahead.
	float glass_picture_reach(const float (&along)[3], float power);

	// #139: Great Destroyer's field stop (0.38 off the line, scopezoom.cpp). Owner 2026-11-24 (VR eye box): the whole
	// field while the eye is within `eye_box` (metres, setting scope_eye_box) of the sight line, then closing smoothly
	// from the edge over 1.5 times that again (the 2 cm linear close of S3 left the glass black at 1-2 cm, where a head
	// in a headset always is).
	constexpr float glass_field_stop_on_line = 0.38f;
	constexpr float glass_eye_box = 0.015f;   // the default full-field box
	constexpr float glass_eye_box_fade = 1.5f; // the fade's width, times the box
	float glass_field_stop(float eye_off_line_metres, float eye_box = glass_eye_box);

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
