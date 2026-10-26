#include "ui/vignette.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::ui
{
	namespace
	{
		float ramp(float value, float start, float full)
		{
			return full > start ? std::clamp((value - start) / (full - start), 0.0f, 1.0f) : (value >= start ? 1.0f : 0.0f);
		}
	}

	float vignette::update(float yaw_rate, float speed, float seconds)
	{
		seconds = std::max(seconds, 0.0f);
		float target = 0;
		if (config_.mode == vignette_mode::turning || config_.mode == vignette_mode::both)
		{
			target = std::max(target, ramp(std::fabs(yaw_rate), config_.turn_start, config_.turn_full));
		}
		if (config_.mode == vignette_mode::moving || config_.mode == vignette_mode::both)
		{
			target = std::max(target, ramp(std::fabs(speed), config_.move_start, config_.move_full));
		}
		target *= std::clamp(config_.strength, 0.0f, 1.0f);
		const float time = target > strength_ ? config_.attack : config_.release;
		const float step = time > 0 ? seconds / time : 1.0f;
		strength_ = target > strength_ ? std::min(target, strength_ + step) : std::max(target, strength_ - step);
		return strength_;
	}

	void vignette_texture(float strength, int size, std::vector<std::uint32_t>& pixels)
	{
		strength = std::clamp(strength, 0.0f, 1.0f);
		pixels.assign(static_cast<std::size_t>(size) * static_cast<std::size_t>(size), 0u);
		// Radius 1 = the texture's half width. The clear circle shrinks from beyond the corners
		// (1.45: nothing drawn) to 0.45 at full strength; the edge fades over 0.3.
		const float inner = 1.45f - strength;
		const float feather = 0.3f;
		const float half = 0.5f * static_cast<float>(size);
		for (int y = 0; y < size; ++y)
		{
			for (int x = 0; x < size; ++x)
			{
				const float dx = (static_cast<float>(x) + 0.5f - half) / half;
				const float dy = (static_cast<float>(y) + 0.5f - half) / half;
				const float t = std::clamp((std::sqrt(dx * dx + dy * dy) - inner) / feather, 0.0f, 1.0f);
				const float alpha = t * t * (3.0f - 2.0f * t); // smoothstep
				pixels[static_cast<std::size_t>(y) * static_cast<std::size_t>(size) + static_cast<std::size_t>(x)] =
					static_cast<std::uint32_t>(std::lround(alpha * 255.0f)) << 24;
			}
		}
	}
}
