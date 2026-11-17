#include "weapon/grenade_hands.hpp"

#include <cmath>

namespace bfvr::weapon
{
	namespace
	{
		v3 minus(v3 a, v3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		float length(v3 a) { return std::sqrt(a.x * a.x + a.y * a.y + a.z * a.z); }
	}

	void grenade_hands::reset()
	{
		*this = grenade_hands(config_);
	}

	// Average velocity of the off hand over velocity_window seconds ending at the sample that is
	// back samples old (0 = the newest).
	v3 grenade_hands::velocity_ending(int back) const
	{
		if (history_count_ - back < 2)
		{
			return {};
		}
		const sample& newest = history_[(history_next_ + history_size - 1 - back) % history_size];
		const sample* oldest = &newest;
		for (int i = back + 2; i <= history_count_; ++i)
		{
			const sample& s = history_[(history_next_ + history_size - i) % history_size];
			oldest = &s;
			if (newest.time - s.time >= config_.velocity_window)
			{
				break;
			}
		}
		const float dt = newest.time - oldest->time;
		if (dt <= 1e-4f)
		{
			return {};
		}
		const v3 d = minus(newest.position, oldest->position);
		return { d.x / dt, d.y / dt, d.z / dt };
	}

	// Hands often slow down just before the fingers open: the fastest motion shortly before release.
	v3 grenade_hands::release_velocity() const
	{
		if (history_count_ == 0)
		{
			return {};
		}
		const float now = history_[(history_next_ + history_size - 1) % history_size].time;
		v3 best;
		float best_speed = -1.0f;
		for (int back = 0; back < history_count_; ++back)
		{
			if (now - history_[(history_next_ + history_size - 1 - back) % history_size].time > config_.release_lookback)
			{
				break;
			}
			const v3 v = velocity_ending(back);
			const float speed = length(v);
			if (speed > best_speed)
			{
				best_speed = speed;
				best = v;
			}
		}
		return best;
	}

	grenade_result grenade_hands::update(const grenade_frame& frame)
	{
		grenade_result result;
		const bool off_pressed = frame.off_grip && !last_off_grip_;
		const bool off_released = !frame.off_grip && last_off_grip_;
		const bool gun_pressed = frame.gun_grip && !last_gun_grip_;
		const bool gun_released = !frame.gun_grip && last_gun_grip_;
		last_off_grip_ = frame.off_grip;
		last_gun_grip_ = frame.gun_grip;
		clock_ += frame.seconds;

		if (!frame.hands_valid)
		{
			// Tracking lost: a held grenade goes back (never a throw from a stale pose).
			if (holding_)
			{
				result.event = grenade_event::put_back;
			}
			holding_ = armed_ = pin_held_ = bitten_ = false;
			bite_time_ = 0.0f;
			history_count_ = 0;
			return result;
		}

		const bool off_near_belt = length(minus(frame.off_hand, frame.belt)) < config_.reach_belt;
		const bool gun_near_belt = frame.either_hand && length(minus(frame.gun_hand, frame.belt)) < config_.reach_belt;
		if (!holding_)
		{
			if (off_pressed && off_near_belt && frame.have_grenade)
			{
				holding_ = true;
				holder_ = 0;
				armed_ = pin_held_ = bitten_ = false;
				bite_time_ = 0.0f;
				history_count_ = 0;
				result.event = grenade_event::taken;
			}
			else if (gun_pressed && gun_near_belt && frame.have_grenade)
			{
				holding_ = true;
				holder_ = 1;
				armed_ = pin_held_ = bitten_ = false;
				bite_time_ = 0.0f;
				history_count_ = 0;
				result.event = grenade_event::taken;
			}
		}
		// The holding hand and the pin hand, by who took the grenade.
		const bool gun_holds = holding_ && holder_ == 1;
		const v3 holder = gun_holds ? frame.gun_hand : frame.off_hand;
		const v3 puller = gun_holds ? frame.off_hand : frame.gun_hand;
		const bool puller_grip = gun_holds ? frame.off_grip : frame.gun_grip;
		const bool puller_pressed = gun_holds ? off_pressed : gun_pressed;
		const bool holder_released = gun_holds ? gun_released : off_released;
		history_[history_next_] = { clock_, holder };
		history_next_ = (history_next_ + 1) % history_size;
		history_count_ = history_count_ < history_size ? history_count_ + 1 : history_size;
		const float pin_distance = length(minus(puller, holder));

		if (holding_ && result.event != grenade_event::taken)
		{
			if (!armed_ && frame.has_mouth)
			{
				// The teeth: held at the mouth a moment the pin is bitten; yanked away it comes out.
				const float to_mouth = length(minus(holder, frame.mouth));
				if (!bitten_)
				{
					bite_time_ = to_mouth < config_.mouth_reach ? bite_time_ + frame.seconds : 0.0f;
					if (bite_time_ >= config_.bite_seconds)
					{
						bitten_ = true;
						result.bite_now = true;
					}
				}
				else if (to_mouth > config_.mouth_pull)
				{
					armed_ = true;
					bitten_ = pin_held_ = false;
					result.event = grenade_event::pin_pulled;
				}
			}
			if (!armed_)
			{
				if (puller_pressed && pin_distance < config_.reach_pin)
				{
					pin_held_ = true;
					pin_start_ = puller;
				}
				if (pin_held_ && !puller_grip)
				{
					pin_held_ = false; // let go of the pin before pulling it out
				}
				if (pin_held_ && pin_distance > config_.reach_pin * 0.5f
					&& length(minus(puller, pin_start_)) >= config_.pin_pull)
				{
					armed_ = true;
					pin_held_ = bitten_ = false;
					result.event = grenade_event::pin_pulled;
				}
			}
			if (holder_released)
			{
				const v3 v = release_velocity();
				result.position = holder;
				result.velocity = v;
				if (!armed_)
				{
					result.event = grenade_event::put_back;
				}
				else
				{
					result.event = length(v) >= config_.throw_speed ? grenade_event::thrown : grenade_event::dropped;
				}
				holding_ = armed_ = pin_held_ = bitten_ = false;
				bite_time_ = 0.0f;
			}
		}

		result.holding = holding_;
		result.pin_bitten = holding_ && bitten_;
		result.armed = armed_;
		result.hand = holder_;
		const bool pin_busy = holding_ && (pin_held_ || pin_distance < config_.reach_pin);
		result.off_busy = gun_holds ? pin_busy : holding_ || off_near_belt;
		result.gun_busy = gun_holds ? true : pin_busy || (!holding_ && gun_near_belt);
		return result;
	}
	bool hide_quick_throw_switch(bool throw_pending, int current_slot, int previous_slot)
	{
		return throw_pending && current_slot == hand_grenade_slot && previous_slot != hand_grenade_slot && previous_slot >= 0;
	}
	throw_in_frame throw_along_aim(v3 offset_from_eyes, v3 velocity)
	{
		throw_in_frame out;
		const float speed = length(velocity);
		if (!(speed > 1e-3f) || !std::isfinite(speed))
		{
			return out;
		}
		const v3 forward{ velocity.x / speed, velocity.y / speed, velocity.z / speed };
		// right = forward x up (LOCAL: x right, y up, z back); straight up or down has no right: take LOCAL's.
		v3 right{ -forward.z, 0.0f, forward.x };
		const float r = length(right);
		right = r > 1e-3f ? v3{ right.x / r, right.y / r, right.z / r } : v3{ 1.0f, 0.0f, 0.0f };
		const v3 up{ right.y * forward.z - right.z * forward.y, right.z * forward.x - right.x * forward.z, right.x * forward.y - right.y * forward.x };
		const auto dot = [](v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; };
		out.valid = true;
		out.aim = forward;
		out.position = { -dot(offset_from_eyes, right), dot(offset_from_eyes, up), dot(offset_from_eyes, forward) };
		out.speed = { 0.0f, 0.0f, speed };
		return out;
	}
}
