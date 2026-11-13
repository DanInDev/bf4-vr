#include "ui/calibration_screen.hpp"

#include "ui/vector_art.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace bfvr::ui
{
	namespace
	{
		constexpr std::uint32_t accent = 0xF28C1C;  // orange: the current step, lit segments, pressed triggers
		constexpr std::uint32_t done_green = 0x7CD992;
		constexpr std::uint32_t text_main = 0xE8ECF0;
		constexpr std::uint32_t text_dim = 0x8C97A3;
		constexpr std::uint32_t line_dim = 0x3A4450;
		constexpr std::uint32_t figure_idle = 0x9AA6B2;
		constexpr std::uint32_t segment_off = 0x232A33;
		constexpr std::uint32_t chip_off = 0x2A323C;

		constexpr int margin = 64;
		constexpr int figure_centre = 410;
		constexpr int steps_left = 860;
		constexpr int bar_top = 608;

		void box(std::vector<draw_item>& items, rect area, std::uint32_t color, int radius = 0)
		{
			draw_item item;
			item.kind = draw_item::type::box;
			item.area = area;
			item.color = color;
			item.radius = radius;
			items.push_back(item);
		}

		void polygon(std::vector<draw_item>& items, std::vector<int> points, std::uint32_t color)
		{
			draw_item item;
			item.kind = draw_item::type::polygon;
			item.points = std::move(points);
			item.color = color;
			// The bounding box, so on-screen checks see polygons too.
			int x0 = item.points[0], x1 = item.points[0], y0 = item.points[1], y1 = item.points[1];
			for (std::size_t i = 0; i + 1 < item.points.size(); i += 2)
			{
				x0 = std::min(x0, item.points[i]);
				x1 = std::max(x1, item.points[i]);
				y0 = std::min(y0, item.points[i + 1]);
				y1 = std::max(y1, item.points[i + 1]);
			}
			item.area = { x0, y0, x1 - x0, y1 - y0 };
			items.push_back(item);
		}

		// A parallelogram leaning right by skew pixels (labels, loading segments, accents).
		void slanted(std::vector<draw_item>& items, rect r, int skew, std::uint32_t color)
		{
			polygon(items, { r.x + skew, r.y, r.x + r.w, r.y, r.x + r.w - skew, r.y + r.h, r.x, r.y + r.h }, color);
		}

		// A box with its top-left and bottom-right corners cut off (panels, step chips).
		void chamfered(std::vector<draw_item>& items, rect r, int cut, std::uint32_t color)
		{
			polygon(items, { r.x + cut, r.y, r.x + r.w, r.y, r.x + r.w, r.y + r.h - cut, r.x + r.w - cut, r.y + r.h, r.x, r.y + r.h, r.x, r.y + cut }, color);
		}

		void text(std::vector<draw_item>& items, rect area, std::uint32_t color, std::string content, int size, int align = 0, bool heading = false, bool wrap = false)
		{
			draw_item item;
			item.kind = draw_item::type::text;
			item.area = area;
			item.color = color;
			item.text = std::move(content);
			item.size = size;
			item.align = align;
			item.bold = heading;
			item.face = heading ? 1 : 0;
			item.wrap = wrap;
			items.push_back(item);
		}

		// Tactical corner brackets around the screen.
		void corners(std::vector<draw_item>& items, std::uint32_t color)
		{
			constexpr int arm = 56, thick = 4, inset = 20;
			const int right = calibration_width - inset, bottom = calibration_height - inset;
			box(items, { inset, inset, arm, thick }, color);
			box(items, { inset, inset, thick, arm }, color);
			box(items, { right - arm, inset, arm, thick }, color);
			box(items, { right - thick, inset, thick, arm }, color);
			box(items, { inset, bottom - thick, arm, thick }, color);
			box(items, { inset, bottom - arm, thick, arm }, color);
			box(items, { right - arm, bottom - thick, arm, thick }, color);
			box(items, { right - thick, bottom - arm, thick, arm }, color);
		}

		void dashed_horizontal(std::vector<draw_item>& items, int x0, int x1, int y, std::uint32_t color, bool tick_left = true, bool tick_right = true)
		{
			for (int x = x0; x < x1; x += 18)
			{
				box(items, { x, y, std::min(10, x1 - x), 2 }, color);
			}
			if (tick_left)
			{
				box(items, { x0, y - 8, 2, 18 }, color);
			}
			if (tick_right)
			{
				box(items, { x1 - 2, y - 8, 2, 18 }, color);
			}
		}

		void dashed_vertical(std::vector<draw_item>& items, int x, int y0, int y1, std::uint32_t color)
		{
			for (int y = y0; y < y1; y += 18)
			{
				box(items, { x, y, 2, std::min(10, y1 - y) }, color);
			}
			box(items, { x - 8, y0, 18, 2 }, color);
			box(items, { x - 8, y1 - 2, 18, 2 }, color);
		}

		std::string metres(float value)
		{
			char out[32];
			std::snprintf(out, sizeof(out), "%.2f m", value);
			return out;
		}

		// The T-pose figure, built from rounded bars: head, torso, arms straight out, legs; a trigger chip
		// under each hand lights when that trigger is held.
		// Our soldier (assets/ui/soldier-tpose.svg), tinted by the state: grey, orange once the arms are
		// out, green when done. The arm-span and height guides sit behind it; a trigger chip under each
		// glove lights while that trigger is held.
		void figure(std::vector<draw_item>& items, const calibration_view& view, int step)
		{
			// The art's layout in viewBox units (see the comment in the SVG).
			constexpr float art_top = 56.0f, art_feet = 952.0f, art_centre = 500.0f;
			constexpr float glove_x = 100.0f, glove_y = 295.0f, glove_radius = 30.0f;
			constexpr float scale = 0.43f;
			constexpr int top = 204;
			const auto sx = [](float x) { return static_cast<int>(std::lround(figure_centre + (x - art_centre) * scale)); };
			const auto sy = [](float y) { return static_cast<int>(std::lround(top + (y - art_top) * scale)); };
			const bool finished = step >= 4;
			const std::uint32_t body = finished ? done_green : view.arms_out ? accent : figure_idle;

			const int span_left = sx(glove_x), span_right = sx(1000.0f - glove_x);
			const int span_y = sy(glove_y - 80.0f);
			// Broken around the head so it does not cross the face.
			dashed_horizontal(items, span_left, sx(art_centre - 90.0f), span_y, line_dim, true, false);
			dashed_horizontal(items, sx(art_centre + 90.0f), span_right, span_y, line_dim, false, true);
			const std::string span_label = finished && view.span > 0.0f ? "ARM SPAN  " + metres(view.span) : "ARM SPAN";
			text(items, { span_left - 6, span_y - 34, 240, 28 }, finished ? done_green : text_dim, span_label, 20, 0, true);
			const int height_x = span_right + 60;
			const int head = sy(art_top), feet = sy(art_feet);
			dashed_vertical(items, height_x, head, feet, line_dim);
			const std::string height_label = finished && view.measured > 0.0f ? metres(view.measured) : "HEIGHT";
			text(items, { height_x + 14, (head + feet) / 2 - 14, 130, 28 }, finished ? done_green : text_dim, height_label, 20, 0, true);

			for (const vector_shape& shape : soldier_tpose().shapes)
			{
				std::vector<int> points;
				points.reserve(shape.xy.size());
				for (std::size_t i = 0; i < shape.xy.size(); ++i)
				{
					points.push_back(i % 2 ? sy(shape.xy[i]) : sx(shape.xy[i]));
				}
				polygon(items, std::move(points), shape.cut ? calibration_backdrop : body);
			}

			for (const int side : { -1, 1 })
			{
				const int glove = sx(side < 0 ? glove_x : 1000.0f - glove_x);
				const int chip_top = sy(glove_y + glove_radius) + 10;
				const bool pressed = side < 0 ? view.left_trigger : view.right_trigger;
				box(items, { glove - 26, chip_top, 52, 14 }, pressed ? accent : chip_off, 7);
				text(items, { glove - 80, chip_top + 18, 160, 26 }, pressed ? accent : text_dim, side < 0 ? "LEFT TRIGGER" : "RIGHT TRIGGER", 18, 1, true);
			}
		}

		void step_row(std::vector<draw_item>& items, int index, int current, int top, const char* title, const char* detail)
		{
			const bool done = current > index;
			const bool now = current == index;
			const std::uint32_t chip = done ? done_green : now ? accent : chip_off;
			chamfered(items, { steps_left, top, 58, 58 }, 12, chip);
			text(items, { steps_left, top, 58, 58 }, done || now ? 0x10141A : text_dim, done ? "OK" : std::to_string(index), done ? 24 : 32, 1, true);
			text(items, { steps_left + 82, top - 4, 560, 40 }, done ? text_dim : now ? text_main : text_dim, title, 34, 0, true);
			text(items, { steps_left + 82, top + 36, 600, 30 }, text_dim, detail, 22);
			if (now)
			{
				box(items, { steps_left - 22, top, 6, 58 }, accent);
			}
		}
	}

	std::vector<see_through> calibration_keys()
	{
		return { { calibration_backdrop, 205 }, { calibration_scanline, 215 }, { calibration_panel, 232 } };
	}

	int calibration_step(const calibration_view& view)
	{
		if (view.at == calibration_view::stage::done || view.at == calibration_view::stage::kept)
		{
			return 4;
		}
		if (view.lower_first)
		{
			return 1;
		}
		if (view.holding)
		{
			return 3;
		}
		if (view.arms_out)
		{
			return 2;
		}
		return 1;
	}

	int calibration_segments_lit(const calibration_view& view)
	{
		if (calibration_step(view) >= 4)
		{
			return calibration_segments;
		}
		if (!view.holding)
		{
			return 0;
		}
		const float p = std::clamp(view.progress, 0.0f, 1.0f);
		return std::clamp(static_cast<int>(std::floor(p * calibration_segments)), 0, calibration_segments);
	}

	std::vector<draw_item> calibration_screen(const calibration_view& view)
	{
		std::vector<draw_item> items;
		const int step = calibration_step(view);
		const bool finished = step >= 4;
		box(items, { 0, 0, calibration_width, calibration_height }, calibration_backdrop);
		for (int y = 1; y < calibration_height; y += 4)
		{
			box(items, { 0, y, calibration_width, 1 }, calibration_scanline);
		}
		// The panel behind the steps.
		chamfered(items, { steps_left - 44, 200, calibration_width - margin - (steps_left - 44), 384 }, 28, calibration_panel);
		corners(items, finished ? done_green : accent);

		// Header: the slanted tag, the title, the step counter.
		slanted(items, { margin, 52, 290, 40 }, 14, finished ? done_green : accent);
		text(items, { margin + 26, 52, 260, 40 }, 0x10141A, "BODY CALIBRATION", 26, 0, true);
		const char* title = "STAND IN A T-POSE";
		if (view.at == calibration_view::stage::confirm)
		{
			title = "CONFIRM YOUR HEIGHT";
		}
		else if (view.at == calibration_view::stage::done)
		{
			title = "BODY FITTED";
		}
		else if (view.at == calibration_view::stage::kept)
		{
			title = "HEIGHT KEPT";
		}
		text(items, { margin, 92, 1100, 80 }, finished ? done_green : text_main, title, 70, 0, true);
		char counter[32];
		std::snprintf(counter, sizeof(counter), finished ? "COMPLETE" : "STEP %d / 3", step);
		text(items, { calibration_width - margin - 400, 54, 400, 38 }, finished ? done_green : accent, counter, 28, 2, true);
		box(items, { margin, 184, calibration_width - 2 * margin, 2 }, line_dim);
		slanted(items, { margin, 180, 260, 9 }, 7, finished ? done_green : accent);

		figure(items, view, step);

		// The steps, or what the confirmation needs.
		if (view.at == calibration_view::stage::confirm)
		{
			char line[160];
			std::snprintf(line, sizeof(line), "Measured %s, stored %s.", metres(view.measured).c_str(), metres(view.stored).c_str());
			text(items, { steps_left, 206, 680, 34 }, accent, line, 26, 0, true);
			text(items, { steps_left, 236, 680, 30 }, text_dim, "A second T-pose keeps the new height.", 22);
		}
		const int first_top = view.at == calibration_view::stage::confirm ? 290 : 236;
		step_row(items, 1, step, first_top, view.lower_first ? "LOWER YOUR ARMS" : "ARMS OUT TO THE SIDES",
			view.lower_first ? "Then raise them again for the second pose" : "Straight out at shoulder height, stand tall");
		step_row(items, 2, step, first_top + 106, "HOLD BOTH TRIGGERS", "Index fingers on both triggers, keep them in");
		step_row(items, 3, step, first_top + 212, "HOLD STILL", "Two seconds: measuring height and arm span");

		// The segmented loading bar and the line under it.
		const int segment_gap = 6;
		const int segment_width = (calibration_width - 2 * margin - (calibration_segments - 1) * segment_gap) / calibration_segments;
		const int lit = calibration_segments_lit(view);
		for (int i = 0; i < calibration_segments; ++i)
		{
			slanted(items, { margin + i * (segment_width + segment_gap), bar_top, segment_width, 14 }, 6, i < lit ? (finished ? done_green : accent) : segment_off);
		}
		std::string status;
		if (view.at == calibration_view::stage::done)
		{
			status = "Saved: height " + metres(view.measured) + (view.span > 0.0f ? ", arm span " + metres(view.span) : std::string{});
		}
		else if (view.at == calibration_view::stage::kept)
		{
			status = "No second pose: your height stays " + metres(view.stored);
		}
		else if (view.holding)
		{
			char line[64];
			std::snprintf(line, sizeof(line), "MEASURING  %d %%", static_cast<int>(std::lround(std::clamp(view.progress, 0.0f, 1.0f) * 100.0f)));
			status = line;
		}
		else
		{
			status = step == 1 ? "Waiting for the pose" : "Waiting for both triggers";
		}
		text(items, { margin, bar_top + 26, 900, 36 }, finished ? done_green : view.holding ? accent : text_main, status, 26, 0, true);
		if (!finished && view.seconds_left > 0)
		{
			char skip[64];
			std::snprintf(skip, sizeof(skip), "Skips in %d s", view.seconds_left);
			text(items, { calibration_width - margin - 400, bar_top + 26, 400, 36 }, text_dim, skip, 22, 2);
		}
		return items;
	}
}
