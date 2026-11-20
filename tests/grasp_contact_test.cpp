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

		// Racking hold: palm up under the handle hooks, palm down on it pinches, sideways keeps the last.
		expect(underhand_rack(false, 0.8f), "palm up takes the handle underhand");
		expect(!underhand_rack(true, -0.8f), "palm down takes it overhand");
		expect(underhand_rack(true, 0.0f) && !underhand_rack(false, 0.0f), "a sideways palm keeps the last stroke's hold");
		expect(underhand_rack(true, std::nanf("")) && !underhand_rack(false, std::nanf("")), "no reading keeps the hold");
	}

	// Palming: the held magazine turns aside about the barrel axis through its body.
	void test_tilt()
	{
		ik::vec3 top{ 0, 0, 0 };
		ik::quat rotation{};
		const float angle = 40.0f * 0.017453293f;
		tilt_magazine(top, rotation, {}, angle, 0.07f);
		expect(near(top, { 0.07f * std::sin(angle), 0.07f * (std::cos(angle) - 1.0f), 0.0f }), "its top swings towards the gun's left (+x) about the pivot 7 cm down");
		expect(near(ik::rotate(rotation, { 0, 1, 0 }), { std::sin(angle), std::cos(angle), 0.0f }), "it leans the same way");
		expect(near(ik::rotate(rotation, { 0, 0, 1 }), { 0, 0, 1 }), "the barrel axis stays");
		tilt_magazine(top, rotation, {}, -angle, 0.07f);
		expect(near(top, { 0, 0, 0 }) && near(ik::rotate(rotation, { 0, 1, 0 }), { 0, 1, 0 }), "turning back undoes it");
		ik::vec3 still{ 1, 2, 3 };
		ik::quat same{};
		tilt_magazine(still, same, {}, 0.0f, 0.07f);
		expect(near(still, { 1, 2, 3 }), "no angle, no turn");
	}

	// #40: the palmed pair. The caught magazine sits beside the new one, parallel, and their bodies do not
	// cross; untilted where it seated, the old one would cross the leaning new one (the headset report).
	void test_palmed_pair()
	{
		const magazine_size size;
		const box upright = magazine_box({ 0, 0, 0 }, {}, {}, size);
		expect(near(upright.centre, { 0.0f, -0.5f * size.height, 0.0f }) && near(upright.half.y, 0.5f * size.height), "the body hangs below the top");

		const float angle = 40.0f * 0.017453293f;
		ik::vec3 top{ 0, 0, 0 };
		ik::quat rotation{};
		tilt_magazine(top, rotation, {}, angle, 0.07f);
		const box held = magazine_box(top, rotation, {}, size);
		ik::vec3 caught_top = top;
		beside_magazine(caught_top, rotation, {}, size, 0.006f);
		const box caught = magazine_box(caught_top, rotation, {}, size);
		const ik::vec3 apart = caught.centre - held.centre;
		expect(near(ik::length(apart), size.width + 0.006f, 1e-4f), "one width plus the gap apart");
		expect(near(ik::dot(apart, ik::rotate(rotation, { 0, 1, 0 })), 0.0f, 1e-4f), "side by side, not one above the other");
		expect(ik::dot(apart, ik::rotate(rotation, { 1, 0, 0 })) < 0.0f, "on the inside, away from the lean");
		// Sample the caught body's centre line: every point is outside the held body.
		bool clear = true;
		for (int i = 0; i <= 10; ++i)
		{
			const ik::vec3 point = caught_top + ik::rotate(rotation, { 0.0f, -size.height * static_cast<float>(i) / 10.0f, 0.0f });
			clear = clear && signed_distance(held, point) > 0.0f;
		}
		expect(clear, "the caught body stays clear of the held one");
		bool crosses = false;
		for (int i = 0; i <= 10; ++i)
		{
			const ik::vec3 point = ik::vec3{ 0.0f, -size.height * static_cast<float>(i) / 10.0f, 0.0f };
			crosses = crosses || signed_distance(held, point) < 0.0f;
		}
		expect(crosses, "the old placement (untilted at the seat) did cross it");
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
		const ik::vec3 index{ 0.1f, 0.0f, 0.0f };
		// Live (2026-11-03): the captured pinch leaves the thumb 4.2 cm from the index side.
		const ik::vec3 thumb{ 0.1f, 0.042f, 0.0f };
		const ik::vec3 at = pinch_point(index, thumb, knob, radius);
		expect(near(at, { 0.1f, 0.021f, 0.0f }), "the knob sits centred between the index and the thumb");
		const box handle{ at, {}, { knob, knob, knob } };
		expect(near(signed_distance(handle, index), signed_distance(handle, thumb)), "with the same room on both sides");
		const ik::vec3 pressed{ 0.1f, 0.02f, 0.0f };
		const ik::vec3 tight = pinch_point(index, pressed, knob, radius);
		expect(near(tight, { 0.1f, knob + radius, 0.0f }), "a tighter thumb than the knob: it rests on the index");
		expect(near(signed_distance(box{ tight, {}, { knob, knob, knob } }, index), radius), "the index's side touches it (one finger radius off its surface)");
		expect(near(pinch_point(index, index, knob, radius), index), "a thumb on the index gives the index itself");
	}
}

static void test_rack_fist_offset()
{
	const contact::rack_fist_offsets offsets;
	const ik::vec3 left_over = contact::rack_fist_offset(false, 1.0f, offsets);
	expect(left_over.x == 0.005f && left_over.y == 0.005f && left_over.z == 0.0f, "rack fist: overhand just out on a left-side handle (SCAR-H) and just up");
	const ik::vec3 right_over = contact::rack_fist_offset(false, -1.0f, offsets);
	expect(right_over.x == -0.005f && right_over.y == 0.005f, "rack fist: just out to the right for a right-side handle (AK)");
	const ik::vec3 under = contact::rack_fist_offset(true, -1.0f, offsets);
	expect(under.x == -0.005f && under.y == -0.005f, "rack fist: underhand just out and below the knob");
	const ik::vec3 centred = contact::rack_fist_offset(false, 0.0f, offsets);
	expect(centred.x == 0.0f && centred.y == 0.005f, "rack fist: a centred handle moves the fist only up");
	expect(contact::rack_fist_offset(false, 0.4f, offsets).x == 0.005f, "rack fist: any positive side is the left");
	// #87: the grasp centre within 1 cm of the knob (the owner's marks: 5 cm out and above).
	const float reach = std::sqrt(left_over.x * left_over.x + left_over.y * left_over.y);
	const float under_reach = std::sqrt(under.x * under.x + under.y * under.y);
	expect(reach < 0.01f && under_reach < 0.01f, "rack fist: the grasp centre within 1 cm of the knob, over and under");
}

static void test_support_slide()
{
	contact::support_slide slide;
	const float rest = 0.5f;
	expect(slide.update(true, 0.55f, rest, 0.06f, 0.08f, true) == 0.55f && slide.latched(), "support: the hold takes the place it grabbed");
	expect(slide.update(true, 0.62f, rest, 0.06f, 0.08f, true) == 0.55f, "support: locked, the controller moving forward does not slide the hand");
	expect(slide.update(true, 0.40f, rest, 0.06f, 0.08f, true) == 0.55f, "support: nor moving back");
	expect(!(slide.update(false, 0.40f, rest, 0.06f, 0.08f, true) == 0.55f) && !slide.latched(), "support: let go, the lock ends");
	expect(slide.update(true, 0.80f, rest, 0.06f, 0.08f, true) == 0.58f, "support: a grab 30 cm ahead (on the barrel) is held at the range's front");
	slide.update(false, 0.0f, rest, 0.06f, 0.08f, true);
	expect(slide.update(true, 0.30f, rest, 0.06f, 0.08f, true) == 0.44f, "support: a grab far back is held at the range's back");
	expect(slide.update(true, 0.30f, rest, 0.02f, 0.08f, true) == 0.48f, "support: a smaller range set while held still bounds it");
	contact::support_slide free;
	expect(free.update(true, 0.55f, rest, 0.06f, 0.08f, false) == 0.55f && free.update(true, 0.57f, rest, 0.06f, 0.08f, false) == 0.57f,
		"support: unlocked it follows the controller within the range");
	expect(free.update(true, NAN, rest, 0.06f, 0.08f, false) == rest, "support: a broken reading gives the rest spot");
}

int main()
{
	test_support_slide();
	test_rack_fist_offset();
	test_pinch();
	test_coupling();
	test_distance();
	test_cup();
	test_switch();
	test_tilt();
	test_palmed_pair();
	test_closing();
	if (failures == 0)
	{
		std::printf("grasp_contact: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
