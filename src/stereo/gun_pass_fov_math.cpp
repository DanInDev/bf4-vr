#include "stereo/gun_pass_fov_math.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::stereo::gun_pass_fov_math
{
	namespace
	{
		constexpr float degrees_per_radian = 57.2957795f;
	}

	float life_size_half_fov(float world_half, float gun_scale)
	{
		const float scale = std::clamp(gun_scale, 0.05f, 20.0f);
		const float half = std::atan(std::tan(world_half) / scale);
		return std::clamp(half, 0.5f / degrees_per_radian, 85.0f / degrees_per_radian);
	}

	float degrees_of(float half_fov)
	{
		return 2.0f * half_fov * degrees_per_radian;
	}

	bool plausible_degrees(float degrees)
	{
		return std::isfinite(degrees) && degrees >= 1.0f && degrees <= 170.0f;
	}
}
