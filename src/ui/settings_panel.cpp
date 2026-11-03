#include "ui/settings_panel.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace bfvr::ui
{
	namespace
	{
		constexpr int margin = 20;
		constexpr int title_height = 64;
		constexpr int tabs_top = 72;
		constexpr int tabs_height = 52;
		constexpr int rows_top = 136;
		constexpr int row_pitch = 58;
		constexpr int row_height = 52;
		constexpr int rows_bottom = settings_panel::height - 150;
		constexpr int help_top = settings_panel::height - 140;
		constexpr int buttons_top = settings_panel::height - 78;

		constexpr std::uint32_t background = 0x1E2228;
		constexpr std::uint32_t bar = 0x2A3038;
		constexpr std::uint32_t button = 0x3A424D;
		constexpr std::uint32_t button_hover = 0x55606E;
		constexpr std::uint32_t active_tab = 0x3D6FB6;
		constexpr std::uint32_t value_back = 0x15181D;
		constexpr std::uint32_t text_main = 0xE8ECF0;
		constexpr std::uint32_t text_dim = 0xB8C0CA;
		constexpr std::uint32_t text_changed = 0xF2C14E;

		const char* const axis_names[3] = { "x", "y", "z" };

		struct group
		{
			const char* title;
			std::vector<const char*> names;
		};

		const std::vector<group>& groups()
		{
			static const std::vector<group> table = {
				{ "View", { "stereo_render", "cutscene_view","ipd_scale", "view_crop_h", "view_crop_v", "gun_life_size", "gun_scale", "vignette", "vignette_strength" } },
				{ "Body", { "arm_ik", "arm_length", "shoulder_offset", "shoulder_width", "arm_reach", "shoulder_reach", "arm_stretch", "hide_legs", "torso_follow", "finger_poses", "off_hand_pose", "magazine_grip", "rack_grip" } },
				{ "Weapon", { "aim", "gun_pose", "move_direction", "reticle", "reticle_size", "two_handed", "two_hand_mode", "aim_convergence", "recoil", "accessory_toggle", "reload", "reload_sounds", "game_sounds", "drop_discards_ammo", "grenade",
					"grenade_throw_scale", "holsters", "grab_debug", "squad_signal", "melee_gesture", "knife_grip", "knife_pitch", "use_reach", "c4_trigger", "grips_to_game", "ads_trigger" } },
				{ "HUD", { "hud_mode", "hud_show_crosshair", "hud_show_main", "hud_show_minimap", "hud_show_ammo", "hud_show_bottom", "hud_distance", "hud_width",
					"hud_marker_distance", "hud_watch_width", "hud_ammo_width" } },
				{ "Controls", { "right_stick", "turn", "snap_turn_degrees", "stance_flicks", "neutral_pitch", "dominant_hand", "haptics", "haptics_strength", "haptics_game", "haptics_gestures", "physical_pad", "menu_input", "pause_flat", "x_menu", "vr_panel", "stick_menu", "menu_hint", "focus_notice" } },
			};
			return table;
		}

		void add_rows(page& target, const settings::registry& store, const std::string& name)
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
					target.rows.push_back({ name, axis });
				}
				return;
			}
			target.rows.push_back({ name, -1 });
		}

		std::string format_number(float number)
		{
			char text[32];
			std::snprintf(text, sizeof(text), "%.3g", number == 0.0f ? 0.0f : number);
			return text;
		}

		draw_item box(rect area, std::uint32_t color)
		{
			return { draw_item::type::box, area, color, {}, 0, 0, false };
		}

		draw_item text(rect area, std::uint32_t color, std::string content, int align, int size)
		{
			return { draw_item::type::text, area, color, std::move(content), align, size, false };
		}
	}

	std::vector<page> default_pages(const settings::registry& store)
	{
		std::vector<page> result;
		std::vector<std::string> listed;
		for (const group& entry : groups())
		{
			page next{ entry.title, {} };
			for (const char* name : entry.names)
			{
				add_rows(next, store, name);
				listed.emplace_back(name);
			}
			if (!next.rows.empty())
			{
				result.push_back(std::move(next));
			}
		}
		page other{ "Other", {} };
		for (const std::string& name : store.names())
		{
			if (std::find(listed.begin(), listed.end(), name) == listed.end())
			{
				add_rows(other, store, name);
			}
		}
		if (!other.rows.empty())
		{
			result.push_back(std::move(other));
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

	int settings_panel::visible_rows() const
	{
		return (rows_bottom - rows_top) / row_pitch;
	}

	rect settings_panel::page_tab(int index) const
	{
		const int count = std::max(1, static_cast<int>(pages_.size()));
		const int span = (width - 2 * margin) / count;
		return { margin + index * span, tabs_top, span - 6, tabs_height };
	}

	rect settings_panel::row_area(int visible) const
	{
		return { margin, rows_top + visible * row_pitch, 920, row_height };
	}

	rect settings_panel::minus_button(int visible) const
	{
		return { 540, rows_top + visible * row_pitch, 70, row_height };
	}

	rect settings_panel::value_box(int visible) const
	{
		return { 620, rows_top + visible * row_pitch, 230, row_height };
	}

	rect settings_panel::plus_button(int visible) const
	{
		return { 860, rows_top + visible * row_pitch, 70, row_height };
	}

	rect settings_panel::rows_up_button() const
	{
		return { 950, rows_top, 54, 120 };
	}

	rect settings_panel::rows_down_button() const
	{
		return { 950, rows_bottom - 120, 54, 120 };
	}

	rect settings_panel::save_button() const
	{
		return { margin, buttons_top, 240, 60 };
	}

	rect settings_panel::close_button() const
	{
		return { width - margin - 240, buttons_top, 240, 60 };
	}

	const row* settings_panel::visible_row(int visible) const
	{
		if (pages_.empty() || visible < 0 || visible >= visible_rows())
		{
			return nullptr;
		}
		const std::vector<row>& rows = pages_[static_cast<std::size_t>(page_)].rows;
		const int index = first_row_ + visible;
		return index < static_cast<int>(rows.size()) ? &rows[static_cast<std::size_t>(index)] : nullptr;
	}

	target settings_panel::hit(float u, float v) const
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
			return { target_kind::save, -1 };
		}
		if (close_button().contains(x, y))
		{
			return { target_kind::close, -1 };
		}
		if (!pages_.empty())
		{
			const int count = static_cast<int>(pages_[static_cast<std::size_t>(page_)].rows.size());
			if (first_row_ > 0 && rows_up_button().contains(x, y))
			{
				return { target_kind::rows_up, -1 };
			}
			if (first_row_ + visible_rows() < count && rows_down_button().contains(x, y))
			{
				return { target_kind::rows_down, -1 };
			}
		}
		for (int visible = 0; visible < visible_rows(); ++visible)
		{
			if (!visible_row(visible))
			{
				break;
			}
			if (minus_button(visible).contains(x, y))
			{
				return { target_kind::minus, visible };
			}
			if (plus_button(visible).contains(x, y))
			{
				return { target_kind::plus, visible };
			}
			if (value_box(visible).contains(x, y))
			{
				return { target_kind::value, visible };
			}
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
			first_row_ = std::max(0, first_row_ - visible_rows());
			return action::none;
		case target_kind::rows_down:
			if (!pages_.empty())
			{
				const int count = static_cast<int>(pages_[static_cast<std::size_t>(page_)].rows.size());
				first_row_ = std::clamp(first_row_ + visible_rows(), 0, std::max(0, count - visible_rows()));
			}
			return action::none;
		case target_kind::minus:
		case target_kind::plus:
		case target_kind::value:
		{
			const row* line = visible_row(on.index);
			if (!line)
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
			return current->number[0] != 0 ? "on" : "off";
		case settings::kind::vec3:
			return line.component >= 0 && line.component < 3 ? format_number(current->number[line.component]) : current->text;
		default:
			return current->text;
		}
	}

	std::vector<draw_item> settings_panel::draw(const settings::registry& store, std::string_view status) const
	{
		std::vector<draw_item> items;
		const auto shade = [&](const target& self, std::uint32_t normal) { return hover_ == self ? button_hover : normal; };

		items.push_back(box({ 0, 0, width, height }, background));
		items.push_back(box({ 0, 0, width, title_height }, bar));
		items.push_back(text({ margin, 0, 500, title_height }, text_main, "bf-vr settings", 0, 34));
		const bool unsaved = store.changed();
		items.push_back(text({ 520, 0, width - 520 - margin, title_height }, unsaved ? text_changed : text_dim,
			!status.empty() ? std::string(status) : unsaved ? "unsaved changes" : "", 2, 26));

		for (int index = 0; index < static_cast<int>(pages_.size()); ++index)
		{
			const rect tab = page_tab(index);
			items.push_back(box(tab, index == page_ ? active_tab : shade({ target_kind::page, index }, bar)));
			items.push_back(text(tab, text_main, pages_[static_cast<std::size_t>(index)].title, 1, 28));
		}

		std::string help = "Point at a button and pull the trigger. Hold both sticks in (or the menu button) to close.";
		for (int visible = 0; visible < visible_rows(); ++visible)
		{
			const row* line = visible_row(visible);
			if (!line)
			{
				break;
			}
			const rect area = row_area(visible);
			items.push_back(box(area, visible % 2 ? 0x20252C : 0x242931));
			std::string label = line->name;
			if (line->component >= 0)
			{
				label += std::string(" ") + axis_names[line->component];
			}
			items.push_back(text({ area.x + 12, area.y, 500, area.h }, store.changed(line->name) ? text_changed : text_main, label, 0, 26));
			items.push_back(box(minus_button(visible), shade({ target_kind::minus, visible }, button)));
			items.push_back(text(minus_button(visible), text_main, "-", 1, 34));
			items.push_back(box(value_box(visible), hover_ == target{ target_kind::value, visible } ? button : value_back));
			items.push_back(text(value_box(visible), text_main, value_text(*line, store), 1, 28));
			items.push_back(box(plus_button(visible), shade({ target_kind::plus, visible }, button)));
			items.push_back(text(plus_button(visible), text_main, "+", 1, 34));
			const bool hovered_row = (hover_.kind == target_kind::minus || hover_.kind == target_kind::plus || hover_.kind == target_kind::value)
				&& hover_.index == visible;
			if (hovered_row)
			{
				if (const auto setting = store.definition_of(line->name))
				{
					help = setting->help.empty() ? line->name : setting->help;
				}
			}
		}

		if (!pages_.empty())
		{
			const int count = static_cast<int>(pages_[static_cast<std::size_t>(page_)].rows.size());
			if (first_row_ > 0)
			{
				items.push_back(box(rows_up_button(), shade({ target_kind::rows_up, -1 }, button)));
				items.push_back(text(rows_up_button(), text_main, "\xE2\x96\xB2", 1, 30)); // up triangle
			}
			if (first_row_ + visible_rows() < count)
			{
				items.push_back(box(rows_down_button(), shade({ target_kind::rows_down, -1 }, button)));
				items.push_back(text(rows_down_button(), text_main, "\xE2\x96\xBC", 1, 30)); // down triangle
			}
		}

		items.push_back(text({ margin, help_top, width - 2 * margin, 60 }, text_dim, help, 0, 22));
		items.back().wrap = true;
		items.push_back(box(save_button(), hover_ == target{ target_kind::save, -1 } ? 0x3C9A63 : 0x2F7D4F));
		items.push_back(text(save_button(), text_main, "Save", 1, 30));
		items.push_back(box(close_button(), hover_ == target{ target_kind::close, -1 } ? 0x9A3C44 : 0x7D2F35));
		items.push_back(text(close_button(), text_main, "Close", 1, 30));
		return items;
	}
}
