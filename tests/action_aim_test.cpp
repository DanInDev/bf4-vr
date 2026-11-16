// Checks the action aim timing (src/weapon/action_aim, #70): the aim turns first, the button follows after the
// lead as press and release events that a slow frame cannot skip, the aim stays for the tail, one action at a time.
#include <cstdint>
#include <cstdio>
#include <initializer_list>

#include "weapon/action_aim.hpp"

using namespace bfvr::weapon;

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

	// Runs one action sampled every frame_ms from start: when the press and release came, and whether the aim
	// was on at the press.
	struct run
	{
		std::int64_t pressed_at{ -1 };
		std::int64_t released_at{ -1 };
		bool aim_at_press{};
		bool aim_after{};
	};

	run sample(std::uint64_t frame_ms, std::uint64_t hold_ms, std::uint64_t stall_from = 0, std::uint64_t stall_ms = 0)
	{
		action_aim action;
		run r;
		std::uint64_t now = 1000;
		action.start(now, hold_ms);
		for (int frame = 0; frame < 200; ++frame)
		{
			now += frame_ms;
			if (stall_ms && now >= stall_from && now < stall_from + frame_ms)
			{
				now += stall_ms;
			}
			const action_button event = action.update(now);
			if (event == action_button::press)
			{
				r.pressed_at = static_cast<std::int64_t>(now);
				r.aim_at_press = action.aiming(now);
			}
			if (event == action_button::release)
			{
				r.released_at = static_cast<std::int64_t>(now);
			}
		}
		r.aim_after = action.aiming(now);
		return r;
	}
}

int main()
{
	action_aim order; // lead 100, tail 150
	expect(!order.aiming(0) && order.update(0) == action_button::none && order.done(0), "idle: no aim, no button");
	expect(order.start(1000, 80), "an action starts");
	expect(order.aiming(1000) && order.update(1000) == action_button::none && !order.down(), "at once: the aim turns, the button waits");
	expect(order.update(1099) == action_button::none, "not before the 100 ms lead");
	expect(order.update(1100) == action_button::press && order.down(), "the press after the lead");
	expect(order.update(1179) == action_button::none && order.down(), "held 80 ms");
	expect(order.update(1180) == action_button::release && !order.down(), "then released");
	expect(order.update(1200) == action_button::none, "one release");
	expect(order.aiming(1329) && !order.aiming(1330), "the aim stays 150 ms after the release");
	expect(!order.start(1200, 80), "one action at a time: a second start while it runs is refused");
	expect(order.done(1330) && order.start(1330, 80), "after the tail the next one can start");

	// Monitor runs are slow (4-16 fps headless): the press and release must never be skipped.
	for (const std::uint64_t frame : { 11ull, 90ull, 95ull, 250ull })
	{
		const run r = sample(frame, 80);
		expect(r.pressed_at >= 1100 && r.released_at >= r.pressed_at + 80, "slow frames: pressed after the lead, released after the hold");
		expect(r.aim_at_press && !r.aim_after, "the aim is on at the press and off once it is over");
	}
	const run stalled = sample(11, 80, 1050, 400);
	expect(stalled.pressed_at >= 1100 && stalled.released_at >= stalled.pressed_at + 80, "a 400 ms stall across the button window: still pressed and released");

	action_aim quick({ 0, 0 });
	expect(quick.start(50, 10) && quick.update(50) == action_button::press, "no lead: the press at once");
	expect(quick.update(55) == action_button::none && quick.update(60) == action_button::release, "released after the hold");

	if (failures == 0)
	{
		std::printf("action_aim: all tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}
