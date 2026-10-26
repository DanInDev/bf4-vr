#include "input/right_stick.hpp"

#include <algorithm>
#include <cmath>

namespace bfvr::input
{
	right_stick_output right_stick::update(float x, float y, float seconds)
	{
		right_stick_output output;
		seconds = std::max(seconds, 0.0f);
		cooldown_ = std::max(cooldown_ - seconds, 0.0f);
		x = std::clamp(x, -1.0f, 1.0f);
		y = std::clamp(y, -1.0f, 1.0f);

		if (std::hypot(x, y) < config_.deadzone)
		{
			owner_ = axis::none;
			snap_armed_ = stance_armed_ = true;
			return output;
		}

		const float ax = std::fabs(x);
		const float ay = std::fabs(y);
		if (owner_ == axis::none)
		{
			if (ax >= config_.axis_bias * ay)
			{
				owner_ = axis::horizontal;
			}
			else if (ay >= config_.axis_bias * ax)
			{
				owner_ = axis::vertical;
			}
			else
			{
				return output;
			}
		}

		if (owner_ == axis::horizontal)
		{
			const float direction = x < 0 ? -1.0f : 1.0f;
			switch (config_.turn)
			{
			case turn_mode::off:
				break;
			case turn_mode::snap:
				if (ax < config_.snap_release)
				{
					snap_armed_ = true;
				}
				else if (snap_armed_ && ax >= config_.snap_press && cooldown_ <= 0)
				{
					output.turn = direction * config_.snap_angle;
					snap_armed_ = false;
					cooldown_ = config_.snap_cooldown;
				}
				break;
			case turn_mode::smooth:
			{
				const float deflection = std::clamp((ax - config_.deadzone) / (1.0f - config_.deadzone), 0.0f, 1.0f);
				output.turn = direction * deflection * config_.smooth_speed * seconds;
				break;
			}
			}
		}
		else if (config_.stance_flicks)
		{
			if (ay < config_.stance_release)
			{
				stance_armed_ = true;
			}
			else if (stance_armed_ && ay >= config_.stance_press)
			{
				output.stance = y > 0 ? stance_command::up : stance_command::down;
				stance_armed_ = false;
			}
		}
		return output;
	}

	void right_stick::reset()
	{
		owner_ = axis::none;
		snap_armed_ = stance_armed_ = true;
		cooldown_ = 0;
	}

	stance next_stance(stance current, stance_command command)
	{
		switch (command)
		{
		case stance_command::up:
			return current == stance::prone ? stance::crouch : stance::stand;
		case stance_command::down:
			return current == stance::stand ? stance::crouch : stance::prone;
		case stance_command::none:
			break;
		}
		return current;
	}
}
