#include "weapon/reload_hands.hpp"

#include <cmath>

namespace bfvr::weapon
{
	namespace
	{
		v3 operator-(v3 a, v3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		v3 operator+(v3 a, v3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		float dot(v3 a, v3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		float distance(v3 a, v3 b) { return std::sqrt(dot(a - b, a - b)); }

		q4 multiply(q4 a, q4 b)
		{
			return { a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z };
		}

		q4 conjugate(q4 q) { return { -q.x, -q.y, -q.z, q.w }; }

		v3 rotate(q4 q, v3 v)
		{
			const q4 p = multiply(multiply(q, { v.x, v.y, v.z, 0 }), conjugate(q));
			return { p.x, p.y, p.z };
		}
	}

	void reload_hands::reset()
	{
		*this = reload_hands(config_);
	}

	void reload_hands::emit(hand_result& result, reload_event event)
	{
		if (event != reload_event::none && result.count < hand_result::max_events)
		{
			result.events[result.count++] = event;
		}
	}

	void reload_hands::start_fall(v3 from, q4 rotation)
	{
		falling_ = true;
		fall_time_ = 0.0f;
		fall_start_ = from;
		fall_rotation_ = rotation;
	}

	hand_result reload_hands::update(reload& gun, const hand_frame& frame)
	{
		hand_result result;
		const bool grip_pressed = frame.grip && !last_grip_;
		const bool grip_released = !frame.grip && last_grip_;
		const bool button_pressed = frame.button && !last_button_;
		last_grip_ = frame.grip;
		last_button_ = frame.button;

		const bool valid = frame.hands_valid;
		const float to_magwell = valid ? distance(frame.off_hand, frame.magwell) : 1e9f;
		const float to_chest = valid ? distance(frame.off_hand, frame.chest) : 1e9f;
		const float to_slide = valid && frame.has_slide ? distance(frame.off_hand, frame.slide) : 1e9f;
		const bool near_magwell = to_magwell < config_.reach_magazine;
		const bool near_chest = to_chest < config_.reach_chest;
		const q4 held_rotation = multiply(frame.gun_rotation, held_relative_);
		const auto start_holding = [&](bool armed)
		{
			held_relative_ = multiply(conjugate(frame.gun_rotation), frame.magazine_rotation);
			insert_armed_ = armed;
		};

		if (button_pressed)
		{
			const reload_event event = gun.press_button(frame.grip && near_magwell);
			if (event == reload_event::magazine_out)
			{
				start_holding(false);
			}
			else if (event == reload_event::magazine_dropped)
			{
				start_fall(frame.magwell, frame.magazine_rotation);
			}
			emit(result, event);
		}

		if (grip_pressed && valid && !gun.state().holding)
		{
			if (to_slide < config_.reach_slide)
			{
				racking_ = true;
				rack_start_ = frame.off_hand;
			}
			else if (near_magwell)
			{
				const reload_event event = gun.grab_magazine();
				if (event == reload_event::magazine_out)
				{
					start_holding(false);
				}
				emit(result, event);
			}
			else if (near_chest)
			{
				const reload_event event = gun.take_from_chest();
				if (event == reload_event::magazine_taken)
				{
					// A fresh magazine sits in the hand as the seated one sits on the gun.
					start_holding(true);
				}
				emit(result, event);
			}
		}

		if (gun.state().holding && valid)
		{
			if (to_magwell > config_.rearm_distance)
			{
				insert_armed_ = true;
			}
			if (frame.grip && insert_armed_ && near_magwell)
			{
				const reload_event event = gun.insert();
				if (event == reload_event::flicked_in)
				{
					start_fall(frame.magwell, frame.magazine_rotation);
				}
				emit(result, event);
			}
		}

		if (grip_released)
		{
			if (racking_)
			{
				racking_ = false;
				// Back = +z of the aim rotation (the barrel points down -z).
				const v3 back = rotate(frame.gun_rotation, { 0, 0, 1 });
				if (valid && dot(frame.off_hand - rack_start_, back) >= config_.rack_pull)
				{
					emit(result, gun.rack());
				}
			}
			else if (gun.state().holding)
			{
				const reload_event event = gun.let_go(near_chest);
				if (event == reload_event::magazine_dropped)
				{
					start_fall(frame.off_hand, held_rotation);
				}
				emit(result, event);
			}
		}

		// Also before the grip closes: the game must never see the press that starts a gesture.
		result.off_hand_busy = gun.state().holding || racking_ || near_magwell || near_chest || to_slide < config_.reach_slide;

		if (gun.state().holding && valid)
		{
			result.draw = magazine_draw::in_hand;
			result.magazine_position = frame.off_hand;
			result.magazine_rotation = held_rotation;
		}
		else if (gun.state().magazine_in)
		{
			result.draw = magazine_draw::with_gun;
		}
		else
		{
			// Out of the gun and not in a tracked hand.
			result.draw = magazine_draw::hidden;
			result.magazine_position = frame.magwell + v3{ 0, -50.0f, 0 };
			result.magazine_rotation = frame.magazine_rotation;
		}
		if (falling_)
		{
			fall_time_ += frame.seconds;
			if (fall_time_ >= config_.fall_seconds)
			{
				falling_ = false;
			}
			else if (result.draw == magazine_draw::hidden)
			{
				// One magazine mesh: a flicked-out magazine is not drawn falling, the new one is seated.
				result.draw = magazine_draw::falling;
				result.magazine_position = fall_start_ + v3{ 0, -4.905f * fall_time_ * fall_time_, 0 };
				result.magazine_rotation = fall_rotation_;
			}
		}
		return result;
	}
}
