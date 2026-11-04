#include "ui/menu_button.hpp"

#include <algorithm>

namespace bfvr::ui
{
	menu_button_events menu_button::update(bool down, float seconds)
	{
		menu_button_events events;
		if (down)
		{
			held_ = down_ ? held_ + std::max(seconds, 0.0f) : 0.0f;
			if (!down_)
			{
				fired_ = false;
			}
			if (!fired_ && held_ >= hold_)
			{
				events.long_press = true;
				fired_ = true;
			}
		}
		else if (down_)
		{
			events.short_press = !fired_;
		}
		down_ = down;
		return events;
	}

	stick_chord_events stick_chord::update(bool left, bool right, float seconds)
	{
		stick_chord_events events;
		seconds = std::max(seconds, 0.0f);
		const bool down[2] = { left, right };
		for (click& one : clicks_)
		{
			one.tap = std::max(one.tap - seconds, 0.0f);
		}
		if (left && right)
		{
			if (!both_)
			{
				events.started = true;
				fresh_ = !chord_ && !clicks_[0].passing && !clicks_[1].passing;
				held_ = 0.0f;
				fired_ = false;
				for (click& one : clicks_)
				{
					one = { true, false, 0.0f, 0.0f };
				}
			}
			else
			{
				held_ += seconds;
			}
			chord_ = both_ = true;
			if (!fired_ && held_ >= hold_)
			{
				events.fired = true;
				fired_ = true;
			}
			return events;
		}

		both_ = false;
		if (chord_)
		{
			chord_ = left || right;
			events.tapped = !chord_ && fresh_ && !fired_;
			for (int stick = 0; stick < 2; ++stick)
			{
				clicks_[stick].down = down[stick];
			}
			return events;
		}
		for (int stick = 0; stick < 2; ++stick)
		{
			click& one = clicks_[stick];
			if (down[stick])
			{
				one.alone = one.down ? one.alone + seconds : 0.0f;
				one.passing = one.passing || one.alone >= grace_;
			}
			else
			{
				if (one.down && !one.passing)
				{
					one.tap = tap_;
				}
				one.passing = false;
				one.alone = 0.0f;
			}
			one.down = down[stick];
		}
		return events;
	}

	bool stick_chord::to_game(int stick) const
	{
		const click& one = clicks_[stick & 1];
		return one.passing || one.tap > 0.0f;
	}

	bool level_hint::update(bool level_running, float seconds)
	{
		seconds = std::max(seconds, 0.0f);
		if (!level_running)
		{
			running_ = false;
			away_ += seconds;
			return false;
		}
		if (!running_ && away_ >= rearm_)
		{
			left_ = show_;
		}
		running_ = true;
		away_ = 0.0f;
		const bool shown = left_ > 0.0f;
		left_ = std::max(left_ - seconds, 0.0f);
		return shown;
	}
}
