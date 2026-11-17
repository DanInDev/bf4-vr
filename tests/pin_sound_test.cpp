// Checks the grenade pin sound (src/weapon/pin_sound, #82): quieter than before, scaled by pin_volume, silent at 0.
#include <cmath>
#include <cstdio>

#include "weapon/pin_sound.hpp"

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

	float peak(const std::vector<float>& samples)
	{
		float p = 0.0f;
		for (const float s : samples)
		{
			p = std::fmax(p, std::fabs(s));
		}
		return p;
	}
}

int main()
{
	const float full = peak(pin_samples(44100, 1.0f));
	const float default_level = peak(pin_samples(44100, 0.35f));
	expect(full > 0.2f && full <= 0.6f, "at full volume below the old ping's 0.8 (0.4 ping + 0.4 tick)");
	expect(std::fabs(default_level - 0.35f * full) < 0.01f, "the volume scales the whole sound");
	expect(peak(pin_samples(44100, 0.0f)) == 0.0f, "volume 0 is silent");
	expect(peak(pin_samples(44100, 3.0f)) == full, "the volume is clamped to 1");
	expect(pin_samples(44100, 1.0f).size() < 44100 / 4, "shorter than the old 0.25 s");
	if (failures == 0)
	{
		std::printf("pin_sound: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
