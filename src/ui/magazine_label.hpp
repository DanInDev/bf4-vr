#pragma once

#include <vector>

#include "ui/settings_panel.hpp"

// The rounds of a magazine held in the off hand (#56, magazine check): "27 / 30" on a small dark card
// beside the magazine, rasterised by ui/panel_canvas. Engine-free; tested by tests/settings_panel_test.cpp.
namespace bfvr::ui
{
	constexpr int magazine_label_width = 256;
	constexpr int magazine_label_height = 96;
	// Keyed see-through by the rasteriser: the canvas around the card, and the card itself (dimmed).
	constexpr std::uint32_t magazine_label_clear = 0x000001u;
	constexpr std::uint32_t magazine_label_card = 0x1C2128u;

	// Text colour of the count: empty red, a third or less amber, else white.
	std::uint32_t magazine_label_color(int rounds, int capacity);
	std::vector<draw_item> magazine_label_items(int rounds, int capacity);
}
