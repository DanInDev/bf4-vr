#include "ui/settings_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "common/setting_catalog.hpp"
#include "common/settings_store.hpp"

namespace bfvr::ui
{
	namespace
	{
		constexpr int margin = 16;
		constexpr int title_height = 72;
		constexpr int side_width = 196;
		constexpr int tabs_top = 88;
		constexpr int tab_height = 54;
		constexpr int tab_pitch = 62;
		constexpr int content_x = margin + side_width + 16;
		constexpr int content_width = 724;
		constexpr int content_top = 88;
		constexpr int content_bottom = 612;
		constexpr int scroll_x = content_x + content_width + 10;
		constexpr int scroll_width = settings_panel::width - margin - scroll_x;
		constexpr int setting_height = 64;
		constexpr int heading_height = 40;
		constexpr int row_gap = 6;
		constexpr int help_top = 622;
		constexpr int buttons_top = 692;
		constexpr int button_height = 58;
		constexpr int control_width = 300;
		constexpr int control_height = 42;
		constexpr int step_width = 46;

		constexpr std::uint32_t background = panel_background;
		constexpr std::uint32_t bar = panel_bar;
		constexpr std::uint32_t accent = 0xF08A24;
		constexpr std::uint32_t on_accent = 0x15181C;
		constexpr std::uint32_t row_back = 0x232830;
		constexpr std::uint32_t row_hover = 0x2C323B;
		constexpr std::uint32_t control_back = 0x323943;
		constexpr std::uint32_t control_hover = 0x46505C;
		constexpr std::uint32_t tab_active = 0x2D343D;
		constexpr std::uint32_t tab_hover = 0x262B32;
		constexpr std::uint32_t text_main = 0xE8ECF0;
		constexpr std::uint32_t text_dim = 0x8F98A3;
		constexpr std::uint32_t text_soft = 0xB4BCC6;
		constexpr std::uint32_t text_changed = 0xF2C14E;
		constexpr std::uint32_t toggle_off = 0x3C434D;
		constexpr std::uint32_t knob_off = 0xC9D1D9;

		const char* const axis_names[3] = { "x, right", "y, up", "z, back" };

		void add_rows(page& target, const settings::registry& store, std::string_view name)
		{
			const auto setting = store.definition_of(name);
			if (!setting)
			{
				return;
			}
			if (setting->type == settings::kind::vec3)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					target.rows.push_back({ std::string(name), axis, {} });
				}
				return;
			}
			target.rows.push_back({ std::string(name), -1, {} });
		}

		void add_heading(page& target, std::string_view heading)
		{
			target.rows.push_back({ {}, -1, std::string(heading) });
		}

		std::string format_number(float number)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.3g", number == 0.0f ? 0.0f : number);
			return text;
		}

		std::string upper(std::string_view text)
		{
			std::string out(text);
			for (char& c : out)
			{
				if (c >= 'a' && c <= 'z')
				{
					c = static_cast<char>(c - 'a' + 'A');
				}
			}
			return out;
		}

		draw_item box(rect area, std::uint32_t color, int radius = 0)
		{
			draw_item item{ draw_item::type::box, area, color, {}, 0, 0, false };
			item.radius = radius;
			return item;
		}

		draw_item text(rect area, std::uint32_t color, std::string content, int align, int size, bool bold = false)
		{
			draw_item item{ draw_item::type::text, area, color, std::move(content), align, size, false };
			item.bold = bold;
			return item;
		}

		int height_of(const row& line)
		{
			return line.is_heading() ? heading_height : setting_height;
		}

		// Names longer than this get a stepper instead of option buttons.
		constexpr std::size_t option_name_limit = 11;
	}

	std::vector<page> default_pages(const settings::registry& store)
	{
		std::vector<page> result;
		std::vector<std::string> listed;
		const auto catalog = settings::catalog();
		for (const std::string_view title : settings::catalog_pages())
		{
			page next{ std::string(title), {} };
			const bool advanced_page = title == "Advanced";
			std::string_view section;
			for (const settings::presentation& entry : catalog)
			{
				if (!store.definition_of(entry.name))
				{
					continue;
				}
				if (advanced_page ? !entry.advanced : (entry.advanced || entry.page != title))
				{
					continue;
				}
				const std::string_view heading = advanced_page ? entry.page : entry.section;
				if (heading != section)
				{
					// The advanced page groups by page: catalog order already runs page by page.
					add_heading(next, heading);
					section = heading;
				}
				add_rows(next, store, entry.name);
				listed.emplace_back(entry.name);
			}
			if (advanced_page)
			{
				bool other = false;
				for (const std::string& name : store.names())
				{
					if (std::find(listed.begin(), listed.end(), name) == listed.end() && !settings::is_fixed(name))
					{
						if (!other)
						{
							add_heading(next, "Other");
							other = true;
						}
						add_rows(next, store, name);
					}
				}
			}
			if (!next.rows.empty())
			{
				result.push_back(std::move(next));
			}
		}
		return result;
	}

	settings_panel::settings_panel(std::vector<page> pages)
	{
		set_pages(std::move(pages));
	}

	void settings_panel::set_pages(std::vector<page> pages)
	{
		pages_ = std::move(pages);
		page_ = 0;
		first_row_ = 0;
		hover_ = {};
	}

	const std::vector<row>* settings_panel::rows() const
	{
		return pages_.empty() ? nullptr : &pages_[static_cast<std::size_t>(page_)].rows;
	}

	const row* settings_panel::row_at(int index) const
	{
		const std::vector<row>* all = rows();
		return all && index >= 0 && index < static_cast<int>(all->size()) ? &(*all)[static_cast<std::size_t>(index)] : nullptr;
	}

	std::vector<settings_panel::slot> settings_panel::layout() const
	{
		std::vector<slot> slots;
		const std::vector<row>* all = rows();
		if (!all)
		{
			return slots;
		}
		int y = content_top;
		for (int index = first_row_; index < static_cast<int>(all->size()); ++index)
		{
			const int h = height_of((*all)[static_cast<std::size_t>(index)]);
			if (y + h > content_bottom)
			{
				break;
			}
			slots.push_back({ index, { content_x, y, content_width, h } });
			y += h + row_gap;
		}
		return slots;
	}

	bool settings_panel::can_scroll_down() const
	{
		const std::vector<row>* all = rows();
		const auto slots = layout();
		return all && !slots.empty() && slots.back().row + 1 < static_cast<int>(all->size());
	}

	// The first row that still shows the page's last row at the bottom.
	int settings_panel::first_for_end() const
	{
		const std::vector<row>* all = rows();
		if (!all || all->empty())
		{
			return 0;
		}
		int used = 0;
		int first = static_cast<int>(all->size());
		while (first > 0)
		{
			const int h = height_of((*all)[static_cast<std::size_t>(first - 1)]) + (used ? row_gap : 0);
			if (used + h > content_bottom - content_top)
			{
				break;
			}
			used += h;
			--first;
		}
		return first;
	}

	rect settings_panel::page_tab(int index) const
	{
		return { margin, tabs_top + index * tab_pitch, side_width, tab_height };
	}

	rect settings_panel::row_area(int row_index) const
	{
		for (const slot& item : layout())
		{
			if (item.row == row_index)
			{
				return item.area;
			}
		}
		return {};
	}

	row_control settings_panel::control_of(const row& line, const settings::registry& store)
	{
		if (line.is_heading())
		{
			return row_control::heading;
		}
		const auto setting = store.definition_of(line.name);
		if (setting && setting->type == settings::kind::boolean)
		{
			return row_control::toggle;
		}
		if (setting && setting->type == settings::kind::choice)
		{
			const auto options = options_of(line.name, store);
			bool short_names = options.size() <= 3;
			for (const auto& option : options)
			{
				short_names = short_names && option.second.size() <= option_name_limit;
			}
			return short_names ? row_control::options : row_control::stepper;
		}
		return row_control::stepper;
	}

	std::vector<std::pair<std::string, std::string>> settings_panel::options_of(std::string_view name, const settings::registry& store)
	{
		std::vector<std::pair<std::string, std::string>> result;
		const auto setting = store.definition_of(name);
		if (!setting || setting->type != settings::kind::choice)
		{
			return result;
		}
		std::vector<std::string> values;
		std::string_view rest = setting->choices;
		while (!rest.empty())
		{
			const std::size_t bar_at = rest.find('|');
			values.emplace_back(rest.substr(0, bar_at));
			rest = bar_at == std::string_view::npos ? std::string_view{} : rest.substr(bar_at + 1);
		}
		// The catalog's order first (it reads naturally: "By hand", "Button"), then any value it misses.
		if (const settings::presentation* entry = settings::presentation_of(name))
		{
			std::string_view listed = entry->choices;
			while (!listed.empty())
			{
				const std::size_t bar_at = listed.find('|');
				const std::string_view item = listed.substr(0, bar_at);
				const std::string value(item.substr(0, item.find('=')));
				if (std::find(values.begin(), values.end(), value) != values.end())
				{
					result.emplace_back(value, settings::choice_label(name, value));
				}
				listed = bar_at == std::string_view::npos ? std::string_view{} : listed.substr(bar_at + 1);
			}
		}
		for (const std::string& value : values)
		{
			const bool known = std::any_of(result.begin(), result.end(), [&](const auto& option) { return option.first == value; });
			if (!known)
			{
				result.emplace_back(value, settings::choice_label(name, value));
			}
		}
		return result;
	}

	rect settings_panel::minus_button(int row_index, const settings::registry& store) const
	{
		const rect area = row_area(row_index);
		const row* line = row_at(row_index);
		if (area.empty() || !line || control_of(*line, store) != row_control::stepper)
		{
			return {};
		}
		const int x = area.x + area.w - 12 - control_width;
		return { x, area.y + (area.h - control_height) / 2, step_width, control_height };
	}

	rect settings_panel::plus_button(int row_index, const settings::registry& store) const
	{
		const rect minus = minus_button(row_index, store);
		return minus.empty() ? rect{} : rect{ minus.x + control_width - step_width, minus.y, step_width, control_height };
	}

	rect settings_panel::value_box(int row_index, const settings::registry& store) const
	{
		const rect area = row_area(row_index);
		const row* line = row_at(row_index);
		if (area.empty() || !line)
		{
			return {};
		}
		switch (control_of(*line, store))
		{
		case row_control::toggle:
			return area;
		case row_control::stepper:
		{
			const rect minus = minus_button(row_index, store);
			return { minus.x + step_width + 4, minus.y, control_width - 2 * step_width - 8, control_height };
		}
		default:
			return {};
		}
	}

	rect settings_panel::option_button(int row_index, int option, const settings::registry& store) const
	{
		const rect area = row_area(row_index);
		const row* line = row_at(row_index);
		if (area.empty() || !line || control_of(*line, store) != row_control::options)
		{
			return {};
		}
		const int count = static_cast<int>(options_of(line->name, store).size());
		if (option < 0 || option >= count)
		{
			return {};
		}
		const int x = area.x + area.w - 12 - control_width;
		const int each = (control_width - 4 * (count - 1)) / count;
		return { x + option * (each + 4), area.y + (area.h - control_height) / 2, each, control_height };
	}

	rect settings_panel::rows_up_button() const
	{
		return { scroll_x, content_top, scroll_width, 110 };
	}

	rect settings_panel::rows_down_button() const
	{
		return { scroll_x, content_bottom - 110, scroll_width, 110 };
	}

	rect settings_panel::reset_button() const
	{
		return { content_x, buttons_top, 200, button_height };
	}

	rect settings_panel::close_button() const
	{
		return { width - margin - 2 * 210 - 10, buttons_top, 210, button_height };
	}

	rect settings_panel::save_button() const
	{
		return { width - margin - 210, buttons_top, 210, button_height };
	}

	target settings_panel::hit(float u, float v, const settings::registry& store) const
	{
		const int x = static_cast<int>(std::floor(u * width));
		const int y = static_cast<int>(std::floor(v * height));
		for (int index = 0; index < static_cast<int>(pages_.size()); ++index)
		{
			if (page_tab(index).contains(x, y))
			{
				return { target_kind::page, index };
			}
		}
		if (save_button().contains(x, y))
		{
			return { target_kind::save };
		}
		if (close_button().contains(x, y))
		{
			return { target_kind::close };
		}
		if (!pages_.empty() && reset_button().contains(x, y))
		{
			return { target_kind::reset_page };
		}
		if (can_scroll_up() && rows_up_button().contains(x, y))
		{
			return { target_kind::rows_up };
		}
		if (can_scroll_down() && rows_down_button().contains(x, y))
		{
			return { target_kind::rows_down };
		}
		for (const slot& item : layout())
		{
			if (!item.area.contains(x, y))
			{
				continue;
			}
			const row& line = *row_at(item.row);
			switch (control_of(line, store))
			{
			case row_control::toggle:
				return { target_kind::value, item.row };
			case row_control::options:
				for (int option = 0; option < static_cast<int>(options_of(line.name, store).size()); ++option)
				{
					if (option_button(item.row, option, store).contains(x, y))
					{
						return { target_kind::option, item.row, option };
					}
				}
				break;
			case row_control::stepper:
				if (minus_button(item.row, store).contains(x, y))
				{
					return { target_kind::minus, item.row };
				}
				if (plus_button(item.row, store).contains(x, y))
				{
					return { target_kind::plus, item.row };
				}
				if (value_box(item.row, store).contains(x, y))
				{
					return { target_kind::value, item.row };
				}
				break;
			case row_control::heading:
				break;
			}
			return {};
		}
		return {};
	}

	bool settings_panel::step_row(const row& line, int direction, settings::registry& store) const
	{
		const auto setting = store.definition_of(line.name);
		if (!setting)
		{
			return false;
		}
		if (setting->type != settings::kind::vec3)
		{
			const auto before = store.get(line.name);
			return store.step(line.name, direction) && before && store.get(line.name)->text != before->text;
		}
		const auto current = store.get(line.name);
		if (!current || line.component < 0 || line.component > 2)
		{
			return false;
		}
		const float step = setting->step > 0 ? setting->step : (setting->max - setting->min) / 40.0f;
		float numbers[3] = { current->number[0], current->number[1], current->number[2] };
		numbers[line.component] = std::clamp(numbers[line.component] + static_cast<float>(direction) * step, setting->min, setting->max);
		const std::string text = format_number(numbers[0]) + " " + format_number(numbers[1]) + " " + format_number(numbers[2]);
		return text != current->text && store.set(line.name, text);
	}

	settings_panel::action settings_panel::click(const target& on, settings::registry& store)
	{
		switch (on.kind)
		{
		case target_kind::page:
			if (on.index >= 0 && on.index < static_cast<int>(pages_.size()))
			{
				page_ = on.index;
				first_row_ = 0;
			}
			return action::none;
		case target_kind::rows_up:
		{
			const std::vector<row>* all = rows();
			if (!all)
			{
				return action::none;
			}
			int used = 0;
			int first = first_row_;
			while (first > 0)
			{
				const int h = height_of((*all)[static_cast<std::size_t>(first - 1)]) + (used ? row_gap : 0);
				if (used + h > content_bottom - content_top)
				{
					break;
				}
				used += h;
				--first;
			}
			first_row_ = first;
			return action::none;
		}
		case target_kind::rows_down:
			if (can_scroll_down())
			{
				first_row_ = std::min(layout().back().row + 1, first_for_end());
			}
			return action::none;
		case target_kind::minus:
		case target_kind::plus:
		case target_kind::value:
		{
			const row* line = row_at(on.index);
			if (!line || line->is_heading())
			{
				return action::none;
			}
			if (on.kind == target_kind::value)
			{
				const auto setting = store.definition_of(line->name);
				if (!setting || (setting->type != settings::kind::boolean && setting->type != settings::kind::choice))
				{
					return action::none;
				}
			}
			return step_row(*line, on.kind == target_kind::minus ? -1 : 1, store) ? action::changed : action::none;
		}
		case target_kind::option:
		{
			const row* line = row_at(on.index);
			if (!line || line->is_heading())
			{
				return action::none;
			}
			const auto options = options_of(line->name, store);
			if (on.option < 0 || on.option >= static_cast<int>(options.size()) || store.text(line->name) == options[static_cast<std::size_t>(on.option)].first)
			{
				return action::none;
			}
			return store.set(line->name, options[static_cast<std::size_t>(on.option)].first) ? action::changed : action::none;
		}
		case target_kind::reset_page:
		{
			bool changed = false;
			if (const std::vector<row>* all = rows())
			{
				for (const row& line : *all)
				{
					if (line.is_heading() || line.component > 0)
					{
						continue;
					}
					const auto before = store.get(line.name);
					store.reset(line.name);
					const auto after = store.get(line.name);
					changed = changed || (before && after && before->text != after->text);
				}
			}
			return changed ? action::changed : action::none;
		}
		case target_kind::save:
			return action::save;
		case target_kind::close:
			return action::close;
		case target_kind::none:
			break;
		}
		return action::none;
	}

	std::string settings_panel::value_text(const row& line, const settings::registry& store) const
	{
		const auto setting = store.definition_of(line.name);
		const auto current = store.get(line.name);
		if (!setting || !current)
		{
			return "?";
		}
		switch (setting->type)
		{
		case settings::kind::boolean:
			return current->number[0] != 0 ? "On" : "Off";
		case settings::kind::vec3:
			return line.component >= 0 && line.component < 3 ? format_number(current->number[line.component]) : current->text;
		case settings::kind::choice:
			return settings::choice_label(line.name, current->text);
		default:
			return current->text;
		}
	}

	std::vector<draw_item> settings_panel::draw(const settings::registry& store, std::string_view status) const
	{
		std::vector<draw_item> items;
		const auto hovered = [&](const target& self) { return hover_ == self; };

		// Title bar: the brand, the panel's name, and a chip for unsaved changes or the last action.
		items.push_back(box({ 0, 0, width, height }, background));
		items.push_back(box({ 0, 0, width, title_height }, bar));
		items.push_back(text({ margin + 8, 0, 70, title_height }, text_main, "BF4", 0, 32, true));
		items.push_back(text({ margin + 70, 0, 50, title_height }, accent, "VR", 0, 32, true));
		items.push_back(text({ margin + 124, 0, 300, title_height }, text_soft, "Settings", 0, 28));
		const bool unsaved = store.changed();
		const std::string chip = !status.empty() ? std::string(status) : unsaved ? "Unsaved changes" : "";
		if (!chip.empty())
		{
			const int chip_width = 26 + static_cast<int>(chip.size()) * 11;
			const rect area{ width - margin - chip_width, (title_height - 36) / 2, chip_width, 36 };
			const bool good = !status.empty() && status.rfind("error", 0) != 0;
			items.push_back(box(area, good ? 0x1F3B2B : 0x3A2A17, 18));
			items.push_back(text(area, good ? 0x7FD49B : 0xF5B06A, chip, 1, 20));
		}

		// Pages down the side.
		for (int index = 0; index < static_cast<int>(pages_.size()); ++index)
		{
			const rect tab = page_tab(index);
			const bool active = index == page_;
			if (active || hovered({ target_kind::page, index }))
			{
				items.push_back(box(tab, active ? tab_active : tab_hover, 10));
			}
			if (active)
			{
				items.push_back(box({ tab.x, tab.y + 12, 5, tab.h - 24 }, accent, 2));
			}
			items.push_back(text({ tab.x + 22, tab.y, tab.w - 30, tab.h }, active ? text_main : text_soft, pages_[static_cast<std::size_t>(index)].title, 0, 25, active));
		}

		std::string help = hint_;
		for (const slot& item : layout())
		{
			const row& line = *row_at(item.row);
			const rect area = item.area;
			if (line.is_heading())
			{
				items.push_back(text({ area.x + 4, area.y + 10, area.w, area.h - 10 }, accent, upper(line.heading), 0, 18, true));
				continue;
			}
			const bool row_hovered = hover_.index == item.row && hover_.kind != target_kind::page && hover_.kind != target_kind::none;
			items.push_back(box(area, row_hovered ? row_hover : row_back, 10));
			const settings::presentation* entry = settings::presentation_of(line.name);
			std::string label = entry && !entry->label.empty() ? std::string(entry->label) : line.name;
			if (line.component >= 0)
			{
				label += std::string(" (") + axis_names[line.component] + ")";
			}
			std::string summary = entry ? std::string(entry->summary) : std::string();
			if (summary.empty() && line.name.rfind("bind_", 0) == 0)
			{
				if (const auto setting = store.definition_of(line.name))
				{
					summary = "Default: " + settings::choice_label(line.name, setting->fallback);
				}
			}
			const row_control control = control_of(line, store);
			const int text_width = area.w - control_width - 40;
			const bool changed = store.changed(line.name);
			if (summary.empty())
			{
				items.push_back(text({ area.x + 18, area.y, text_width, area.h }, changed ? text_changed : text_main, label, 0, 24));
			}
			else
			{
				items.push_back(text({ area.x + 18, area.y + 6, text_width, 30 }, changed ? text_changed : text_main, label, 0, 24));
				items.push_back(text({ area.x + 18, area.y + 34, text_width, 24 }, text_dim, summary, 0, 18));
			}
			if (row_hovered)
			{
				if (const auto setting = store.definition_of(line.name))
				{
					help = setting->help.empty() ? label : setting->help;
					if (!help.empty() && help[0] >= 'a' && help[0] <= 'z')
					{
						help[0] = static_cast<char>(help[0] - 'a' + 'A');
					}
				}
			}

			switch (control)
			{
			case row_control::toggle:
			{
				const bool on = store.boolean(line.name);
				const rect track{ area.x + area.w - 12 - 80, area.y + (area.h - 40) / 2, 80, 40 };
				items.push_back(box(track, on ? accent : toggle_off, 20));
				const rect knob{ on ? track.x + track.w - 36 : track.x + 4, track.y + 4, 32, 32 };
				items.push_back(box(knob, on ? 0xFFFFFF : knob_off, 16));
				break;
			}
			case row_control::options:
			{
				const auto options = options_of(line.name, store);
				const std::string current = store.text(line.name);
				for (int option = 0; option < static_cast<int>(options.size()); ++option)
				{
					const rect button = option_button(item.row, option, store);
					const bool chosen = options[static_cast<std::size_t>(option)].first == current;
					const bool over = hovered({ target_kind::option, item.row, option });
					items.push_back(box(button, chosen ? accent : over ? control_hover : control_back, 8));
					items.push_back(text(button, chosen ? on_accent : text_soft, options[static_cast<std::size_t>(option)].second, 1, 20, chosen));
				}
				break;
			}
			case row_control::stepper:
			{
				const auto setting = store.definition_of(line.name);
				const bool choice = setting && setting->type == settings::kind::choice;
				const rect minus = minus_button(item.row, store);
				const rect plus = plus_button(item.row, store);
				const rect value = value_box(item.row, store);
				items.push_back(box(minus, hovered({ target_kind::minus, item.row }) ? control_hover : control_back, 8));
				items.push_back(text(minus, text_main, choice ? "\xE2\x80\xB9" : "\xE2\x88\x92", 1, 30)); // single angle quote, minus sign
				items.push_back(box(plus, hovered({ target_kind::plus, item.row }) ? control_hover : control_back, 8));
				items.push_back(text(plus, text_main, choice ? "\xE2\x80\xBA" : "+", 1, 30));
				const std::string shown = value_text(line, store);
				if (choice && hovered({ target_kind::value, item.row }))
				{
					items.push_back(box(value, control_back, 8));
				}
				items.push_back(text(value, text_main, shown, 1, shown.size() > 14 ? 18 : 23, true));
				break;
			}
			case row_control::heading:
				break;
			}
		}

		// Scrolling: arrows at both ends of a thin track that shows where the view is.
		if (const std::vector<row>* all = rows(); all && (can_scroll_up() || can_scroll_down()))
		{
			const auto slots = layout();
			const rect up = rows_up_button();
			const rect down = rows_down_button();
			const int track_top = up.y + up.h + 8;
			const int track_height = down.y - 8 - track_top;
			items.push_back(box({ up.x + up.w / 2 - 3, track_top, 6, track_height }, control_back, 3));
			const float count = static_cast<float>(all->size());
			const int thumb_top = track_top + static_cast<int>(track_height * (first_row_ / count));
			const int thumb_height = std::max(24, static_cast<int>(track_height * (slots.size() / count)));
			items.push_back(box({ up.x + up.w / 2 - 3, thumb_top, 6, std::min(thumb_height, track_top + track_height - thumb_top) }, text_dim, 3));
			if (can_scroll_up())
			{
				items.push_back(box(up, hovered({ target_kind::rows_up }) ? control_hover : control_back, 10));
				items.push_back(text(up, text_main, "\xE2\x96\xB2", 1, 24)); // up triangle
			}
			if (can_scroll_down())
			{
				items.push_back(box(down, hovered({ target_kind::rows_down }) ? control_hover : control_back, 10));
				items.push_back(text(down, text_main, "\xE2\x96\xBC", 1, 24)); // down triangle
			}
		}

		items.push_back(text({ content_x + 4, help_top, width - margin - content_x - 4, 60 }, text_soft, help, 0, 19));
		items.back().wrap = true;

		items.push_back(text({ margin + 8, buttons_top, side_width, button_height }, text_dim, "bf-vr", 0, 18));
		items.push_back(box(reset_button(), hovered({ target_kind::reset_page }) ? control_hover : control_back, 10));
		items.push_back(text(reset_button(), text_main, "Reset page", 1, 24));
		items.push_back(box(close_button(), hovered({ target_kind::close }) ? control_hover : control_back, 10));
		items.push_back(text(close_button(), text_main, close_label_, 1, 24));
		items.push_back(box(save_button(), hovered({ target_kind::save }) ? 0xF5A04B : accent, 10));
		items.push_back(text(save_button(), on_accent, "Save", 1, 25, true));
		return items;
	}
}
