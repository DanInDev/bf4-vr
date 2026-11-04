#include "stereo/foveation_pattern.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::stereo::foveation_pattern
{
	rings level(std::string_view name)
	{
		if (name == "light")
		{
			return { true, 0.80f, 1.10f };
		}
		if (name == "medium")
		{
			return { true, 0.65f, 0.95f };
		}
		if (name == "strong")
		{
			return { true, 0.50f, 0.80f };
		}
		return {};
	}

	namespace
	{
		std::uint8_t rate_at(float x, float y, const rings& r)
		{
			const float radius = std::sqrt(x * x + y * y);
			return radius < r.inner ? 0 : (radius < r.outer ? 1 : 2);
		}
	}

	std::vector<std::uint8_t> make(std::uint32_t width, std::uint32_t height, const rings& r, std::uint32_t& tiles_x, std::uint32_t& tiles_y)
	{
		constexpr std::uint32_t tile = 16;
		tiles_x = (width + tile - 1) / tile;
		tiles_y = (height + tile - 1) / tile;
		std::vector<std::uint8_t> rates(static_cast<std::size_t>(tiles_x) * tiles_y, 0);
		if (!r.enabled || width == 0 || height == 0)
		{
			return rates;
		}
		for (std::uint32_t ty = 0; ty < tiles_y; ++ty)
		{
			for (std::uint32_t tx = 0; tx < tiles_x; ++tx)
			{
				// The tile's corner nearest the centre decides: a tile touching the full-rate disc stays full.
				const float left = static_cast<float>(tx * tile), right = static_cast<float>(std::min((tx + 1) * tile, width));
				const float top = static_cast<float>(ty * tile), bottom = static_cast<float>(std::min((ty + 1) * tile, height));
				const float cx = 0.5f * static_cast<float>(width), cy = 0.5f * static_cast<float>(height);
				const float nx = std::fmax(0.0f, std::fmax(left - cx, cx - right)) / cx;
				const float ny = std::fmax(0.0f, std::fmax(top - cy, cy - bottom)) / cy;
				rates[static_cast<std::size_t>(ty) * tiles_x + tx] = rate_at(nx, ny, r);
			}
		}
		return rates;
	}

	void coverage(const rings& r, float (&share)[3])
	{
		share[0] = share[1] = share[2] = 0.0f;
		constexpr int steps = 200;
		for (int j = 0; j < steps; ++j)
		{
			for (int i = 0; i < steps; ++i)
			{
				const float x = (static_cast<float>(i) + 0.5f) / steps * 2.0f - 1.0f;
				const float y = (static_cast<float>(j) + 0.5f) / steps * 2.0f - 1.0f;
				share[r.enabled ? rate_at(x, y, r) : 0] += 1.0f / (steps * steps);
			}
		}
	}
}
