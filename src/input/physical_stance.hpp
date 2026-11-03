#pragma once

// Physical stance (#33): crouching or lying down in the room changes the game's stance. The head's
// height (OpenXR LOCAL y) is measured against the highest it has been since the start (standing);
// drops past the thresholds, held for a moment, ask for crouch or prone, with hysteresis on the way
// back. Only changes are reported, so stance flicks on the stick keep working in between.
// Engine-free; tested by tests/physical_stance_test.cpp.
namespace bfvr::input
{
	struct physical_stance_config
	{
		float crouch_drop{ 0.30f };   // head this far below standing: crouch ...
		float stand_drop{ 0.20f };    // ... and back above this: stand
		float prone_drop{ 0.85f };    // lying down
		float unprone_drop{ 0.70f };  // getting up from prone (to crouch, or stand above stand_drop)
		float hold_seconds{ 0.25f };  // a new stance must hold this long
	};

	struct physical_stance_result
	{
		int stance{};      // 0 stand, 1 crouch, 2 prone (the game's numbering)
		bool changed{};    // this update moved to it
		float drop{};      // metres below the standing reference
	};

	class physical_stance
	{
	public:
		explicit physical_stance(const physical_stance_config& config = {}) : config_(config) {}
		physical_stance_result update(float head_height, float seconds);
		void reset();
		int stance() const { return stance_; }

	private:
		int wanted(float drop) const;

		physical_stance_config config_;
		bool calibrated_{};
		float reference_{};
		int stance_{};
		int pending_{};
		float timer_{};
	};
}
