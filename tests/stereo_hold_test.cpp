// Checks the theater's hold of the last stereo pair (src/stereo/stereo_hold, #163): a frame without a head
// pose keeps the pair for a short while, a menu drops it at once, and a long loss drops it once.
#include <cstdio>

#include "stereo/stereo_hold.hpp"

using namespace bfvr::stereo;

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
	{
		stereo_hold hold(300);
		hold.on_stereo();
		expect(hold.keep_on_mono(mono_reason::no_head, 1000), "a single frame without a head pose keeps the pair");
		expect(hold.keep_on_mono(mono_reason::no_head, 1100), "still held 100 ms in");
		expect(hold.keep_on_mono(mono_reason::no_head, 1299), "still held just under the limit");
		expect(!hold.keep_on_mono(mono_reason::no_head, 1300), "dropped at the limit");
		expect(!hold.keep_on_mono(mono_reason::no_head, 1310), "stays dropped until a stereo frame");
		expect(hold.dropped(mono_reason::no_head) == 1, "a long loss counts as one fall");
		expect(hold.held_frames() == 3, "held frames counted");
		hold.on_stereo();
		expect(hold.keep_on_mono(mono_reason::no_head, 5000), "a new loss after stereo is held again");
	}
	{
		stereo_hold hold(300);
		hold.on_stereo();
		expect(!hold.keep_on_mono(mono_reason::menu, 1000), "a menu drops the pair at once");
		expect(!hold.keep_on_mono(mono_reason::no_head, 1010), "no hold restarts inside a fall");
		expect(hold.dropped(mono_reason::menu) == 1, "the menu fall counted");
		expect(hold.dropped(mono_reason::no_head) == 0, "not counted twice");
	}
	{
		stereo_hold hold(300);
		hold.on_stereo();
		expect(hold.keep_on_mono(mono_reason::no_head, 1000), "held");
		expect(!hold.keep_on_mono(mono_reason::scene_screen, 1010), "a scene on the screen ends the hold");
		expect(hold.dropped(mono_reason::scene_screen) == 1, "counted as a scene fall");
	}
	{
		stereo_hold hold(300);
		expect(!hold.keep_on_mono(mono_reason::off, 0), "stereo off never holds");
	}
	if (failures == 0)
	{
		std::printf("stereo_hold: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
