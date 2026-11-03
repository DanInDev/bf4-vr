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

	void test_gun_profile()
	{
		const recoil_config config;
		gun_recoil reference, heavy, first;
		reference.shot(0.0, 1, 1.0f, false);
		heavy.shot(0.0, 1, 1.0f, false, recoil_profile{ 2.0f });
		expect(near(heavy.at(0.0).degrees, 2.0f * reference.at(0.0).degrees), "a gun with twice the kick climbs twice as far");
		expect(near(heavy.at(0.0).back, 2.0f * reference.at(0.0).back), "and comes back twice as far");
		expect(reference.at(0.0).yaw == 0.0f, "no sideways data, no yaw");

		first.shot(0.0, 1, 1.0f, false, recoil_profile{ 1.0f, 2.0f });
		expect(near(first.at(0.0).degrees, 2.0f * config.degrees_per_shot), "a shot from rest gets the first-shot extra");
		first.shot(0.05, 1, 1.0f, false, recoil_profile{ 1.0f, 2.0f });
		const float second = first.at(0.05).degrees - 2.0f * config.degrees_per_shot * std::exp(-0.05f / config.pitch_recovery);
		expect(near(second, config.degrees_per_shot, 1e-3f), "a follow-up shot does not");

		gun_recoil wander;
		const recoil_profile sideways{ 1.0f, 1.0f, -1.0f, 1.5f };
		bool inside = true, left = false, right = false;
		for (int i = 0; i < 40; ++i)
		{
			const float before = wander.at(i * 0.5).yaw; // half a second apart: mostly settled
			wander.shot(i * 0.5, 1, 1.0f, false, sideways);
			const float step = wander.at(i * 0.5).yaw - before;
			inside = inside && step >= -1.0f - 1e-4f && step <= 1.5f + 1e-4f;
			left = left || step < -0.2f;
			right = right || step > 0.2f;
		}
		expect(inside && left && right, "sideways kicks fall between the gun's min and max, both ways");
		gun_recoil spray;
		for (int i = 0; i < 100; ++i)
		{
			spray.shot(i * 0.01, 3, 1.0f, false, recoil_profile{ 1.0f, 1.0f, 2.0f, 3.0f });
		}
		expect(spray.at(0.99).yaw <= config.max_yaw, "the yaw stays under its cap");
	}

	void test_profile_from_game()
	{
		const recoil_config config;
		// Live BF4 values (2026-11-03): AK-12 the reference, SCAR-H the heavier gun.
		const game_recoil ak12{ 0.3f, -0.12f, 0.17f, 18.0f, 2.1f };
		const game_recoil scar{ 0.5f, -0.225f, 0.225f, 12.0f, 1.75f };
		const recoil_profile same = profile_from_game(ak12, ak12);
		expect(near(same.vertical, 1.0f) && near(same.first_shot, 2.1f) && near(same.recovery, 1.0f), "the reference gun keeps the tuned kick");
		const float gain = 0.5f * config.degrees_per_shot / 0.3f;
		expect(near(same.yaw_min, -0.12f * gain) && near(same.yaw_max, 0.17f * gain), "sideways at half the vertical gain");
		const recoil_profile heavy = profile_from_game(scar, ak12);
		expect(near(heavy.vertical, 0.5f / 0.3f) && near(heavy.recovery, 1.5f), "the SCAR-H kicks harder and settles slower");
		game_recoil swapped = ak12;
		swapped.horizontal_min = 0.17f;
		swapped.horizontal_max = -0.12f;
		expect(near(profile_from_game(swapped, ak12).yaw_min, same.yaw_min), "min and max in any order");
		game_recoil broken = ak12;
		broken.vertical = std::nanf("");
		expect(profile_from_game(broken, ak12).vertical == 1.0f && profile_from_game(broken, ak12).yaw_max == 0.0f,
			"unreadable data falls back to the default kick");
		broken.vertical = 0.0f;
		expect(profile_from_game(broken, ak12).vertical == 1.0f, "a zero kick falls back too");
		broken.vertical = 100.0f;
		broken.decrease = 0.0f;
		expect(profile_from_game(broken, ak12).vertical <= 4.0f && profile_from_game(broken, ak12).recovery == 1.0f,
			"an extreme value is capped, a missing decrease keeps the settle time");
	}

	void test_recovery()
	{
		const recoil_config config;
		gun_recoil quick, slow;
		quick.shot(0.0, 1, 1.0f, false);
		slow.shot(0.0, 1, 1.0f, false, recoil_profile{ 1.0f, 1.0f, 0.0f, 0.0f, 2.0f });
		expect(near(slow.at(2.0 * config.pitch_recovery).degrees, quick.at(config.pitch_recovery).degrees, 1e-3f),
			"recovery 2 takes twice as long to settle");
	}
}

int main()
{
	test_single_shot();
	test_full_auto();
	test_scaling();
	test_gun_profile();
	test_profile_from_game();
	test_recovery();
	if (failures == 0)
	{
		std::printf("gun_recoil: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
