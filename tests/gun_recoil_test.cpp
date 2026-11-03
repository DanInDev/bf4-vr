// Checks the drawn gun's kick (src/body/gun_recoil): a shot pitches and pushes, full auto stacks to
// a cap, both settle, two hands and the strength setting scale it, and stale or reversed time is safe.
#include <cmath>
#include <cstdio>

#include "body/gun_recoil.hpp"

using namespace bfvr::body;

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

	bool near(float a, float b, float tolerance = 1e-4f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	void test_single_shot()
	{
		const recoil_config config;
		gun_recoil recoil(config);
		expect(recoil.at(1.0).degrees == 0.0f, "no kick before a shot");
		recoil.shot(1.0, 1, 1.0f, false);
		const recoil_offset kick = recoil.at(1.0);
		expect(near(kick.degrees, config.degrees_per_shot) && near(kick.back, config.back_per_shot), "one shot: one kick");
		const recoil_offset later = recoil.at(1.0 + config.pitch_recovery);
		expect(near(later.degrees, config.degrees_per_shot * std::exp(-1.0f), 1e-3f), "the muzzle settles with its time constant");
		expect(later.back < 0.1f * config.back_per_shot, "the push back settles faster than the muzzle");
		expect(recoil.at(3.0).degrees == 0.0f && recoil.at(3.0).back == 0.0f, "nothing left two seconds after the shot");
		expect(recoil.at(0.5).degrees == 0.0f, "a time before the shot reads no kick");
	}

	void test_full_auto()
	{
		const recoil_config config;
		gun_recoil recoil(config);
		double now = 0.0;
		float previous = 0.0f;
		bool climbing = true;
		for (int i = 0; i < 5; ++i)
		{
			recoil.shot(now, 1, 1.0f, false);
			const float degrees = recoil.at(now).degrees;
			climbing = climbing && degrees > previous;
			previous = degrees;
			now += 0.1; // 600 rounds a minute
		}
		expect(climbing, "full auto climbs");
		for (int i = 0; i < 200; ++i)
		{
			recoil.shot(now, 3, 1.0f, false);
			now += 0.01;
		}
		expect(recoil.at(now - 0.01).degrees <= config.max_degrees && recoil.at(now - 0.01).back <= config.max_back, "the kick stays under its caps");
	}

	void test_scaling()
	{
		gun_recoil one, two, off, half, batch;
		one.shot(0.0, 1, 1.0f, false);
		two.shot(0.0, 1, 1.0f, true);
		off.shot(0.0, 1, 0.0f, false);
		half.shot(0.0, 1, 0.5f, false);
		batch.shot(0.0, 2, 1.0f, false);
		expect(two.at(0.0).degrees < 0.5f * one.at(0.0).degrees, "two hands steady the gun");
		expect(off.at(0.0).degrees == 0.0f, "strength 0 is off");
		expect(near(half.at(0.0).degrees, 0.5f * one.at(0.0).degrees), "strength scales the kick");
		expect(near(batch.at(0.0).degrees, 2.0f * one.at(0.0).degrees), "two shots in one frame kick twice");
		gun_recoil none;
		none.shot(0.0, 0, 1.0f, false);
		expect(none.at(0.0).degrees == 0.0f, "no shots, no kick");
	}
}

int main()
{
	test_single_shot();
	test_full_auto();
	test_scaling();
	if (failures == 0)
	{
		std::printf("gun_recoil: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
