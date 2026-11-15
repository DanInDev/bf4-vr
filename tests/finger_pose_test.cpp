// Checks the finger poses (src/body/finger_pose): bend extraction and replacement keep the spread,
// shapes per task, the animated blend, and the smoother.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "body/finger_pose.hpp"

using namespace bfvr::body;
using namespace bfvr::body::fingers;

namespace
{
	int failures = 0;
	constexpr float degrees = 3.14159265f / 180.0f;

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

	ik::quat axis_angle(ik::vec3 axis, float angle)
	{
		const ik::vec3 a = ik::normalize(axis);
		const float s = std::sin(0.5f * angle);
		return { a.x * s, a.y * s, a.z * s, std::cos(0.5f * angle) };
	}

	float angle_between(ik::quat a, ik::quat b)
	{
		const float d = std::fabs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w);
		return 2.0f * std::acos(std::fmin(1.0f, d));
	}

	void test_bend()
	{
		// Live left middle knuckle on the AK handguard: q = (-0.014, -0.099, 0.519, 0.849).
		const ik::quat knuckle = ik::normalize({ -0.014f, -0.099f, 0.519f, 0.849f });
		expect(near(bend_of(knuckle), 62.6f * degrees, 0.5f * degrees), "bend of the live knuckle ~62.6 deg");
		const ik::quat pure = axis_angle({ 0, 0, 1 }, 40 * degrees);
		expect(near(bend_of(pure), 40 * degrees), "bend of a pure z rotation");
		// The right hand's quaternions come with w < 0 (same rotation): the bend is the same.
		const ik::quat negated{ -pure.x, -pure.y, -pure.z, -pure.w };
		expect(near(bend_of(negated), 40 * degrees), "bend ignores the quaternion's sign");

		// Replacing the bend keeps the spread: the swing part is unchanged.
		const ik::quat spread = axis_angle({ 0, 1, 0 }, 8 * degrees);
		const ik::quat joint = spread * axis_angle({ 0, 0, 1 }, 50 * degrees);
		const ik::quat rebent = with_bend(joint, 10 * degrees);
		expect(near(bend_of(rebent), 10 * degrees, 1e-3f), "with_bend sets the bend");
		expect(angle_between(rebent, spread * axis_angle({ 0, 0, 1 }, 10 * degrees)) < 1e-3f, "with_bend keeps the spread");
		expect(angle_between(with_bend(joint, bend_of(joint)), joint) < 1e-3f, "with_bend of its own bend is the identity");
	}

	void test_shapes()
	{
		finger_input none;
		// Racking with rack_grip fist (owner 2026-11-15): closer to a closed fist than the old handle hook.
		const hand_shape over = shape_for(task::fist_over, none);
		const hand_shape under = shape_for(task::fist_under, none);
		const hand_shape hook = shape_for(task::handle, none);
		bool closed = true;
		for (int f = index; f < count; ++f)
		{
			closed = closed && over.curl[f] >= 0.85f && under.curl[f] >= 0.85f && over.curl[f] >= hook.curl[f] && over.animated[f] == 0.0f;
		}
		expect(closed, "fist rack: index .. pinky curled at least as far as the hook, 0.85 or more, nothing animated");
		expect(under.curl[thumb] < over.curl[thumb], "fist rack: underhand keeps the thumb along the side");
		const hand_shape item = shape_for(task::item, none);
		for (int f = 0; f < count; ++f)
		{
			expect(item.animated[f] == 1.0f, "an item keeps the animated fingers");
		}
		const ik::quat animated = axis_angle({ 0, 0, 1 }, 60 * degrees);
		expect(angle_between(shaped(animated, middle, 0, item), animated) < 1e-6f, "animated shape leaves the joint alone");

		// The gun hand's index leaves the trigger only when the controller says the finger is off it.
		finger_input off_trigger{ true, 0.0f, 0.0f, false, true, true };
		const hand_shape grip = shape_for(task::gun_grip, off_trigger);
		expect(grip.animated[index] == 0.0f && grip.curl[index] < 0.1f, "off the trigger the index straightens");
		expect(grip.animated[middle] == 1.0f, "the other fingers keep the gun's grip");
		finger_input on_trigger = off_trigger;
		on_trigger.trigger_touch = true;
		expect(shape_for(task::gun_grip, on_trigger).animated[index] == 1.0f, "on the trigger the index stays animated");
		expect(shape_for(task::gun_grip, none).animated[index] == 1.0f, "without controller input the grip stays animated");
		finger_input no_sensors = off_trigger;
		no_sensors.touch_sensors = false;
		expect(shape_for(task::gun_grip, no_sensors).animated[index] == 1.0f, "without touch sensors the index stays on the trigger");

		// A free hand follows the squeeze; the index follows the trigger.
		finger_input open{ true, 0.0f, 0.0f, false, false, true };
		finger_input fist{ true, 1.0f, 1.0f, true, true, true };
		const hand_shape a = shape_for(task::free, open);
		const hand_shape b = shape_for(task::free, fist);
		expect(a.curl[middle] < 0.2f && b.curl[middle] > 0.9f, "squeeze curls the middle finger");
		expect(a.curl[index] < 0.2f && b.curl[index] > 0.85f, "trigger curls the index");
		expect(b.curl[thumb] > a.curl[thumb], "thumb touch curls the thumb");
		expect(a.animated[middle] == 0.0f, "a free hand is drawn, not animated");

		// Without touch sensors the index follows the trigger with no jump at the first pull.
		finger_input bare{ true, 0.0f, 0.0f, true, true, false };
		const float rest = shape_for(task::free, bare).curl[index];
		bare.trigger = 0.06f;
		const float first_pull = shape_for(task::free, bare).curl[index];
		expect(rest > 0.1f && first_pull - rest < 0.06f, "no touch sensors: the index rests half bent and moves smoothly");
		expect(shape_for(task::free, bare).curl[thumb] < 0.35f, "no touch sensors: a reported thumb touch is ignored");

		// Grips: wrapped around a magazine less than a fist, hooked harder on the handle, open to reach.
		const hand_shape magazine = shape_for(task::magazine, none);
		const hand_shape handle = shape_for(task::handle, none);
		const hand_shape reach = shape_for(task::reach, none);
		expect(magazine.curl[middle] > 0.4f && magazine.curl[middle] < 0.8f, "magazine grip is half closed");
		expect(handle.curl[index] > magazine.curl[index], "the handle hook is tighter than the magazine grip");
		expect(reach.curl[middle] < 0.1f, "reaching opens the hand");
		const hand_shape underhand = shape_for(task::underhand, none);
		expect(underhand.curl[index] > 0.5f && underhand.curl[middle] > 0.5f && underhand.curl[thumb] < 0.3f,
			"underhand rack: index and middle hooked, the thumb out of the way");
		expect(underhand.curl[ring] > underhand.curl[middle] && underhand.animated[index] == 0.0f, "the outer fingers tucked, nothing animated");

		// Bend range: open is nearly straight, a fist past the game's handguard grip (62 deg).
		expect(bend_at(middle, 0, 0.0f) < 10 * degrees && bend_at(middle, 0, 1.0f) > 80 * degrees, "knuckle range");
		expect(near(bend_of(shaped(animated, middle, 0, reach)), bend_at(middle, 0, reach.curl[middle])), "shaped joint takes the curl's bend");
		hand_shape half = reach;
		half.animated[middle] = 0.5f;
		const float mixed = bend_of(shaped(animated, middle, 0, half));
		expect(mixed > bend_at(middle, 0, reach.curl[middle]) && mixed < 60 * degrees, "a partly animated finger lies between");
		expect(bend_at(static_cast<finger>(count), 0, 1.0f) == 0.0f && bend_at(middle, joints, 1.0f) == 0.0f, "out of range is no bend");
	}

	void test_grasp()
	{
		// from_basis reproduces a known rotation from its axes.
		const ik::quat known = ik::normalize(axis_angle({ 0.3f, -0.8f, 0.5f }, 2.4f));
		const ik::quat rebuilt = from_basis(ik::rotate(known, { 1, 0, 0 }), ik::rotate(known, { 0, 1, 0 }), ik::rotate(known, { 0, 0, 1 }));
		expect(angle_between(known, rebuilt) < 1e-3f, "from_basis rebuilds a rotation");
		for (const ik::vec3 axis : { ik::vec3{ 1, 0, 0 }, ik::vec3{ 0, 1, 0 }, ik::vec3{ 0, 0, 1 } })
		{
			const ik::quat half_turn = axis_angle(axis, 3.14159265f);
			expect(angle_between(half_turn, from_basis(ik::rotate(half_turn, { 1, 0, 0 }), ik::rotate(half_turn, { 0, 1, 0 }), ik::rotate(half_turn, { 0, 0, 1 }))) < 1e-3f,
				"from_basis handles half turns");
		}

		// A fist pointing along -z (knuckles across x, index at +x), fingers curling down (-y), thumb
		// closing from below: an object's top comes out at the index side (+x), its front faces the curl.
		const ik::vec3 index_knuckle{ 0.04f, 0, 0 }, pinky_knuckle{ -0.03f, 0, 0 };
		const ik::vec3 middle_knuckle{ 0.01f, 0, 0 }, middle_tip{ 0.01f, -0.05f, -0.02f }, thumb_tip{ 0.03f, -0.04f, 0.01f };
		const grasp g = grasp_of(index_knuckle, pinky_knuckle, middle_knuckle, middle_tip, thumb_tip);
		expect(g.valid, "a fist gives a grasp");
		const ik::vec3 up = ik::rotate(g.frame, { 0, 1, 0 });
		const ik::vec3 out = ik::rotate(g.frame, { 0, 0, 1 });
		expect(near(up.x, 1.0f), "the held object's top points to the index");
		expect(near(ik::dot(out, ik::normalize(ik::vec3{ 0, -0.05f, -0.02f })), 1.0f, 1e-3f), "its front faces the curled fingers");
		expect(near(ik::dot(ik::rotate(g.frame, { 1, 0, 0 }), ik::cross(up, out)), 1.0f), "the frame is right-handed");
		expect(g.centre.y < 0.0f && g.centre.y > -0.05f, "the centre lies inside the curl");
		expect(!grasp_of(index_knuckle, index_knuckle, middle_knuckle, middle_tip, thumb_tip).valid, "no grasp without a knuckle line");
	}

	void test_mirror()
	{
		// A right hand: fingers (+x local) forward-down, palm (+y local) to the left, index (+z local) up.
		const ik::quat right = from_basis(ik::normalize(ik::vec3{ 0, -0.5f, -1 }), ik::vec3{ -1, 0, 0 },
			ik::cross(ik::normalize(ik::vec3{ 0, -0.5f, -1 }), ik::vec3{ -1, 0, 0 }));
		const ik::quat left = mirrored_hand(right);
		const ik::vec3 fingers = ik::rotate(left, { 1, 0, 0 });
		const ik::vec3 palm = ik::rotate(left, { 0, 1, 0 });
		const ik::vec3 index_side = ik::rotate(left, { 0, 0, -1 }); // the left index is at local -z
		const ik::vec3 right_index = ik::rotate(right, { 0, 0, 1 });
		expect(near(fingers.x, 0) && near(ik::dot(fingers, ik::normalize(ik::vec3{ 0, -0.5f, -1 })), 1.0f), "mirrored fingers point the same way");
		expect(near(palm.x, 1.0f), "the left palm faces right");
		expect(near(index_side.x, -right_index.x) && near(index_side.y, right_index.y) && near(index_side.z, right_index.z), "the index side mirrors");
		expect(angle_between(mirrored_hand(left), right) < 1e-3f, "mirroring twice is the identity");
	}

	void test_smoother()
	{
		shape_smoother smoother;
		hand_shape open{};
		for (float& a : open.animated) a = 0.0f;
		hand_shape closed = open;
		for (float& c : closed.curl) c = 1.0f;
		expect(smoother.update(open, 0.0f).curl[middle] == 0.0f, "the first update takes the target");
		const float after_one = smoother.update(closed, 0.011f).curl[middle];
		expect(after_one > 0.2f && after_one < 0.8f, "one frame moves part of the way");
		smoother.update(closed, 0.011f);
		smoother.update(closed, 0.011f);
		smoother.update(closed, 0.011f);
		expect(smoother.current().curl[middle] > 0.9f, "closing (onto the trigger, a grab) is 90 % there in about 45 ms");
		for (int i = 0; i < 60; ++i)
		{
			smoother.update(closed, 0.011f);
		}
		expect(near(smoother.current().curl[middle], 1.0f, 1e-3f), "settles in well under a second");

		// Opening is slower than closing.
		const float opened = smoother.update(open, 0.011f).curl[middle];
		expect(1.0f - opened < after_one, "opening moves less per frame than closing");
		hand_shape back_on = open;
		back_on.animated[index] = 1.0f;
		const float on = smoother.update(back_on, 0.011f).animated[index];
		expect(on > 0.4f, "returning to the animated grip (the trigger) is a closing move");

		// Frame-rate independent: four 90 Hz frames land where two 45 Hz frames do.
		shape_smoother fast, slow;
		fast.update(open, 0.0f);
		slow.update(open, 0.0f);
		for (int i = 0; i < 4; ++i) fast.update(closed, 1.0f / 90.0f);
		for (int i = 0; i < 2; ++i) slow.update(closed, 1.0f / 45.0f);
		expect(near(fast.current().curl[middle], slow.current().curl[middle], 1e-4f), "same result at any frame rate");
	}
}

// Pose lab: the hand shaped by the panel's curls only, nothing of the animation; values clamped.
int main()
{
	test_bend();
	test_shapes();
	test_grasp();
	test_mirror();
	test_smoother();
	if (failures == 0)
	{
		std::printf("finger_pose: all tests passed\n");
	}
	return failures == 0 ? 0 : 1;
}
