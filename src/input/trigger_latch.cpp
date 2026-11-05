#include "input/trigger_latch.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::input
{
	std::uint8_t latch_trigger(float value, bool& held, float press)
	{
		if (!std::isfinite(press))
		{
			press = 0.55f;
		}
		press = std::clamp(press, 0.05f, 0.95f);
		const float release = std::max(press - trigger_release_gap, 0.02f);
		if (!std::isfinite(value))
		{
			value = 0.0f;
		}
		held = held ? value > release : value > press;
		return held ? 255 : 0;
	}
}
