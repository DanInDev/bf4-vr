// Checks contact grips (src/body/grasp_contact): box distances, the cupped magazine's placement, the
// palm-up switch, and a finger closing until it touches.
#include <cmath>
#include <cstdio>

#include "body/grasp_contact.hpp"

using namespace bfvr::body;
using namespace bfvr::body::contact;

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

	bool near(ik::vec3 a, ik::vec3 b, float tolerance = 1e-4f)
	{
		return ik::length(a - b) <= tolerance;
	}

	void test_distance()
	{
		const float half = 0.5f * 0.7853982f; // 45 degrees about y
		const box shape{ { 1, 2, 3 }, { 0, std::sin(half), 0, std::cos(half) }, { 0.1f, 0.2f, 0.3f } };
		expect(near(signed_distance(shape, { 1, 2, 3 }), -0.1f), "the centre is the smallest half extent deep");
		expect(near(signed_distance(shape, { 1, 2.5f, 3 }), 0.3f), "above the top face");
		const ik::vec3 along_x = ik::rotate(shape.frame, { 1, 0, 0 });
		expect(near(signed_distance(shape, shape.centre + along_x * 0.15f), 0.05f), "the box's own axes count, not the world's");
		expect(near(signed_distance(shape, shape.centre + ik::rotate(shape.frame, { 0.2f, 0.3f, 0 })), std::sqrt(0.01f + 0.01f)), "near a corner: the distance to the edge");
	}

	void test_cup()
	{
		const magazine_size size;
		// Palm facing up (+y), fingers forward (-z).
		const cup_placement cup = cup_magazine({ 0, 1, 0 }, { 0, 1, 0 }, { 0, 0.2f, -1 }, size, 0.01f);
		expect(cup.valid, "a palm and a finger direction place the magazine");
		expect(near(cup.top, { 0, 1.0f + 0.01f + size.height, 0 }), "its top stands a magazine's height above the palm");
		expect(near(ik::rotate(cup.frame, { 0, 1, 0 }), { 0, 1, 0 }), "it stands along the palm normal");
		expect(near(ik::rotate(cup.frame, { 0, 0, 1 }), { 0, 0, -1 }), "its front faces the fingertips (the finger direction made level with the palm)");
		expect(near(signed_distance(cup.body, { 0, 1.0f + 0.01f, 0 }), 0.0f), "the base plate touches the palm gap's top");
		expect(signed_distance(cup.body, { 0, 1, 0 }) > 0.0f, "the palm itself stays outside");
		expect(!cup_magazine({ 0, 0, 0 }, { 0, 1, 0 }, { 0, 1, 0 }, size).valid, "fingers along the palm normal give no frame");
	}

	void test_switch()
	{
		expect(!cup_grip(false, 0.3f), "a sideways palm wraps");
		expect(cup_grip(false, 0.6f), "palm up cups");
		expect(cup_grip(true, 0.3f), "turning back a little keeps the cup");
		expect(!cup_grip(true, 0.1f), "turning well back wraps again");
		expect(cup_grip(true, std::nanf("")) && !cup_grip(false, std::nanf("")), "no reading keeps the grip");
	}

	void test_closing()
	{
		// A planar finger: knuckle at the origin, two 4 cm segments along +x, curling up about z, under
		// a slab (the magazine's face) from y = 0.05 up.
		const box wall{ { 0.0f, 0.1f, 0 }, {}, { 0.1f, 0.05f, 0.05f } };
		const auto tip = [](float curl) {
			const float a = curl * 1.5f, b = curl * 3.0f; // knuckle and middle joint bend
			const ik::vec3 middle{ 0.04f * std::cos(a), 0.04f * std::sin(a), 0 };
			return middle + ik::vec3{ 0.04f * std::cos(b), 0.04f * std::sin(b), 0 };
		};
		const auto touching = [&](float curl) { return signed_distance(wall, tip(curl)) <= 0.008f; };
		const float curl = close_until(touching, 0.0f, 1.0f);
		expect(curl > 0.0f && curl < 1.0f, "the finger stops part way");
		expect(near(signed_distance(wall, tip(curl)), 0.008f, 2e-3f), "it stops with the tip on the surface (finger radius)");
		expect(!touching(curl - 0.01f), "a little less curl does not touch");
		expect(close_until([](float) { return true; }, 0.2f, 1.0f) == 0.2f, "already touching: no closing");
		expect(close_until([](float) { return false; }, 0.0f, 0.8f) == 0.8f, "nothing to touch: closes fully");
	}
}

namespace
{
	void test_coupling()
	{
		// Live cup (2026-11-03): middle and ring touched at 0.54 / 0.51, index and pinky closed fully.
		float curl[5]{ 1.0f, 1.0f, 0.54f, 0.51f, 1.0f };
		const bool touched[5]{ false, false, true, true, false };
		couple_free_fingers(curl, touched);
		expect(near(curl[1], 0.64f) && near(curl[4], 0.64f), "free outer fingers stay with the most-closed touching one");
		expect(near(curl[2], 0.54f) && near(curl[3], 0.51f), "touching fingers keep their contact curl");
		expect(curl[0] == 1.0f, "the thumb is left alone");
		float open[5]{ 0.2f, 0.3f, 0.54f, 0.51f, 0.2f };
		couple_free_fingers(open, touched);
		expect(near(open[1], 0.3f) && near(open[4], 0.2f), "it only limits, never closes a finger further");
		float none[5]{ 1, 1, 1, 1, 1 };
		const bool nothing[5]{};
		couple_free_fingers(none, nothing);
		expect(none[1] == 1.0f, "nothing touched: no limit");
	}
}

namespace
{
	void test_pinch()
	{
		const float knob = 0.008f, radius = 0.009f;
		const ik::vec3 index{ 0.1f, 0.0f, 0.0f }, thumb{ 0.1f, 0.05f, 0.0f };
		const ik::vec3 at = pinch_point(index, thumb, knob, radius);
		expect(near(at, { 0.1f, knob + radius, 0.0f }), "the knob sits against the index, towards the thumb");
		const box handle{ at, {}, { knob, knob, knob } };
		expect(near(signed_distance(handle, index), radius), "the index's side touches the knob (one finger radius off its surface)");
		expect(signed_distance(handle, thumb) > radius, "the thumb, still open, does not");
		expect(near(pinch_point(index, index, knob, radius), index), "a thumb on the index gives the index itself");
	}
}

int main()
{
	test_pinch();
	test_coupling();
	test_distance();
	test_cup();
	test_switch();
	test_closing();
	if (failures == 0)
	{
		std::printf("grasp_contact: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
