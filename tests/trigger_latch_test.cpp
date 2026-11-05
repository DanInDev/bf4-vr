// Checks the trigger latch (src/input/trigger_latch): a resting finger never fires, a pull does, and the
// release needs the trigger let out past the gap (no chatter around one threshold).
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "input/trigger_latch.hpp"

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
	bool held = false;
	// The values a resting finger gave in the headset (66..120 of 255).
	for (const float resting : { 66.0f / 255.0f, 94.0f / 255.0f, 120.0f / 255.0f })
	{
		expect(latch_trigger(resting, held, 0.55f) == 0 && !held, "a finger resting on the trigger does not fire");
	}
	expect(latch_trigger(0.6f, held, 0.55f) == 255 && held, "a pull past the press point fires");
	expect(latch_trigger(0.5f, held, 0.55f) == 255, "easing off a little keeps firing (hysteresis)");
	expect(latch_trigger(0.46f, held, 0.55f) == 255, "still above the release point");
	expect(latch_trigger(0.44f, held, 0.55f) == 0 && !held, "let out past the release point stops");
	expect(latch_trigger(0.5f, held, 0.55f) == 0, "between release and press after a release: still off");
	expect(latch_trigger(1.0f, held, 0.3f) == 255, "a lower press point (setting) fires earlier");
	held = false;
	expect(latch_trigger(0.25f, held, 0.3f) == 0, "but not below it");
	held = false;
	expect(latch_trigger(std::nanf(""), held, 0.55f) == 0, "a broken sample is no press");
	expect(latch_trigger(0.9f, held, std::nanf("")) == 255, "a broken setting falls back to 0.55");
	held = false;
	expect(latch_trigger(0.04f, held, 0.0f) == 0, "the press point never goes below 5 %");
	std::printf(failures ? "trigger_latch_test: %d failures\n" : "trigger_latch_test: all passed\n", failures);
	return failures ? 1 : 0;
}
