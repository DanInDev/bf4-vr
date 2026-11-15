#include "weapon/action_aim.hpp"

namespace bfvr::weapon
{
	bool action_aim::start(std::uint64_t now, std::uint64_t hold_ms)
	{
		if (!done(now))
		{
			return false;
		}
		active_ = true;
		down_at_ = now + config_.lead_ms;
		up_at_ = down_at_ + hold_ms;
		until_ = up_at_ + config_.tail_ms;
		return true;
	}

	bool action_aim::button(std::uint64_t now) const
	{
		return active_ && now >= down_at_ && now < up_at_;
	}

	bool action_aim::aiming(std::uint64_t now) const
	{
		return active_ && now < until_;
	}
}
