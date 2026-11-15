#pragma once

#include "body/arm_ik.hpp"

// Cutscenes (stereo/scripted_view): while the game animates the first-person body itself, the arms, gun and
// fingers stay as it animates them; the mod's pose (controllers, body turn, finger shapes) eases out as a
// scene starts and back in after it. Engine-free; tested by tests/scene_blend_test.cpp.
namespace bfvr::body
{
	class scene_blend
	{
	public:
		// scripted: a cutscene (or the death fall) this frame. Returns how much of the mod's pose to draw:
		// 1 in play, 0 in a scene, eased between with the time constant (seconds).
		float update(bool scripted, float seconds, float time_constant = 0.15f);
		float weight() const { return weight_; }

	private:
		float weight_{ 1.0f };
	};

	// Blends each bone of edited back towards animated by (1 - weight): weight 1 keeps edited, 0 gives animated.
	void blend_pose(ik::bone_transform* edited, const ik::bone_transform* animated, int count, float weight);
}
