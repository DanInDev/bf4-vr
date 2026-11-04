#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

// Engine-free: the shading-rate map for fixed foveated rendering (stereo/foveation), tested by
// tests/foveation_test.cpp. One byte per 16x16-pixel tile: 0 = full rate, 1 = one shade per 2x2
// pixels, 2 = one per 4x4.
namespace bfvr::stereo::foveation_pattern
{
	struct rings
	{
		bool enabled{};
		float inner{}; // full rate inside this radius (1 = the image's half height / half width)
		float outer{}; // 2x2 up to this radius, 4x4 beyond
	};

	// Setting foveation: off | light | medium | strong.
	rings level(std::string_view name);

	// Rates for a target of width x height pixels (tiles cover partial edges). The radius is measured in
	// the image's own half extents, so the full-rate region is the same share of the view on any target.
	std::vector<std::uint8_t> make(std::uint32_t width, std::uint32_t height, const rings& r, std::uint32_t& tiles_x, std::uint32_t& tiles_y);

	// Share of pixels shaded at each rate (full, 2x2, 4x4) for a ring setting: what it saves.
	void coverage(const rings& r, float (&share)[3]);
}
