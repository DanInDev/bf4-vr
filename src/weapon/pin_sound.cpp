#include "weapon/pin_sound.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace bfvr::weapon
{
	std::vector<float> pin_samples(int rate, float volume)
	{
		constexpr float pi = 3.14159265f;
		const float v = std::clamp(volume, 0.0f, 1.0f);
		std::vector<float> out(static_cast<std::size_t>(rate * 0.18f));
		std::uint32_t seed = 51;
		for (std::size_t i = 0; i < out.size(); ++i)
		{
			const float t = static_cast<float>(i) / static_cast<float>(rate);
			seed = seed * 1664525u + 1013904223u;
			const float noise = static_cast<float>(seed >> 8) / 8388608.0f - 1.0f;
			const float ring = std::exp(-t * 30.0f) * (std::sin(2.0f * pi * 2200.0f * t) + 0.35f * std::sin(2.0f * pi * 3300.0f * t));
			const float click = std::exp(-t * 500.0f) * noise;
			out[i] = v * (0.30f * ring + 0.25f * click);
		}
		return out;
	}
}
