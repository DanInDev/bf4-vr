#include "weapon/ladder_climb.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::weapon::ladder
{
	namespace
	{
		v3 add(const v3& a, const v3& b) { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
		v3 sub(const v3& a, const v3& b) { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
		v3 scale(const v3& a, float s) { return { a.x * s, a.y * s, a.z * s }; }
		float dot(const v3& a, const v3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
		float length(const v3& a) { return std::sqrt(dot(a, a)); }

		// A break: the drawn hand cannot stay on a rung the controller has gone this far from (the body could not
		// follow: blocked, or the game moved it).
		constexpr float break_distance = 0.70f;
	}

	v3 to_local(const frame& ladder, const v3& world)
	{
		const v3 d = sub(world, ladder.foot);
		return { dot(d, ladder.right), dot(d, ladder.up), dot(d, ladder.out) };
	}

	v3 to_world(const frame& ladder, const v3& local)
	{
		return add(ladder.foot, add(scale(ladder.right, local.x), add(scale(ladder.up, local.y), scale(ladder.out, local.z))));
	}

	bool nearest_rung(const frame& ladder, const rung_model& rungs, const v3& point, float reach, v3& on_rung, int& index)
	{
		const float rails = ladder.length > 0.0f ? ladder.length : rungs.default_length;
		const int last = static_cast<int>(std::floor((rails - rungs.first) / rungs.spacing));
		if (last < 0 || rungs.spacing <= 0.0f)
		{
			return false;
		}
		const v3 local = to_local(ladder, point);
		const int k = std::clamp(static_cast<int>(std::lround((local.y - rungs.first) / rungs.spacing)), 0, last);
		const v3 rung{ std::clamp(local.x, -rungs.half_width, rungs.half_width), rungs.first + static_cast<float>(k) * rungs.spacing, rungs.plane };
		if (length(sub(local, rung)) > reach)
		{
			return false;
		}
		on_rung = to_world(ladder, rung);
		index = k;
		return true;
	}

	bool top_grab(const frame& ladder, const rung_model& rungs, const v3& point, float reach, v3& hold)
	{
		const float rails = ladder.length > 0.0f ? ladder.length : rungs.default_length;
		const int last = static_cast<int>(std::floor((rails - rungs.first) / rungs.spacing));
		const float top_rung = rungs.first + static_cast<float>(last) * rungs.spacing;
		const v3 local = to_local(ladder, point);
		// The rails' tops and hooks: from the top rung up to the hooks' top, across the rails, from the rungs' plane in
		// over the edge.
		const v3 on{ std::clamp(local.x, -rungs.half_width - 0.04f, rungs.half_width + 0.04f), std::clamp(local.y, top_rung, rails + 0.27f),
			std::clamp(local.z, -0.33f, rungs.plane) };
		if (length(sub(local, on)) > reach)
		{
			return false;
		}
		hold = to_world(ladder, on);
		return true;
	}

	float climb_speed(const stick_curve& curve, float speed, float stick)
	{
		const float share = std::min(1.0f, curve.gain * std::max(0.0f, std::fabs(stick) - curve.dead));
		return std::copysign(speed * share, stick);
	}

	float stick_for(const stick_curve& curve, float speed, float v)
	{
		if (std::fabs(v) < 0.001f || speed <= 0.0f || curve.gain <= 0.0f)
		{
			return 0.0f;
		}
		const float share = std::min(1.0f, std::fabs(v) / speed);
		return std::copysign(std::min(1.0f, curve.dead + share / curve.gain), v);
	}

	void climber::reset()
	{
		held_[0] = held_[1] = false;
		last_grip_[0] = last_grip_[1] = false;
		was_on_ = false;
		settling_ = false;
		mounting_ = 0.0f;
		mount_hand_ = -1;
		mount_ = mount_kind::none;
	}

	void climber::rebase(const climb_in& in)
	{
		body_anchor_ = target_;
		for (int h = 0; h < 2; ++h)
		{
			if (held_[h])
			{
				palm_anchor_[h] = in.hands[h].palm.y;
			}
		}
	}

	climb_out climber::update(const climb_in& in)
	{
		climb_out out;
		bool pressed[2]{};
		for (int h = 0; h < 2; ++h)
		{
			pressed[h] = in.hands[h].grip && !last_grip_[h];
			last_grip_[h] = in.hands[h].grip;
		}
		const auto usable = [&](int h) { return in.hands[h].tracked && !in.hands[h].busy; };

		if (!in.on_ladder)
		{
			if (was_on_)
			{
				// Off the ladder (climbed off at the top, stepped off at the bottom): the hands let go.
				was_on_ = false;
				for (int h = 0; h < 2; ++h)
				{
					if (held_[h])
					{
						held_[h] = false;
						out.events[h] = event::released;
					}
				}
			}
			if (mount_hand_ >= 0)
			{
				mounting_ -= in.dt;
				// Falling, not just stepping onto the hooks (the state flickered to InAir up there): the view dropping
				// from the highest it was since the grab (on the hooks). As early as that: a frame later (monitor, 4 fps)
				// he was 0.6 m out from the wall and fell past the ladder to its foot.
				if (mount_ == mount_kind::step_off)
				{
					mount_y_ = std::max(mount_y_, in.body_y);
				}
				if (mount_ == mount_kind::step_off && in.airborne && in.body_y < mount_y_ - 0.05f)
				{
					// Off the edge: now towards the ladder, and the game catches it (monitor 2026-11-26: state 3 at the
					// top, the camera 1.3 m below where he stood).
					mount_ = mount_kind::catch_it;
					mounting_ = config_.catch_seconds;
				}
				if (mounting_ <= 0.0f || !in.hands[mount_hand_].grip || !usable(mount_hand_))
				{
					out.events[mount_hand_] = event::released;
					mounting_ = 0.0f;
					mount_hand_ = -1;
					mount_ = mount_kind::none;
				}
			}
			else if (in.have_ladder)
			{
				for (int h = 0; h < 2 && mount_hand_ < 0; ++h)
				{
					int index = 0;
					if (!pressed[h] || !usable(h))
					{
						continue;
					}
					const bool on_rung = nearest_rung(in.ladder, config_.rungs, in.hands[h].palm_world, config_.grab_reach, mount_rung_, index);
					// Standing on top, the rails' tops and the hooks are what a hand takes (the top rung is at the feet).
					const float rails = in.ladder.length > 0.0f ? in.ladder.length : config_.rungs.default_length;
					const bool on_top = !on_rung && in.has_feet && in.feet_y > in.ladder.foot.y + rails - config_.top_reach_down - config_.rungs.spacing
						&& top_grab(in.ladder, config_.rungs, in.hands[h].palm_world, config_.grab_reach, mount_rung_);
					if (on_rung || on_top)
					{
						mount_hand_ = h;
						// Standing level with the rung or above it (on the roof at the ladder's top): BF4 takes the ladder
						// from there only falling past it with the stick towards it, so the soldier steps back off the
						// edge first. Below it: he walks on.
						const bool above = on_top || (in.has_feet && in.feet_y > mount_rung_.y - config_.top_reach_down);
						mount_ = above ? mount_kind::step_off : mount_kind::walk_on;
						mount_y_ = in.body_y;
						mounting_ = above ? config_.step_off_seconds : config_.mount_seconds;
						out.events[h] = event::grabbed;
					}
				}
			}
			if (mount_hand_ >= 0)
			{
				// Getting onto the ladder, the hand already on its rung. The stick is in the soldier's frame, who faces
				// the ladder meanwhile (the adapter turns the game's aim into it).
				out.mount = true;
				const v3 in_flat{ -in.ladder.out.x, 0.0f, -in.ladder.out.z };
				const float l = length(in_flat);
				out.mount_direction = l > 1e-4f ? scale(in_flat, 1.0f / l) : v3{};
				out.mount_stick = mount_ == mount_kind::step_off ? -config_.step_off_stick : mount_ == mount_kind::catch_it ? 1.0f : config_.walk_on_stick;
				out.holding[mount_hand_] = true;
				out.rung[mount_hand_] = mount_rung_;
			}
			target_ = in.body_y;
			return out;
		}

		bool changed = false;
		const float previous_body = was_on_ ? last_body_ : in.body_y;
		last_body_ = in.body_y;
		const float sent = was_on_ ? last_v_ : 0.0f; // the climb speed last frame's stick asked for
		const float sent_before = was_on_ ? last_v2_ : 0.0f; // and the frame before's
		last_v2_ = sent;
		last_v_ = 0.0f;
		if (!was_on_)
		{
			was_on_ = true;
			target_ = in.body_y;
			if (mount_hand_ >= 0 && in.hands[mount_hand_].grip && usable(mount_hand_))
			{
				// The reach that walked us on keeps its rung.
				held_[mount_hand_] = true;
				rung_[mount_hand_] = mount_rung_;
				changed = true;
			}
			mounting_ = 0.0f;
			mount_hand_ = -1;
			mount_ = mount_kind::none;
			settling_ = true;
			settle_seconds_ = 0.0f;
			still_seconds_ = 0.0f;
			settle_y_ = in.body_y;
		}
		if (settling_)
		{
			// The game's own mount moves the body for a moment (live: up 0.9 m and back within a second): the hands
			// wait for it, holding on, and start from where it leaves the body.
			settle_seconds_ += in.dt;
			still_seconds_ = std::fabs(in.body_y - settle_y_) < 0.01f ? still_seconds_ + in.dt : 0.0f;
			settle_y_ = in.body_y;
			if (still_seconds_ >= config_.settle_still || settle_seconds_ >= config_.settle_limit)
			{
				settling_ = false;
				// The game moved the view against the held rung (monitor: 0.6 m up): each holding hand closes on the
				// rung nearest its controller now, so the drawn hand is not left far below the real one.
				for (int h = 0; h < 2; ++h)
				{
					if (held_[h])
					{
						v3 on_plane = to_local(in.ladder, in.hands[h].palm_world);
						on_plane.z = config_.rungs.plane;
						int index = 0;
						v3 rung;
						if (nearest_rung(in.ladder, config_.rungs, to_world(in.ladder, on_plane), config_.rungs.spacing, rung, index))
						{
							rung_[h] = rung;
						}
					}
				}
			}
		}
		const bool any_before = held_[0] || held_[1];
		for (int h = 0; h < 2; ++h)
		{
			if (held_[h])
			{
				// Only how far the controller is above or below its rung breaks a hold: across it, the game's own mount
				// pulls the body 0.4 m in to the ladder (the controller then sits beyond the rung, harmlessly).
				const float gap = in.hands[h].palm_world.y - rung_[h].y;
				if (!in.hands[h].grip || !usable(h) || (!settling_ && std::fabs(gap) > break_distance))
				{
					held_[h] = false;
					out.events[h] = event::released;
					changed = true;
				}
			}
			else if (pressed[h] && usable(h))
			{
				int index = 0;
				if (nearest_rung(in.ladder, config_.rungs, in.hands[h].palm_world, config_.grab_reach, rung_[h], index))
				{
					held_[h] = true;
					out.events[h] = event::grabbed;
					changed = true;
				}
			}
		}
		if (!any_before || settling_)
		{
			target_ = in.body_y; // a fresh hold (or the game still placing the body) starts from where the body is
		}
		if (changed || settling_)
		{
			rebase(in);
		}
		int count = 0;
		float pulled = 0.0f;
		for (int h = 0; h < 2; ++h)
		{
			out.holding[h] = held_[h];
			out.rung[h] = rung_[h];
			if (held_[h])
			{
				pulled += in.hands[h].palm.y - palm_anchor_[h];
				++count;
			}
		}
		if (count == 0)
		{
			target_ = in.body_y;
			out.target_y = target_;
			return out;
		}
		if (settling_)
		{
			out.target_y = target_;
			return out;
		}
		if (changed || in.dt <= 0.0f)
		{
			own_move_ = 0.0f;
		}
		else
		{
			// The body moving beyond what the stick asked (or the other way), piling up over a moment: the game's own
			// move (its climb-off at the top lifts the view 1.65 m in half a second while the hands still hold). The
			// target takes that move along instead of pushing against it. Summed with a short memory, so the game's
			// frame of lag at the start and end of a pull (it follows the stick one frame late) cancels out.
			// What the last two frames' sticks asked for both count as asked: the game follows a frame late (at the
			// headless 4 fps a frame is a quarter second of climbing; the first pull lost 28 of its 30 cm on the monitor
			// when only last frame's counted). Moving less than asked, or not at all, is a block, not a move of the
			// game's: the lead limit below handles it, and at the foot the push down it keeps is what steps the soldier
			// off (monitor 2026-11-26: taken as a move, the stick came to rest on the bottom rung and he stayed on).
			const float moved = in.body_y - previous_body;
			const float a = sent * in.dt, b = sent_before * in.dt;
			own_move_ = own_move_ * std::exp(-in.dt / 0.15f) + (moved - std::clamp(moved, std::min({ a, b, 0.0f }), std::max({ a, b, 0.0f })));
			if (std::fabs(own_move_) > config_.own_move)
			{
				body_anchor_ += own_move_;
				own_move_ = 0.0f;
			}
		}
		const float offset = pulled / static_cast<float>(count);
		target_ = body_anchor_ - offset;
		const float lo = in.body_y - config_.lead_limit, hi = in.body_y + config_.lead_limit;
		if (target_ < lo || target_ > hi)
		{
			// The hands ran ahead of a body that cannot follow: the target waits at the limit, so the hands' further
			// travel counts from there instead of piling up a launch.
			target_ = std::clamp(target_, lo, hi);
			body_anchor_ = target_ + offset;
		}
		// At most 0.8 of the gap per frame: at the headless 4 fps the full follow rate (12/s) overshot and rang.
		const float gain = in.dt > 0.0f ? std::min(config_.follow, 0.8f / in.dt) : config_.follow;
		const float v = std::clamp(gain * (target_ - in.body_y), -in.speed, in.speed);
		out.drive = true;
		out.stick_y = stick_for(config_.curve, in.speed, v);
		out.target_y = target_;
		last_v_ = climb_speed(config_.curve, in.speed, out.stick_y);
		return out;
	}
}
