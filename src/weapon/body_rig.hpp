#pragma once

#include "weapon/reload_hands.hpp"

// The off hand's pouches on the chest rig (user layout, 2026-11-04), in the same lazy body frame as
// the holsters (weapon/holsters): looking down at a pouch does not move it. Engine-free; OpenXR LOCAL
// metres. Tested by tests/reload_test.cpp.
namespace bfvr::weapon
{
	// Body frame: x toward the gun side, y up, z back; from the head (between the eyes).
	constexpr v3 grenade_pouch{ 0.11f, -0.27f, -0.10f };  // high chest, gun side (the off hand reaches across)
	constexpr v3 magazine_pouch{ -0.04f, -0.47f, -0.10f }; // low chest, a little to the off side

	// body_yaw: radians, 0 = facing -z, + = turned left (holsters::body_yaw). Mirrored when left-handed.
	v3 body_point(const v3& head, float body_yaw, const v3& offset, bool left_handed);
}
