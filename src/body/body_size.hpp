#pragma once

#include <cmath>

// Body size (#50): where the shoulders, chest pouches and hip holsters sit for this player, from their
// height and any measure set by hand. The zones and the shoulder anchor are laid out for a reference
// body of 1.78 m; a measure left at 0 follows the height. Engine-free, header-only; tested by
// tests/body_calibration_test.cpp.
namespace bfvr::body
{
	inline constexpr float reference_stature = 1.78f;
	inline constexpr float reference_shoulder_drop = 0.22f; // shoulder joints below the eyes
	inline constexpr float reference_chest_depth = 0.10f;   // chest pouches and knife in front of the eyes
	inline constexpr float reference_hip_drop = 0.75f;      // hip holsters below the eyes
	// Zones lower than this below the eyes are hip zones (the holsters at -0.75); the belly sling (-0.50)
	// and the chest pouches are not.
	inline constexpr float hip_zone_line = -0.6f;

	// Live settings: height in metres (0 = not calibrated: the reference body), the rest in metres with
	// 0 = from the height.
	struct size_settings
	{
		float height{};
		float shoulder_drop{};
		float chest_depth{};
		float hip_drop{};
	};

	struct body_size
	{
		float scale{ 1.0f }; // height against the reference: widths, the over-shoulder zones
		float shoulder_drop{ reference_shoulder_drop };
		float chest_depth{ reference_chest_depth };
		float hip_drop{ reference_hip_drop };
	};

	inline body_size size_for(const size_settings& settings)
	{
		body_size size;
		const float ratio = settings.height / reference_stature;
		size.scale = std::isfinite(ratio) && ratio > 0.5f && ratio < 1.5f ? ratio : 1.0f;
		const auto pick = [&](float manual, float reference, float low, float high) {
			return std::isfinite(manual) && manual >= low && manual <= high ? manual : reference * size.scale;
		};
		size.shoulder_drop = pick(settings.shoulder_drop, reference_shoulder_drop, 0.05f, 0.45f);
		size.chest_depth = pick(settings.chest_depth, reference_chest_depth, 0.02f, 0.30f);
		size.hip_drop = pick(settings.hip_drop, reference_hip_drop, 0.40f, 1.10f);
		return size;
	}
}
