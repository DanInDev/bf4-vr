// Checks the action aim timing (src/weapon/action_aim, #70): the aim turns first, the button follows after the
// lead, the aim stays for the tail, and one action at a time.
#include <cstdio>

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
}

int main()
{
	action_aim order; // lead 100, tail 150
	expect(!order.aiming(0) && !order.button(0) && order.done(0), "idle: no aim, no button");
	expect(order.start(1000, 80), "an action starts");
	expect(order.aiming(1000) && !order.button(1000), "at once: the aim turns, the button waits");
	expect(!order.button(1099) && order.button(1100), "the button goes down after the 100 ms lead");
	expect(order.button(1179) && !order.button(1180), "and is held 80 ms");
	expect(order.aiming(1329) && !order.aiming(1330), "the aim stays 150 ms after the button lets go");
	expect(!order.start(1200, 80), "one action at a time: a second start while it runs is refused");
	expect(order.done(1330) && order.start(1330, 80) && order.button(1430), "after the tail the next one can start");

	action_aim quick({ 0, 0 });
	expect(quick.start(50, 10) && quick.button(50) && !quick.aiming(60), "no lead or tail: the button at once, the aim only while it is down");

	if (failures == 0)
	{
		std::printf("action_aim: all tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}
