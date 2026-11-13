#include "weapon/sling_primary.hpp"

namespace bfvr::weapon
{
	sling_primary_result sling_primary::update(const sling_primary_frame& frame)
	{
		sling_primary_result result;
		if (!frame.slot_valid)
		{
			in_hand_ = 0;
			return result;
		}
		if (frame.slot == 0)
		{
			// A different gun in the primary slot (a pickup, a crate): the old capture is not this one.
			if (frame.name != name_)
			{
				result.forget = !name_.empty() || frame.captured;
				name_ = frame.name;
				in_hand_ = 0;
			}
			in_hand_ += frame.seconds > 0 ? frame.seconds : 0.0f;
			const bool captured = frame.captured && !result.forget;
			result.capture = frame.enabled && !captured && in_hand_ >= config_.settle_seconds
				&& (result.forget ? 0 : frame.attempts) < config_.max_attempts;
			return result;
		}
		in_hand_ = 0;
		result.show = frame.enabled && frame.captured && frame.sling_valid && !name_.empty();
		return result;
	}

	void sling_primary::reset()
	{
		name_.clear();
		in_hand_ = 0;
	}
}
