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
		for (int i = 0; i < 27; ++i) // a 0.3 s glance
		{
			torso.update(30.0f * degree, none, 0.011f);
		}
		expect(std::fabs(torso.yaw()) < 8.0f * degree, "a glance 30 degrees aside barely moves the chest");
		torso.update(90.0f * degree, none, 0.011f);
		expect(near(torso.yaw(), 90.0f * degree - config.max_twist, 0.02f), "a fast turn past the limit drags it along at once, never further behind");
		for (int i = 0; i < 1000; ++i) // 11 s
		{
			torso.update(90.0f * degree, none, 0.011f);
		}
		expect(near(torso.yaw(), 90.0f * degree, 0.5f * degree), "held there (turned in the room), the chest settles onto the head");
		torso_yaw ahead_again;
		ahead_again.reset(40.0f * degree);
		ahead_again.update(0.0f, none, 0.011f);
		expect(ahead_again.yaw() <= config.max_twist + 1e-4f && ahead_again.yaw() > 30.0f * degree, "looking back ahead: within the limit, settling slowly");
	}

	void test_hands()
	{
		torso_yaw torso;
		expect(near(settle(torso, 0.0f, ahead(25.0f * degree)), 25.0f * degree), "hands out to the left turn the torso to them");
		torso_yaw far;
		const torso_config config;
		// Hands together 44 degrees left of a head turned 30: inside the cone, past the twist limit.
		torso_hands narrow = ahead(74.0f * degree);
		for (int hand = 0; hand < 2; ++hand)
		{
			narrow.offset[hand][0] = -std::sin(74.0f * degree) * (0.4f + 0.1f * static_cast<float>(hand));
			narrow.offset[hand][2] = -std::cos(74.0f * degree) * (0.4f + 0.1f * static_cast<float>(hand));
		}
		expect(near(settle(far, 30.0f * degree, narrow), 30.0f * degree + config.max_twist), "never more than the twist limit from the head");
		torso_yaw aside;
		expect(near(settle(aside, 0.0f, ahead(80.0f * degree)), 0.0f), "hands far off to the side of the face do not count");
		torso_yaw close;
		expect(near(settle(close, 0.0f, ahead(60.0f * degree, 0.05f)), 0.0f), "hands at the chest do not count");
		torso_hands one = ahead(25.0f * degree);
		one.valid[1] = false;
		torso_yaw single;
		expect(near(settle(single, 0.0f, one), 0.0f), "one hand alone does not turn it");
		// Crossing the reach threshold eases in: just past it the hands barely pull.
		torso_yaw edge;
		edge.update(0.0f, ahead(40.0f * degree, config.min_hand_reach + 0.01f), 0.05f);
		torso_yaw full;
		full.update(0.0f, ahead(40.0f * degree, config.min_hand_reach + config.reach_blend + 0.05f), 0.05f);
		expect(std::fabs(edge.yaw()) < 0.05f * std::fabs(full.yaw()), "hands just past the reach threshold barely turn it");
	}

	// One hand swung round the head (headset 2026-11-05: a "lasso" with the gun hand spun the body),
	// the other low in front: the torso stays with the head all the way round.
	void test_lasso()
	{
		torso_yaw torso;
		float worst = 0.0f;
		for (int step = 0; step <= 72; ++step)
		{
			const float a = static_cast<float>(step) * 5.0f * degree;
			torso_hands hands;
			hands.valid[0] = hands.valid[1] = true;
			hands.offset[0][0] = -0.15f;
			hands.offset[0][1] = -0.4f;
			hands.offset[0][2] = -0.3f;
			hands.offset[1][0] = -0.35f * std::sin(a);
			hands.offset[1][1] = -0.1f;
			hands.offset[1][2] = -0.35f * std::cos(a);
			for (int i = 0; i < 10; ++i)
			{
				torso.update(0.0f, hands, 0.011f);
			}
			// Swinging through the cone in front pulls a little (both hands ahead); never past it.
			if (a > 70.0f * degree && a < 290.0f * degree)
			{
				worst = std::fmax(worst, std::fabs(torso.yaw()));
			}
		}
		std::printf("lasso: largest torso turn with the gun hand beside or behind the head %.1f degrees\n", worst / degree);
		expect(worst < 25.0f * degree, "a hand swung round the head does not swing the torso to it");
		torso_yaw settled;
		torso_hands behind;
		behind.valid[0] = behind.valid[1] = true;
		behind.offset[0][0] = -0.15f;
		behind.offset[0][2] = -0.3f;
		behind.offset[1][0] = 0.1f;
		behind.offset[1][2] = 0.35f;
		expect(near(settle(settled, 0.0f, behind), 0.0f), "the gun hand behind the head has no pull");
		torso_yaw chest;
		torso_hands at_chest = ahead(30.0f * degree);
		at_chest.offset[0][0] = 0.02f;
		at_chest.offset[0][2] = -0.05f; // the off hand at the chest pouch
		expect(near(settle(chest, 0.0f, at_chest), 0.0f), "with the off hand at the chest the gun hand alone does not turn it");
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
		expect(std::fabs(angle_between(wrap.yaw(), 180.0f * degree)) <= 10.0f * degree + 1e-3f, "across the back it takes the short way (through 180, not round the front)");
	}
}

// The neck sits behind the eyes along the head's facing, so a player turned round in the room has it
// in front of where they started (the body turned about a fixed point there: 20 cm off at 180 degrees).
void test_neck()
{
	float out[3];
	neck_offset(0.0f, 0.1f, out);
	expect(near(out[0], 0.0f) && near(out[2], 0.1f), "facing ahead: the neck is behind (+z)");
	neck_offset(90.0f * degree, 0.1f, out);
	expect(near(out[0], 0.1f) && near(out[2], 0.0f), "facing left: the neck is to the right (+x)");
	neck_offset(180.0f * degree, 0.1f, out);
	expect(near(out[0], 0.0f) && near(out[2], -0.1f), "turned round: the neck is ahead of the start (-z)");
	// From the eyes on a circle about a fixed neck (a real head turn), the neck comes out the same.
	for (int step = -6; step <= 6; ++step)
	{
		const float yaw = 30.0f * degree * static_cast<float>(step);
		const float eyes[2] = { -0.1f * std::sin(yaw), -0.1f * std::cos(yaw) }; // neck at the origin
		neck_offset(yaw, 0.1f, out);
		expect(near(eyes[0] + out[0], 0.0f) && near(eyes[1] + out[2], 0.0f), "a head turning about the neck keeps the neck still");
	}
	float forward[3];
	const float half = 0.5f * 40.0f * degree;
	expect(near(yaw_of(0.0f, std::sin(half), 0.0f, std::cos(half)), 40.0f * degree), "yaw_of: a head turned 40 degrees left");
	neck_offset(yaw_of(0.0f, std::sin(half), 0.0f, std::cos(half)), 0.1f, forward);
	expect(forward[0] > 0.0f && forward[2] > 0.0f, "turned left: the neck is behind and to the right");
}

int main()
{
	test_angles();
	test_neck();
	test_head_only();
	test_hands();
	test_lasso();
	test_easing();
	if (failures == 0)
	{
		std::printf("torso: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
