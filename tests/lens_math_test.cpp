// Checks the BF3-style lens's CPU math (src/stereo/lens_math): the reticle's angular span from the game's
// material size, and the gun's axes square to the sight line (so the reticle turns with the gun, not the head).
#include <cmath>
#include <cstdio>

#include "stereo/lens_math.hpp"

using namespace bfvr::stereo;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance = 1e-3f)
	{
		return std::fabs(a - b) <= tolerance;
	}
}

int main()
{
	// EOTech, live 2026-11-13: size 0.038 of the screen height, ring 70 % of the art; at the game's 40 degree ADS
	// view the ring is about 1.1 degrees, as the real 65 MOA ring.
	const float span = reticle_span(0.038f, 40.0f);
	expect(near(span, 0.02766f, 1e-4f), "EOTech span at 40 degrees");
	const float ring_degrees = 2.0f * std::atan(0.5f * 0.70f * span) * 180.0f / 3.14159265f;
	expect(ring_degrees > 1.0f && ring_degrees < 1.25f, "the EOTech ring about 1.1 degrees, as the real sight");
	expect(reticle_span(0.040f, 40.0f) > span, "the PK-AS's larger art is wider");

	// #140: the art's view from the reticle's own zoom level; the setting (10 degrees or more) overrides; 40 unknown.
	expect(reticle_fov(0.0f, 40.0f) == 40.0f && near(reticle_span(0.038f, reticle_fov(0.0f, 40.0f)), span, 1e-6f), "a red dot or holo: 40, unchanged");
	expect(reticle_fov(0.0f, 17.2f) == 17.2f, "the ACOG's level: 17.2");
	expect(reticle_fov(35.0f, 17.2f) == 35.0f, "the setting overrides the data");
	expect(reticle_fov(5.0f, 17.2f) == 17.2f, "a setting below 10 degrees is no override");
	expect(reticle_fov(0.0f, 0.0f) == 40.0f && reticle_fov(0.0f, 300.0f) == 40.0f, "no or nonsense level: 40 as before");
	// Live levels (#138): CZ-805 EOTech + magnifier 45/40/27, ACOG 45/17.2.
	const float magnifier_levels[3]{ 45.0f, 40.0f, 27.0f }, acog_levels[2]{ 45.0f, 17.2f };
	expect(reticle_level_fov(magnifier_levels, 3, 8, 1, false) == 40.0f, "magnifier folded: the holo's own 40");
	expect(reticle_level_fov(magnifier_levels, 3, 8, 2, true) == 40.0f, "magnifier flipped: still the holo's 40, magnified by the glass");
	expect(reticle_level_fov(acog_levels, 2, 8, 1, false) == 17.2f, "ACOG: its own 17.2");
	expect(reticle_level_fov(acog_levels, 2, 8, 0, false) == 0.0f && reticle_level_fov(acog_levels, 2, 8, 2, false) == 0.0f, "hip or past the levels: unknown");
	expect(reticle_level_fov(magnifier_levels, 3, 2, 2, true) == 0.0f && reticle_level_fov(nullptr, 3, 8, 1, false) == 0.0f, "past the array: unknown");
	// Through the glass: a flipped magnifier's ring grows with its power (a real magnifier); the ACOG's art at its own
	// level, magnified.
	const float flipped = glass_reticle_span(reticle_span(0.038f, reticle_fov(0.0f, reticle_level_fov(magnifier_levels, 3, 8, 2, true))), 1.73f);
	expect(near(flipped / span, 1.73f, 0.001f), "flipped magnifier: the ring 1.73x, with the power");
	const float acog = glass_reticle_span(reticle_span(0.038f, 17.2f), 2.74f);
	expect(near(acog / span, 1.138f, 0.01f), "ACOG through its 2.74x glass: 1.14x the 1x span (its art is drawn for 17.2 degrees)");
	expect(glass_reticle_span(span, 0.5f) == 0.0f, "below 1x: none");

	// Owner 2026-11-24: the optic's own reticle material (ps cb1 as live, the AUG's ACOG quads).
	{
		const float lines_block[20]{ 1, 1, 1, 1, 1, 0, 0, 0, 0.135f, 0, 0, 0, 2.0f, 0, 0, 0, 0.5f, 0.11f, 0, 0 };
		const reticle_material lines = read_reticle_material(lines_block);
		expect(lines.known && lines.size == 0.135f && lines.aspect == 2.0f && lines.anchor[0] == 0.5f && lines.anchor[1] == 0.11f, "ACOG lines: size, aspect 2, anchor on the bar line");
		const float chevron_block[20]{ 1, 1, 1, 1, 1, 0, 0, 0, 0.045f, 0, 0, 0, 0.65f, 0, 0, 0, 0.5f, 0.5f, 0, 0 };
		expect(read_reticle_material(chevron_block).known && read_reticle_material(chevron_block).aspect == 0.65f, "ACOG chevron: aspect 0.65");
		// A fire particle quad in the same state (live: 0.25 0 0 0 | 0 1.837 0 69.3 | 1.108 ...) is no reticle material.
		const float fire_block[20]{ 0.25f, 0, 0, 0, 0, 1.837f, 0, 69.306f, 1.108f, 0, 1.4653f, 375.83f, 0, 0, 0, 1, 0.269f, 0.746f, 0.609f, 1.335f };
		const reticle_material fire = read_reticle_material(fire_block);
		expect(!fire.known && fire.size == 0.038f && fire.aspect == 1.0f, "fire quad: not a reticle, the old default");
		// The ACOG's full-height quad anchored at its corner (live: size 1, aspect 1, anchor 1 1): not a reticle on the line.
		const float corner_block[20]{ 1, 1, 1, 1, 1, 0, 0, 0, 1.0f, 0, 0, 0, 1.0f, 0, 0, 0, 1.0f, 1.0f, 0, 0 };
		expect(!read_reticle_material(corner_block).known, "a material anchored at its corner: not the line's reticle");
		// A material with only a size (as read before): that size, square and centred.
		const float size_only[20]{ 0, 0, 0, 0, 0, 0, 0, 0, 0.040f };
		const reticle_material old = read_reticle_material(size_only);
		expect(!old.known && old.size == 0.040f && old.anchor[0] == 0.5f && old.anchor[1] == 0.5f, "size only: square, centred");

		// On the line the art's anchor; the lines art's bar line is 0.11 down its height.
		float uv[2]{};
		const float zero[2]{};
		const float lines_span = reticle_span(lines.size, 17.2f);
		expect(reticle_art_uv(lines, lines_span, zero, uv) && near(uv[0], 0.5f) && near(uv[1], 0.11f), "on the line: the anchor");
		// Half the art's width to the right is its right edge (width = aspect x the height span).
		const float right_edge[2]{ 0.5f * lines_span * 2.0f * 0.999f, 0.0f };
		expect(reticle_art_uv(lines, lines_span, right_edge, uv) && near(uv[0], 1.0f, 0.002f), "half the width right: the right edge");
		// Above the line by more than 0.11 of the height: past the art's top (v down).
		const float above[2]{ 0.0f, 0.12f * lines_span };
		expect(!reticle_art_uv(lines, lines_span, above, uv), "above the bar line by 0.12 of the height: outside");
		const float below[2]{ 0.0f, -0.5f * lines_span };
		expect(reticle_art_uv(lines, lines_span, below, uv) && near(uv[1], 0.61f), "half the height below the line: v 0.61 (the ladder)");
		expect(!reticle_art_uv(lines, 0.0f, zero, uv), "no span: nothing");
	}

	// Scope reticles from their quad's corners (live vertex bytes, 2026-11-24: position halves at byte 0, uv at byte 24).
	{
		expect(near(half_to_float(0x2837), 0.03293f, 1e-5f) && near(half_to_float(0xa81a), -0.03204f, 1e-5f) && half_to_float(0x3c00) == 1.0f
				&& half_to_float(0x0000) == 0.0f && half_to_float(0x8000) == 0.0f,
			"half floats");
		const auto quad = [](const std::uint16_t (&words)[4][4], float (&position)[4][2], float (&uv)[4][2]) {
			for (int k = 0; k < 4; ++k)
			{
				position[k][0] = half_to_float(words[k][0]);
				position[k][1] = half_to_float(words[k][1]);
				uv[k][0] = half_to_float(words[k][2]);
				uv[k][1] = half_to_float(words[k][3]);
			}
		};
		float position[4][2]{}, uv[4][2]{};
		reticle_material art;
		// The ACOG's line quad: its origin is the material's anchor (0.5, 0.11), the model's +x left (u runs the other way).
		const std::uint16_t acog_lines[4][4]{ { 0xa834, 0x1af7, 0x3c00, 0x0000 }, { 0x283a, 0x1af7, 0x0000, 0x0000 }, { 0xa834, 0xa71f, 0x3c00, 0x3c00 },
			{ 0x283a, 0xa71f, 0x0000, 0x3c00 } };
		quad(acog_lines, position, uv);
		expect(scope_art_from_quad(position, uv, art) && near(art.anchor[0], 0.5f, 0.01f) && near(art.anchor[1], 0.11f, 0.01f) && near(art.aspect, 2.1f, 0.05f),
			"ACOG lines from the quad: the material's anchor, aspect ~2");
		// The SRS's 8x rifle scope: a crosshair centred on the line, about square.
		const std::uint16_t eight[4][4]{ { 0x2837, 0xa81a, 0x8000, 0x3c00 }, { 0xa837, 0xa81a, 0x3c00, 0x3c00 }, { 0x2837, 0x281b, 0x8000, 0x0002 },
			{ 0xa837, 0x281b, 0x3c00, 0x0002 } };
		quad(eight, position, uv);
		expect(scope_art_from_quad(position, uv, art) && near(art.anchor[0], 0.5f, 0.01f) && near(art.anchor[1], 0.5f, 0.01f) && near(art.aspect, 1.03f, 0.02f),
			"8x scope: centred, square");
		// The PKS-07: 6.6 x 3.7 cm, the line near the top (its chevron and bar line).
		const std::uint16_t pks[4][4]{ { 0x2833, 0xa899, 0x0000, 0x3c00 }, { 0xa83b, 0xa899, 0x3c00, 0x3c00 }, { 0xa83b, 0x1321, 0x3c00, 0x17a9 },
			{ 0x2833, 0x1321, 0x0000, 0x17a9 } };
		quad(pks, position, uv);
		expect(scope_art_from_quad(position, uv, art) && near(art.anchor[0], 0.5f, 0.01f) && art.anchor[1] > 0.01f && art.anchor[1] < 0.05f && near(art.aspect, 1.79f, 0.03f),
			"PKS-07: the line near its top");
		// A fire particle's quad (metres away, not round the origin): no scope art.
		const std::uint16_t fire[4][4]{ { 0x4129, 0x8029, 0x3c00, 0x3c00 }, { 0x4129, 0x0029, 0x3c00, 0x0000 }, { 0xc129, 0x8029, 0x0000, 0x3c00 },
			{ 0xc129, 0x0029, 0x0000, 0x0000 } };
		quad(fire, position, uv);
		expect(!scope_art_from_quad(position, uv, art), "a large quad: no scope art");
		// A quad whose origin is its corner (the ACOG draws one, anchor 1 1): no scope art.
		const std::uint16_t corner[4][4]{ { 0x0000, 0x0000, 0x3c00, 0x3c00 }, { 0xa83b, 0x0000, 0x0000, 0x3c00 }, { 0x0000, 0x283b, 0x3c00, 0x0000 },
			{ 0xa83b, 0x283b, 0x0000, 0x0000 } };
		quad(corner, position, uv);
		expect(!scope_art_from_quad(position, uv, art), "origin at a corner: no scope art");
		// Width 0.8 of the screen height at the level: the 8x at 8.7 degrees.
		expect(near(scope_art_span(1.0f, 8.7f), 0.8f * 2.0f * std::tan(4.35f * 3.14159265f / 180.0f), 1e-5f) && scope_art_span(2.0f, 8.7f) < scope_art_span(1.0f, 8.7f)
				&& scope_art_span(1.0f, 0.0f) == 0.0f,
			"scope art span");
	}

	float right[3]{}, up[3]{};
	{
		// Straight ahead, upright: right is +x, up is +y.
		const float along[3]{ 0.0f, 0.0f, -1.0f }, gun_up[3]{ 0.0f, 1.0f, 0.0f };
		expect(lens_axes(along, gun_up, right, up), "upright axes");
		expect(near(right[0], 1.0f) && near(up[1], 1.0f), "upright: right +x, up +y");
	}
	{
		// The gun canted 30 degrees right (head level): the axes turn with the gun.
		const float s = std::sin(0.5235988f), c = std::cos(0.5235988f);
		const float along[3]{ 0.0f, 0.0f, -1.0f }, gun_up[3]{ s, c, 0.0f };
		expect(lens_axes(along, gun_up, right, up), "canted axes");
		expect(near(up[0], s) && near(up[1], c) && near(right[0], c) && near(right[1], -s), "canted 30: right and up turn with the gun");
	}
	{
		// An up not quite square to the line (the sight 2 degrees below the bore): made square.
		const float along[3]{ 0.0f, -0.0349f, -0.9994f }, gun_up[3]{ 0.0f, 1.0f, 0.0f };
		expect(lens_axes(along, gun_up, right, up), "nearly square axes");
		const float dot = up[0] * along[0] + up[1] * along[1] + up[2] * along[2];
		expect(near(dot, 0.0f), "up square to the line");
	}
	{
		const float along[3]{ 0.0f, 1.0f, 0.0f }, gun_up[3]{ 0.0f, 1.0f, 0.0f };
		expect(!lens_axes(along, gun_up, right, up), "up along the line: no axes");
	}

	// #139 scope_glass: the scope picture's uv per unit of slope through the glass.
	{
		// The picture is rendered along the eye's view axis; the gun's line may point elsewhere (live hold: 6.7 degrees
		// below the view's centre).
		float uv[2]{};
		const float ahead[3]{ 0.0f, 0.0f, -1.0f }, gun_right[3]{ 1.0f, 0.0f, 0.0f }, gun_up[3]{ 0.0f, 1.0f, 0.0f };
		const float zero[2]{ 0.0f, 0.0f };
		expect(glass_picture_uv(ahead, gun_right, gun_up, zero, 4.0f, 0.2f, uv) && near(uv[0], 0.5f) && near(uv[1], 0.5f), "line on the view axis: the picture's centre");
		// 0.4 off the line through a 4x glass is 0.1 in the world: a quarter of a 0.2 disc's width right of the centre.
		const float aside[2]{ 0.4f, 0.0f };
		expect(glass_picture_uv(ahead, gun_right, gun_up, aside, 4.0f, 0.2f, uv) && near(uv[0], 0.75f) && near(uv[1], 0.5f), "4x: 0.4 through the glass is 0.1 in the world");
		const float s = std::sin(0.1169f), c = std::cos(0.1169f);
		const float low[3]{ 0.0f, -s, -c }, low_up[3]{ 0.0f, c, -s };
		expect(glass_picture_uv(low, gun_right, low_up, zero, 2.74f, 0.25f, uv) && near(uv[0], 0.5f) && near(uv[1], 0.5f + 0.5f * (s / c) / 0.25f, 1e-3f),
			"a line 6.7 degrees low: its point lies that far below the picture's centre");
		expect(!glass_picture_uv(ahead, gun_right, gun_up, aside, 1.0f, 0.2f, uv), "1x: 0.4 off is outside a 0.2 disc");
		expect(!glass_picture_uv(ahead, gun_right, gun_up, zero, 0.9f, 0.2f, uv) && !glass_picture_uv(ahead, gun_right, gun_up, zero, 2.0f, 0.0f, uv), "unusable power or disc");
		// The reach the picture needs: the line's tangent off the axis plus 0.38 / power, 15 % room.
		expect(near(glass_picture_reach(ahead, 2.0f), 1.15f * 0.19f), "line on the axis: the field stop's world width");
		expect(near(glass_picture_reach(low, 2.74f), 1.15f * (s / c + 0.38f / 2.74f), 1e-3f), "a low line: its offset added");
		const float back[3]{ 0.0f, 0.0f, 1.0f };
		expect(glass_picture_reach(back, 2.0f) == 0.0f, "a line pointing back: none");

		// #158: the picture is rendered along the gun's line (the scope camera). The low line again, with the picture turned
		// down onto it (a pitch of -6.7 degrees about x): the line is the picture's centre.
		const float half = -0.5f * 0.1169f;
		const float pitched[4]{ std::sin(half), 0.0f, 0.0f, std::cos(half) };
		expect(glass_picture_uv(low, gun_right, low_up, zero, 2.74f, 0.25f, pitched, uv) && near(uv[0], 0.5f) && near(uv[1], 0.5f),
			"picture along the line: the line at its centre");
		// The head rolled 20 degrees right at the stock (the eye's view rolls, the gun does not): in the eye's view the gun's
		// right runs 20 degrees up the other way; the picture keeps the gun's roll, so a point right of the line through
		// the glass lies right of the picture's centre, level.
		const float roll = 20.0f * 3.14159265f / 180.0f;
		const float rolled_right[3]{ std::cos(roll), std::sin(roll), 0.0f }, rolled_up[3]{ -std::sin(roll), std::cos(roll), 0.0f };
		const float picture_roll[4]{ 0.0f, 0.0f, std::sin(0.5f * roll), std::cos(0.5f * roll) };
		expect(glass_picture_uv(ahead, rolled_right, rolled_up, aside, 4.0f, 0.2f, picture_roll, uv) && near(uv[0], 0.75f) && near(uv[1], 0.5f),
			"head rolled: the picture stays level with the gun");
		// The same with the old assumption (the picture on the eye's axes) tilts it: the bug of #158.
		expect(glass_picture_uv(ahead, rolled_right, rolled_up, aside, 4.0f, 0.2f, uv) && !near(uv[1], 0.5f, 0.02f), "on the eye's axes the point tilts");
	}
	{
		// The field stop (owner 2026-11-24: a VR eye box): the whole 0.38 within 1.5 cm of the line, then a smooth close
		// over 2.25 cm more.
		expect(near(glass_field_stop(0.0f), 0.38f), "on the line: 0.38");
		expect(near(glass_field_stop(0.012f), 0.38f) && near(glass_field_stop(0.015f), 0.38f), "1.2 and 1.5 cm off: still the whole field (it was nearly shut)");
		expect(glass_field_stop(0.025f) < 0.38f && glass_field_stop(0.025f) > 0.1f, "2.5 cm off: darkened from the edge, not shut");
		expect(glass_field_stop(0.0375f) == 0.0f && glass_field_stop(0.05f) == 0.0f, "past the box and its fade: dark");
		expect(near(glass_field_stop(-0.02f), glass_field_stop(0.02f)), "either side alike");
		expect(near(glass_field_stop(0.025f, 0.03f), 0.38f) && glass_field_stop(0.02f, 0.005f) == 0.0f, "the box follows the setting");
	}
	{
		// Which lens draws are the glass (live counts: ACOG two draws 5.8k / 5.7k pixels, 29 at the line's point).
		glass_choice kept;
		const std::uint64_t acog_area[2]{ 5800, 5700 }, acog_line[2]{ 29, 29 };
		expect(choose_glass(kept, acog_area, acog_line, 2) == 3u && kept.smallest == 1, "ACOG: both draws on the line are the glass");
		// The eye moves 1.7 cm off the line: no draw at its point any more, same optic (the draw partly hidden): kept.
		const std::uint64_t off_area[2]{ 5260, 4900 }, off_line[2]{ 0, 0 };
		expect(choose_glass(kept, off_area, off_line, 2) == 3u, "off the line: the glass is kept");
		// One of the kept draws shrinks a lot (live: the ACOG's 30-index glass, partly hidden off the line) while the
		// two together stay within half to double: still kept.
		const std::uint64_t shrunk_area[2]{ 5600, 900 };
		expect(choose_glass(kept, shrunk_area, off_line, 2) == 3u, "one glass draw shrinks: kept by the total");
		// The third glass draw of a three-draw optic is not drawn off the line: the other two still hold it.
		glass_choice three;
		const std::uint64_t on3[3]{ 6100, 5800, 900 }, line3[3]{ 26, 26, 26 };
		expect(choose_glass(three, on3, line3, 3) == 7u, "three glass draws on the line");
		const std::uint64_t off2[2]{ 5700, 5250 }, none2[2]{ 0, 0 };
		expect(choose_glass(three, off2, none2, 2) == 7u, "one of them not drawn off the line: kept");
		// Another optic (SRS: two large draws beside its line): not kept, no glass.
		const std::uint64_t srs_area[2]{ 116000, 106000 };
		expect(choose_glass(kept, srs_area, off_line, 2) == 0u && kept.smallest == -1, "another optic off its line: no glass");
		// Nothing on the line from the start: no glass; then the line meets a draw: that one.
		glass_choice fresh;
		expect(choose_glass(fresh, srs_area, off_line, 2) == 0u, "no draw on the line: no glass");
		const std::uint64_t mixed_area[3]{ 116000, 4000, 900 }, mixed_line[3]{ 0, 25, 0 };
		expect(choose_glass(fresh, mixed_area, mixed_line, 3) == 2u, "only the draw on the line, not the smaller one beside it");
		// No lens draw this time (the optic is put away): forgotten.
		expect(choose_glass(fresh, mixed_area, mixed_line, 0) == 0u && fresh.smallest == -1, "no lens draws: forgotten");
	}

	if (failures == 0)
	{
		std::printf("lens_math: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
