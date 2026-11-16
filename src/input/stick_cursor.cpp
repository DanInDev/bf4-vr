#include "input/stick_cursor.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::input
{
	bool stick_cursor::update(float x, float y, float seconds, float aspect)
	{
		if (!std::isfinite(x) || !std::isfinite(y) || !std::isfinite(seconds) || !std::isfinite(aspect) || aspect <= 0.0f)
		{
			return false;
		}
		const float tilt = std::sqrt(x * x + y * y);
		if (tilt <= config_.deadzone)
		{
			return false;
		}
		const float reach = std::min(1.0f, (tilt - config_.deadzone) / (1.0f - config_.deadzone));
		const float rate = std::pow(reach, config_.exponent) * config_.speed * std::clamp(seconds, 0.0f, config_.max_step);
		u_ = std::clamp(u_ + rate * x / tilt, 0.0f, 1.0f);
		v_ = std::clamp(v_ - rate * aspect * y / tilt, 0.0f, 1.0f);
		return true;
	}

	void stick_cursor::reset(float u, float v)
	{
		u_ = std::clamp(u, 0.0f, 1.0f);
		v_ = std::clamp(v, 0.0f, 1.0f);
	}
}
