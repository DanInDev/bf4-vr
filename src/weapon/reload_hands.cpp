#include "weapon/reload_hands.hpp"

#include <cmath>

namespace bfvr::weapon
{
	namespace
	{
		v3 operator-(v3 a, v3 b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		v3 operator+(v3 a, v3 b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		v3 operator*(v3 a, float s) { return { a.x * s, a.y * s, a.z * s }; }
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

		q4 nlerp(q4 a, q4 b, float t)
		{
			const float s = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0 ? -1.0f : 1.0f;
			q4 q{ a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t };
			const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
			return length > 1e-6f ? q4{ q.x / length, q.y / length, q.z / length, q.w / length } : b;
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
		const v3 reach = frame.has_palm ? frame.off_palm : frame.off_hand;
		const bool ak = gun.profile().kind == variant::ak;
		const v3 magazine_body = frame.has_root ? frame.magwell + rotate(frame.root_rotation, { 0, -config_.magazine_grab_drop, 0 }) : frame.magwell;
		const bool on_handguard = frame.has_root && dot(reach - frame.magwell, rotate(frame.root_rotation, { 0, 0, 1 })) > config_.magazine_ahead_limit;
		const float to_magwell = valid && !on_handguard ? distance(reach, magazine_body) : 1e9f;
		const float to_chest = valid ? distance(reach, frame.chest) : 1e9f;
		// The charging handle: from the gun frame per variant, else the slide bone.
		v3 handle = frame.slide;
		float handle_reach = config_.reach_slide;
		const v3 handle_in_gun = frame.has_handle_point ? frame.handle_point : ak ? config_.handle_ak : config_.handle_ar;
		// Where the hand grabs it: the knob itself, or for an AK a larger zone out beside the gun on the
		// handle's side, so it is also found reaching under the gun (headset 2026-11-04).
		v3 handle_grab = handle;
		if (frame.has_root)
		{
			handle = frame.root + rotate(frame.root_rotation, handle_in_gun);
			handle_reach = ak ? config_.reach_handle_ak : config_.reach_handle;
			handle_grab = ak ? handle + rotate(frame.root_rotation, config_.handle_grab_ak) : handle;
		}
		// Back along the barrel: -z of the gun frame, else +z of the aim rotation.
		const v3 back = frame.has_root ? rotate(frame.root_rotation, { 0, 0, -1 }) : rotate(frame.gun_rotation, { 0, 0, 1 });
		const float to_slide = valid && (frame.has_root || frame.has_slide) ? distance(reach, handle_grab) : 1e9f;
		// Zones overlap on small guns: the nearest one wins.
		const bool near_magwell = to_magwell < config_.reach_magazine && !(to_slide < handle_reach && to_slide < to_magwell);
		const bool near_handle = to_slide < handle_reach && !near_magwell;
		const bool near_chest = to_chest < config_.reach_chest;
		// A held magazine turns with the off hand; its top sits where the hand holds it.
		const q4 held_rotation = frame.has_held ? frame.held_rotation : multiply(frame.off_rotation, held_relative_);
		const v3 held_top = frame.has_held ? frame.held_top : frame.off_hand + rotate(frame.off_rotation, ak ? config_.hold_ak : config_.hold_ar);
		const bool held_at_magwell = valid && distance(held_top, frame.magwell) < config_.reach_magazine;
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
			if (near_handle)
			{
				racking_ = true;
				rack_full_ = false;
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
			if (distance(held_top, frame.magwell) > config_.rearm_distance)
			{
				insert_armed_ = true;
			}
			if (frame.grip && insert_armed_ && held_at_magwell)
			{
				const reload_event event = gun.insert();
				if (event == reload_event::flicked_in)
				{
					start_fall(frame.magwell, frame.magazine_rotation);
				}
				else if (event == reload_event::magazine_in)
				{
					seating_ = true;
					seat_time_ = 0.0f;
					seat_from_ = held_top;
					seat_rotation_ = held_rotation;
				}
				emit(result, event);
			}
		}

		if (grip_released)
		{
			if (racking_)
			{
				racking_ = false;
				// Once at the rear the bolt goes home on release, wherever the hand rode it.
				if (valid && (rack_full_ || dot(frame.off_hand - rack_start_, back) >= config_.rack_pull))
				{
					emit(result, gun.rack());
				}
			}
			else if (gun.state().holding)
			{
				const reload_event event = gun.let_go(near_chest);
				if (event == reload_event::magazine_dropped)
				{
					start_fall(held_top, held_rotation);
				}
				emit(result, event);
			}
		}

		// Also before the grip closes: the game must never see the press that starts a gesture.
		result.off_hand_busy = gun.state().holding || racking_ || near_magwell || near_chest || to_slide < handle_reach;
		result.zone_centre[0] = magazine_body;
		result.zone_reach[0] = config_.reach_magazine;
		result.zone_centre[1] = handle_grab;
		result.zone_reach[1] = handle_reach;
		result.has_handle = frame.has_root || frame.has_slide;
		if (racking_ && valid)
		{
			result.racking = true;
			result.handle_in_gun = handle_in_gun;
			result.rack_pull = std::fmax(0.0f, std::fmin(config_.slide_travel, dot(frame.off_hand - rack_start_, back)));
			if (!rack_full_ && result.rack_pull >= config_.rack_pull)
			{
				rack_full_ = true;
				result.rack_at_rear = true;
			}
		}
		result.zone_centre[2] = frame.chest;
		result.zone_reach[2] = config_.reach_chest;
		if (!gun.state().holding && !racking_ && !frame.grip)
		{
			result.in_zone = near_handle ? hand_result::zone::handle : near_magwell ? hand_result::zone::magazine
				: near_chest ? hand_result::zone::chest : hand_result::zone::none;
		}

		if (gun.state().holding && valid)
		{
			result.draw = magazine_draw::in_hand;
			result.magazine_position = held_top;
			result.magazine_rotation = held_rotation;
		}
		else if (gun.state().magazine_in)
		{
			result.draw = magazine_draw::with_gun;
			if (seating_ && valid)
			{
				// Pushed home: from where the hand let it go into the magwell, easing in.
				seat_time_ += frame.seconds;
				const float t = config_.seat_seconds > 0.0f ? std::fmin(1.0f, seat_time_ / config_.seat_seconds) : 1.0f;
				if (t < 1.0f)
				{
					const float eased = t * t * (3.0f - 2.0f * t);
					result.draw = magazine_draw::in_hand;
					result.magazine_position = seat_from_ + (frame.magwell - seat_from_) * eased;
					result.magazine_rotation = nlerp(seat_rotation_, frame.magazine_rotation, eased);
				}
				else
				{
					seating_ = false;
				}
			}
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
