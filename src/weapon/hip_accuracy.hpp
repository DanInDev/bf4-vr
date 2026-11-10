#pragma once

#include <cstddef>
#include <string>

// hip_accuracy (#39): the game spreads hip-fire shots over a 3-7 degree cone (GunSwayData NoZoom
// dispersion), aiming down sights over 0.2-1.7. With sights live without the game's aim, the hip
// values of the player's weapons are blended towards their Zoom values: 1 = as accurate as ADS,
// 0 = the game's. The originals are kept and written back when the setting goes to 0.
namespace bfvr::weapon
{
	// GunSwayDispersionData: MinAngle, MaxAngle, IncreasePerShot, DecreasePerSecond,
	// NoFireDecreasePerSecond, NoFireTimeThreshold.
	constexpr std::size_t dispersion_floats = 6;

	// Engine-free: hip blended towards zoom by strength (clamped to 0..1), field by field.
	void blend_dispersion(const float (&hip)[dispersion_floats], const float (&zoom)[dispersion_floats], float strength,
		float (&out)[dispersion_floats]);

	void set_hip_accuracy(float strength);
	float hip_accuracy_strength();
	// Present thread, once per frame: patches newly seen weapons, follows a changed strength.
	void update_hip_accuracy();
	// Pipe "hipaccuracy": strength, patched weapons, the current weapon's standing hip and zoom angles.
	std::string hip_accuracy_report();
}
