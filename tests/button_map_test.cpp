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
