#pragma once

#include <string>

// Life-size gun through the gun pass's own FOV (#39, re/notes/first_person.md "The gun pass FOV, found").
// The game draws the gun and arms with their own projection: at the hip the per-player view's +0x13D0
// (45 deg), while aiming the current weapon's runtime zoom levels' RenderFov (17 deg on the AK-12 red dot),
// blended in 0x140FB8290. Setting both to the life-size FOV keeps the first-person camera orthonormal, so
// the sights' lens and reticle draw as the game means them (the old camera squeeze lost the red dot).
// Game thread (the camera hook). BF4 build 179547, x64 only.
namespace bfvr::stereo::gun_pass_fov
{
	// Writes the gun pass FOV (half vertical, radians) where the game reads it; remembers the game's values.
	// False when the per-player view is not reachable (then nothing is written).
	bool apply(float half_fov);
	// Puts the game's values back (safe to call every frame).
	void restore();
	std::string report();
}
