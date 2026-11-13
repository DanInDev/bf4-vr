#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "common/settings.hpp"

// The in-VR settings panel (issue #7, docs/tasks/vr-settings.md): pages of registry settings in
// sections, each with a plain-language name and a short line (common/setting_catalog), switched
// by a toggle, option buttons or a stepper; Reset page, Close and Save. Laid out in panel pixels.
// Engine-free: it hit-tests texture coordinates from the laser pointer, applies clicks to a
// registry and describes what to draw (ui/panel_canvas rasterises it). Tested by
// tests/settings_panel_test.cpp.
namespace bfvr::ui
{
	// The panel's backdrop and its title bar: the colours vr_panel draws see-through.
	constexpr std::uint32_t panel_background = 0x1E2228;
	constexpr std::uint32_t panel_bar = 0x2A3038;

	struct rect
	{
		int x{}, y{}, w{}, h{};
		bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
		bool empty() const { return w <= 0 || h <= 0; }
	};

	enum class target_kind
	{
		none,
		page,       // index = page
		minus,      // index = row on the current page
		plus,
		value,      // a toggle row, or a stepper's value: toggles booleans, cycles choices
		option,     // index = row, option = which of the row's option buttons
		rows_up,
		rows_down,
		save,
		close,
		reset_page,
	};

	struct target
	{
		target_kind kind{ target_kind::none };
		int index{ -1 };
		int option{ -1 };
		bool operator==(const target&) const = default;
	};

	// One line: a section heading (heading set, name empty), a setting, or one component of a vec3.
	struct row
	{
		std::string name;
		int component{ -1 }; // 0..2 for vec3 (x, y, z), else -1
		std::string heading;
		bool is_heading() const { return name.empty(); }
	};

	struct page
	{
		std::string title;
		std::vector<row> rows;
	};

	// The panel's pages from the setting catalog: View, Body, Weapon, HUD, Controls, each in its
	// sections, then Advanced (the catalog's advanced settings under their page's name, and any
	// setting the catalog does not know under Other). vec3 settings become three rows. Empty pages
	// are left out.
	std::vector<page> default_pages(const settings::registry& store);

	struct draw_item
	{
		enum class type
		{
			box,
			text,
			polygon, // points: x0, y0, x1, y1, ... (antialiased, even-odd); area unused
		};
		type kind{ type::box };
		rect area;
		std::uint32_t color{}; // 0xRRGGBB
		std::string text;
		int align{};           // text: 0 left, 1 centre, 2 right
		int size{};            // text: pixel height
		bool wrap{};           // text: break into lines within the area (else one line, ellipsis)
		int radius{};          // box: rounded corners (antialiased); half the height makes a pill
		bool bold{};           // text: semibold
		int face{};            // text: 0 Segoe UI, 1 Barlow Condensed SemiBold (headings of the calibration step, compiled in)
		std::vector<int> points; // polygon corners in panel pixels
	};

	// How a setting row is switched.
	enum class row_control
	{
		heading,
		toggle,   // booleans: the whole row toggles
		options,  // choices with up to three short names: one button each
		stepper,  // numbers and other choices: - value +
	};

	class settings_panel
	{
	public:
		static constexpr int width = 1024;
		static constexpr int height = 768;

		explicit settings_panel(std::vector<page> pages = {});
		void set_pages(std::vector<page> pages);

		// u, v in [0, 1] from the pointer, (0, 0) top left.
		target hit(float u, float v, const settings::registry& store) const;
		void hover(const target& over) { hover_ = over; }
		const target& hovered() const { return hover_; }

		enum class action
		{
			none,
			changed,
			save,
			close,
		};
		action click(const target& on, settings::registry& store);

		// status: shown in the title bar (e.g. "saved"); the hovered row's help goes above the buttons.
		std::vector<draw_item> draw(const settings::registry& store, std::string_view status) const;

		// The Close button's caption (the launcher's panel discards changes with it).
		void set_close_label(std::string label) { close_label_ = std::move(label); }
		// The line above the buttons while nothing is hovered (the launcher's says to click, not point).
		void set_hint(std::string hint) { hint_ = std::move(hint); }

		int current_page() const { return page_; }
		int first_row() const { return first_row_; }
		const std::vector<page>& pages() const { return pages_; }

		// Layout, for drawing and tests. Rows are indices on the current page; a row scrolled out of
		// view, or without that part, gives an empty rect.
		rect page_tab(int index) const;
		rect row_area(int row) const;
		rect minus_button(int row, const settings::registry& store) const;
		rect plus_button(int row, const settings::registry& store) const;
		rect value_box(int row, const settings::registry& store) const;
		rect option_button(int row, int option, const settings::registry& store) const;
		rect rows_up_button() const;
		rect rows_down_button() const;
		rect save_button() const;
		rect close_button() const;
		rect reset_button() const;
		bool can_scroll_up() const { return first_row_ > 0; }
		bool can_scroll_down() const;

		static row_control control_of(const row& line, const settings::registry& store);
		// A choice setting's values with their names, in display order.
		static std::vector<std::pair<std::string, std::string>> options_of(std::string_view name, const settings::registry& store);

	private:
		struct slot
		{
			int row;
			rect area;
		};
		std::vector<slot> layout() const;
		const std::vector<row>* rows() const;
		const row* row_at(int index) const;
		int first_for_end() const;
		bool step_row(const row& line, int direction, settings::registry& store) const;
		std::string value_text(const row& line, const settings::registry& store) const;

		std::vector<page> pages_;
		int page_{};
		int first_row_{};
		target hover_;
		std::string close_label_{ "Close" };
		std::string hint_{ "Point at a setting and pull the trigger. Hold the menu button, or both stick clicks, to close." };
	};
}
