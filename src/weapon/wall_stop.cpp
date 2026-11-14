#include "weapon/wall_stop.hpp"

#include <cmath>

namespace bfvr::weapon
{
	float wall_stop::update(float seconds, bool known, float inside)
	{
		float target = 0.0f;
		let_go_ = false;
		if (known && std::isfinite(inside) && inside > -config_.margin)
		{
			target = inside + config_.margin;
			if (target > config_.max_push)
			{
				target = 0.0f;
				let_go_ = true;
			}
		}
		const float dt = std::isfinite(seconds) && seconds > 0.0f ? std::fmin(seconds, 0.1f) : 0.0f;
		const float tau = target > push_ ? config_.ease_in : config_.ease_out;
		push_ += (target - push_) * (tau > 0.0f ? 1.0f - std::exp(-dt / tau) : 1.0f);
		if (target == 0.0f && push_ < 1e-4f)
		{
			push_ = 0.0f;
		}
		return push_;
	}
}
