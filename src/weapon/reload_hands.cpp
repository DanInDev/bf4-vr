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

		// Distance from p to the segment a..b.
		float segment_distance(v3 p, v3 a, v3 b)
		{
			const v3 ab = b - a;
			const float length2 = dot(ab, ab);
			const float t = length2 > 1e-9f ? std::fmax(0.0f, std::fmin(1.0f, dot(p - a, ab) / length2)) : 0.0f;
			return distance(p, a + ab * t);
		}

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

		q4 normalize(q4 q)
		{
			const float length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
			return length > 1e-6f ? q4{ q.x / length, q.y / length, q.z / length, q.w / length } : q4{};
		}

		q4 axis_angle(v3 axis, float angle)
		{
			const float length = std::sqrt(dot(axis, axis));
			if (length < 1e-6f)
			{
				return {};
			}
			const float s = std::sin(0.5f * angle) / length;
			return { axis.x * s, axis.y * s, axis.z * s, std::cos(0.5f * angle) };
		}
	}

	void tilt_magazine(v3& top, q4& rotation, q4 seated, float angle, float pivot_drop)
	{
		if (angle == 0.0f)
		{
			return;
		}
		const q4 gun = normalize(multiply(rotation, conjugate(seated)));
		const v3 pivot = top + rotate(gun, { 0.0f, -pivot_drop, 0.0f });
		// About +z a positive angle takes +y towards -x: the top goes left (+x) for a negative one.
		const q4 turn = axis_angle(rotate(gun, { 0.0f, 0.0f, 1.0f }), -angle);
		top = pivot + rotate(turn, top - pivot);
		rotation = normalize(multiply(turn, rotation));
	}

	bool grip_before_reload(v3 palm, v3 grip, float regrip_reach, const hand_result& last)
	{
		const auto gap = [&](v3 p) { return std::sqrt((palm.x - p.x) * (palm.x - p.x) + (palm.y - p.y) * (palm.y - p.y) + (palm.z - p.z) * (palm.z - p.z)); };
		const float to_grip = gap(grip);
		if (!(to_grip < regrip_reach))
		{
			return false;
		}
		for (int i = 0; i < 3; ++i)
		{
			if (i == 1 && !last.has_handle)
			{
				continue;
			}
			if (last.zone_reach[i] > 0.0f && gap(last.zone_centre[i]) <= to_grip)
			{
				return false;
			}
		}
		return true;
	}

	float angle_between(q4 a, q4 b)
	{
		const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
		return 2.0f * std::acos(std::fmin(1.0f, d)) * 57.2957795f;
	}

	float palm_lean_degrees(q4 in_gun, q4 target_in_gun)
	{
		// The turn from target to now, in gun axes; its twist about the barrel (+z).
		q4 turn = multiply(in_gun, conjugate(target_in_gun));
		if (turn.w < 0.0f)
		{
			turn = { -turn.x, -turn.y, -turn.z, -turn.w };
		}
		return std::fabs(2.0f * std::atan2(turn.z, turn.w)) * 57.2957795f;
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

	void reload_hands::start_fall(v3 from, q4 rotation, v3 velocity, const hand_frame& frame)
	{
		flight_.stop();
		loose_on_ = false;
		if (frame.throw_flight && frame.has_world && frame.ground)
		{
			// #61: a rigid box in the world. Its tumble: end over end across the throw, faster the faster it goes.
			const q4 to_world = conjugate(normalize(frame.world_to_local));
			const v3 world_velocity = rotate(to_world, velocity);
			const float horizontal = std::sqrt(world_velocity.x * world_velocity.x + world_velocity.z * world_velocity.z);
			const float speed = std::sqrt(dot(world_velocity, world_velocity));
			const v3 across = horizontal > 0.05f ? v3{ world_velocity.z / horizontal, 0.0f, -world_velocity.x / horizontal } : v3{ 1.0f, 0.0f, 0.0f };
			const v3 spin = across * std::fmin(14.0f, 4.0f * speed);
			// The box hangs from the magazine's top along the gun's -y, as it sits in the gun.
			const q4 seated = frame.has_root ? multiply(conjugate(frame.root_rotation), frame.magazine_rotation) : q4{};
			physics::loose_shape shape;
			shape.axes = conjugate(normalize(seated));
			shape.half = config_.magazine_half;
			shape.centre = rotate(shape.axes, { 0.0f, -config_.magazine_half.y, 0.0f });
			loose_.start(frame.world_origin + rotate(to_world, from), multiply(to_world, rotation), world_velocity, spin, shape);
			loose_on_ = true;
			return;
		}
		flight_config flight;
		flight.fallback_seconds = config_.fall_seconds;
		flight_ = magazine_flight(flight);
		// Flight off: the old straight drop (no velocity, no floor).
		flight_.start(from, rotation, frame.throw_flight ? velocity : v3{}, frame.throw_flight && frame.floor_valid, frame.floor_y);
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
		// #47: smoothed hand and magwell velocities (~50 ms), for a magazine let go in motion.
		if (valid && frame.seconds > 1e-4f)
		{
			if (have_last_)
			{
				const float k = std::fmin(1.0f, frame.seconds / 0.05f);
				const v3 palm_now = (reach - last_palm_) * (1.0f / frame.seconds);
				const v3 well_now = (frame.magwell - last_magwell_) * (1.0f / frame.seconds);
				palm_velocity_ = palm_velocity_ + (palm_now - palm_velocity_) * k;
				magwell_velocity_ = magwell_velocity_ + (well_now - magwell_velocity_) * k;
			}
			last_palm_ = reach;
			last_magwell_ = frame.magwell;
			have_last_ = true;
		}
		else if (!valid)
		{
			have_last_ = false;
			palm_velocity_ = magwell_velocity_ = {};
		}
		const v3 magazine_body = frame.has_root ? frame.magwell + rotate(frame.root_rotation, { 0, -config_.magazine_grab_drop, 0 }) : frame.magwell;
		const bool on_handguard = frame.has_root && dot(reach - frame.magwell, rotate(frame.root_rotation, { 0, 0, 1 })) > config_.magazine_ahead_limit;
		const float to_magwell = valid && !on_handguard ? distance(reach, magazine_body) : 1e9f;
		const float to_chest = !valid ? 1e9f : frame.has_chest_hip ? segment_distance(reach, frame.chest, frame.chest_hip) : distance(reach, frame.chest);
		const float chest_reach = frame.chest_reach > 0 ? frame.chest_reach : config_.reach_chest;
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
		const bool near_magwell = !frame.handle_only && to_magwell < config_.reach_magazine && !(to_slide < handle_reach && to_slide < to_magwell);
		const bool near_handle = to_slide < handle_reach && !near_magwell;
		const bool near_chest = !frame.handle_only && to_chest < chest_reach;
		const float stow_reach = chest_reach * config_.stow_reach_scale;
		const bool near_stow = !frame.handle_only && to_chest < stow_reach;
		// A held magazine turns with the off hand; its top sits where the hand holds it.
		q4 held_rotation = frame.has_held ? frame.held_rotation : multiply(frame.off_rotation, held_relative_);
		v3 held_top = frame.has_held ? frame.held_top : frame.off_hand + rotate(frame.off_rotation, ak ? config_.hold_ak : config_.hold_ar);
		// Palming turns the held magazine aside: the drawn hand drew it turned (held_tilt), else it is
		// turned here. fist_*: as the fist holds it without the turn, where the caught one sits.
		const q4 seated = frame.has_root ? multiply(conjugate(frame.root_rotation), frame.magazine_rotation) : q4{};
		if (frame.has_root && !frame.has_held)
		{
			tilt_magazine(held_top, held_rotation, seated, tilt_, config_.palm_pivot_drop);
		}
		v3 fist_top = held_top;
		q4 fist_rotation = held_rotation;
		if (frame.has_root)
		{
			tilt_magazine(fist_top, fist_rotation, seated, -(frame.has_held ? frame.held_tilt : tilt_), config_.palm_pivot_drop);
		}
		const bool held_at_magwell = valid && distance(held_top, frame.magwell) < config_.reach_magazine;
		const auto start_holding = [&](bool armed)
		{
			held_relative_ = multiply(conjugate(frame.gun_rotation), frame.magazine_rotation);
			insert_armed_ = armed;
		};
		// Grip lock: the rail runs from the magwell down the gun's -y; where the held magazine's top is on it.
		const bool lockable = frame.grip_lock && frame.has_root && valid;
		const v3 rail_down = frame.has_root ? rotate(frame.root_rotation, { 0, -1, 0 }) : v3{ 0, -1, 0 };
		const float rail_depth = dot(held_top - frame.magwell, rail_down);
		const v3 rail_point = frame.magwell + rail_down * std::fmax(0.0f, std::fmin(config_.lock_rail, rail_depth));
		const float off_rail = distance(held_top, rail_point);
		const auto start_pull_out = [&]()
		{
			// Taken out of the gun: it slides out along the rail with the locked hand.
			pulling_out_ = guided_ = lockable && frame.grip;
		};
		if (!lockable || !gun.state().holding)
		{
			guided_ = pulling_out_ = false;
		}
		if (!lockable || !frame.grip || !gun.state().magazine_in || distance(reach, frame.magwell) > config_.lock_leave)
		{
			lock_hold_ = false;
		}

		// Palming (AR): a new magazine at the magwell of a loaded gun gets ready; the hand leaving ends it.
		using palm_stage = hand_result::palm_stage;
		if (!gun.state().holding)
		{
			kept_old_ = false;
		}
		if (palm_ == palm_stage::caught && !gun.state().palmed)
		{
			palm_ = palm_stage::none;
		}
		const bool can_palm = frame.palming && !ak && frame.has_root && valid && frame.grip && gun.state().holding && !gun.state().palmed
			&& !kept_old_ && gun.state().magazine_in;
		const float fist_to_magwell = valid ? distance(fist_top, frame.magwell) : 1e9f;
		if (palm_ == palm_stage::ready && (!can_palm || fist_to_magwell > config_.palm_leave))
		{
			palm_ = palm_stage::none;
		}
		if (palm_ == palm_stage::none && can_palm && insert_armed_ && fist_to_magwell < config_.reach_magazine)
		{
			palm_ = palm_stage::ready;
			result.palm_ready_now = true;
		}

		if (button_pressed)
		{
			const reload_event event = gun.press_button(frame.grip && near_magwell, palm_ == palm_stage::ready);
			if (event == reload_event::magazine_palmed)
			{
				palm_ = palm_stage::caught;
				catch_in_gun_ = multiply(conjugate(frame.root_rotation), fist_rotation);
			}
			else if (event == reload_event::magazine_out)
			{
				start_holding(false);
				keep_snap_ = true;
				start_pull_out();
			}
			else if (event == reload_event::magazine_dropped)
			{
				// Released from the gun: it leaves with the gun's motion.
				start_fall(frame.magwell, frame.magazine_rotation, magwell_velocity_, frame);
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
					keep_snap_ = true;
					start_pull_out();
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

		if (gun.state().holding && valid && palm_ == palm_stage::caught)
		{
			// Turned back as it was held at the catch (relative to the gun), the new one seats; the old one
			// stays in the hand. Only the lean about the barrel counts: a wrist rolls about the forearm, which
			// lies off the barrel (monitor 2026-11-05: 40 degrees of roll left 26 of the full angle).
			const float lean = palm_lean_degrees(multiply(conjugate(frame.root_rotation), held_rotation), catch_in_gun_);
			result.palm_misalign_degrees = lean;
			result.palm_off_degrees = angle_between(multiply(conjugate(frame.root_rotation), held_rotation), catch_in_gun_);
			result.palm_gap = held_top - frame.magwell;
			const bool lined_up = lean < config_.palm_align_degrees && result.palm_off_degrees < config_.palm_max_degrees;
			if (frame.grip && distance(held_top, frame.magwell) < config_.palm_seat_reach && lined_up)
			{
				const reload_event event = gun.insert();
				if (event == reload_event::magazine_in)
				{
					seating_ = true;
					seat_time_ = 0.0f;
					seat_from_ = held_top;
					seat_rotation_ = held_rotation;
					palm_ = palm_stage::none;
					kept_old_ = true;
					insert_armed_ = false;
				}
				emit(result, event);
			}
		}
		else if (gun.state().holding && valid)
		{
			if (distance(held_top, frame.magwell) > config_.rearm_distance)
			{
				insert_armed_ = true;
			}
			seat_pause_ = std::fmax(0.0f, seat_pause_ - frame.seconds);
			bool push_home = frame.grip && insert_armed_ && held_at_magwell;
			if (lockable)
			{
				// Locks on near the rail when it is turned roughly as it sits; leaves off the rail, past its end
				// or let go. Locked, it seats once pushed up to lock_seat_depth.
				// A loaded AR's magwell is the palming's (tactical reload); an AK's new magazine knocks the old one out.
				const float align = angle_between(held_rotation, frame.magazine_rotation);
				const bool aligned = align < config_.lock_align_degrees;
				result.rail_probe_depth = rail_depth;
				result.rail_probe_off = off_rail;
				result.rail_probe_degrees = align;
				const bool well_free = !gun.state().magazine_in || gun.profile().flick;
				result.rail_probe_flags = (frame.grip ? 1 : 0) | (insert_armed_ ? 2 : 0) | (well_free ? 4 : 0) | (aligned ? 8 : 0);
				if (!guided_ && frame.grip && insert_armed_ && aligned && well_free && palm_ == palm_stage::none && rail_depth > -config_.lock_seat_depth && rail_depth < config_.lock_rail
					&& off_rail < config_.lock_catch)
				{
					guided_ = true;
				}
				if (guided_ && (!frame.grip || off_rail > config_.lock_leave || rail_depth > config_.lock_rail + config_.lock_seat_depth))
				{
					guided_ = pulling_out_ = false;
					result.rail_probe_flags |= 32;
				}
				result.rail_probe_flags |= guided_ ? 16 : 0;
				push_home = guided_ && insert_armed_ && rail_depth <= config_.lock_seat_depth;
			}
			if (push_home && seat_pause_ <= 0.0f)
			{
				const reload_event event = gun.insert();
				if (event == reload_event::flicked_out)
				{
					// #47, two steps: the old magazine flies first; the new one, still in the hand, seats a
					// moment later if the hand keeps it at the magwell.
					seat_pause_ = config_.flick_seat_delay;
					// AK quick reload: the new magazine knocks the old one out of the magwell, downward out of the
					// gun, carrying the sideways part of the hand's swing (the push itself goes up into the well).
					const v3 down = frame.has_root ? rotate(frame.root_rotation, { 0, -1, 0 }) : v3{ 0, -1, 0 };
					const v3 swing{ palm_velocity_.x * 0.3f, 0.0f, palm_velocity_.z * 0.3f };
					start_fall(frame.magwell, frame.magazine_rotation, magwell_velocity_ + down * config_.flick_speed + swing, frame);
				}
				else if (event == reload_event::magazine_in)
				{
					seating_ = true;
					seat_time_ = 0.0f;
					seat_from_ = guided_ ? rail_point : held_top;
					seat_rotation_ = guided_ ? frame.magazine_rotation : held_rotation;
					lock_hold_ = guided_;
					guided_ = pulling_out_ = false;
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
				const reload_event event = gun.let_go(near_stow);
				if (event == reload_event::magazine_dropped)
				{
					// Let go or tossed: it keeps the hand's velocity.
					start_fall(held_top, held_rotation, palm_velocity_, frame);
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
		result.zone_reach[2] = chest_reach;
		result.has_chest_hip = frame.has_chest_hip;
		result.stow_reach = stow_reach;
		result.stow_zone = gun.state().holding && near_stow;
		result.chest_hip = frame.chest_hip;
		if (!gun.state().holding && !racking_ && !frame.grip)
		{
			result.in_zone = near_handle ? hand_result::zone::handle : near_magwell ? hand_result::zone::magazine
				: near_chest ? hand_result::zone::chest : hand_result::zone::none;
		}
		// #40: the open hand closing on the seated magazine snaps onto it: from the zone's edge (0) to
		// magazine_snap_full from its grab point (1), smoothstepped. A magazine just pulled out keeps the
		// snapped hold by the same distance, so it slides out with the hand instead of jumping to the
		// controller; once the hand has left the zone the hold is the controller's.
		// Squeezed or not: an AR's magazine waits in the closed hand for the release button.
		const bool snap_zone = near_magwell && !near_handle && !gun.state().holding && !racking_ && gun.state().magazine_in;
		const bool pulling = keep_snap_ && gun.state().holding && palm_ == palm_stage::none;
		keep_snap_ = pulling && to_magwell < config_.reach_magazine;
		if ((snap_zone || keep_snap_) && config_.reach_magazine > config_.magazine_snap_full)
		{
			const float t = std::fmax(0.0f, std::fmin(1.0f, (config_.reach_magazine - to_magwell) / (config_.reach_magazine - config_.magazine_snap_full)));
			result.magazine_snap = t * t * (3.0f - 2.0f * t);
		}

		const float tilt_target = palm_ == palm_stage::none ? 0.0f : config_.palm_tilt_degrees * 0.017453293f;
		tilt_ += (tilt_target - tilt_) * (config_.palm_tilt_seconds > 0.0f ? 1.0f - std::exp(-frame.seconds / config_.palm_tilt_seconds) : 1.0f);
		if (std::fabs(tilt_ - tilt_target) < 1e-3f)
		{
			tilt_ = tilt_target;
		}
		result.palm = palm_;
		result.magazine_tilt = tilt_;

		if (gun.state().holding && valid && !kept_old_ && guided_)
		{
			// Grip lock: upright on the rail, the hand on it.
			result.draw = magazine_draw::in_hand;
			result.magazine_position = rail_point;
			result.magazine_rotation = frame.magazine_rotation;
			result.guided = true;
			result.rail_depth = std::fmax(0.0f, std::fmin(config_.lock_rail, rail_depth));
			result.hand_lock = 1.0f;
			result.lock_position = rail_point;
			result.lock_rotation = frame.magazine_rotation;
		}
		else if (gun.state().holding && valid && !kept_old_)
		{
			result.draw = magazine_draw::in_hand;
			result.in_fist = true;
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
			if (lock_hold_ && valid)
			{
				// The locked hand pushes it home and stays on it while the grip is held.
				result.hand_lock = 1.0f;
				result.lock_position = result.draw == magazine_draw::in_hand ? result.magazine_position : frame.magwell;
				result.lock_rotation = result.draw == magazine_draw::in_hand ? result.magazine_rotation : frame.magazine_rotation;
			}
		}
		else
		{
			// Out of the gun and not in a tracked hand.
			result.draw = magazine_draw::hidden;
			result.magazine_position = frame.magwell + v3{ 0, -50.0f, 0 };
			result.magazine_rotation = frame.magazine_rotation;
		}
		bool falling = false;
		v3 fall_position;
		q4 fall_rotation;
		if (loose_on_ && frame.has_world && frame.ground)
		{
			// #61: on the level's ground; the STAGE floor (or the soldier's) under ground not known yet.
			const q4 to_local = normalize(frame.world_to_local);
			const auto local_of = [&](v3 world) { return rotate(to_local, world - frame.world_origin); };
			const v3 local = local_of(loose_.centre());
			float fallback = NAN;
			if (frame.floor_valid)
			{
				fallback = (frame.world_origin + rotate(conjugate(to_local), { local.x, frame.floor_y, local.z })).y;
			}
			bool seen = false;
			if (frame.has_view)
			{
				const v3 to = local - frame.view_position;
				const float far = std::sqrt(dot(to, to));
				const float forward = std::sqrt(dot(frame.view_forward, frame.view_forward));
				seen = far < 0.3f || (far < 40.0f && forward > 1e-4f && dot(to, frame.view_forward) / (far * forward) > std::cos(config_.seen_degrees * 0.017453293f));
			}
			loose_on_ = loose_.update(frame.seconds, *frame.ground, fallback, seen);
			falling = loose_on_;
			fall_position = local_of(loose_.pose_position());
			fall_rotation = multiply(to_local, loose_.pose_rotation());
			result.loose_resting = loose_on_ && loose_.resting();
		}
		else if (loose_on_)
		{
			// No world this frame (a weapon switch): it waits, not drawn.
		}
		else
		{
			falling = flight_.active() && flight_.update(frame.seconds);
			fall_position = flight_.position();
			fall_rotation = flight_.rotation();
		}
		result.loose = falling;
		if (falling && result.draw == magazine_draw::hidden)
		{
			// The rig's one magazine mesh, when nothing else needs it.
			result.draw = magazine_draw::falling;
			result.magazine_position = fall_position;
			result.magazine_rotation = fall_rotation;
		}
		// The second magazine (a copy of the game's draw where available): with one in the gun and one in the
		// hand, whichever the rig's mesh does not show; the caught one while palming; one falling while the
		// rig's mesh is busy elsewhere.
		if (gun.state().holding && gun.state().magazine_in && result.draw == magazine_draw::in_hand)
		{
			result.extra = hand_result::extra_magazine::seated;
		}
		else if (gun.state().holding && gun.state().magazine_in && result.draw == magazine_draw::with_gun)
		{
			result.extra = hand_result::extra_magazine::in_fist;
		}
		else if (gun.state().palmed)
		{
			result.extra = hand_result::extra_magazine::in_fist;
		}
		else if (falling && result.draw != magazine_draw::falling)
		{
			result.extra = hand_result::extra_magazine::falling;
			result.extra_position = fall_position;
			result.extra_rotation = fall_rotation;
		}
		return result;
	}
}
