// Checks the left-handed role swap (src/input/handedness): mirrored pad and swapped hand poses.
#include <cstdio>

#include "input/handedness.hpp"

using namespace bfvr::input;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}
}

int main()
{
	gamepad_state pad;
	pad.connected = true;
	pad.left_x = 100;
	pad.left_y = -200;
	pad.right_x = 300;
	pad.right_y = -400;
	pad.left_trigger = 10;
	pad.right_trigger = 250;
	pad.buttons = 0x1000 | 0x0100 | 0x0040 | 0x0010 | 0x0001; // A, LB, LS, Start, D-pad up
	const gamepad_state m = mirrored(pad);
	expect(m.connected, "connected kept");
	expect(m.left_x == 300 && m.left_y == -400 && m.right_x == 100 && m.right_y == -200, "sticks swapped");
	expect(m.left_trigger == 250 && m.right_trigger == 10, "triggers swapped");
	// Face buttons and stick clicks are mirrored on the Quest buttons before the controller mapping
	// (input/button_map mirror_held), so the pad keeps them: only the grips (LB/RB) trade places here.
	expect(m.buttons == (0x1000 | 0x0200 | 0x0040 | 0x0010 | 0x0001), "LB->RB; A, LS, Start and D-pad kept");
	pad.buttons = 0x0100 | 0x0200 | 0x2000; // LB + RB + B
	expect(mirrored(pad).buttons == pad.buttons, "both grips stay, B kept");
	expect(mirrored(mirrored(pad)).buttons == pad.buttons, "mirror twice is identity");

	bfvr::stereo::hand_sample hands;
	hands.valid[0] = true;
	hands.grip[0].position[0] = -0.2f;
	hands.aim[1].position[0] = 0.3f;
	const bfvr::stereo::hand_sample s = swapped(hands);
	expect(!s.valid[0] && s.valid[1], "valid swapped");
	expect(s.grip[1].position[0] == -0.2f && s.aim[0].position[0] == 0.3f, "poses swapped");

	set_left_handed(true);
	expect(left_handed(), "flag on");
	set_left_handed(false);
	expect(!left_handed(), "flag off");

	if (failures == 0)
	{
		std::printf("handedness: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
