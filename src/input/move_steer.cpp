#include "input/move_steer.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::input
{
	namespace
	{
		constexpr float pi = 3.14159265f;
	}

	float wrap_angle(float radians)
	{
		if (!std::isfinite(radians))
		{
			return 0.0f;
		}
		radians = std::fmod(radians + pi, 2.0f * pi);
		if (radians < 0.0f)
		{
			radians += 2.0f * pi;
		}
		return radians - pi;
	}

	steer_output move_steer::update(const steer_input& in)
	{
		const float magnitude = std::hypot(in.x, in.y);
		const float push_angle = magnitude > 1e-4f ? std::atan2(-in.x, in.y) : 0.0f; // left of the head, positive
		const bool forward = magnitude >= config_.push && std::fabs(push_angle) <= config_.cone;
		const bool click = in.sprint_click && !click_was_;
		const bool jump = in.jump && !jump_was_;
		click_was_ = in.sprint_click;
		jump_was_ = in.jump;

		if (click && forward)
		{
			sprinting_ = true;
		}
		if (sprinting_ && (magnitude < config_.release || std::fabs(push_angle) > config_.leave || in.fire))
		{
			sprinting_ = false;
		}
		vault_left_ = std::max(0.0f, vault_left_ - std::max(in.seconds, 0.0f));
		if (jump && forward)
		{
			vault_left_ = config_.vault_seconds;
		}
		if (magnitude < config_.release)
		{
			vault_left_ = 0.0f;
		}

		steer_output out;
		out.sprinting = sprinting_;
		out.steer = sprinting_ || vault_left_ > 0.0f;
		out.yaw = wrap_angle(in.head_yaw + push_angle);
		// The push is meant relative to the head; the game reads it relative to the soldier.
		const float turn = wrap_angle(in.head_yaw - in.soldier_yaw);
		const float c = std::cos(turn), s = std::sin(turn);
		out.x = std::clamp(in.x * c - in.y * s, -1.0f, 1.0f);
		out.y = std::clamp(in.y * c + in.x * s, -1.0f, 1.0f);
		return out;
	}

	void move_steer::reset()
	{
		sprinting_ = false;
		click_was_ = false;
		jump_was_ = false;
		vault_left_ = 0.0f;
	}
}
