#include "weapon/melee_gesture.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::weapon
{
	bool melee_gesture::update(const stereo::pose& head, const float (&hand)[3], bool tracked, bool busy, float seconds)
	{
		seconds = std::max(seconds, 0.0f);
		cooldown_ = std::max(cooldown_ - seconds, 0.0f);
		if (!tracked)
		{
			has_last_ = false;
			forward_speed_ = 0;
			velocity_[0] = velocity_[1] = velocity_[2] = 0;
			return false;
		}
		if (has_last_ && seconds > 0)
		{
			const float blend = config_.smoothing > 0 ? std::min(seconds / config_.smoothing, 1.0f) : 1.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				velocity_[axis] += ((hand[axis] - last_[axis]) / seconds - velocity_[axis]) * blend;
			}
		}
		for (int axis = 0; axis < 3; ++axis)
		{
			last_[axis] = hand[axis];
		}
		has_last_ = true;

		// The head's forward (-z) flattened: a strike goes where you face, whatever the head's pitch.
		const float* q = head.orientation;
		float fx = -2.0f * (q[0] * q[2] + q[1] * q[3]);
		float fz = -(1.0f - 2.0f * (q[0] * q[0] + q[1] * q[1]));
		const float length = std::sqrt(fx * fx + fz * fz);
		if (length < 1e-4f)
		{
			return false;
		}
		fx /= length;
		fz /= length;
		forward_speed_ = velocity_[0] * fx + velocity_[2] * fz;
		const float speed = std::sqrt(velocity_[0] * velocity_[0] + velocity_[1] * velocity_[1] + velocity_[2] * velocity_[2]);
		if (speed < config_.rearm_speed)
		{
			armed_ = true;
		}
		if (busy || !armed_ || cooldown_ > 0 || forward_speed_ < config_.forward_speed)
		{
			return false;
		}
		armed_ = false;
		cooldown_ = config_.cooldown;
		return true;
	}

	void melee_gesture::reset()
	{
		*this = melee_gesture(config_);
	}
}
