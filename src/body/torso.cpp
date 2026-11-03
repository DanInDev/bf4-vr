#include "body/torso.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body
{
	namespace
	{
		constexpr float pi = 3.14159265f;
	}

	float angle_between(float a, float b)
	{
		float d = std::fmod(a - b, 2.0f * pi);
		if (d > pi)
		{
			d -= 2.0f * pi;
		}
		else if (d < -pi)
		{
			d += 2.0f * pi;
		}
		return d;
	}

	float yaw_of(float qx, float qy, float qz, float qw)
	{
		// forward = q * (0, 0, -1)
		const float fx = -2.0f * (qx * qz + qw * qy);
		const float fz = -(1.0f - 2.0f * (qx * qx + qy * qy));
		return std::atan2(-fx, -fz);
	}

	float torso_yaw::update(float head_yaw, const torso_hands& hands, float seconds)
	{
		if (!std::isfinite(head_yaw))
		{
			return yaw_;
		}
		float anchor = yaw_;
		if (hands.valid[0] && hands.valid[1])
		{
			const float x = 0.5f * (hands.offset[0][0] + hands.offset[1][0]);
			const float z = 0.5f * (hands.offset[0][2] + hands.offset[1][2]);
			const float reach = std::sqrt(x * x + z * z);
			if (std::isfinite(x) && std::isfinite(z) && reach > config_.min_hand_reach)
			{
				// Weighted in over reach_blend past the threshold: hands passing it (the off hand to the
				// chest or belt) do not snap the shoulders round.
				const float t = std::clamp((reach - config_.min_hand_reach) / config_.reach_blend, 0.0f, 1.0f);
				anchor = yaw_ + angle_between(std::atan2(-x, -z), yaw_) * (t * t * (3.0f - 2.0f * t));
			}
		}
		const float twist = std::clamp(angle_between(anchor, head_yaw), -config_.max_twist, config_.max_twist);
		const float target = head_yaw + twist;
		const float step = seconds > 0.0f ? 1.0f - std::exp(-seconds / config_.follow_seconds) : 0.0f;
		yaw_ = angle_between(yaw_ + angle_between(target, yaw_) * step, 0.0f);
		return yaw_;
	}
}
