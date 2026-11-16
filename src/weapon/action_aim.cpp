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
		pressed_ = released_ = false;
		down_at_ = now + config_.lead_ms;
		hold_ms_ = hold_ms;
		return true;
	}

	action_button action_aim::update(std::uint64_t now)
	{
		if (!active_ || released_)
		{
			return action_button::none;
		}
		if (!pressed_)
		{
			if (now < down_at_)
			{
				return action_button::none;
			}
			pressed_ = true;
			pressed_at_ = now;
			return action_button::press;
		}
		if (now < pressed_at_ + hold_ms_)
		{
			return action_button::none;
		}
		released_ = true;
		released_at_ = now;
		return action_button::release;
	}

	bool action_aim::aiming(std::uint64_t now) const
	{
		return active_ && !done(now);
	}
}
