#include "weapon/use_reach.hpp"

#include <cmath>

namespace bfvr::weapon
{
	bool use_reach::in_pose(const use_frame& frame) const
	{
		if (!frame.head_valid || !frame.hand_valid)
		{
			return false;
		}
		// Head yaw axes in OpenXR LOCAL: forward (-sin, 0, -cos), right (cos, 0, -sin).
		const float s = std::sin(frame.head_yaw), c = std::cos(frame.head_yaw);
		const float dx = frame.off_hand.x - frame.head.x, dy = frame.off_hand.y - frame.head.y, dz = frame.off_hand.z - frame.head.z;
		const float forward = -dx * s - dz * c;
		const float side = dx * c - dz * s;
		if (forward < config_.min_forward || dy > config_.max_up || dy < config_.min_down || std::fabs(side) > config_.max_side)
		{
			return false;
		}
		if (frame.gun_hand_valid)
		{
			const float gx = frame.off_hand.x - frame.gun_hand.x, gy = frame.off_hand.y - frame.gun_hand.y, gz = frame.off_hand.z - frame.gun_hand.z;
			if (std::sqrt(gx * gx + gy * gy + gz * gz) < config_.min_gun_distance)
			{
				return false;
			}
		}
		return true;
	}

	use_result use_reach::update(const use_frame& frame)
	{
		const bool pressed = frame.off_grip && !last_grip_;
		last_grip_ = frame.off_grip;
		const bool pose = in_pose(frame);
		if (!frame.off_grip || !frame.hand_valid)
		{
			using_ = false;
		}
		else if (pressed && pose && !frame.busy)
		{
			using_ = true;
		}
		use_result result;
		result.use = using_;
		result.reaching = using_ || (pose && !frame.busy);
		return result;
	}
}
