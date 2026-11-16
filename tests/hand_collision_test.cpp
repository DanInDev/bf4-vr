// Checks the free hand kept out of the gun (src/body/hand_collision, #62): push-out to the surface, the side a
// hand came from kept through a fast slap, letting go when the controller is far beyond, and the gun's boxes.
#include <cmath>
#include <cstdio>

#include "body/hand_collision.hpp"

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

	constexpr ik::quat level{ 0, 0, 0, 1 };
	// A receiver-like slab: 5 cm wide, 4 cm high, 30 cm long, centred at the origin.
	const box slab{ { 0, 0, 0 }, level, { 0.025f, 0.02f, 0.15f } };

	ik::vec3 resolve_palm(hand_collider& collider, ik::vec3 palm, float radius = 0.04f)
	{
		const sphere spheres[1]{ { palm, radius } };
		return palm + collider.resolve(&slab, 1, spheres, 1);
	}

	void test_outside()
	{
		hand_collider collider;
		const ik::vec3 palm{ 0, 0.2f, 0 };
		const ik::vec3 at = resolve_palm(collider, palm);
		expect(near(at.y, 0.2f) && !collider.touching(), "a hand clear of the gun is left where it is");
	}

	void test_onto_surface()
	{
		hand_collider collider;
		resolve_palm(collider, { 0, 0.2f, 0 });
		resolve_palm(collider, { 0, 0.08f, 0 });
		const ik::vec3 at = resolve_palm(collider, { 0, 0.03f, 0 }); // 3 cm into the top (sphere 4 cm)
		expect(near(at.y, 0.06f), "lowered into the top, the palm rests on it (half height + radius)");
		expect(near(at.x, 0.0f) && near(at.z, 0.0f), "the push is along the top's normal only");
		expect(collider.touching(), "touching is reported");
		expect(near(collider.depth(), 0.03f), "the depth before the push is reported");
	}

	void test_slap_through()
	{
		hand_collider collider;
		resolve_palm(collider, { 0.0f, 0.1f, 0.05f });
		// One frame from above to below the slab, inside its outline: it came from the top, it stays on top.
		const ik::vec3 at = resolve_palm(collider, { 0.0f, -0.07f, 0.05f });
		expect(near(at.y, 0.06f), "a fast slap through the receiver ends on the side it came from");
	}

	void test_around()
	{
		hand_collider collider;
		resolve_palm(collider, { 0.0f, 0.1f, 0.0f });
		resolve_palm(collider, { 0.12f, 0.0f, 0.0f }); // beside it: now outside through its +x face
		const ik::vec3 at = resolve_palm(collider, { 0.0f, -0.1f, 0.0f });
		expect(near(at.y, -0.1f) && !collider.touching(), "a hand moved round the side may go under");
	}

	void test_give_up()
	{
		hand_collider collider;
		resolve_palm(collider, { 0.0f, 0.1f, 0.0f });
		// The controller far below: holding the drawn hand on top would stretch the arm; it is let through.
		const ik::vec3 at = resolve_palm(collider, { 0.0f, -0.35f, 0.0f });
		expect(near(at.y, -0.35f), "a push beyond give_up_through lets the hand through");
		const ik::vec3 next = resolve_palm(collider, { 0.0f, -0.07f, 0.0f });
		expect(near(next.y, -0.07f), "and the side is forgotten: from below it stays below");
	}

	void test_unknown_side()
	{
		hand_collider collider;
		const ik::vec3 at = resolve_palm(collider, { 0.0f, 0.0f, 0.14f }); // starts inside, near the front end
		expect(near(at.z, 0.19f) && near(at.y, 0.0f), "started inside: out through the nearest face");
	}

	void test_rotated()
	{
		hand_collider collider;
		const float half = 0.5f * 1.5707963f; // 90 degrees about z: the box's +y is world -x
		const box turned{ { 1, 0, 0 }, { 0, 0, std::sin(half), std::cos(half) }, { 0.025f, 0.02f, 0.15f } };
		const sphere far_out[1]{ { { 0.8f, 0, 0 }, 0.04f } };
		collider.resolve(&turned, 1, far_out, 1);
		const sphere in[1]{ { { 0.97f, 0, 0 }, 0.04f } };
		const ik::vec3 shift = collider.resolve(&turned, 1, in, 1);
		expect(near(0.97f + shift.x, 0.94f) && near(shift.y, 0.0f), "a turned box pushes along its own face");
	}

	void test_two_spheres()
	{
		hand_collider collider;
		const sphere above[2]{ { { 0, 0.2f, 0 }, 0.04f }, { { 0, 0.25f, 0.05f }, 0.03f } };
		collider.resolve(&slab, 1, above, 2);
		const sphere in[2]{ { { 0, 0.1f, 0 }, 0.04f }, { { 0, 0.03f, 0.05f }, 0.03f } }; // the knuckles 2 cm in
		const ik::vec3 shift = collider.resolve(&slab, 1, in, 2);
		expect(near(shift.y, 0.02f), "the deepest sphere decides, the whole hand moves");
	}

	void test_two_boxes()
	{
		hand_collider collider;
		const box boxes[2]{ slab, { { 0, 0, 0.3f }, level, { 0.025f, 0.025f, 0.15f } } };
		const sphere above[1]{ { { 0, 0.2f, 0.3f }, 0.04f } };
		collider.resolve(boxes, 2, above, 1);
		const sphere in[1]{ { { 0, 0.04f, 0.3f }, 0.04f } };
		const ik::vec3 shift = collider.resolve(boxes, 2, in, 1);
		expect(near(0.04f + shift.y, 0.065f), "the handguard pushes the hand onto its own top");
	}

	// The SCAR-H's receiver as built on the monitor (2026-11-16): half (0.025, 0.035, 0.149).
	const box receiver{ { 0, 0, 0 }, level, { 0.025f, 0.035f, 0.149f } };

	float palm_y_through_receiver(hand_collider& collider, float y)
	{
		const sphere palm[1]{ { { 0.0f, y, 0.0f }, 0.035f } };
		return y + collider.resolve(&receiver, 1, palm, 1).y;
	}

	void test_slap_through_receiver()
	{
		// Slaps from above that carry on 2, 8 and 23 cm past the receiver's far side (face -0.035, palm 3.5 cm).
		const float past[] = { 0.02f, 0.08f, 0.23f };
		const float expected[] = { 0.07f, 0.07f, -0.30f };
		for (int i = 0; i < 3; ++i)
		{
			hand_collider collider;
			palm_y_through_receiver(collider, 0.12f);
			const float end = -0.07f - past[i];
			const float at = palm_y_through_receiver(collider, end);
			expect(near(at, i < 2 ? expected[i] : end), i == 0 ? "a slap 2 cm through the receiver ends on top"
				: i == 1 ? "a slap 8 cm through the receiver ends on top" : "23 cm through: let go (the arm would stretch)");
		}
		hand_collider collider;
		palm_y_through_receiver(collider, 0.12f);
		expect(near(palm_y_through_receiver(collider, 0.0f), 0.07f), "slap frame 1: inside, back on top");
		expect(near(palm_y_through_receiver(collider, -0.15f), 0.07f), "slap frame 2: past, still on top");
	}

	void test_push_up_from_below()
	{
		hand_collider collider;
		palm_y_through_receiver(collider, -0.15f);
		expect(near(palm_y_through_receiver(collider, -0.05f), -0.07f), "pushed up into the bottom: held under it");
		expect(near(palm_y_through_receiver(collider, 0.03f), -0.07f), "past the middle: still under it");
		expect(near(palm_y_through_receiver(collider, 0.2f), -0.07f), "above the gun: still under it");
		expect(near(palm_y_through_receiver(collider, 0.3f), 0.3f), "far above: let go");
	}

	void test_gun_boxes()
	{
		// AR-like bones in the gun frame (+z muzzle, +y up): magazine top, gun hand at the grip, support hand.
		const gun_points points{ { 0, -0.02f, 0.10f }, { 0, -0.06f, -0.02f }, { 0.02f, -0.04f, 0.32f } };
		box boxes[hand_collider::max_boxes];
		const int count = gun_boxes(points, gun_shape{}, boxes);
		expect(count == 2, "a rifle has a receiver and a handguard");
		expect(signed_distance(boxes[0], { 0, 0.0f, 0.10f }) < 0.0f, "the receiver holds the space above the magwell");
		expect(signed_distance(boxes[0], { 0, -0.05f, 0.10f }) > 0.0f, "the magazine below the magwell is outside it");
		expect(signed_distance(boxes[1], { 0, points.magazine_top.y + gun_shape{}.barrel_above, 0.35f }) < 0.0f, "the handguard holds the barrel line at the support hand");
		expect(signed_distance(boxes[1], { 0, 0.02f, 0.60f }) > 0.0f, "the barrel beyond the handguard is free");
		const gun_points bullpup{ { 0, -0.02f, -0.10f }, { 0, -0.06f, 0.10f }, { 0.02f, -0.04f, 0.12f } };
		expect(gun_boxes(bullpup, gun_shape{}, boxes) >= 1, "odd layouts still give a receiver");
	}
}

int main()
{
	test_outside();
	test_onto_surface();
	test_slap_through();
	test_around();
	test_give_up();
	test_unknown_side();
	test_rotated();
	test_two_spheres();
	test_two_boxes();
	test_slap_through_receiver();
	test_push_up_from_below();
	test_gun_boxes();
	if (failures == 0)
	{
		std::printf("hand_collision: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
