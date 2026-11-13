#pragma once

#include "weapon/vec.hpp"

// A discarded magazine in flight (#47): it leaves the hand (or the gun) with that velocity, tumbles, falls
// under gravity, bounces softly on the floor once or twice and lies there a few seconds before it goes.
// Without a known floor (no STAGE space) it falls for fallback_seconds and goes, as before. Fake physics:
// no collision with the level. Engine-free, OpenXR LOCAL metres (y up); tested by tests/magazine_flight_test.cpp.
namespace bfvr::weapon
{
	struct flight_config
	{
		float gravity{ 9.81f };
		float max_speed{ 8.0f };         // a release faster than this is a tracking jump: clamped
		float bounce{ 0.25f };           // vertical speed kept at a floor hit
		float floor_friction{ 0.55f };   // horizontal speed kept at a floor hit
		float rest_speed{ 0.35f };       // below this at a floor hit it stops
		float rest_seconds{ 4.0f };      // lies on the floor this long
		float max_seconds{ 8.0f };       // gone after this in any case
		float fallback_seconds{ 0.6f };  // without a floor: falls this long, then gone
		float spin_per_speed{ 4.0f };    // tumble, radians per second per m/s of release speed
		float max_spin{ 14.0f };
		float magazine_half_length{ 0.08f }; // rests with its centre this far above the floor
	};

	class magazine_flight
	{
	public:
		explicit magazine_flight(const flight_config& config = {}) : config_(config) {}

		void start(const v3& position, const q4& rotation, const v3& velocity, bool floor_known, float floor_y);
		// Advances; false once the magazine is gone.
		bool update(float seconds);
		void stop() { active_ = false; }

		bool active() const { return active_; }
		bool resting() const { return resting_; }
		const v3& position() const { return position_; }
		const q4& rotation() const { return rotation_; }
		const v3& velocity() const { return velocity_; }

	private:
		flight_config config_;
		bool active_{};
		bool resting_{};
		bool floor_known_{};
		float floor_y_{};
		float age_{};
		float rest_age_{};
		v3 position_;
		v3 velocity_;
		q4 rotation_;
		v3 spin_axis_{ 1, 0, 0 };
		float spin_{};
	};
}
