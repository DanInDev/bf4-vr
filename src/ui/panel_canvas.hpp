#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "ui/settings_panel.hpp"

// Rasterises the settings panel's draw items with GDI (no D3D, no fonts to ship): top-down BGRA
// pixels with alpha 255, ready for a B8G8R8A8 texture. Text is UTF-8, drawn antialiased (not
// ClearType: the image is resampled in the headset).
namespace bfvr::ui
{
	bool rasterise(const std::vector<draw_item>& items, int width, int height, std::vector<std::uint32_t>& pixels);

	// 32-bit BMP of rasterised pixels (pipe "panel shot", tests).
	bool write_bmp(const std::string& path, const std::vector<std::uint32_t>& pixels, int width, int height);
}
