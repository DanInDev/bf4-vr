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
}
