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

	// Average velocity over the last velocity_window seconds of the off hand.
	v3 grenade_hands::velocity() const
	{
		if (history_count_ < 2)
		{
			return {};
		}
		const sample& newest = history_[(history_next_ + history_size - 1) % history_size];
		const sample* oldest = &newest;
		for (int i = 2; i <= history_count_; ++i)
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

	grenade_result grenade_hands::update(const grenade_frame& frame)
	{
		grenade_result result;
		const bool off_pressed = frame.off_grip && !last_off_grip_;
		const bool off_released = !frame.off_grip && last_off_grip_;
		const bool gun_pressed = frame.gun_grip && !last_gun_grip_;
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
			holding_ = armed_ = pin_held_ = false;
			history_count_ = 0;
			return result;
		}

		history_[history_next_] = { clock_, frame.off_hand };
		history_next_ = (history_next_ + 1) % history_size;
		history_count_ = history_count_ < history_size ? history_count_ + 1 : history_size;

		const bool near_belt = length(minus(frame.off_hand, frame.belt)) < config_.reach_belt;
		const float pin_distance = length(minus(frame.gun_hand, frame.off_hand));

		if (!holding_)
		{
			if (off_pressed && near_belt && frame.have_grenade)
			{
				holding_ = true;
				armed_ = pin_held_ = false;
				result.event = grenade_event::taken;
			}
		}
		else
		{
			if (!armed_)
			{
				if (gun_pressed && pin_distance < config_.reach_pin)
				{
					pin_held_ = true;
					pin_start_ = frame.gun_hand;
				}
				if (pin_held_ && !frame.gun_grip)
				{
					pin_held_ = false; // let go of the pin before pulling it out
				}
				if (pin_held_ && pin_distance > config_.reach_pin * 0.5f
					&& length(minus(frame.gun_hand, pin_start_)) >= config_.pin_pull)
				{
					armed_ = true;
					pin_held_ = false;
					result.event = grenade_event::pin_pulled;
				}
			}
			if (off_released)
			{
				const v3 v = velocity();
				result.position = frame.off_hand;
				result.velocity = v;
				if (!armed_)
				{
					result.event = grenade_event::put_back;
				}
				else
				{
					result.event = length(v) >= config_.throw_speed ? grenade_event::thrown : grenade_event::dropped;
				}
				holding_ = armed_ = pin_held_ = false;
			}
		}

		result.holding = holding_;
		result.armed = armed_;
		result.off_busy = holding_ || near_belt;
		result.gun_busy = holding_ && (pin_held_ || pin_distance < config_.reach_pin);
		return result;
	}
}
