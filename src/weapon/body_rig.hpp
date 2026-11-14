#pragma once

#include "body/body_size.hpp"
#include "weapon/reload_hands.hpp"

// The off hand's pouches on the chest rig (user layout, 2026-11-04), in the same lazy body frame as
// the holsters (weapon/holsters): looking down at a pouch does not move it. Engine-free; OpenXR LOCAL
// metres. Tested by tests/reload_test.cpp.
namespace bfvr::weapon
{
	// Body frame: x toward the gun side, y up, z back; from the head (between the eyes).
	constexpr v3 grenade_pouch{ 0.11f, -0.27f, -0.10f };  // high chest, gun side (the off hand reaches across)
	constexpr v3 magazine_pouch{ -0.04f, -0.47f, -0.10f }; // low chest, a little to the off side
	// #57: the magazine zone reaches from the pouch down to the off-side hip (a capsule): the hip is free
	// since gadget 2 moved over the off-side shoulder.
	constexpr v3 magazine_hip{ -0.22f, -0.75f, 0.0f };
	// Where a let-go gun hangs on its sling (#38): belly height, gun side, a hand's width in front.
	constexpr v3 sling_point{ 0.10f, -0.50f, -0.12f };

	// A body-frame offset laid out for the reference body, fitted to this one (#46, #50): widths and the
	// over-shoulder zones scale with the height; a hip zone sits at hip_drop, a zone in front of the eyes
	// at chest_depth (in proportion to the reference).
	v3 sized_offset(const v3& offset, const body::body_size& size);

	// Where the eyes would be with the head level (headset 2026-11-14: looking down at a pouch or the hip
	// tipped the eyes ~5-10 cm forward and down about the neck, and every zone with them). The neck pivot
	// sits eye_above_neck below and eye_ahead_of_neck behind the eyes; the head's pitch and roll are taken
	// out about it, its yaw kept. head_rotation: the eyes' orientation (y up, -z forward).
	constexpr float eye_above_neck = 0.11f;
	constexpr float eye_ahead_of_neck = 0.09f;
	v3 level_head(const v3& eyes, const q4& head_rotation);

	// body_yaw: radians, 0 = facing -z, + = turned left (holsters::body_yaw). Mirrored when left-handed.
	v3 body_point(const v3& head, float body_yaw, const v3& offset, bool left_handed, const body::body_size& size = {});
}
