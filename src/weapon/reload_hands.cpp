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
		// #95: a held belt or lid, or the hand at them, keeps the grip: with the gun in the off hand the gun hand reloads,
		// and the take-back opened the grip of a belt carried near the pistol grip (owner's marks 67-68: hand=1, eight
		// belt_taken belt_dropped pairs).
		if (!(to_grip < regrip_reach) || last.belt_zone || last.lid_held || last.belt_draw == 2)
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

	bool needs_rack(const gun_state& state)
	{
		return !state.chamber || state.bolt_locked;
	}

	float rack_side(v3 handle_in_gun, bool ak)
	{
		return handle_in_gun.x > 0.005f ? 1.0f : handle_in_gun.x < -0.005f ? -1.0f : ak ? -1.0f : 0.0f;
	}

	rack_zone_test rack_zone_of(v3 palm, v3 knob, q4 root_rotation, float side, const hand_config& config)
	{
		rack_zone_test test;
		const v3 over = config.rack_over_offset;
		const v3 under = config.rack_under_offset;
		test.over_centre = knob + rotate(root_rotation, { side * over.x, over.y, over.z });
		test.under_centre = knob + rotate(root_rotation, { side * under.x, under.y, under.z });
		test.over_distance = distance(palm, test.over_centre);
		const v3 local = rotate(conjugate(root_rotation), palm - test.under_centre);
		const v3 radii = config.rack_under_radii;
		if (radii.x > 0.0f && radii.y > 0.0f && radii.z > 0.0f)
		{
			const v3 scaled{ local.x / radii.x, local.y / radii.y, local.z / radii.z };
			test.under_scaled = std::sqrt(dot(scaled, scaled));
		}
		const bool in_over = test.over_distance < config.rack_over_reach;
		const bool in_under = test.under_scaled < 1.0f;
		const float height = rotate(conjugate(root_rotation), palm - knob).y;
		test.zone = in_under && (!in_over || height < -config.rack_under_below) ? rack_zone::under : in_over ? rack_zone::over : rack_zone::none;
		return test;
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

	hand_result reload_hands::update(reload& gun, const hand_frame& frame_in)
	{
		// Palming lock (owner 2026-11-16): while a new magazine is ready to palm, everything below sees the off hand where it
		// was on the gun when it got ready; only the real controller leaving that place lets go.
		hand_frame frame = frame_in;
		if (palm_lock_on_ && (palm_ != hand_result::palm_stage::ready || !frame_in.has_root || !frame_in.hands_valid || !frame_in.grip))
		{
			palm_lock_on_ = false;
		}
		if (palm_lock_on_)
		{
			const v3 real = frame_in.has_palm ? frame_in.off_palm : frame_in.off_hand;
			const v3 locked_palm = frame_in.root + rotate(frame_in.root_rotation, palm_lock_palm_);
			if (distance(real, locked_palm) > config_.palm_lock_leave)
			{
				palm_lock_on_ = false;
			}
			else
			{
				frame.off_hand = frame_in.root + rotate(frame_in.root_rotation, palm_lock_hand_);
				frame.off_palm = locked_palm;
				frame.off_rotation = multiply(frame_in.root_rotation, palm_lock_rotation_);
			}
		}
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
		const float way_out = frame.has_way_out ? frame.way_out_degrees * 0.017453293f : 0.0f;
		const v3 out_in_gun{ 0.0f, -std::cos(way_out), std::sin(way_out) };
		const v3 grab_offset = (frame.has_magazine_grab ? frame.magazine_grab : out_in_gun * config_.magazine_grab_drop) + frame.magazine_shift;
		const float magazine_reach = config_.reach_magazine * frame.magazine_size; // the grab zone's (a held magazine seats by config_'s)
		const v3 magazine_body = frame.has_root ? frame.magwell + rotate(frame.root_rotation, grab_offset) : frame.magwell;
		// Ahead of the grab point along the barrel (not the magwell: the AK-12's own hand takes it 3.9 cm ahead).
		const bool on_handguard = frame.has_root && dot(reach - magazine_body, rotate(frame.root_rotation, { 0, 0, 1 })) > config_.magazine_ahead_limit;
		const float to_magwell = valid && !on_handguard ? distance(reach, magazine_body) : 1e9f;
		const float to_chest = !valid ? 1e9f : frame.has_chest_hip ? segment_distance(reach, frame.chest, frame.chest_hip) : distance(reach, frame.chest);
		const float chest_reach = frame.chest_reach > 0 ? frame.chest_reach : config_.reach_chest;
		// The charging handle: from the gun frame per variant, else the slide bone. With the gun frame it
		// has two zones (rack_zone_of): overhand beside and above the knob, underhand a larger one below.
		v3 handle = frame.slide;
		float handle_reach = config_.reach_slide * frame.handle_size;
		const v3 handle_in_gun = (frame.has_handle_point ? frame.handle_point : ak ? config_.handle_ak : config_.handle_ar) + frame.handle_shift;
		v3 handle_grab = handle;
		rack_zone_test rack_test;
		hand_config zones = config_;
		if (frame.has_rack_shape)
		{
			zones.rack_over_offset = frame.rack_shape.over_offset;
			zones.rack_over_reach = frame.rack_shape.over_reach;
			zones.rack_under_offset = frame.rack_shape.under_offset;
			zones.rack_under_radii = frame.rack_shape.under_radii;
		}
		zones.rack_over_reach *= frame.handle_size;
		zones.rack_under_radii = zones.rack_under_radii * frame.handle_size;
		if (frame.has_root)
		{
			handle = frame.root + rotate(frame.root_rotation, handle_in_gun);
			rack_test = rack_zone_of(reach, handle, frame.root_rotation, rack_side(handle_in_gun, ak), zones);
			handle_reach = zones.rack_over_reach;
			handle_grab = rack_test.over_centre;
			// The large underhand zone only while the gun needs charging: below the handle is the handguard on
			// guns like the SCAR-H, whose two-handed grab it took (monitor 2026-11-15).
			if (rack_test.zone == rack_zone::under && !needs_rack(gun.state()))
			{
				rack_test.zone = rack_zone::none;
			}
		}
		else if (frame.has_slide)
		{
			rack_test.over_distance = distance(reach, handle);
			rack_test.zone = rack_test.over_distance < handle_reach ? rack_zone::over : rack_zone::none;
		}
		if (!valid || frame.pump || frame.revolver) // a revolver has no handle to rack
		{
			rack_test.zone = rack_zone::none; // a pump's fore-end is the two-handed hold's; the held hand pumps it (below)
		}
		// Back along the barrel: -z of the gun frame, else +z of the aim rotation.
		const v3 back = frame.has_root ? rotate(frame.root_rotation, { 0, 0, -1 }) : rotate(frame.gun_rotation, { 0, 0, 1 });
		const bool in_rack_zone = rack_test.zone != rack_zone::none;
		// Zones overlap: the magazine beats the underhand zone (the large fallback below the handle);
		// against the overhand zone the nearer one wins.
		const bool near_magwell = !frame.handle_only && to_magwell < magazine_reach && !((frame.tube || frame.revolver) && !gun.state().holding)
			&& !(rack_test.zone == rack_zone::over && rack_test.over_distance < to_magwell);
		const bool near_handle = in_rack_zone && !near_magwell;
		const bool near_chest = !frame.handle_only && to_chest < chest_reach;
		const float stow_reach = chest_reach * config_.stow_reach_scale;
		const bool near_stow = !frame.handle_only && to_chest < stow_reach;
		// A held magazine turns with the off hand; its top sits where the hand holds it.
		q4 held_rotation = frame.has_held ? frame.held_rotation : multiply(frame.off_rotation, held_relative_);
		v3 held_top = frame.has_held ? frame.held_top
			: frame.off_hand + rotate(frame.off_rotation, frame.tube ? config_.hold_shell : ak ? config_.hold_ak : config_.hold_ar);
		// Palming turns the held magazine aside: the drawn hand drew it turned (held_tilt), else it is
		// turned here. fist_*: as the fist holds it without the turn, where the caught one sits.
		const q4 seated = frame.has_root ? multiply(conjugate(frame.root_rotation), frame.magazine_rotation) : q4{};
		if (guided_ && has_guide_hold_ && !frame.has_held)
		{
			held_top = frame.off_hand + rotate(frame.off_rotation, guide_offset_);
			held_rotation = multiply(frame.off_rotation, guide_rotation_);
		}
		else if (frame.has_root && !frame.has_held)
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
			flick_rearm_ = flick_second_ = false; // a new magazine in the hand: no flick pending
		};
		// Grip lock: the rail runs from the magwell down the gun's -y; where the held magazine's top is on it.
		const bool lockable = frame.grip_lock && frame.has_root && valid && !frame.tube && !frame.revolver;
		const v3 rail_down = frame.has_root ? rotate(frame.root_rotation, out_in_gun) : v3{ 0, -1, 0 };
		const float rail_depth = dot(held_top - frame.magwell, rail_down);
		const v3 rail_point = frame.magwell + rail_down * std::fmax(0.0f, std::fmin(config_.lock_rail, rail_depth));
		// Sideways from the rail's line: pushed on past the magwell it is still on the rail.
		const float off_rail = distance(held_top, frame.magwell + rail_down * std::fmin(config_.lock_rail, rail_depth));
		const auto keep_hold = [&](v3 top, q4 rotation)
		{
			const q4 hand_inverse = conjugate(frame.off_rotation);
			guide_offset_ = rotate(hand_inverse, top - frame.off_hand);
			guide_rotation_ = multiply(hand_inverse, rotation);
			has_guide_hold_ = true;
		};
		const auto start_pull_out = [&]()
		{
			// Taken out of the gun: it slides out along the rail with the locked hand, held where it sat.
			from_gun_ = true;
			from_gun_offset_ = rotate(conjugate(frame.off_rotation), frame.magwell - frame.off_hand);
			pulling_out_ = guided_ = lockable && frame.grip;
			if (guided_)
			{
				keep_hold(frame.magwell, frame.magazine_rotation);
			}
		};
		if (!lockable || !gun.state().holding)
		{
			guided_ = pulling_out_ = false;
		}
		if (!gun.state().holding || !valid)
		{
			from_gun_ = false;
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
		// #98: no palming where there is no button to catch the old magazine with (the P90, the AK 5C): it would wait for a B
		// that does nothing, and a loaded well never takes the new one.
		const bool can_palm = frame.palming && !ak && !gun.pulled_by_hand() && !frame.tube && !frame.revolver && frame.has_root && valid && frame.grip && gun.state().holding && !gun.state().palmed
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
			if (frame.grip_lock)
			{
				const q4 to_gun = conjugate(frame.root_rotation);
				palm_lock_on_ = true;
				palm_lock_hand_ = rotate(to_gun, frame.off_hand - frame.root);
				palm_lock_palm_ = rotate(to_gun, (frame.has_palm ? frame.off_palm : frame.off_hand) - frame.root);
				palm_lock_rotation_ = multiply(to_gun, frame.off_rotation);
			}
		}
		if (palm_ != palm_stage::ready)
		{
			palm_lock_on_ = false;
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
				// Released from the gun: pushed out of the well along its way out, with the gun's motion
				// (a flick throws it further; headset 2026-11-14: it dropped straight down).
				start_fall(frame.magwell, frame.magazine_rotation, magwell_velocity_ + rail_down * config_.eject_speed, frame);
			}
			emit(result, event);
		}

		// Belt-fed: the lid and the belt, before the box, the handle and the pouch.
		bool belt_feed_grab = false;
		const bool belt_points = (frame.belt || frame.revolver) && frame.has_belt_points && frame.has_root; // the lid (or frame) is known
		const q4 to_gun = conjugate(frame.root_rotation);
		const auto hinge_angle = [&](v3 p)
		{
			const v3 d = rotate(to_gun, p - frame.lid_hinge);
			return std::atan2(d.z, d.y); // grows as the lid turns up (about +x)
		};
		const float lid_full = frame.lid_degrees * 0.017453293f;
		const v3 lid_grab = frame.lid_grab + rotate(frame.root_rotation, frame.lid_shift); // tuned per gun (zone_tuning)
		const float lid_reach = config_.lid_reach * frame.lid_size;
		result.has_lid_zone = belt_points;
		result.lid_centre = lid_grab;
		result.lid_reach = lid_reach;
		const v3 belt_point = gun.state().belt_laid ? frame.feed_tray : frame.belt_end;
		const bool near_belt = belt_points && frame.belt && valid && gun.state().lid_open && gun.state().magazine_in && !gun.state().holding
			&& distance(reach, belt_point) < config_.belt_reach;
		const bool near_lid = belt_points && valid && !gun.state().holding && !gun.state().belt_held && distance(reach, lid_grab) < lid_reach;
		if (belt_points && grip_pressed && valid && !gun.state().holding && !gun.state().belt_held)
		{
			if (near_belt && (!near_lid || distance(reach, belt_point) <= distance(reach, lid_grab)))
			{
				emit(result, gun.take_belt());
				belt_feed_grab = true;
				belt_armed_ = false;
			}
			else if (near_lid)
			{
				lid_held_ = true;
				lid_closed_angle_ = hinge_angle(lid_grab) - lid_ * lid_full;
				lid_hand_offset_ = hinge_angle(reach) - hinge_angle(lid_grab);
				belt_feed_grab = true;
			}
		}
		if (lid_held_ && (!frame.grip || !valid || !belt_points))
		{
			lid_held_ = false;
			// Let go: past halfway it swings up, else shut (the state now; the drawing follows below).
			if (lid_ > 0.5f)
			{
				emit(result, gun.open_lid());
			}
			else
			{
				emit(result, gun.close_lid());
			}
		}
		if (lid_held_)
		{
			float angle = hinge_angle(reach) - lid_hand_offset_ - lid_closed_angle_;
			angle = std::atan2(std::sin(angle), std::cos(angle));
			lid_ = std::fmax(0.0f, std::fmin(1.0f, angle / lid_full));
			if (!gun.state().lid_open && lid_ > 0.1f)
			{
				emit(result, gun.open_lid());
			}
			else if (gun.state().lid_open && lid_ <= 0.02f)
			{
				emit(result, gun.close_lid());
			}
		}
		else if (frame.belt || frame.revolver)
		{
			// The revolver shut by a flick of the gun up (the barrel's pitch rising fast), as top breaks are closed.
			if (frame.revolver && gun.state().lid_open && frame.has_root && valid && frame.seconds > 1e-4f)
			{
				const float pitch = rotate(frame.root_rotation, { 0, 0, 1 }).y;
				if (has_last_pitch_ && (pitch - last_pitch_) / frame.seconds > config_.flick_close_rate && lid_ > 0.9f)
				{
					emit(result, gun.close_lid());
				}
				last_pitch_ = pitch;
				has_last_pitch_ = true;
			}
			else
			{
				has_last_pitch_ = false;
			}
			const float target = gun.state().lid_open ? 1.0f : 0.0f;
			const float step = config_.lid_swing_seconds > 0.0f ? frame.seconds / config_.lid_swing_seconds : 1.0f;
			lid_ = target > lid_ ? std::fmin(target, lid_ + step) : std::fmax(target, lid_ - step);
		}
		if (gun.state().belt_held)
		{
			if (!frame.grip || !valid)
			{
				emit(result, gun.drop_belt());
			}
			else if (belt_points)
			{
				const float to_tray = distance(reach, frame.feed_tray);
				belt_armed_ = belt_armed_ || to_tray > 1.5f * config_.belt_lay;
				if (belt_armed_ && to_tray < config_.belt_lay)
				{
					emit(result, gun.lay_belt());
				}
			}
		}
		if (frame.belt || frame.revolver)
		{
			result.lid = lid_;
			result.lid_held = lid_held_;
			if (lid_held_ && belt_points)
			{
				result.handle_in_gun = rotate(to_gun, lid_grab - frame.root); // the drawn hand held on the lid as it turns
			}
			result.belt_zone = belt_points && (lid_held_ || gun.state().belt_held || near_lid || near_belt);
			result.belt_draw = gun.state().belt_laid ? 0 : gun.state().belt_held ? 2 : 1;
			result.belt_position = reach;
			result.belt_rotation = frame.off_rotation;
		}

		if (grip_pressed && valid && !gun.state().holding && !belt_feed_grab)
		{
			if (near_handle)
			{
				racking_ = true;
				rack_underhand_ = rack_test.zone == rack_zone::under;
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
				else if (lockable && gun.state().magazine_in)
				{
					// Owner 2026-11-16: a grip on the seated magazine (an AR's, which stays in) locks the hand on it like
					// the handguard grab, with the game's grip, until the grip opens or the controller is lock_leave off.
					lock_hold_ = true;
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
			// After a flick only a pull back down out of the well re-arms (along the magwell's down axis): pushing on up
			// past the magwell, as the hand does after knocking the old one out, is no second motion (monitor 2026-11-16).
			const v3 carried = from_gun_ ? frame.off_hand + rotate(frame.off_rotation, from_gun_offset_) : held_top;
			const bool pulled_back = flick_rearm_ ? rail_depth > config_.flick_rearm : distance(carried, frame.magwell) > config_.rearm_distance;
			if (pulled_back)
			{
				insert_armed_ = true;
				from_gun_ = false;
				flick_second_ = flick_rearm_;
				flick_rearm_ = false;
			}
			seat_pause_ = std::fmax(0.0f, seat_pause_ - frame.seconds);
			bool push_home = frame.grip && insert_armed_ && held_at_magwell
				&& (!flick_second_ || rail_depth <= config_.flick_seat_reach); // the second push: back up to (or past) the magwell
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
					keep_hold(held_top, held_rotation);
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
					// The second motion: away from the magwell and in again (not a seat while the hand just stays).
					insert_armed_ = false;
					flick_rearm_ = true;
					// AK quick reload: the new magazine knocks the old one out of the magwell, downward out of the
					// gun along its way out, carrying the sideways part of the hand's swing (the push itself goes up into the well).
					const v3 swing{ palm_velocity_.x * 0.3f, 0.0f, palm_velocity_.z * 0.3f };
					start_fall(frame.magwell, frame.magazine_rotation, magwell_velocity_ + rail_down * config_.flick_speed + swing, frame);
				}
				else if (event == reload_event::magazine_in)
				{
					flick_second_ = flick_rearm_ = false;
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

		// A pump gun: the hand holding the fore-end (the two-handed hold) slides it back and forward. Like a real pump,
		// the action is locked while a round is chambered: it moves after a shot or on an empty chamber, so a hand
		// drifting along the fore-end while aiming never ejects a live shell. Let go at the rear, it cycles too.
		const bool pump_free = frame.pump && frame.has_root && valid && frame.grip && !gun.state().holding && !gun.state().chamber;
		if (pump_free && !pumping_ && distance(reach, handle) < config_.pump_reach * frame.handle_size)
		{
			pumping_ = true;
			pump_back_ = false;
			pump_start_ = reach;
		}
		if (pumping_ && (!pump_free || distance(reach, pump_start_) > config_.pump_reach * frame.handle_size + config_.slide_travel))
		{
			if (pump_back_ && !gun.state().chamber)
			{
				emit(result, gun.rack());
			}
			pumping_ = pump_back_ = false;
		}
		if (pumping_)
		{
			const float along = dot(reach - pump_start_, back);
			if (along < 0.0f)
			{
				pump_start_ = reach; // the hand settled further forward: the stroke starts there
			}
			const float pull = std::fmax(0.0f, std::fmin(config_.slide_travel, along));
			if (!pump_back_ && pull >= config_.rack_pull)
			{
				pump_back_ = true;
				result.rack_at_rear = true;
			}
			else if (pump_back_ && pull <= config_.pump_return)
			{
				emit(result, gun.rack());
				pump_back_ = pumping_ = false;
			}
			result.pumping = true;
			result.rack_pull = pull;
			result.handle_in_gun = handle_in_gun;
			result.rack_side = rack_side(handle_in_gun, ak);
			result.rack_underhand = true; // the fist round the fore-end from below
		}

		// Also before the grip closes: the game must never see the press that starts a gesture.
		result.off_hand_busy = gun.state().holding || racking_ || near_magwell || near_chest || in_rack_zone
			|| lid_held_ || gun.state().belt_held || near_lid || near_belt;
		result.zone_centre[0] = magazine_body;
		result.zone_reach[0] = magazine_reach;
		result.zone_centre[1] = handle_grab;
		result.zone_reach[1] = handle_reach;
		result.has_handle = frame.has_root || frame.has_slide;
		result.has_under_zone = frame.has_root && needs_rack(gun.state());
		result.under_centre = rack_test.under_centre;
		result.under_radii = frame.has_rack_shape ? frame.rack_shape.under_radii : config_.rack_under_radii;
		if (racking_ && valid)
		{
			result.racking = true;
			result.rack_underhand = rack_underhand_;
			result.rack_side = frame.has_root ? rack_side(handle_in_gun, ak) : 0.0f;
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
		keep_snap_ = pulling && to_magwell < magazine_reach;
		if ((snap_zone || keep_snap_) && magazine_reach > config_.magazine_snap_full)
		{
			const float t = std::fmax(0.0f, std::fmin(1.0f, (magazine_reach - to_magwell) / (magazine_reach - config_.magazine_snap_full)));
			result.magazine_snap = t * t * (3.0f - 2.0f * t);
		}

		const float tilt_target = palm_ == palm_stage::none ? 0.0f : config_.palm_tilt_degrees * 0.017453293f;
		tilt_ += (tilt_target - tilt_) * (config_.palm_tilt_seconds > 0.0f ? 1.0f - std::exp(-frame.seconds / config_.palm_tilt_seconds) : 1.0f);
		if (std::fabs(tilt_ - tilt_target) < 1e-3f)
		{
			tilt_ = tilt_target;
		}
		result.palm = palm_;
		result.palm_locked = palm_lock_on_;
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
			if (palm_lock_on_)
			{
				// Palming lock: the drawn hand holds the new magazine where it was on the gun.
				result.hand_lock = 1.0f;
				result.lock_position = held_top;
				result.lock_rotation = held_rotation;
			}
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
