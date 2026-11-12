// Checks the calibration step's screen (src/ui/calibration_screen): which step is current, how many
// loading segments are lit, the titles per stage, and that everything stays on the screen.
// With a folder as the first argument it also writes each stage as a BMP (for looking at the design).
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>

#include "ui/calibration_screen.hpp"
#include "ui/panel_canvas.hpp"
#include "ui/vector_art.hpp"

using namespace bfvr::ui;

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

	bool has_text(const std::vector<draw_item>& items, const std::string& wanted)
	{
		for (const draw_item& item : items)
		{
			if (item.kind == draw_item::type::text && item.text.find(wanted) != std::string::npos)
			{
				return true;
			}
		}
		return false;
	}

	bool inside(const std::vector<draw_item>& items)
	{
		for (const draw_item& item : items)
		{
			if (item.area.x < 0 || item.area.y < 0 || item.area.x + item.area.w > calibration_width || item.area.y + item.area.h > calibration_height)
			{
				std::printf("  off screen: \"%s\" at %d,%d %dx%d\n", item.text.c_str(), item.area.x, item.area.y, item.area.w, item.area.h);
				return false;
			}
		}
		return true;
	}

	struct example
	{
		const char* name;
		calibration_view view;
	};
}

int main(int argc, char** argv)
{
	calibration_view waiting;
	waiting.seconds_left = 38;
	calibration_view arms = waiting;
	arms.arms_out = true;
	arms.left_trigger = true;
	calibration_view measuring = arms;
	measuring.right_trigger = true;
	measuring.holding = true;
	measuring.progress = 0.5f;
	calibration_view confirm = waiting;
	confirm.at = calibration_view::stage::confirm;
	confirm.measured = 1.97f;
	confirm.stored = 1.86f;
	confirm.lower_first = true;
	calibration_view done;
	done.at = calibration_view::stage::done;
	done.measured = 1.86f;
	done.span = 1.83f;
	calibration_view kept;
	kept.at = calibration_view::stage::kept;
	kept.stored = 1.86f;

	// Our vector art: the subset reader, then the soldier compiled in from assets/ui/soldier-tpose.svg.
	const vector_art tiny = parse_vector_art(
		"<svg viewBox=\"0 0 100 50\"><rect fill=\"#1e2228\"/>"
		"<path fill=\"#ffffff\" d=\"M 0 0 L 10 0 10 10 Z\"/>"   // an implicit second line after L
		"<path fill=\"#000000\" d=\"M 0 0 Q 50 50 100 0 Z\"/>"  // a curve, flattened
		"<path fill=\"#ff0000\" d=\"M 0 0 L 1 1 L 2 0 Z\"/>"    // another colour: not ours, skipped
		"<path fill=\"#ffffff\" d=\"m 0 0 l 1 1 l 1 -1 z\"/>"   // relative commands: outside the subset, skipped
		"</svg>");
	expect(tiny.width == 100.0f && tiny.height == 50.0f, "the viewBox");
	expect(tiny.shapes.size() == 2, "two shapes in the subset, the red and the relative one left out");
	expect(tiny.shapes.size() == 2 && !tiny.shapes[0].cut && tiny.shapes[0].xy.size() == 6, "M, L and an implicit L: a triangle");
	expect(tiny.shapes.size() == 2 && tiny.shapes[1].cut && tiny.shapes[1].xy.size() == 2 + 2 * 10, "black is a cut; the curve is ten segments");
	const vector_art& soldier = soldier_tpose();
	expect(soldier.width == 1000.0f && soldier.height == 1000.0f && soldier.shapes.size() >= 40, "the soldier art is compiled in and read");
	bool within = true, has_cut = false;
	float left = 1e9f, right = -1e9f;
	for (const vector_shape& shape : soldier.shapes)
	{
		has_cut = has_cut || shape.cut;
		for (std::size_t i = 0; i < shape.xy.size(); i += 2)
		{
			within = within && shape.xy[i] >= 0.0f && shape.xy[i] <= 1000.0f && shape.xy[i + 1] >= 0.0f && shape.xy[i + 1] <= 1000.0f;
			left = std::min(left, shape.xy[i]);
			right = std::max(right, shape.xy[i]);
		}
	}
	expect(within && has_cut, "every point inside the viewBox, with cut lines");
	expect(std::fabs((left + right) - 1000.0f) < 1.0f, "the T-pose is symmetric about the centre");

	expect(calibration_step(waiting) == 1, "no pose: step 1, arms out");
	expect(calibration_step(arms) == 2, "arms out, one trigger: step 2, both triggers");
	expect(calibration_step(measuring) == 3, "holding: step 3, hold still");
	expect(calibration_step(confirm) == 1, "confirm, still in the first pose: step 1, lower the arms");
	expect(calibration_step(done) == 4 && calibration_step(kept) == 4, "done or kept: complete");

	expect(calibration_segments_lit(waiting) == 0 && calibration_segments_lit(arms) == 0, "not measuring: the bar is empty");
	expect(calibration_segments_lit(measuring) == calibration_segments / 2, "half held: half the segments");
	measuring.progress = 1.4f;
	expect(calibration_segments_lit(measuring) == calibration_segments, "progress past 1: full, not more");
	measuring.progress = 0.5f;
	expect(calibration_segments_lit(done) == calibration_segments, "done: the bar full");

	expect(has_text(calibration_screen(waiting), "STAND IN A T-POSE") && has_text(calibration_screen(waiting), "STEP 1 / 3"), "the pose title and step counter");
	expect(has_text(calibration_screen(measuring), "MEASURING  50 %"), "measuring shows its percentage");
	expect(has_text(calibration_screen(confirm), "CONFIRM YOUR HEIGHT") && has_text(calibration_screen(confirm), "1.97 m") && has_text(calibration_screen(confirm), "LOWER YOUR ARMS"),
		"confirm: the title, the new height, lower the arms first");
	expect(has_text(calibration_screen(done), "BODY FITTED") && has_text(calibration_screen(done), "1.86 m") && has_text(calibration_screen(done), "COMPLETE"), "done: fitted, the height");
	expect(has_text(calibration_screen(kept), "HEIGHT KEPT"), "kept: the old height stays");
	expect(has_text(calibration_screen(waiting), "Skips in 38 s") && !has_text(calibration_screen(done), "Skips in"), "the countdown only while waiting");

	const example examples[] = { { "waiting", waiting }, { "triggers", arms }, { "measuring", measuring }, { "confirm", confirm }, { "done", done }, { "kept", kept } };
	for (const example& e : examples)
	{
		const std::vector<draw_item> items = calibration_screen(e.view);
		if (!inside(items))
		{
			std::printf("FAIL: %s: an item leaves the screen\n", e.name);
			++failures;
		}
		std::vector<std::uint32_t> pixels;
		const auto start = std::chrono::steady_clock::now();
		expect(rasterise(items, calibration_width, calibration_height, pixels, calibration_keys()) && pixels.size() == static_cast<std::size_t>(calibration_width) * calibration_height,
			"rasterises at full size");
		const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
		if (argc > 1)
		{
			std::printf("%s: %zu items, rasterised in %.1f ms\n", e.name, items.size(), ms);
			write_bmp(std::string(argv[1]) + "\\calibration-" + e.name + ".bmp", pixels, calibration_width, calibration_height);
		}
	}

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("calibration_screen: all checks passed\n");
	return 0;
}
