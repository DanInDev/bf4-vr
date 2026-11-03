// Checks the scripted-camera detector (src/stereo/scripted_view): play stays play, a camera turned
// away from the aim for a while is scripted, death is at once, and it returns with hysteresis.
#include <cmath>
#include <cstdio>

#include "stereo/scripted_view.hpp"

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

	scripted_input playing(float deviation)
	{
		return { true, false, true, deviation };
	}

	bool run(scripted_view& view, const scripted_input& input, float seconds, float step = 0.011f)
	{
		for (float t = 0.0f; t < seconds; t += step)
		{
			view.update(input, step);
		}
		return view.active();
	}

	void test_deviation()
	{
		// Live (2026-11-03): camera row 2 (0, 0, -1) i.e. forward (0, 0, 1), aimer yaw 0 pitch 0.
		const float ahead[3] = { 0.0f, 0.0f, 1.0f };
		expect(std::fabs(aim_deviation_degrees(ahead, 0.0f, 0.0f)) < 0.01f, "camera along the aim: no deviation");
		const float left[3] = { -1.0f, 0.0f, 0.0f };
		expect(std::fabs(aim_deviation_degrees(left, 1.5707963f, 0.0f)) < 0.01f, "camera yaw = -aimer yaw");
		const float up[3] = { 0.0f, 0.5f, 0.8660254f };
		expect(std::fabs(aim_deviation_degrees(up, 0.0f, 0.5235988f)) < 0.01f, "pitch up is positive in both");
		expect(std::fabs(aim_deviation_degrees(ahead, 0.0f, 0.5235988f) - 30.0f) < 0.01f, "a 30 degree pitch difference reads 30");
		const float zero[3] = {};
		expect(std::isnan(aim_deviation_degrees(zero, 0.0f, 0.0f)), "no camera forward: unknown");
	}

	void test_switching()
	{
		scripted_view view;
		expect(!run(view, playing(1.0f), 2.0f), "play (camera on the aim, recoil-sized wobble) stays play");
		expect(!run(view, playing(30.0f), 0.1f), "a short glance-sized deviation is not yet scripted");
		expect(run(view, playing(30.0f), 0.3f), "the camera away from the aim for a while: scripted");
		expect(run(view, playing(6.0f), 1.0f), "between the thresholds it stays scripted");
		expect(run(view, playing(2.0f), 0.3f), "back on the aim briefly: still scripted");
		expect(!run(view, playing(2.0f), 0.4f), "back on the aim for half a second: play again");
	}

	void test_death_and_no_soldier()
	{
		scripted_view view;
		expect(view.update({ true, true, true, 0.0f }, 0.011f), "death switches at once");
		expect(!run(view, playing(0.5f), 0.6f), "alive again with the camera on the aim: play");
		expect(view.update({ false, false, false, 0.0f }, 0.011f), "no soldier: scripted");
		scripted_view unknown;
		expect(!run(unknown, { true, false, false, 90.0f }, 1.0f), "no aim reading: no guess");
	}
}

int main()
{
	test_deviation();
	test_switching();
	test_death_and_no_soldier();
	if (failures == 0)
	{
		std::printf("scripted_view: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
