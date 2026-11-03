// Checks the torso yaw estimate (src/body/torso): the head may look around within the twist limit,
// turning further drags the torso, hands out in front turn it towards them, and it eases.
#include <cmath>
#include <cstdio>

#include "body/torso.hpp"

using namespace bfvr::body;

namespace
{
	int failures = 0;
	constexpr float degree = 3.14159265f / 180.0f;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool near(float a, float b, float tolerance = 1e-3f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	torso_hands ahead(float yaw, float distance = 0.45f)
	{
		torso_hands hands;
		for (int hand = 0; hand < 2; ++hand)
		{
			hands.valid[hand] = true;
			hands.offset[hand][0] = -std::sin(yaw) * distance + (hand == 0 ? -0.1f : 0.1f) * std::cos(yaw);
			hands.offset[hand][1] = -0.3f;
			hands.offset[hand][2] = -std::cos(yaw) * distance - (hand == 0 ? -0.1f : 0.1f) * std::sin(yaw);
		}
		return hands;
	}

	float settle(torso_yaw& torso, float head, const torso_hands& hands)
	{
		for (int i = 0; i < 200; ++i)
		{
			torso.update(head, hands, 0.011f);
		}
		return torso.yaw();
	}

	void test_angles()
	{
		expect(near(yaw_of(0, 0, 0, 1), 0.0f), "identity faces forward");
		const float half = 0.5f * 30.0f * degree;
		expect(near(yaw_of(0, std::sin(half), 0, std::cos(half)), 30.0f * degree), "a turn left about up is positive");
		expect(near(angle_between(170.0f * degree, -170.0f * degree), -20.0f * degree), "differences wrap");
	}

	void test_head_only()
	{
		const torso_config config;
		torso_yaw torso;
		const torso_hands none;
		expect(near(settle(torso, 30.0f * degree, none), 0.0f), "looking 30 degrees aside leaves the torso");
		expect(near(settle(torso, 90.0f * degree, none), 90.0f * degree - config.max_twist), "looking further drags it to the twist limit");
		expect(near(settle(torso, 0.0f, none), config.max_twist), "looking back ahead pulls it back to the limit, no further");
	}

	void test_hands()
	{
		torso_yaw torso;
		expect(near(settle(torso, 0.0f, ahead(25.0f * degree)), 25.0f * degree), "hands out to the left turn the torso to them");
		torso_yaw far;
		const torso_config config;
		expect(near(settle(far, 0.0f, ahead(80.0f * degree)), config.max_twist), "never more than the twist limit from the head");
		torso_yaw close;
		expect(near(settle(close, 0.0f, ahead(60.0f * degree, 0.05f)), 0.0f), "hands at the chest do not count");
		torso_hands one = ahead(25.0f * degree);
		one.valid[1] = false;
		torso_yaw single;
		expect(near(settle(single, 0.0f, one), 0.0f), "one hand alone does not turn it");
	}

	void test_easing()
	{
		const torso_config config;
		torso_yaw torso;
		const torso_hands hands = ahead(30.0f * degree);
		torso.update(0.0f, hands, config.follow_seconds);
		expect(near(torso.yaw(), 30.0f * degree * (1.0f - std::exp(-1.0f)), 2e-3f), "it eases with its time constant");
		torso_yaw stuck;
		stuck.update(0.0f, hands, 0.0f);
		expect(stuck.yaw() == 0.0f, "no time, no movement");
		stuck.update(std::nanf(""), hands, 0.1f);
		expect(stuck.yaw() == 0.0f, "a broken head sample is ignored");
		torso_yaw wrap;
		wrap.reset(170.0f * degree);
		settle(wrap, -170.0f * degree, torso_hands{});
		expect(std::fabs(angle_between(wrap.yaw(), 170.0f * degree)) < 1e-3f, "across the back it takes the short way (within the limit already)");
	}
}

int main()
{
	test_angles();
	test_head_only();
	test_hands();
	test_easing();
	if (failures == 0)
	{
		std::printf("torso: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
