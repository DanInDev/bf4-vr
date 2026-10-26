#include "weapon/accessory_touch.hpp"

#include <cmath>

namespace bfvr::weapon
{
	namespace
	{
		float distance(v3 a, v3 b)
		{
			const float x = a.x - b.x, y = a.y - b.y, z = a.z - b.z;
			return std::sqrt(x * x + y * y + z * z);
		}
	}

	accessory_result accessory_touch::update(const accessory_frame& frame)
	{
		accessory_result result;
		const bool pressed = frame.grip && !last_grip_;
		last_grip_ = frame.grip;
		if (!frame.hands_valid || frame.holding)
		{
			return result;
		}
		float nearest = 1e9f;
		if (frame.has_optic)
		{
			nearest = std::fmin(nearest, distance(frame.off_hand, frame.optic));
		}
		if (frame.has_rail)
		{
			nearest = std::fmin(nearest, distance(frame.off_hand, frame.rail));
		}
		result.busy = nearest < reach_ && nearest < distance(frame.off_hand, frame.magwell);
		result.toggle = result.busy && pressed;
		return result;
	}
}
