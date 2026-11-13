#include "weapon/slot_capture_start.hpp"

namespace bfvr::weapon
{
	slot_capture_request slot_capture_start::update(const slot_capture_frame& frame)
	{
		const float seconds = frame.seconds > 0 ? frame.seconds : 0.0f;
		if (!frame.slot_valid)
		{
			invalid_for_ += seconds;
			gun_for_ = 0;
			if (invalid_for_ >= config_.rearm_seconds)
			{
				armed_ = true;
				tried_grenade_ = tried_knife_ = 0;
			}
			return slot_capture_request::none;
		}
		invalid_for_ = 0;
		const bool gun = frame.slot == 0 || frame.slot == 1;
		if (!frame.enabled || !armed_ || !gun || frame.busy)
		{
			gun_for_ = 0;
			return slot_capture_request::none;
		}
		gun_for_ += seconds;
		if (gun_for_ < config_.settle_seconds || frame.primary_pending)
		{
			return slot_capture_request::none;
		}
		gun_for_ = 0;
		// Grenade and knife take turns, each up to `tries` times, so one that keeps failing does not hold up the other.
		const bool grenade = !frame.grenade_captured && tried_grenade_ < config_.tries;
		const bool knife = !frame.knife_captured && tried_knife_ < config_.tries;
		if (grenade && (!knife || tried_grenade_ <= tried_knife_))
		{
			++tried_grenade_;
			return slot_capture_request::grenade;
		}
		if (knife)
		{
			++tried_knife_;
			return slot_capture_request::knife;
		}
		armed_ = false;
		return slot_capture_request::none;
	}
}
