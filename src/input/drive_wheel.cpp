#include "input/drive_wheel.hpp"

#include <algorithm>
#include <cmath>

#include "input/move_steer.hpp"

namespace bfvr::input
{
	namespace
	{
		float trigger(float value, float dead)
		{
			value = std::clamp(std::isfinite(value) ? value : 0.0f, 0.0f, 1.0f);
			return value <= dead ? 0.0f : (value - dead) / (1.0f - dead);
		}
	}

	wheel_output drive_wheel::update(const wheel_input& in)
	{
		const float seconds = std::clamp(std::isfinite(in.seconds) ? in.seconds : 0.0f, 0.0f, 0.1f);
		const bool held[2] = { in.hands[0].held, in.hands[1].held };
		const bool pair = held[0] && held[1];
		float turn = 0.0f;
		if (pair)
		{
			// Both hands: the line from the left hand to the right one turns with the wheel.
			const float line = std::atan2(in.hands[1].y - in.hands[0].y, in.hands[1].x - in.hands[0].x);
			if (pair_was_)
			{
				turn = wrap_angle(line - pair_angle_);
			}
			pair_angle_ = line;
		}
		for (int hand = 0; hand < 2; ++hand)
		{
			const float radius = std::hypot(in.hands[hand].x, in.hands[hand].y);
			const bool usable = held[hand] && radius >= config_.min_radius;
			const float around = usable ? std::atan2(in.hands[hand].y, in.hands[hand].x) : 0.0f;
			if (usable && was_held_[hand] && !pair)
			{
				turn = wrap_angle(around - hand_angle_[hand]);
			}
			hand_angle_[hand] = around;
			was_held_[hand] = usable;
		}
		pair_was_ = pair;

		wheel_output out;
		out.hands = static_cast<int>(held[0]) + static_cast<int>(held[1]);
		if (out.hands)
		{
			angle_ = std::clamp(angle_ + turn, -config_.lock, config_.lock);
		}
		else
		{
			const float back = std::min(std::fabs(angle_), config_.return_rate * seconds);
			angle_ -= angle_ > 0.0f ? back : -back;
		}
		out.angle = angle_;
		out.steer = std::clamp(-angle_ / config_.lock, -1.0f, 1.0f);
		// A free wheel leaves the steering to the left stick (once it is back near straight).
		if (!out.hands && std::fabs(in.stick_x) > config_.stick_dead && std::fabs(out.steer) < config_.stick_dead)
		{
			out.steer = std::clamp(in.stick_x, -1.0f, 1.0f);
		}
		const float throttle = trigger(in.throttle, config_.trigger_dead);
		const float brake = trigger(in.brake, config_.trigger_dead);
		out.drive = std::clamp(throttle - brake, -1.0f, 1.0f);
		if (throttle == 0.0f && brake == 0.0f && std::fabs(in.stick_y) > config_.stick_dead)
		{
			out.drive = std::clamp(in.stick_y, -1.0f, 1.0f);
		}
		return out;
	}

	void drive_wheel::reset()
	{
		angle_ = 0.0f;
		was_held_[0] = was_held_[1] = false;
		pair_was_ = false;
	}
}
