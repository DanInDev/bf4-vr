#include "stereo/steady_camera.hpp"

#include <cmath>

namespace bfvr::stereo
{
	float sway_filter::update(float offset, float seconds)
	{
		if (!std::isfinite(offset))
		{
			return 0.0f;
		}
		if (!primed_ || std::fabs(offset - steady_) > jump)
		{
			primed_ = true;
			steady_ = offset;
			return 0.0f;
		}
		if (std::isfinite(seconds) && seconds > 0.0f)
		{
			steady_ += (offset - steady_) * (1.0f - std::exp(-std::fmin(seconds, 0.25f) / time_constant));
		}
		return offset - steady_;
	}

	void sway_filter::reset()
	{
		primed_ = false;
		steady_ = 0.0f;
	}
}
