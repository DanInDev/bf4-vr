#include "input/physical_stance.hpp"

#include <cmath>

namespace bfvr::input
{
	int physical_stance::wanted(float drop) const
	{
		switch (stance_)
		{
		case 0:
			return drop > config_.prone_drop ? 2 : drop > config_.crouch_drop ? 1 : 0;
		case 1:
			return drop > config_.prone_drop ? 2 : drop < config_.stand_drop ? 0 : 1;
		default:
			return drop < config_.stand_drop ? 0 : drop < config_.unprone_drop ? 1 : 2;
		}
	}

	physical_stance_result physical_stance::update(float head_height, float seconds)
	{
		physical_stance_result result;
		result.stance = stance_;
		if (!std::isfinite(head_height))
		{
			return result;
		}
		if (!calibrated_ || head_height > reference_)
		{
			reference_ = head_height;
			calibrated_ = true;
		}
		result.drop = reference_ - head_height;
		const int target = wanted(result.drop);
		if (target == stance_)
		{
			timer_ = 0.0f;
			pending_ = stance_;
			return result;
		}
		if (target != pending_)
		{
			pending_ = target;
			timer_ = 0.0f;
		}
		timer_ += seconds;
		if (timer_ >= config_.hold_seconds)
		{
			stance_ = target;
			timer_ = 0.0f;
			result.stance = stance_;
			result.changed = true;
		}
		return result;
	}

	void physical_stance::reset()
	{
		calibrated_ = false;
		stance_ = pending_ = 0;
		timer_ = 0.0f;
	}
}
