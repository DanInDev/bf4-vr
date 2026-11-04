#pragma once

// Walking sway (steady_walk): the game's walk animation swings the first-person camera about 6 cm to
// each side at the step rate (0.9 Hz, monitor 2026-11-04), which sways the whole world in a headset.
// The filter keeps the slow part of the eye's sideways offset (stance, leaning into a cover) and
// returns the fast part to remove. Engine-free; tested by tests/steady_camera_test.cpp.
namespace bfvr::stereo
{
	class sway_filter
	{
	public:
		// offset: the eye's sideways offset in soldier space (metres); seconds since the last call.
		// Returns the part to remove from the camera: offset - its low-passed value.
		float update(float offset, float seconds);
		void reset();

		static constexpr float time_constant = 2.0f; // 0.9 Hz sway: about 9 % left
		static constexpr float jump = 0.15f;         // a larger step is a cut (teleport, vehicle): follow at once

	private:
		bool primed_{};
		float steady_{};
	};
}
