// Checks the in-VR settings panel (src/ui/settings_panel, src/ui/panel_canvas): pages, hit testing,
// clicks on a registry, scrolling, the draw list and GDI rasterising. With a path argument it also
// writes the panel for the real settings as a BMP, to look at without a headset.
#include <cstdio>
#include <cstdlib>
#include <string>

#include "common/settings_store.hpp"
#include "ui/menu_button.hpp"
#include "ui/panel_canvas.hpp"
#include "ui/settings_panel.hpp"

using namespace bfvr;
using namespace bfvr::ui;
using settings::kind;

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

	void sample(settings::registry& store)
	{
		store.add({ "stereo_render", kind::choice, 0, 0, 0, "aer", "aer|pair", "eye rendering" });
		store.add({ "gun_scale", kind::real, 0.25f, 2.0f, 0.05f, "1", "", "apparent gun size" });
		store.add({ "arm_ik", kind::boolean, 0, 0, 0, "1", "", "arms follow the controllers" });
		store.add({ "shoulder_offset", kind::vec3, -0.5f, 0.5f, 0.0f, "0 0 0", "", "shoulders" });
		for (const char* name : { "aim", "two_handed", "aim_convergence", "accessory_toggle", "reload", "drop_discards_ammo", "grenade",
				 "grenade_throw_scale", "holsters", "squad_signal" })
		{
			store.add({ name, kind::boolean, 0, 0, 0, "0", "", "weapon setting" });
		}
		store.add({ "mystery", kind::integer, 0, 10, 1, "5", "", "not on a page" });
	}

	// Texture coordinates of a rect's centre.
	float u_of(const rect& area) { return (static_cast<float>(area.x) + area.w * 0.5f) / settings_panel::width; }
	float v_of(const rect& area) { return (static_cast<float>(area.y) + area.h * 0.5f) / settings_panel::height; }

	target at(const settings_panel& panel, const rect& area)
	{
		return panel.hit(u_of(area), v_of(area));
	}

	int find_row(const settings_panel& panel, const char* name, int component = -1)
	{
		const auto& rows = panel.pages()[static_cast<std::size_t>(panel.current_page())].rows;
		for (int i = 0; i < static_cast<int>(rows.size()); ++i)
		{
			if (rows[static_cast<std::size_t>(i)].name == name && rows[static_cast<std::size_t>(i)].component == component)
			{
				return i - panel.first_row();
			}
		}
		return -1;
	}

	void test_pages()
	{
		settings::registry store;
		sample(store);
		const auto pages = default_pages(store);
		expect(pages.size() == 4, "View, Body, Weapon and Other (empty pages left out)");
		expect(pages[0].title == "View" && pages[0].rows.size() == 2 && pages[0].rows[0].name == "stereo_render", "View rows in table order");
		expect(pages[1].title == "Body" && pages[1].rows.size() == 4 && pages[1].rows[1].component == 0 && pages[1].rows[3].component == 2,
			"vec3 becomes three rows");
		expect(pages[2].title == "Weapon" && pages[2].rows.size() == 10, "weapon page");
		expect(pages[3].title == "Other" && pages[3].rows.size() == 1 && pages[3].rows[0].name == "mystery", "unlisted settings go to Other");
	}

	void test_hits_and_clicks()
	{
		settings::registry store;
		sample(store);
		settings_panel panel(default_pages(store));
		expect(at(panel, panel.page_tab(1)) == target{ target_kind::page, 1 }, "tab hit");
		expect(at(panel, panel.save_button()) == target{ target_kind::save, -1 }, "save hit");
		expect(at(panel, panel.close_button()) == target{ target_kind::close, -1 }, "close hit");
		expect(panel.hit(0.001f, 0.999f).kind == target_kind::none, "corner is nothing");
		expect(at(panel, panel.minus_button(5)).kind == target_kind::none, "no row there on a short page");
		expect(at(panel, panel.rows_down_button()).kind == target_kind::none, "no scrolling on a short page");

		// View page: gun_scale is row 1.
		expect(panel.click(at(panel, panel.plus_button(1)), store) == settings_panel::action::changed && store.text("gun_scale") == "1.05", "plus steps a real");
		expect(panel.click(at(panel, panel.value_box(1)), store) == settings_panel::action::none, "value box of a real does nothing");
		expect(panel.click(at(panel, panel.value_box(0)), store) == settings_panel::action::changed && store.text("stereo_render") == "pair", "value box cycles a choice");
		expect(panel.click(at(panel, panel.minus_button(0)), store) == settings_panel::action::changed && store.text("stereo_render") == "aer", "minus cycles back");
		for (int i = 0; i < 40; ++i)
		{
			panel.click(at(panel, panel.plus_button(1)), store);
		}
		expect(store.real("gun_scale") == 2.0f && panel.click(at(panel, panel.plus_button(1)), store) == settings_panel::action::none, "clamped: no change reported");

		// Body page: booleans toggle, vec3 components step alone.
		panel.click({ target_kind::page, 1 }, store);
		expect(panel.current_page() == 1, "page switched");
		expect(panel.click(at(panel, panel.value_box(find_row(panel, "arm_ik"))), store) == settings_panel::action::changed && !store.boolean("arm_ik"), "toggle bool");
		const int y_row = find_row(panel, "shoulder_offset", 1);
		panel.click(at(panel, panel.plus_button(y_row)), store);
		panel.click(at(panel, panel.plus_button(y_row)), store);
		expect(store.text("shoulder_offset") == "0 0.05 0", "vec3 y steps by a fortieth of the range");
		panel.click(at(panel, panel.minus_button(find_row(panel, "shoulder_offset", 0))), store);
		expect(store.text("shoulder_offset") == "-0.025 0.05 0", "vec3 x steps alone");

		expect(panel.click(at(panel, panel.save_button()), store) == settings_panel::action::save, "save action");
		expect(panel.click(at(panel, panel.close_button()), store) == settings_panel::action::close, "close action");
	}

	void test_scrolling()
	{
		settings::registry store;
		sample(store);
		settings_panel panel(default_pages(store));
		panel.click({ target_kind::page, 2 }, store); // Weapon: 10 rows, 8 visible
		expect(panel.visible_rows() == 8, "eight rows fit");
		expect(at(panel, panel.rows_up_button()).kind == target_kind::none, "no up at the top");
		expect(at(panel, panel.rows_down_button()).kind == target_kind::rows_down, "down offered");
		panel.click({ target_kind::rows_down, -1 }, store);
		expect(panel.first_row() == 2, "scrolls to show the last rows");
		expect(at(panel, panel.rows_down_button()).kind == target_kind::none && at(panel, panel.rows_up_button()).kind == target_kind::rows_up, "at the end");
		panel.click({ target_kind::rows_up, -1 }, store);
		expect(panel.first_row() == 0, "back to the top");
		panel.click({ target_kind::rows_down, -1 }, store);
		panel.click({ target_kind::page, 0 }, store);
		expect(panel.first_row() == 0, "a page switch starts at the top");
	}

	bool has_text(const std::vector<draw_item>& items, const std::string& content)
	{
		for (const draw_item& item : items)
		{
			if (item.kind == draw_item::type::text && item.text == content)
			{
				return true;
			}
		}
		return false;
	}

	void test_draw_and_raster()
	{
		settings::registry store;
		sample(store);
		settings_panel panel(default_pages(store));
		auto items = panel.draw(store, "");
		expect(has_text(items, "stereo_render") && has_text(items, "aer") && has_text(items, "1") && has_text(items, "Save"), "rows and buttons drawn");
		expect(!has_text(items, "unsaved changes"), "nothing unsaved yet");
		panel.hover(at(panel, panel.plus_button(1)));
		store.step("gun_scale", 1);
		items = panel.draw(store, "");
		expect(has_text(items, "apparent gun size") && has_text(items, "unsaved changes"), "hovered row's help and the unsaved note");
		expect(has_text(panel.draw(store, "saved"), "saved"), "status replaces the note");

		std::vector<std::uint32_t> pixels;
		expect(rasterise(items, settings_panel::width, settings_panel::height, pixels), "rasterise");
		expect(pixels.size() == static_cast<std::size_t>(settings_panel::width * settings_panel::height), "pixel count");
		const auto pixel = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * settings_panel::width + static_cast<std::size_t>(x)]; };
		expect(pixel(settings_panel::width / 2, settings_panel::height - 5) == 0xFF1E2228, "background colour, opaque");
		const rect save = panel.save_button();
		expect(pixel(save.x + 3, save.y + 3) == 0xFF2F7D4F, "save button colour");
		int text_pixels = 0;
		for (int x = save.x; x < save.x + save.w; ++x)
		{
			text_pixels += pixel(x, save.y + save.h / 2) != 0xFF2F7D4F ? 1 : 0;
		}
		expect(text_pixels > 5, "the Save label is drawn");
	}
	void test_menu_button()
	{
		constexpr float frame = 1.0f / 90.0f;
		menu_button button;
		int shorts = 0, longs = 0;
		const auto run = [&](bool down, float seconds)
		{
			for (float t = 0; t < seconds - 1e-6f; t += frame)
			{
				const menu_button_events events = button.update(down, frame);
				shorts += events.short_press ? 1 : 0;
				longs += events.long_press ? 1 : 0;
			}
		};
		run(false, 0.2f);
		expect(shorts == 0 && longs == 0, "idle button sends nothing");
		run(true, 0.3f);
		expect(shorts == 0 && longs == 0, "nothing while a short press is held (Start waits for the release)");
		run(false, 0.1f);
		expect(shorts == 1 && longs == 0, "short press at release");
		run(true, 2.0f);
		expect(longs == 1 && shorts == 1, "long press once, however long it is held");
		run(false, 0.1f);
		expect(shorts == 1, "no short press after a long one");
		run(true, 0.65f);
		run(false, frame);
		expect(longs == 2 && shorts == 1, "a second long press");
	}
}

int main(int argc, char** argv)
{
	test_menu_button();
	test_pages();
	test_hits_and_clicks();
	test_scrolling();
	test_draw_and_raster();

	// Every live setting sits on a named page (nothing falls through to Other).
	settings::load_global();
	const auto pages = default_pages(settings::global());
	expect(!pages.empty() && pages.back().title != "Other", "every live setting is on a named page");
	if (argc > 1)
	{
		settings_panel panel(pages);
		panel.click({ target_kind::page, argc > 2 ? std::atoi(argv[2]) : 0 }, settings::global());
		panel.hover(at(panel, panel.plus_button(1)));
		std::vector<std::uint32_t> pixels;
		const bool written = rasterise(panel.draw(settings::global(), ""), settings_panel::width, settings_panel::height, pixels)
			&& write_bmp(argv[1], pixels, settings_panel::width, settings_panel::height);
		std::printf("snapshot %s: %s\n", argv[1], written ? "written" : "failed");
	}
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("settings_panel: all checks passed\n");
	return 0;
}
