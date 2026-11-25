#include "stereo/stereo_hold.hpp"

namespace bfvr::stereo
{
	const char* mono_reason_name(mono_reason reason)
	{
		switch (reason)
		{
		case mono_reason::none: return "stereo";
		case mono_reason::off: return "off";
		case mono_reason::menu: return "menu";
		case mono_reason::scene_screen: return "scene";
		case mono_reason::no_head: return "no_head";
		}
		return "?";
	}

	void stereo_hold::on_stereo()
	{
		holding_ = false;
		dropped_now_ = false;
	}

	bool stereo_hold::keep_on_mono(mono_reason reason, std::uint64_t now_ms)
	{
		if (reason == mono_reason::no_head && !dropped_now_)
		{
			if (!holding_)
			{
				holding_ = true;
				since_ms_ = now_ms;
			}
			if (now_ms - since_ms_ < hold_ms_)
			{
				++held_frames_;
				return true;
			}
		}
		// Counted once per fall, not per mono frame.
		if (!dropped_now_)
		{
			++dropped_[static_cast<int>(reason)];
			dropped_now_ = true;
		}
		holding_ = false;
		return false;
	}
}
