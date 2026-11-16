#pragma once

#include "body/grasp_contact.hpp"

// #62: the free hand never goes into the gun. The hand is a few spheres (palm, knuckles), the gun a few oriented
// boxes (receiver, handguard) built from its own bones; a hand that enters a box is pushed out to its surface.
// Engine-free; tested by tests/hand_collision_test.cpp.
namespace bfvr::body::contact
{
	struct sphere
	{
		ik::vec3 centre;
		float radius{};
	};

	class hand_collider
	{
	public:
		static constexpr int max_boxes = 4;

		// The shift that moves the whole hand (every sphere together) out of the boxes, box by box. Each box
		// remembers the face the hand was last outside of and pushes out through that face, so a hand moved
		// through a thin part within one frame (a slap) ends on the side it came from, while a hand moved round
		// the box may come out anywhere. A push longer than give_up lets the hand through (the controller is far
		// beyond: holding the drawn hand back would stretch the arm) and forgets the side; a hand that went through
		// from a known side is held back up to give_up_through (a slap carries on past the gun).
		ik::vec3 resolve(const box* boxes, int box_count, const sphere* spheres, int sphere_count, float give_up = 0.15f, float give_up_through = 0.35f);
		void reset();

		bool touching() const { return touching_; } // the last resolve pushed the hand
		float depth() const { return depth_; }      // the deepest push of the last resolve (metres)

	private:
		int side_[max_boxes]{}; // 0: not known; +-(axis + 1): outside through that face
		bool touching_{};
		float depth_{};
	};

	// Points of the gun in its own frame (Wep_Root: +x left, +y up, +z towards the muzzle): the magazine bone (the
	// top of the magazine, in the magwell), the gun hand's wrist on the grip and the support hand's wrist as the
	// game animates them.
	struct gun_points
	{
		ik::vec3 magazine_top;
		ik::vec3 grip;
		ik::vec3 support;
	};

	// The parts' sizes (metres) around those points.
	struct gun_shape
	{
		float half_width{ 0.025f };      // both parts, across
		float receiver_height{ 0.07f };  // from the magazine top up
		float receiver_back{ 0.02f };    // behind the gun hand's wrist
		float receiver_ahead{ 0.08f };   // ahead of the magazine top (where the handguard starts)
		float barrel_above{ 0.035f };    // the handguard's centre line over the magazine top
		float handguard_half_height{ 0.025f };
		float handguard_beyond{ 0.10f }; // ahead of the support hand's wrist
	};

	// The receiver and, where the support hand is ahead of it, the handguard, in the gun frame. Returns the count.
	int gun_boxes(const gun_points& points, const gun_shape& shape, box (&boxes)[hand_collider::max_boxes]);
}
