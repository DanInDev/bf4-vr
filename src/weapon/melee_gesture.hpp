#pragma once

#include "stereo/tracking.hpp"

// Melee by hand (issue #15): a quick thrust or strike of the gun hand, forward along where the head
// faces, is the game's melee (pad right-stick click: the knife stab). Engine-free, OpenXR LOCAL;
// tested by tests/melee_test.cpp.
namespace bfvr::weapon
{
	struct melee_config
	{
		float forward_speed{ 2.5f }; // m/s of hand speed along the head's level forward that strikes
		float smoothing{ 0.03f };    // seconds; low-pass on the hand velocity against tracking noise
		float cooldown{ 0.8f };      // seconds between strikes (the stab animation is about that long)
		float rearm_speed{ 0.8f };   // the hand must slow below this before the next strike
	};

	class melee_gesture
	{
	public:
		explicit melee_gesture(const melee_config& config = {}) : config_(config) {}
		void configure(const melee_config& config) { config_ = config; }
		const melee_config& config() const { return config_; }
		// head: the eye midpoint pose; hand: the gun hand's grip position. busy: a grenade, reload step
		// or the settings panel owns the hands. Returns true on the frame that strikes.
		bool update(const stereo::pose& head, const float (&hand)[3], bool tracked, bool busy, float seconds);
		float forward_speed() const { return forward_speed_; }
		void reset();

	private:
		melee_config config_;
		bool has_last_{};
		float last_[3]{};
		float velocity_[3]{};
		float forward_speed_{};
		float cooldown_{};
		bool armed_{ true };
	};
}
