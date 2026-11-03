#include "body/gun_recoil.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::body
{
	recoil_offset gun_recoil::decayed(double now) const
	{
		if (last_ < 0.0 || now < last_ || now - last_ >= 2.0)
		{
			return {};
		}
		const float elapsed = static_cast<float>(now - last_);
		return { state_.degrees * std::exp(-elapsed / config_.pitch_recovery), state_.back * std::exp(-elapsed / config_.back_recovery) };
	}

	void gun_recoil::shot(double now, int shots, float strength, bool two_handed)
	{
		if (shots <= 0 || !(strength > 0.0f))
		{
			return;
		}
		const float scale = strength * (two_handed ? config_.two_handed_scale : 1.0f) * static_cast<float>(shots);
		const recoil_offset before = decayed(now);
		state_.degrees = std::min(config_.max_degrees, before.degrees + config_.degrees_per_shot * scale);
		state_.back = std::min(config_.max_back, before.back + config_.back_per_shot * scale);
		last_ = now;
	}

	recoil_offset gun_recoil::at(double now) const
	{
		return decayed(now);
	}
}
