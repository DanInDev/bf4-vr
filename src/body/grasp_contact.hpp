#pragma once

#include "body/arm_ik.hpp"

// Grips that touch what they hold (#14, after Overlord's two magazine grasps): an object as a simple
// shape, a magazine cupped on the palm, and fingers that close until they reach its surface instead
// of a fixed curl. Engine-free; tested by tests/grasp_contact_test.cpp.
namespace bfvr::body::contact
{
	// An oriented box: centre, rotation (its axes), half extents along them.
	struct box
	{
		ik::vec3 centre;
		ik::quat frame;
		ik::vec3 half;
	};

	// Distance from the box's surface, negative inside.
	float signed_distance(const box& shape, ik::vec3 point);

	// The smallest curl in [from, to] at which touching(curl) holds, by bisection; `to` when it never
	// does, `from` when it already does. touching must be monotonic (a finger that touches keeps
	// touching as it closes further).
	template <typename Touching>
	float close_until(Touching touching, float from, float to, int steps = 12)
	{
		if (touching(from))
		{
			return from;
		}
		if (!touching(to))
		{
			return to;
		}
		float open = from, closed = to;
		for (int i = 0; i < steps; ++i)
		{
			const float middle = 0.5f * (open + closed);
			if (touching(middle))
			{
				closed = middle;
			}
			else
			{
				open = middle;
			}
		}
		return closed;
	}

	// Fingers index..pinky (1..4 of 5; 0 is the thumb) that touched nothing close no further than the
	// most-closed finger that did, plus slack: real fingers move together, so an outer finger that
	// passes beside a narrow object curls with its neighbours instead of into a fist.
	void couple_free_fingers(float (&curl)[5], const bool (&touched)[5], float slack = 0.1f);

	// A magazine's body (metres): across, from the top (Wep_Mag, the magwell end) to the base plate,
	// front to back.
	struct magazine_size
	{
		float width{ 0.03f };
		float height{ 0.19f };
		float depth{ 0.075f };
	};

	// A magazine standing on the palm (Overlord's bottom grasp): its base plate on the palm, its front
	// towards the fingertips. frame is where the gun's frame would be for the magazine to sit as it
	// does in the gun (+y away from the palm, +z along the fingers): the magazine bone's rotation is
	// frame * (its rotation relative to Wep_Root). top: the magazine bone (top of the body).
	struct cup_placement
	{
		bool valid{};
		ik::quat frame;
		ik::vec3 top;
		box body;
	};
	cup_placement cup_magazine(ik::vec3 palm_centre, ik::vec3 palm_normal, ik::vec3 finger_direction, const magazine_size& size, float palm_gap = 0.012f);

	// A lateral (key) pinch on a charging handle: the knob between the side of the index's middle
	// segment and the thumb tip, centred, but never closer to the index than resting on it (a thumb
	// pressed tighter than the knob is wide). index_side: the middle of Index2-Index3; thumb_tip: Thumb4;
	// knob_half: the knob's half size. Returns where the knob's centre belongs (the hand is placed so
	// this lands on the handle); the index side itself when the thumb sits on it.
	ik::vec3 pinch_point(ik::vec3 index_side, ik::vec3 thumb_tip, float knob_half, float finger_radius);

	// Cup or wrap: cup once the palm faces up past `enter` (palm normal . up), back to the wrap below
	// `leave`, so a hand near the switch does not flicker.
	bool cup_grip(bool cupped, float palm_up, float enter = 0.5f, float leave = 0.15f);

	// rack_grip = auto: how the hand takes the charging handle, decided as it closes on it and kept for
	// the stroke. Underhand (palm up under the handle, the knob hooked in the curled index and middle)
	// once the palm faces up past `enter` (palm normal . the gun's up), overhand (palm down on it, the
	// thumb-and-index pinch) below `leave`; a hand in between keeps the last stroke's hold.
	bool underhand_rack(bool underhand, float palm_up, float enter = 0.2f, float leave = -0.2f);

	// Palming (weapon/reload_hands, same formula as weapon::tilt_magazine): a magazine held at top and
	// rotation, turned by angle radians about the barrel axis of the gun frame it would sit in, through
	// its body pivot_drop below the top. seated: its rotation relative to that frame (Wep_Root, +z along
	// the barrel, +y up). Positive turns the top towards the gun's left (+x).
	void tilt_magazine(ik::vec3& top, ik::quat& rotation, ik::quat seated, float angle, float pivot_drop);

	// A held magazine's body (top: the magazine bone, rotation: its bone rotation, seated: its rotation
	// relative to the gun frame): it hangs below the top along the gun frame's -y.
	box magazine_box(ik::vec3 top, ik::quat rotation, ik::quat seated, const magazine_size& size);

	// #40, palming: the caught magazine held beside the new one, parallel to it, one width plus gap towards
	// the gun frame's -x (the inside, away from where the new one leans), so the two bodies never cross.
	void beside_magazine(ik::vec3& top, ik::quat rotation, ik::quat seated, const magazine_size& size, float gap);
}
