// Checks the death watch (src/weapon/death_watch, #73) and the release gate's close (input/release_gate).
#include <cstdio>

#include "input/release_gate.hpp"
#include "weapon/death_watch.hpp"

using namespace bfvr;

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
	weapon::death_watch watch;
	expect(watch.update(true, 0.0f) == weapon::life_event::none, "health 0 before any living soldier (a load): no death");
	expect(watch.update(true, 100.0f) == weapon::life_event::none, "alive");
	expect(watch.update(true, 40.0f) == weapon::life_event::none, "hurt");
	expect(watch.update(true, 0.0f) == weapon::life_event::died && watch.dead() && watch.deaths() == 1, "health 0: died");
	expect(watch.update(true, 0.0f) == weapon::life_event::none, "once");
	expect(watch.update(false, 0.0f) == weapon::life_event::none && watch.dead(), "no soldier (death camera): still dead");
	expect(watch.update(true, 100.0f) == weapon::life_event::respawned && !watch.dead(), "health again: respawned");
	expect(watch.update(true, 100.0f) == weapon::life_event::none, "once");
	expect(watch.update(true, -0.0f) == weapon::life_event::died && watch.deaths() == 2, "a second death counts (health -0.0 as seen live)");

	// The gate closed at a death: every held input waits for a release, grips too (nothing to keep through a death).
	input::release_gate gate;
	input::gamepad_state held{};
	held.connected = true;
	held.buttons = 0x0300; // both grips
	held.right_trigger = 255;
	gate.apply(held, true);
	gate.close();
	const input::gamepad_state after = gate.apply(held, true);
	expect(after.buttons == 0 && after.right_trigger == 0, "#73: after close, a trigger and grips held through the death stay closed");
	expect(gate.times_closed() == 1, "a close counts as a closure");
	input::gamepad_state released{};
	released.connected = true;
	gate.apply(released, true);
	expect(gate.apply(held, true).right_trigger == 255 && gate.apply(held, true).buttons == 0x0300, "released once: they work again");
	if (failures == 0)
	{
		std::printf("death_watch: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
