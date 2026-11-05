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

	void neck_offset(float head_yaw, float back, float (&out)[3])
	{
		// Back (+z) turned by the yaw about up: facing left (+90 degrees), behind is +x.
		out[0] = back * std::sin(head_yaw);
		out[1] = 0.0f;
		out[2] = back * std::cos(head_yaw);
	}

	float torso_yaw::update(float head_yaw, const torso_hands& hands, float seconds)
	{
		if (!std::isfinite(head_yaw))
		{
			return yaw_;
		}
		const float step = seconds > 0.0f ? 1.0f - std::exp(-seconds / config_.follow_seconds) : 0.0f;
		// The base follows the head only past the twist limit (the head looks around freely).
		const float head_twist = std::clamp(angle_between(base_, head_yaw), -config_.max_twist, config_.max_twist);
		base_ = angle_between(base_ + angle_between(head_yaw + head_twist, base_) * step, 0.0f);
		// ...and settles onto it slowly: a glance barely moves the chest, a turn in the room brings it round.
		const float settle = seconds > 0.0f && config_.settle_seconds > 0.0f ? 1.0f - std::exp(-seconds / config_.settle_seconds) : 0.0f;
		base_ = angle_between(base_ + angle_between(head_yaw, base_) * settle, 0.0f);
		// The lean towards two hands held together in front; it eases back to none when they stop counting.
		float lean_target = 0.0f;
		if (hands.valid[0] && hands.valid[1])
		{
			const float x = 0.5f * (hands.offset[0][0] + hands.offset[1][0]);
			const float z = 0.5f * (hands.offset[0][2] + hands.offset[1][2]);
			const float reach = std::sqrt(x * x + z * z);
			// Both hands in front of the face: each one's weight fades from the cone's edge outwards.
			float in_front = 1.0f;
			for (int hand = 0; hand < 2; ++hand)
			{
				const float hx = hands.offset[hand][0];
				const float hz = hands.offset[hand][2];
				const float out = std::sqrt(hx * hx + hz * hz);
				const float off = std::fabs(angle_between(std::atan2(-hx, -hz), head_yaw));
				const float edge = std::fmax(config_.hand_cone_edge - config_.hand_cone, 1e-3f);
				const float w = std::isfinite(out) && out > config_.hand_min_reach ? std::clamp((config_.hand_cone_edge - off) / edge, 0.0f, 1.0f) : 0.0f;
				in_front = std::fmin(in_front, w * w * (3.0f - 2.0f * w));
			}
			float apart = 0.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				const float d = hands.offset[0][axis] - hands.offset[1][axis];
				apart += d * d;
			}
			apart = std::sqrt(apart);
			const float together = std::isfinite(apart)
				? std::clamp((config_.hands_apart_edge - apart) / std::fmax(config_.hands_apart_edge - config_.hands_apart, 1e-3f), 0.0f, 1.0f) : 0.0f;
			in_front = std::fmin(in_front, together * together * (3.0f - 2.0f * together));
			if (std::isfinite(x) && std::isfinite(z) && reach > config_.min_hand_reach && in_front > 0.0f)
			{
				// Weighted in over reach_blend past the threshold: hands passing it (the off hand to the
				// chest or belt) do not snap the shoulders round.
				const float t = std::clamp((reach - config_.min_hand_reach) / config_.reach_blend, 0.0f, 1.0f);
				lean_target = angle_between(std::atan2(-x, -z), base_) * (t * t * (3.0f - 2.0f * t)) * in_front;
			}
		}
		lean_ += (lean_target - lean_) * step;
		// Never more than the twist limit from the head.
		const float twist = std::clamp(angle_between(base_ + lean_, head_yaw), -config_.max_twist, config_.max_twist);
		yaw_ = angle_between(head_yaw + twist, 0.0f);
		return yaw_;
	}
}
