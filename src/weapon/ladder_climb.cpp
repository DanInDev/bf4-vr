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
			if (mounting_ > 0.0f)
			{
				mounting_ -= in.dt;
				if (mounting_ <= 0.0f || !in.hands[mount_hand_].grip || !usable(mount_hand_))
				{
					out.events[mount_hand_] = event::released;
					mounting_ = 0.0f;
					mount_hand_ = -1;
				}
			}
			else if (in.have_ladder)
			{
				for (int h = 0; h < 2 && mount_hand_ < 0; ++h)
				{
					int index = 0;
					if (pressed[h] && usable(h) && nearest_rung(in.ladder, config_.rungs, in.hands[h].palm_world, config_.grab_reach, mount_rung_, index))
					{
						mount_hand_ = h;
						mounting_ = config_.mount_seconds;
						out.events[h] = event::grabbed;
					}
				}
			}
			if (mount_hand_ >= 0)
			{
				// Walking onto the ladder, the hand already on its rung.
				out.mount = true;
				const v3 in_flat{ -in.ladder.out.x, 0.0f, -in.ladder.out.z };
				const float l = length(in_flat);
				out.mount_direction = l > 1e-4f ? scale(in_flat, 1.0f / l) : v3{};
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
			// The body moving other than the stick asked, piling up over a moment: the game's own move (its climb-off
			// at the top lifts the view 1.65 m in half a second while the hands still hold) or a block. The target
			// takes that move along instead of pushing against it. Summed with a short memory, so the game's frame of
			// lag at the start and end of a pull (it follows the stick one frame late) cancels out.
			// What the last two frames' sticks asked for both count as asked: the game follows a frame late (at the
			// headless 4 fps a frame is a quarter second of climbing; the first pull lost 28 of its 30 cm on the monitor
			// when only last frame's counted).
			const float moved = in.body_y - previous_body;
			const float a = sent * in.dt, b = sent_before * in.dt;
			own_move_ = own_move_ * std::exp(-in.dt / 0.15f) + (moved - std::clamp(moved, std::min(a, b), std::max(a, b)));
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
