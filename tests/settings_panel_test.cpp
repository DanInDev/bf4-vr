// Checks the in-VR settings panel (src/ui/settings_panel, src/ui/panel_canvas): pages, hit testing,
// clicks on a registry, scrolling, the draw list and GDI rasterising. With a path argument it also
// writes the panel for the real settings as a BMP, to look at without a headset.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

#include "common/setting_catalog.hpp"
#include "common/settings_store.hpp"
#include "ui/menu_button.hpp"
#include "ui/panel_canvas.hpp"
#include "ui/magazine_label.hpp"
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
		store.add({ "reload", kind::choice, 0, 0, 0, "button", "button|physical", "reload" });
		for (const char* name : { "sight_ads", "sight_dot", "reticle", "scope", "drop_discards_ammo", "reload_sounds", "grenade_throw_scale",
				 "c4_trigger", "sling", "free_hands", "use_reach", "melee_gesture", "squad_signal" })
		{
			store.add({ name, kind::boolean, 0, 0, 0, "0", "", "weapon setting" });
		}
		store.add({ "mystery", kind::integer, 0, 10, 1, "5", "", "not in the catalog" });
	}

	// Texture coordinates of a rect's centre.
	float u_of(const rect& area) { return (static_cast<float>(area.x) + area.w * 0.5f) / settings_panel::width; }
	float v_of(const rect& area) { return (static_cast<float>(area.y) + area.h * 0.5f) / settings_panel::height; }

	target at(const settings_panel& panel, const rect& area, const settings::registry& store)
	{
		return area.empty() ? target{} : panel.hit(u_of(area), v_of(area), store);
	}

	int find_row(const settings_panel& panel, const char* name, int component = -1)
	{
		const auto& rows = panel.pages()[static_cast<std::size_t>(panel.current_page())].rows;
		for (int i = 0; i < static_cast<int>(rows.size()); ++i)
		{
			if (rows[static_cast<std::size_t>(i)].name == name && rows[static_cast<std::size_t>(i)].component == component)
			{
				return i;
			}
		}
		return -1;
	}

	int find_page(const settings_panel& panel, const char* title)
	{
		for (int i = 0; i < static_cast<int>(panel.pages().size()); ++i)
		{
			if (panel.pages()[static_cast<std::size_t>(i)].title == title)
			{
				return i;
			}
		}
		return -1;
	}

	void test_pages()
	{
		settings::registry store;
		sample(store);
		const auto pages = default_pages(store);
		expect(pages.size() == 4, "View, Body, Weapon and Advanced (empty pages left out)");
		expect(pages[0].title == "View" && pages[0].rows.size() == 4 && pages[0].rows[0].heading == "Rendering" && pages[0].rows[1].name == "stereo_render"
			&& pages[0].rows[2].heading == "Scale" && pages[0].rows[3].name == "gun_scale", "View: sections with their settings");
		expect(pages[1].title == "Body" && pages[1].rows.size() == 2 && pages[1].rows[1].name == "arm_ik", "Body: advanced settings left out");
		expect(pages[2].title == "Weapon" && pages[2].rows.front().is_heading(), "Weapon page starts with a heading");
		const page& advanced = pages[3];
		expect(advanced.title == "Advanced" && advanced.rows.size() == 6 && advanced.rows[0].heading == "Body" && advanced.rows[1].component == 0
			&& advanced.rows[3].component == 2 && advanced.rows[4].heading == "Other" && advanced.rows[5].name == "mystery",
			"Advanced: by page, vec3 as three rows, unknown settings under Other");
		expect(settings_panel::control_of(pages[0].rows[1], store) == row_control::stepper, "a choice with long names steps");
		expect(settings_panel::control_of(pages[1].rows[1], store) == row_control::toggle, "a boolean toggles");
		const auto options = settings_panel::options_of("reload", store);
		expect(options.size() == 2 && options[0].first == "physical" && options[0].second == "By hand" && options[1].second == "Button",
			"options in the catalog's order with their names");
		expect(settings_panel::control_of({ "reload", -1, {} }, store) == row_control::options, "two short options are buttons");
	}

	void test_hits_and_clicks()
	{
		settings::registry store;
		sample(store);
		settings_panel panel(default_pages(store));
		expect(at(panel, panel.page_tab(1), store) == target{ target_kind::page, 1 }, "tab hit");
		expect(at(panel, panel.save_button(), store) == target{ target_kind::save }, "save hit");
		expect(at(panel, panel.close_button(), store) == target{ target_kind::close }, "close hit");
		expect(at(panel, panel.reset_button(), store) == target{ target_kind::reset_page }, "reset hit");
		expect(panel.hit(0.001f, 0.999f, store).kind == target_kind::none, "corner is nothing");
		expect(panel.row_area(9).empty(), "no row there on a short page");
		expect(at(panel, panel.rows_down_button(), store).kind == target_kind::none, "no scrolling on a short page");
		expect(at(panel, panel.row_area(0), store).kind == target_kind::none, "a heading is not clickable");

		// View: stereo_render is row 1 (a stepper), gun_scale row 3.
		expect(panel.click(at(panel, panel.plus_button(3, store), store), store) == settings_panel::action::changed && store.text("gun_scale") == "1.05", "plus steps a real");
		expect(panel.click(at(panel, panel.value_box(3, store), store), store) == settings_panel::action::none, "value of a real does nothing");
		expect(panel.click(at(panel, panel.value_box(1, store), store), store) == settings_panel::action::changed && store.text("stereo_render") == "pair", "value cycles a choice");
		expect(panel.click(at(panel, panel.minus_button(1, store), store), store) == settings_panel::action::changed && store.text("stereo_render") == "aer", "minus cycles back");
		for (int i = 0; i < 40; ++i)
		{
			panel.click(at(panel, panel.plus_button(3, store), store), store);
		}
		expect(store.real("gun_scale") == 2.0f && panel.click(at(panel, panel.plus_button(3, store), store), store) == settings_panel::action::none, "clamped: no change reported");
		expect(panel.click({ target_kind::reset_page }, store) == settings_panel::action::changed && store.text("gun_scale") == "1", "reset page");
		expect(panel.click({ target_kind::reset_page }, store) == settings_panel::action::none, "nothing left to reset");

		// Body: a click anywhere on a toggle row toggles.
		panel.click({ target_kind::page, 1 }, store);
		expect(panel.current_page() == 1, "page switched");
		const rect arm = panel.row_area(find_row(panel, "arm_ik"));
		expect(panel.click(panel.hit((arm.x + 30.0f) / settings_panel::width, v_of(arm), store), store) == settings_panel::action::changed && !store.boolean("arm_ik"),
			"the row's label toggles too");

		// Weapon: option buttons set their value.
		panel.click({ target_kind::page, 2 }, store);
		const int reload = find_row(panel, "reload");
		const target physical = at(panel, panel.option_button(reload, 0, store), store);
		expect(physical == target{ target_kind::option, reload, 0 }, "option hit");
		expect(panel.click(physical, store) == settings_panel::action::changed && store.text("reload") == "physical", "option sets its value");
		expect(panel.click(physical, store) == settings_panel::action::none, "the chosen option again changes nothing");

		// Advanced: vec3 components step alone.
		panel.click({ target_kind::page, 3 }, store);
		const int y_row = find_row(panel, "shoulder_offset", 1);
		panel.click(at(panel, panel.plus_button(y_row, store), store), store);
		panel.click(at(panel, panel.plus_button(y_row, store), store), store);
		expect(store.text("shoulder_offset") == "0 0.05 0", "vec3 y steps by a fortieth of the range");
		panel.click(at(panel, panel.minus_button(find_row(panel, "shoulder_offset", 0), store), store), store);
		expect(store.text("shoulder_offset") == "-0.025 0.05 0", "vec3 x steps alone");

		expect(panel.click(at(panel, panel.save_button(), store), store) == settings_panel::action::save, "save action");
		expect(panel.click(at(panel, panel.close_button(), store), store) == settings_panel::action::close, "close action");
	}

	void test_scrolling()
	{
		settings::registry store;
		sample(store);
		settings_panel panel(default_pages(store));
		panel.click({ target_kind::page, 2 }, store); // Weapon: 16 settings in sections
		const int count = static_cast<int>(panel.pages()[2].rows.size());
		expect(!panel.can_scroll_up() && panel.can_scroll_down(), "a long page scrolls down");
		expect(at(panel, panel.rows_up_button(), store).kind == target_kind::none, "no up at the top");
		expect(at(panel, panel.rows_down_button(), store).kind == target_kind::rows_down, "down offered");
		int guard = 0;
		while (panel.can_scroll_down() && ++guard < 10)
		{
			panel.click({ target_kind::rows_down }, store);
		}
		expect(!panel.row_area(count - 1).empty() && panel.first_row() > 0, "scrolls until the last row shows");
		expect(at(panel, panel.rows_up_button(), store).kind == target_kind::rows_up, "up offered at the end");
		guard = 0;
		while (panel.can_scroll_up() && ++guard < 10)
		{
			panel.click({ target_kind::rows_up }, store);
		}
		expect(panel.first_row() == 0, "back to the top");
		panel.click({ target_kind::rows_down }, store);
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
		expect(has_text(items, "3D rendering") && has_text(items, "Alternate eyes") && has_text(items, "RENDERING") && has_text(items, "Save"),
			"names, choice names, headings and buttons drawn");
		expect(!has_text(items, "Unsaved changes"), "nothing unsaved yet");
		panel.hover(at(panel, panel.plus_button(3, store), store));
		store.step("gun_scale", 1);
		items = panel.draw(store, "");
		expect(has_text(items, "Apparent gun size") && has_text(items, "Unsaved changes"), "hovered row's help (capitalised) and the unsaved chip");
		expect(has_text(panel.draw(store, "Saved"), "Saved"), "status replaces the chip");

		std::vector<std::uint32_t> pixels;
		expect(rasterise(items, settings_panel::width, settings_panel::height, pixels), "rasterise");
		expect(pixels.size() == static_cast<std::size_t>(settings_panel::width * settings_panel::height), "pixel count");
		const auto pixel = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * settings_panel::width + static_cast<std::size_t>(x)]; };
		expect(pixel(settings_panel::width / 2, settings_panel::height - 5) == 0xFF1E2228, "background colour, opaque");
		const rect save = panel.save_button();
		expect(pixel(save.x + 20, save.y + 20) == 0xFFF08A24, "save button colour");
		expect(pixel(save.x, save.y) != 0xFFF08A24, "rounded corner");
		int text_pixels = 0;
		for (int x = save.x; x < save.x + save.w; ++x)
		{
			text_pixels += pixel(x, save.y + save.h / 2) != 0xFFF08A24 ? 1 : 0;
		}
		expect(text_pixels > 5, "the Save label is drawn");

		// See-through backdrop: the keyed colour gets its alpha, premultiplied; the rest stays opaque.
		expect(rasterise(items, settings_panel::width, settings_panel::height, pixels, { { panel_background, 128 } }), "rasterise keyed");
		expect(pixel(settings_panel::width / 2, settings_panel::height - 5) == 0x800F1114, "backdrop at half alpha, premultiplied");
		expect(pixel(save.x + 20, save.y + 20) == 0xFFF08A24, "buttons stay opaque");
	}

	// #56: the held magazine's rounds, keyed see-through around a dimmed card.
	void test_magazine_label()
	{
		expect(magazine_label_color(0, 30) == 0xFF5A4Au && magazine_label_color(10, 30) == 0xFFC24Au && magazine_label_color(11, 30) == 0xF0F4F8u,
			"empty red, a third amber, else white");
		const auto items = magazine_label_items(27, 30);
		expect(has_text(items, "27") && has_text(items, "/ 30"), "count and capacity drawn");
		expect(!has_text(magazine_label_items(-1, 0), "/"), "no capacity, no slash");
		std::vector<std::uint32_t> pixels;
		const std::vector<see_through> keys = { { magazine_label_clear, 0 }, { magazine_label_card, 200 } };
		expect(rasterise(items, magazine_label_width, magazine_label_height, pixels, keys), "rasterise the label");
		const auto pixel = [&](int x, int y) { return pixels[static_cast<std::size_t>(y) * magazine_label_width + static_cast<std::size_t>(x)]; };
		expect(pixel(0, 0) == 0, "outside the rounded card is clear");
		expect((pixel(magazine_label_width - 30, magazine_label_height - 8) >> 24) == 200, "the card is dimmed see-through");
		int bright = 0;
		for (int x = 12; x < 144; ++x)
		{
			bright += (pixel(x, magazine_label_height / 2) & 0xFFu) > 0x80u ? 1 : 0;
		}
		expect(bright > 5, "the count is drawn");
	}

	// Writes every page of the real settings as BMPs (path_0.bmp ...), to look at without a headset.
	void snapshot(const char* path)
	{
		settings_panel panel(default_pages(settings::global()));
		for (int index = 0; index < static_cast<int>(panel.pages().size()); ++index)
		{
			panel.click({ target_kind::page, index }, settings::global());
			const int row = index == 0 ? 1 : 2;
			panel.hover({ target_kind::plus, row });
			std::vector<std::uint32_t> pixels;
			const std::string file = std::string(path) + "_" + std::to_string(index) + ".bmp";
			const bool written = rasterise(panel.draw(settings::global(), ""), settings_panel::width, settings_panel::height, pixels)
				&& write_bmp(file, pixels, settings_panel::width, settings_panel::height);
			std::printf("snapshot %s: %s\n", file.c_str(), written ? "written" : "failed");
		}
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

	void test_stick_chord()
	{
		constexpr float frame = 1.0f / 90.0f;
		stick_chord chord;
		int starts = 0, fires = 0, taps = 0, left_frames = 0, right_frames = 0;
		const auto run = [&](bool left, bool right, float seconds)
		{
			for (float t = 0; t < seconds - 1e-6f; t += frame)
			{
				const stick_chord_events events = chord.update(left, right, frame);
				starts += events.started ? 1 : 0;
				fires += events.fired ? 1 : 0;
				taps += events.tapped ? 1 : 0;
				left_frames += chord.to_game(0) ? 1 : 0;
				right_frames += chord.to_game(1) ? 1 : 0;
			}
		};
		const auto reset = [&]
		{
			run(false, false, 0.5f);
			starts = fires = taps = left_frames = right_frames = 0;
		};

		run(true, true, 0.6f);
		expect(starts == 1 && fires == 1, "both clicks together: one start, one toggle after the hold");
		expect(left_frames == 0 && right_frames == 0, "the game never sees a chord's clicks");
		run(true, true, 1.5f);
		expect(fires == 1, "one toggle however long the chord is held");
		run(false, true, 0.3f);
		run(false, false, 0.2f);
		expect(taps == 0, "a chord that opened the panel is no tap");
		expect(left_frames == 0 && right_frames == 0, "nothing reaches the game while letting go one click at a time");

		reset();
		run(true, true, 0.3f);
		run(false, false, 0.2f);
		expect(starts == 1 && fires == 0, "a chord let go early does not toggle");
		expect(taps == 1, "a chord let go early is one tap (mark_tap)");

		reset();
		run(true, true, 0.15f);
		run(true, false, 0.1f);
		expect(taps == 0, "no tap while one click is still down");
		run(false, false, 0.1f);
		expect(taps == 1 && left_frames == 0, "the tap comes when both are up; the game saw neither");

		reset();
		run(true, false, 0.05f);
		run(true, true, 0.6f);
		expect(fires == 1 && left_frames == 0 && right_frames == 0, "the second click within the grace time still makes a chord");

		reset();
		run(true, false, 0.08f);
		expect(left_frames == 0, "a click alone waits for the other one");
		run(true, false, 0.3f);
		expect(left_frames > 20 && right_frames == 0 && fires == 0, "a click held alone reaches the game after the grace time");
		run(false, false, 0.2f);
		expect(!chord.to_game(0), "and goes when it is let go");

		reset();
		run(false, true, 0.04f);
		run(false, false, 0.3f);
		expect(right_frames >= 7 && right_frames <= 10 && left_frames == 0, "a quick tap reaches the game as a late tap (melee on R3)");

		reset();
		run(true, false, 0.3f);
		run(true, true, 0.6f);
		expect(fires == 1, "a chord also opens when one click was already held");

		reset();
		run(true, false, 0.3f);
		run(true, true, 0.1f);
		run(false, false, 0.2f);
		expect(taps == 0, "sprinting (left held for the game), then a quick right click: no tap");
		const int before = left_frames;
		run(false, false, 0.2f);
		expect(left_frames == before, "the held click is not given back after the chord");
	}

	void test_level_hint()
	{
		constexpr float frame = 1.0f / 90.0f;
		level_hint hint;
		int shown = 0;
		const auto run = [&](bool level, float seconds)
		{
			for (float t = 0; t < seconds - 1e-6f; t += frame)
			{
				shown += hint.update(level, frame) ? 1 : 0;
			}
		};
		run(false, 5.0f);
		expect(shown == 0, "no hint in the front end");
		run(true, 20.0f);
		expect(shown > 8 * 90 - 3 && shown < 8 * 90 + 3, "about 8 s after the level starts");
		shown = 0;
		run(false, 0.5f);
		run(true, 5.0f);
		expect(shown == 0, "not again after a short blip of the level state");
		run(false, 10.0f);
		run(true, 1.0f);
		hint.dismiss();
		run(true, 10.0f);
		expect(shown > 85 && shown < 95, "again after the next level load, until the panel is opened");
	}
}

int main(int argc, char** argv)
{
	test_menu_button();
	test_stick_chord();
	test_level_hint();
	test_pages();
	test_hits_and_clicks();
	test_scrolling();
	test_draw_and_raster();
	test_magazine_label();

	// Every live setting has a name in the catalog and sits on a named page (none under Other).
	settings::load_global();
	for (const std::string& name : settings::global().names())
	{
		if (!settings::presentation_of(name))
		{
			std::printf("FAIL: %s is not in the setting catalog\n", name.c_str());
			++failures;
		}
	}
	for (const settings::presentation& entry : settings::catalog())
	{
		const auto startup = settings::startup_definitions();
		const bool is_startup = std::any_of(startup.begin(), startup.end(), [&](const settings::definition& item) { return item.name == entry.name; });
		if (entry.startup ? !is_startup : !settings::global().definition_of(entry.name))
		{
			std::printf("FAIL: catalog entry %s is no %s setting\n", std::string(entry.name).c_str(), entry.startup ? "start-up" : "live");
			++failures;
		}
	}
	// #48: settings that only compared an old behaviour while testing are gone; the tested one is fixed.
	// An old bfvr.ini still names them: they are not registered, so loading skips them.
	for (const char* removed : { "two_handed", "two_hand_mode", "arm_reach", "body_shoulders", "finger_poses", "gun_life_size",
			 "gun_fov_life_size", "steady_walk", "reticle_depth", "extra_meshes", "magazine_snap", "palm_reload", "game_sounds",
			 "free_hand_reload", "magazine_grip", "grenade_model", "grenade_mesh", "pouch_models", "holsters", "knife_grip", "aim_with_body", "shots_from_gun", "shot_line", "pause_flat", "focus_notice" })
	{
		if (settings::global().definition_of(removed) || settings::presentation_of(removed))
		{
			std::printf("FAIL: %s should no longer be a setting (#48)\n", removed);
			++failures;
		}
	}
	{
		// Owner 2026-11-22: AR palming is a setting again, off by default, on the panel's Reloading page.
		const auto palming = settings::global().definition_of("magazine_palming");
		if (!palming || palming->fallback != "0" || !settings::presentation_of("magazine_palming"))
		{
			std::printf("FAIL: magazine_palming should be a panel setting, default 0\n");
			++failures;
		}
	}
	{
		// An old bfvr.ini that switched two of them off: the loader never asks for them, and warns about nothing.
		std::vector<std::string> asked;
		const auto old_ini = [&](const std::string& name) -> std::optional<std::string> {
			asked.push_back(name);
			return name == "holsters" || name == "two_hand_mode" ? std::optional<std::string>{ name == "holsters" ? "0" : "soft" } : std::nullopt;
		};
		settings::registry fresh;
		for (const auto& item : settings::live_definitions())
		{
			fresh.add(item);
		}
		expect(fresh.load(old_ini).empty(), "an old bfvr.ini with removed keys loads without warnings");
		expect(!asked.empty() && std::find(asked.begin(), asked.end(), "holsters") == asked.end()
				&& std::find(asked.begin(), asked.end(), "two_hand_mode") == asked.end(),
			"removed keys in an old bfvr.ini are never read");
	}
	expect(default_pages(settings::global()).front().title == "View", "no start-up page in the game's own panel");
	{
		// The launcher's registry: live and start-up settings, the start-up page first.
		settings::registry launcher;
		for (const auto& item : settings::live_definitions())
		{
			launcher.add(item);
		}
		for (const auto& item : settings::startup_definitions())
		{
			launcher.add(item);
		}
		const auto pages = default_pages(launcher);
		expect(pages.front().title == "Start-up" && pages.front().rows.size() == 7, "launcher: start-up page with its heading and six settings");
	}
	if (argc > 1)
	{
		snapshot(argv[1]);
	}

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("settings_panel: all checks passed\n");
	return 0;
}
