#pragma once

#include <cstdint>
#include <span>
#include <string_view>

// Controller mapping: which virtual pad button each Quest button presses. Applied where the
// controllers become the virtual Xbox pad (xr/controller_actions), so the game and every mod
// feature that reads the pad follow a remapped button. Grips, triggers and stick directions keep
// their roles: grabbing, holsters, aiming and turning belong to them. Engine-free; tested by
// tests/button_map_test.cpp.
namespace bfvr::input
{
	// The remappable buttons on the controllers, as they are physically placed (before the
	// left-handed mirror).
	enum class quest_button : std::uint8_t
	{
		a,           // right, lower
		b,           // right, upper
		x,           // left, lower
		y,           // left, upper
		left_stick,  // left thumbstick click
		right_stick, // right thumbstick click
		menu,        // left menu button
		count,
	};
	constexpr int quest_button_count = static_cast<int>(quest_button::count);

	enum class pad_button : std::uint8_t
	{
		none,
		a,
		b,
		x,
		y,
		lb,
		rb,
		left_stick,
		right_stick,
		start,
		back,
		dpad_up,
		dpad_down,
		dpad_left,
		dpad_right,
		count,
	};
	constexpr int pad_button_count = static_cast<int>(pad_button::count);

	struct button_map
	{
		// #48: Y gives the fire mode; weapons change by the holster gesture, not a button.
		pad_button target[quest_button_count]{ pad_button::a, pad_button::b, pad_button::x, pad_button::dpad_down,
			pad_button::left_stick, pad_button::right_stick, pad_button::start };
		bool operator==(const button_map&) const = default;
	};

	// The XInput bit a pad button sets (0 for none).
	std::uint16_t xinput_bit(pad_button button);
	// The pad buttons pressed by the held Quest buttons.
	std::uint16_t pad_buttons(const button_map& map, const bool (&held)[quest_button_count]);
	// Left-handed (#9): A with X, B with Y and the stick clicks trade places before the mapping, so a
	// binding follows the role, not the physical button. The menu button stays.
	void mirror_held(bool (&held)[quest_button_count]);

	// Setting names and values: bind_a ... bind_menu, values "a", "dpad_up", "none" ...
	std::string_view setting_name(quest_button button);
	std::string_view value_name(pad_button button);
	bool parse_pad_button(std::string_view text, pad_button& out);
	// "a|b|x|...|none": the choices of every bind_* setting.
	std::string_view pad_button_choices();
	// What the button does with default settings, for menus ("Jump", "Reload / magazine release").
	std::string_view action_label(pad_button button);
	// Where the button is, for menus ("Right A", "Left stick click").
	std::string_view quest_label(quest_button button);

	struct preset
	{
		std::string_view name;  // controls_preset value
		std::string_view label; // for menus
		button_map map;
	};
	std::span<const preset> presets();
	// The preset with this exact map, else nullptr (custom).
	const preset* matching_preset(const button_map& map);
	const preset* find_preset(std::string_view name);

	// Live mapping used by xr/controller_actions.
	void set_button_map(const button_map& map);
	button_map current_button_map();
}
