// Checks the pad source's gate (src/input/gamepad, #72) in the XR loop's call order: set_controllers_ours, then
// publish. While unfocused, sync returns a neutral pad; the first sample after the return must not open the gate
// on that stale neutral one (verifier 2026-11-16: the held trigger went straight through).
#include <cstdio>

#include "input/gamepad.hpp"

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

	gamepad_state pad(std::uint16_t buttons, std::uint8_t rt)
	{
		gamepad_state p{};
		p.connected = true;
		p.buttons = buttons;
		p.right_trigger = rt;
		return p;
	}

	void frame(bool ours, const gamepad_state& sample)
	{
		set_controllers_ours(ours);
		publish(sample);
	}

	constexpr std::uint16_t grip = 0x0200;
}

int main()
{
	frame(true, pad(grip, 255));
	expect(latest().right_trigger == 255 && latest().buttons == grip, "focused: the held trigger and grip fire");

	frame(false, pad(0, 0)); // the dashboard opens: every action inactive, sync returns neutral
	frame(false, pad(0, 0));
	expect(latest().right_trigger == 0 && latest().buttons == 0 && latest().connected, "unfocused: neutral, still connected");

	frame(true, pad(grip, 255)); // back, the release swallowed: the runtime reports both still pressed
	expect(latest().right_trigger == 0, "first frame back: the stale-held trigger stays closed");
	expect(latest().buttons == 0, "first frame back: the grip stays closed");
	frame(true, pad(grip, 255));
	expect(latest().right_trigger == 0 && latest().buttons == 0, "and on the frames after");

	frame(true, pad(grip, 0));
	frame(true, pad(grip, 255));
	expect(latest().right_trigger == 255 && latest().buttons == 0, "a fresh pull fires; the grip still waits for its release");
	frame(true, pad(0, 255));
	frame(true, pad(grip, 255));
	expect(latest().buttons == grip, "the grip works after its release");

	// The pipe's simulation takes the same path.
	fake_focus("lost");
	publish(pad(0, 0));
	fake_focus("back");
	expect(latest().right_trigger == 0, "fakefocus back alone opens nothing");
	publish(pad(grip, 255));
	expect(latest().right_trigger == 0 && latest().buttons == 0, "fakefocus: a held trigger after the return stays closed");
	fake_focus("off");

	set_focus_release(false);
	frame(false, pad(grip, 255));
	expect(latest().right_trigger == 255, "focus_release = 0: the old behaviour, the pad passes");
	if (failures == 0)
	{
		std::printf("focus_release: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
