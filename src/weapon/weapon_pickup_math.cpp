#include "weapon/weapon_pickup.hpp"

#include <cmath>

namespace bfvr::weapon
{
	int pickup_in_reach(const pickup_config& config, const float (&palm)[3], bool head_valid, const float (&head)[3], const pickup_point* pickups,
		int count)
	{
		int best = -1;
		float best_distance = 1e9f;
		for (int i = 0; pickups && i < count; ++i)
		{
			const pickup_point& p = pickups[i];
			if (!head_valid)
			{
				continue;
			}
			// The game offers it only within its radius of the soldier (horizontally, from the head).
			const float sx = p.position[0] - head[0], sz = p.position[2] - head[2];
			if (std::sqrt(sx * sx + sz * sz) > p.radius)
			{
				continue;
			}
			const float dx = p.position[0] - palm[0], dy = p.position[1] - palm[1], dz = p.position[2] - palm[2];
			const float around = std::sqrt(dx * dx + dz * dz);
			const float distance = std::sqrt(around * around + dy * dy);
			const bool in_sphere = distance <= config.sphere;
			const float above = palm[1] - p.position[1]; // the palm's height over the gun
			const bool lowered = head[1] - palm[1] >= config.lowered && around <= config.column_around && above <= config.column_up
				&& above >= -config.column_down;
			if ((in_sphere || lowered) && distance < best_distance)
			{
				best = i;
				best_distance = distance;
			}
		}
		return best;
	}

	pickup_result pickup_grab::update(const pickup_frame& frame)
	{
		pickup_result result;
		if (hand_ >= 0)
		{
			held_ += frame.seconds > 0.0f ? frame.seconds : 0.0f;
			const pickup_hand& hand = frame.hands[hand_];
			// Let go, taken (the weapon in hand changed) or held long enough: R goes up.
			if (!hand.valid || !hand.grip || frame.weapon_changed || held_ > config_.max_hold)
			{
				hand_ = -1;
				target_ = -1;
				held_ = 0.0f;
			}
		}
		for (int h = 0; h < 2; ++h)
		{
			const pickup_hand& hand = frame.hands[h];
			const bool fresh = hand.valid && hand.grip && !was_grip_[h];
			was_grip_[h] = hand.valid && hand.grip;
			if (!fresh || hand.busy || hand_ >= 0)
			{
				continue;
			}
			const int target = pickup_in_reach(config_, hand.palm, frame.head_valid, frame.head, frame.pickups, frame.count);
			if (target >= 0)
			{
				hand_ = h;
				target_ = target;
				held_ = 0.0f;
				result.started = true;
			}
		}
		result.hold = hand_ >= 0;
		result.hand = hand_;
		result.target = target_;
		return result;
	}
}
