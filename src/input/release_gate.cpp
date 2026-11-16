#include "input/release_gate.hpp"

#include <cstdlib>

namespace bfvr::input
{
	namespace
	{
		bool stick_out(std::int16_t x, std::int16_t y)
		{
			return std::abs(static_cast<int>(x)) > gate_stick_released || std::abs(static_cast<int>(y)) > gate_stick_released;
		}
	}

	gamepad_state release_gate::apply(const gamepad_state& raw, bool controllers_ours)
	{
		if (!controllers_ours)
		{
			if (was_ours_)
			{
				++closures_;
			}
			was_ours_ = false;
			buttons_ = 0xFFFF;
			left_trigger_ = right_trigger_ = left_stick_ = right_stick_ = true;
			gamepad_state neutral{};
			neutral.connected = raw.connected;
			return neutral;
		}
		was_ours_ = true;

		gamepad_state out = raw;
		buttons_ &= raw.buttons; // a closed button opens once it reads released
		out.buttons = static_cast<std::uint16_t>(raw.buttons & ~buttons_);
		if (left_trigger_ && raw.left_trigger <= gate_trigger_released)
		{
			left_trigger_ = false;
		}
		if (right_trigger_ && raw.right_trigger <= gate_trigger_released)
		{
			right_trigger_ = false;
		}
		if (left_stick_ && !stick_out(raw.left_x, raw.left_y))
		{
			left_stick_ = false;
		}
		if (right_stick_ && !stick_out(raw.right_x, raw.right_y))
		{
			right_stick_ = false;
		}
		if (left_trigger_)
		{
			out.left_trigger = 0;
		}
		if (right_trigger_)
		{
			out.right_trigger = 0;
		}
		if (left_stick_)
		{
			out.left_x = out.left_y = 0;
		}
		if (right_stick_)
		{
			out.right_x = out.right_y = 0;
		}
		return out;
	}

	bool release_gate::holding() const
	{
		return buttons_ != 0 || left_trigger_ || right_trigger_ || left_stick_ || right_stick_;
	}

	bool freeze_watch::update(bool tracked, bool changed, float seconds)
	{
		if (!tracked || changed)
		{
			still_ = 0.0f;
			frozen_ = false;
			return false;
		}
		still_ += seconds > 0.0f ? seconds : 0.0f;
		frozen_ = still_ >= after_;
		return frozen_;
	}
}
