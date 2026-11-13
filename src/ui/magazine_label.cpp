#include "ui/magazine_label.hpp"

#include <string>

namespace bfvr::ui
{
	std::uint32_t magazine_label_color(int rounds, int capacity)
	{
		if (rounds <= 0)
		{
			return 0xFF5A4Au;
		}
		if (capacity > 0 && rounds * 3 <= capacity)
		{
			return 0xFFC24Au;
		}
		return 0xF0F4F8u;
	}

	std::vector<draw_item> magazine_label_items(int rounds, int capacity)
	{
		constexpr int w = magazine_label_width;
		constexpr int h = magazine_label_height;
		const int shown = rounds < 0 ? 0 : rounds;
		std::vector<draw_item> items;
		items.push_back({ draw_item::type::box, { 0, 0, w, h }, magazine_label_clear, {}, 0, 0, false });
		items.push_back({ draw_item::type::box, { 0, 0, w, h }, magazine_label_card, {}, 0, 0, false, 18 });
		items.push_back({ draw_item::type::text, { 12, 4, 132, h - 8 }, magazine_label_color(rounds, capacity), std::to_string(shown), 2, 72, false, 0, true });
		if (capacity > 0)
		{
			items.push_back({ draw_item::type::text, { 152, 22, w - 164, h - 30 }, 0x9AA4AEu, "/ " + std::to_string(capacity), 0, 40 });
		}
		return items;
	}
}
