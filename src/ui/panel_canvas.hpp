#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/settings_panel.hpp"

// Rasterises the settings panel's draw items with GDI (no D3D, no fonts to ship): top-down BGRA
// pixels, premultiplied alpha (255 unless a colour is keyed see-through), for a B8G8R8A8 texture. Text is UTF-8, drawn antialiased (not
// ClearType: the image is resampled in the headset).
namespace bfvr::ui
{
	// Pixels of exactly this colour get this alpha (premultiplied), for a see-through backdrop.
	struct see_through
	{
		std::uint32_t color{};
		std::uint8_t alpha{ 255 };
	};

	bool rasterise(const std::vector<draw_item>& items, int width, int height, std::vector<std::uint32_t>& pixels,
		const std::vector<see_through>& keys = {});

	// 32-bit BMP of rasterised pixels (pipe "panel shot", tests).
	bool write_bmp(const std::string& path, const std::vector<std::uint32_t>& pixels, int width, int height);
}
