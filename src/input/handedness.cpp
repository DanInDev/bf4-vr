#include "input/handedness.hpp"

#include <atomic>
#include <utility>

namespace bfvr::input
{
	namespace
	{
		std::atomic<bool> left{};

		// XINPUT_GAMEPAD_* pairs that trade places (Xinput.h stays out of the offline tests).
		constexpr std::uint16_t pairs[][2] = {
			{ 0x0040, 0x0080 }, // LEFT_THUMB, RIGHT_THUMB
			{ 0x0100, 0x0200 }, // LEFT_SHOULDER, RIGHT_SHOULDER
			{ 0x1000, 0x4000 }, // A, X
			{ 0x2000, 0x8000 }, // B, Y
		};
	}

	void set_left_handed(bool enabled)
	{
		left = enabled;
	}

	bool left_handed()
	{
		return left.load(std::memory_order_relaxed);
	}

	gamepad_state mirrored(const gamepad_state& pad)
	{
		gamepad_state out = pad;
		std::swap(out.left_x, out.right_x);
		std::swap(out.left_y, out.right_y);
		std::swap(out.left_trigger, out.right_trigger);
		out.buttons = pad.buttons;
		for (const auto& pair : pairs)
		{
			out.buttons &= static_cast<std::uint16_t>(~(pair[0] | pair[1]));
			if (pad.buttons & pair[0]) out.buttons |= pair[1];
			if (pad.buttons & pair[1]) out.buttons |= pair[0];
		}
		return out;
	}

	stereo::hand_sample swapped(const stereo::hand_sample& hands)
	{
		stereo::hand_sample out;
		for (int hand = 0; hand < 2; ++hand)
		{
			out.valid[hand] = hands.valid[1 - hand];
			out.grip[hand] = hands.grip[1 - hand];
			out.aim[hand] = hands.aim[1 - hand];
		}
		return out;
	}
}
