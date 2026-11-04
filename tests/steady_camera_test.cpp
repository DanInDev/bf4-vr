// Checks the walking-sway filter (src/stereo/steady_camera): the measured 0.9 Hz, +-6 cm sway is
// mostly removed, a slow change passes, a cut is followed at once, standing still removes nothing.
#include <cmath>
#include <cstdio>

#include "stereo/steady_camera.hpp"

using bfvr::stereo::sway_filter;

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
	constexpr float dt = 1.0f / 120.0f;
	constexpr float pi = 3.14159265f;

	// Standing: nothing to remove.
	sway_filter still;
	float removed = 1.0f;
	for (int i = 0; i < 240; ++i)
	{
		removed = still.update(-0.0004f, dt);
	}
	expect(std::fabs(removed) < 1e-6f, "standing still: nothing removed");

	// Walking: +-5.8 cm at 0.9 Hz (measured). After settling, what stays on the camera is small.
	sway_filter walk;
	float worst = 0.0f;
	for (int i = 0; i < 120 * 20; ++i)
	{
		const float t = i * dt;
		const float offset = 0.058f * std::sin(2 * pi * 0.9f * t);
		const float kept = offset - walk.update(offset, dt);
		if (t > 10.0f)
		{
			worst = std::fmax(worst, std::fabs(kept));
		}
	}
	expect(worst < 0.008f, "walking sway reduced below 8 mm");
	expect(worst > 0.0f, "the filter keeps a slow part");

	// A slow 4 cm shift over 10 s (cover, stance) is mostly followed.
	sway_filter slow;
	float kept_slow = 0.0f;
	for (int i = 0; i <= 120 * 14; ++i)
	{
		const float t = i * dt;
		const float offset = 0.04f * std::fmin(t / 10.0f, 1.0f);
		kept_slow = offset - slow.update(offset, dt);
	}
	expect(std::fabs(kept_slow - 0.04f) < 0.004f, "slow shift followed (within 4 mm after 4 s)");

	// A cut (teleport, vehicle seat): followed at once, no slide.
	sway_filter cut;
	for (int i = 0; i < 120; ++i)
	{
		cut.update(0.0f, dt);
	}
	expect(cut.update(0.4f, dt) == 0.0f, "cut followed at once");
	expect(std::fabs(cut.update(0.4f, dt)) < 1e-6f, "and stays");

	// Bad input.
	sway_filter bad;
	expect(bad.update(NAN, dt) == 0.0f, "NaN offset: nothing removed");
	bad.update(0.01f, dt);
	expect(std::isfinite(bad.update(0.02f, NAN)), "NaN time keeps a finite answer");

	if (failures == 0)
	{
		std::printf("steady_camera: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
