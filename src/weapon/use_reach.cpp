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
		const bool ahead = forward >= config_.min_forward && dy <= config_.max_up && dy >= config_.min_down && std::fabs(side) <= config_.max_side;
		const bool ground = dy <= config_.pickup_below && forward >= config_.pickup_min_forward && std::fabs(side) <= config_.max_side
			&& std::sqrt(dx * dx + dz * dz) <= config_.pickup_max_distance;
		if (!ahead && !ground)
		{
			return false;
		}
		if (frame.gun_line_valid)
		{
			// Distance to the segment from the grip through the handguard, extended past it.
			const v3 a = frame.gun_grip;
			const float lx = frame.gun_support.x - a.x, ly = frame.gun_support.y - a.y, lz = frame.gun_support.z - a.z;
			const float length = std::sqrt(lx * lx + ly * ly + lz * lz);
			if (length > 1e-4f)
			{
				const float ux = lx / length, uy = ly / length, uz = lz / length;
				const float px = frame.off_hand.x - a.x, py = frame.off_hand.y - a.y, pz = frame.off_hand.z - a.z;
				const float along = std::fmax(0.0f, std::fmin(length + config_.gun_line_beyond, px * ux + py * uy + pz * uz));
				const float qx = px - ux * along, qy = py - uy * along, qz = pz - uz * along;
				if (std::sqrt(qx * qx + qy * qy + qz * qz) < config_.min_gun_line_distance)
				{
					return false;
				}
				return true;
			}
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
