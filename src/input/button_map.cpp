#include "input/button_map.hpp"

#include <atomic>
#include <utility>

namespace bfvr::input
{
	namespace
	{
		struct pad_info
		{
			std::string_view value;
			std::uint16_t bit;      // XInput
			std::string_view label;
		};
		// Indexed by pad_button.
		constexpr pad_info pads[pad_button_count] = {
			{ "none", 0x0000, "Nothing" },
			{ "a", 0x1000, "Jump" },
			{ "b", 0x2000, "Reload" },
			{ "x", 0x4000, "Game menu" },
			{ "y", 0x8000, "Switch weapon" },
			{ "lb", 0x0100, "Grenade (LB)" },
			{ "rb", 0x0200, "Pad RB" },
			{ "left_stick", 0x0040, "Sprint" },
			{ "right_stick", 0x0080, "Melee" },
			{ "start", 0x0010, "Pause (hold: bf-vr panel)" },
			{ "back", 0x0020, "Pad Back" },
			{ "dpad_up", 0x0001, "Laser, light, magnifier" },
			{ "dpad_down", 0x0002, "Fire mode" },
			{ "dpad_left", 0x0004, "Fire mode (D-pad left)" },
			{ "dpad_right", 0x0008, "Attachment switch" },
		};

		struct quest_info
		{
			std::string_view setting;
			std::string_view label;
		};
		constexpr quest_info quests[quest_button_count] = {
			{ "bind_a", "Right A" },
			{ "bind_b", "Right B" },
			{ "bind_x", "Left X" },
			{ "bind_y", "Left Y" },
			{ "bind_left_stick", "Left stick click" },
			{ "bind_right_stick", "Right stick click" },
			{ "bind_menu", "Left menu button" },
		};

		constexpr button_map swapped_ab = []
		{
			button_map map;
			map.target[static_cast<int>(quest_button::a)] = pad_button::b;
			map.target[static_cast<int>(quest_button::b)] = pad_button::a;
			return map;
		}();

		constexpr preset preset_list[] = {
			{ "default", "Default", button_map{} },
			{ "jump_on_b", "Jump on B, reload on A", swapped_ab },
		};

		// Each target in four bits.
		std::uint64_t pack(const button_map& map)
		{
			std::uint64_t bits = 0;
			for (int i = 0; i < quest_button_count; ++i)
			{
				bits |= static_cast<std::uint64_t>(map.target[i]) << (4 * i);
			}
			return bits;
		}

		button_map unpack(std::uint64_t bits)
		{
			button_map map;
			for (int i = 0; i < quest_button_count; ++i)
			{
				const auto value = static_cast<std::uint8_t>((bits >> (4 * i)) & 0xF);
				map.target[i] = value < pad_button_count ? static_cast<pad_button>(value) : pad_button::none;
			}
			return map;
		}

		std::atomic<std::uint64_t> live{ pack(button_map{}) };
	}

	std::uint16_t xinput_bit(pad_button button)
	{
		const auto index = static_cast<int>(button);
		return index >= 0 && index < pad_button_count ? pads[index].bit : 0;
	}

	std::uint16_t pad_buttons(const button_map& map, const bool (&held)[quest_button_count])
	{
		std::uint16_t bits = 0;
		for (int i = 0; i < quest_button_count; ++i)
		{
			if (held[i])
			{
				bits |= xinput_bit(map.target[i]);
			}
		}
		return bits;
	}

	void mirror_held(bool (&held)[quest_button_count])
	{
		const auto swap = [&](quest_button one, quest_button other) {
			std::swap(held[static_cast<int>(one)], held[static_cast<int>(other)]);
		};
		swap(quest_button::a, quest_button::x);
		swap(quest_button::b, quest_button::y);
		swap(quest_button::left_stick, quest_button::right_stick);
	}

	std::string_view setting_name(quest_button button)
	{
		const auto index = static_cast<int>(button);
		return index >= 0 && index < quest_button_count ? quests[index].setting : std::string_view{};
	}

	std::string_view quest_label(quest_button button)
	{
		const auto index = static_cast<int>(button);
		return index >= 0 && index < quest_button_count ? quests[index].label : std::string_view{};
	}

	std::string_view value_name(pad_button button)
	{
		const auto index = static_cast<int>(button);
		return index >= 0 && index < pad_button_count ? pads[index].value : std::string_view{};
	}

	std::string_view action_label(pad_button button)
	{
		const auto index = static_cast<int>(button);
		return index >= 0 && index < pad_button_count ? pads[index].label : std::string_view{};
	}

	bool parse_pad_button(std::string_view text, pad_button& out)
	{
		for (int i = 0; i < pad_button_count; ++i)
		{
			if (pads[i].value == text)
			{
				out = static_cast<pad_button>(i);
				return true;
			}
		}
		return false;
	}

	std::string_view pad_button_choices()
	{
		return "a|b|x|y|lb|rb|left_stick|right_stick|start|back|dpad_up|dpad_down|dpad_left|dpad_right|none";
	}

	std::span<const preset> presets()
	{
		return preset_list;
	}

	const preset* matching_preset(const button_map& map)
	{
		for (const preset& candidate : preset_list)
		{
			if (candidate.map == map)
			{
				return &candidate;
			}
		}
		return nullptr;
	}

	const preset* find_preset(std::string_view name)
	{
		for (const preset& candidate : preset_list)
		{
			if (candidate.name == name)
			{
				return &candidate;
			}
		}
		return nullptr;
	}

	void set_button_map(const button_map& map)
	{
		live.store(pack(map), std::memory_order_relaxed);
	}

	button_map current_button_map()
	{
		return unpack(live.load(std::memory_order_relaxed));
	}
}
