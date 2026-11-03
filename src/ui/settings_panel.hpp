#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "common/settings.hpp"

// The in-VR settings panel (issue #7, docs/tasks/vr-settings.md): pages of registry settings with
// -/+ buttons, toggles, Save and Close, laid out in panel pixels. Engine-free: it hit-tests
// texture coordinates from the laser pointer, applies clicks to a registry and describes what to
// draw (ui/panel_canvas rasterises it). Tested by tests/settings_panel_test.cpp.
namespace bfvr::ui
{
	// The panel's backdrop and its title/tab bars: the colours vr_panel draws see-through.
	constexpr std::uint32_t panel_background = 0x1E2228;
	constexpr std::uint32_t panel_bar = 0x2A3038;

	struct rect
	{
		int x{}, y{}, w{}, h{};
		bool contains(int px, int py) const { return px >= x && px < x + w && py >= y && py < y + h; }
	};

	enum class target_kind
	{
		none,
		page,       // index = page
		minus,      // index = visible row
		plus,
		value,      // the value box: toggles booleans, cycles choices
		rows_up,
		rows_down,
		save,
		close,
	};

	struct target
	{
		target_kind kind{ target_kind::none };
		int index{ -1 };
		bool operator==(const target&) const = default;
	};

	// One line: a setting, or one component of a vec3 setting.
	struct row
	{
		std::string name;
		int component{ -1 }; // 0..2 for vec3 (x, y, z), else -1
	};

	struct page
	{
		std::string title;
		std::vector<row> rows;
	};

	// The panel's pages for the settings a registry holds: View, Body, Weapon, HUD, Controls, and
	// Other for anything not listed. vec3 settings become three rows.
	std::vector<page> default_pages(const settings::registry& store);

	struct draw_item
	{
		enum class type
		{
			box,
			text,
		};
		type kind{ type::box };
		rect area;
		std::uint32_t color{}; // 0xRRGGBB
		std::string text;
		int align{};           // text: 0 left, 1 centre, 2 right
		int size{};            // text: pixel height
		bool wrap{};           // text: break into lines within the area (else one line, ellipsis)
	};

	class settings_panel
	{
	public:
		static constexpr int width = 1024;
		static constexpr int height = 768;

		explicit settings_panel(std::vector<page> pages = {});
		void set_pages(std::vector<page> pages);

		// u, v in [0, 1] from the pointer, (0, 0) top left.
		target hit(float u, float v) const;
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

		// status: shown in the title bar (e.g. "saved"); the hovered row's help goes in the footer.
		std::vector<draw_item> draw(const settings::registry& store, std::string_view status) const;

		int current_page() const { return page_; }
		int first_row() const { return first_row_; }
		int visible_rows() const;
		const std::vector<page>& pages() const { return pages_; }

		// Layout, for drawing and tests.
		rect page_tab(int index) const;
		rect row_area(int visible) const;
		rect minus_button(int visible) const;
		rect value_box(int visible) const;
		rect plus_button(int visible) const;
		rect rows_up_button() const;
		rect rows_down_button() const;
		rect save_button() const;
		rect close_button() const;

	private:
		const row* visible_row(int visible) const;
		bool step_row(const row& line, int direction, settings::registry& store) const;
		std::string value_text(const row& line, const settings::registry& store) const;

		std::vector<page> pages_;
		int page_{};
		int first_row_{};
		target hover_;
	};
}
