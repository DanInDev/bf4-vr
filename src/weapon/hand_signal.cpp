#include "weapon/hand_signal.hpp"

#include <cmath>

namespace bfvr::weapon
{
	signal_result hand_signal::update(const signal_frame& frame, float seconds)
	{
		signal_result result;
		const bool pressed = frame.off_grip && !last_grip_;
		last_grip_ = frame.off_grip;
		cooldown_ = std::fmax(cooldown_ - seconds, 0.0f);
		if (!frame.head_valid || !frame.hand_valid)
		{
			return result;
		}
		// Head yaw axes in OpenXR LOCAL: forward (-sin, 0, -cos), right (cos, 0, -sin).
		const float s = std::sin(frame.head_yaw), c = std::cos(frame.head_yaw);
		const float dx = frame.off_hand.x - frame.head.x, dy = frame.off_hand.y - frame.head.y, dz = frame.off_hand.z - frame.head.z;
		const float forward = -dx * s - dz * c;
		const float side = dx * c - dz * s;
		result.raised = forward >= config_.min_forward && dy >= config_.min_up && std::fabs(side) <= config_.max_side;
		if (result.raised && pressed && cooldown_ <= 0.0f)
		{
			result.order = true;
			cooldown_ = config_.cooldown;
		}
		return result;
	}
}
