// Checks the controller mapping (src/input/button_map): pad bits, names, presets, the live map.
#include <cstdio>
#include <string>

#include "input/button_map.hpp"

using namespace bfvr::input;

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

	bool none_held[quest_button_count]{};
}

int main()
{
	const button_map identity;
	bool held[quest_button_count]{};
	expect(pad_buttons(identity, none_held) == 0, "nothing held, nothing pressed");
	held[static_cast<int>(quest_button::a)] = true;
	held[static_cast<int>(quest_button::menu)] = true;
	expect(pad_buttons(identity, held) == (0x1000 | 0x0010), "default: A is A, menu is Start");

	button_map custom;
	custom.target[static_cast<int>(quest_button::a)] = pad_button::dpad_up;
	custom.target[static_cast<int>(quest_button::menu)] = pad_button::none;
	expect(pad_buttons(custom, held) == 0x0001, "A on D-pad up, menu unbound");
	custom.target[static_cast<int>(quest_button::b)] = pad_button::dpad_up;
	held[static_cast<int>(quest_button::b)] = true;
	expect(pad_buttons(custom, held) == 0x0001, "two buttons on one target press it once");

	// Every pad button round-trips through its setting value and appears in the choices.
	const std::string choices(pad_button_choices());
	for (int i = 0; i < pad_button_count; ++i)
	{
		const auto button = static_cast<pad_button>(i);
		pad_button parsed{};
		expect(parse_pad_button(value_name(button), parsed) && parsed == button, "value round-trip");
		expect(choices.find(std::string(value_name(button))) != std::string::npos, "value in choices");
		expect(!action_label(button).empty(), "action label");
		expect((xinput_bit(button) == 0) == (button == pad_button::none), "a bit for every real button");
	}
	pad_button parsed{};
	expect(!parse_pad_button("jump", parsed), "unknown value rejected");
	for (int i = 0; i < quest_button_count; ++i)
	{
		const auto button = static_cast<quest_button>(i);
		expect(setting_name(button).substr(0, 5) == "bind_", "setting name");
		expect(!quest_label(button).empty(), "quest label");
	}

	// Presets: default first and equal to the identity; each matches itself; names unique.
	const auto list = presets();
	expect(!list.empty() && list[0].name == "default" && list[0].map == identity, "default preset first");
	for (const preset& item : list)
	{
		expect(matching_preset(item.map) == &item, "preset matches itself");
		expect(find_preset(item.name) == &item, "preset found by name");
	}
	// #48: weapons change by the holster gesture only: no preset puts the game's weapon switch on a button,
	// and the default Y gives the fire mode (no other Quest button reaches it).
	for (const preset& item : list)
	{
		for (const pad_button target : item.map.target)
		{
			expect(target != pad_button::y, "no preset switches weapons by button");
		}
	}
	bool y_held[quest_button_count]{};
	y_held[static_cast<int>(quest_button::y)] = true;
	expect(pad_buttons(identity, y_held) == 0x0002, "default: Y is the fire mode (D-pad down)");
	// Left-handed: the physical buttons trade roles before the mapping, so every binding follows the role
	// (the left X is the jump a right-hander has on A, the left Y the reload on B), whatever the preset.
	bool lefty[quest_button_count]{};
	lefty[static_cast<int>(quest_button::y)] = true;
	lefty[static_cast<int>(quest_button::left_stick)] = true;
	mirror_held(lefty);
	expect(lefty[static_cast<int>(quest_button::b)] && lefty[static_cast<int>(quest_button::right_stick)]
			&& !lefty[static_cast<int>(quest_button::y)] && !lefty[static_cast<int>(quest_button::left_stick)],
		"mirror: Y->B, left stick->right stick");
	mirror_held(lefty);
	expect(lefty[static_cast<int>(quest_button::y)] && lefty[static_cast<int>(quest_button::left_stick)], "mirror twice is identity");
	bool lefty_menu[quest_button_count]{};
	lefty_menu[static_cast<int>(quest_button::menu)] = true;
	mirror_held(lefty_menu);
	expect(lefty_menu[static_cast<int>(quest_button::menu)], "the menu button stays (the right one is the runtime's)");
	{
		bool left_y[quest_button_count]{};
		left_y[static_cast<int>(quest_button::y)] = true;
		mirror_held(left_y);
		expect(pad_buttons(identity, left_y) == 0x2000, "left-handed, default map: the left Y reloads (B)");
	}
	const preset* jump = find_preset("jump_on_b");
	expect(jump && pad_buttons(jump->map, held) == (0x2000 | 0x1000 | 0x0010), "jump_on_b: A gives B, B gives A");
	expect(matching_preset(custom) == nullptr, "a custom map matches no preset");
	expect(find_preset("nope") == nullptr, "unknown preset");

	// The live map.
	expect(current_button_map() == identity, "live map starts at the default");
	set_button_map(custom);
	expect(current_button_map() == custom, "live map set");
	set_button_map(identity);

	if (failures == 0)
	{
		std::printf("button_map: all tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}
