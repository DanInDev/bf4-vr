#pragma once

#include "weapon/vec.hpp"

// Things let go of (#47, #61): a rigid box that flies with the hand's speed and spin, falls, bounces,
// slides, rolls and tips onto a flat side on the ground below each of its corners, then rests. The
// ground comes from a ground_source (the game's own ray cast in the game, planes in tests). World
// metres, y up. The pose the caller draws is a point on the body and its rotation (for a magazine: its
// top, Wep_Mag, and the bone's rotation); the box sits at an offset from it. After the BF3 single-player
// VR mod's idea (ideas only, our own code). Engine-free; tested by tests/loose_body_test.cpp.
namespace bfvr::physics
{
	using weapon::q4;
	using weapon::v3;

	// The ground under a point: the surface the first ray down from above it met.
	struct ground_plane
	{
		enum class kind { unknown, none, plane } what{ kind::unknown };
		v3 point;
		v3 normal{ 0, 1, 0 };
	};

	// Answers without waiting; unknown until it knows (the game's ray is cast on another thread).
	class ground_source
	{
	public:
		virtual ~ground_source() = default;
		virtual ground_plane below(const v3& point) = 0;
	};

	// The box in the pose's frame: centre and axes, half extents along them.
	struct loose_shape
	{
		v3 centre;
		q4 axes;
		v3 half{ 0.015f, 0.095f, 0.04f };
		float mass{ 0.5f };
	};

	struct loose_config
	{
		float gravity{ 9.81f };
		float max_speed{ 8.0f };      // faster at release is a tracking jump: clamped
		float max_spin{ 25.0f };      // radians per second
		float restitution{ 0.25f };   // bounce, for impacts faster than bounce_speed
		float bounce_speed{ 0.6f };
		float friction{ 0.55f };
		float air_drag{ 0.1f };       // per second, linear and angular
		float contact_damping{ 2.5f }; // per second, angular, while touching
		float rest_speed{ 0.08f };    // slower than this ...
		float rest_spin{ 0.8f };      // ... and turning slower than this ...
		float rest_settle{ 0.25f };   // ... for this long, touching: at rest
		float lie_seconds{ 30.0f };   // at rest this long, then gone once not seen (the BF3 user's rule)
		float max_fall_seconds{ 4.0f }; // nothing below (or never answered): gone after this
		float substep{ 1.0f / 240.0f };
	};

	class loose_body
	{
	public:
		explicit loose_body(const loose_config& config = {}) : config_(config) {}

		// pose_position / pose_rotation: the drawn pose at release; velocity of the pose point; spin
		// (angular velocity, radians per second, world axes).
		void start(const v3& pose_position, const q4& pose_rotation, const v3& velocity, const v3& spin, const loose_shape& shape);
		// Advances by seconds. fallback: a floor height used under corners the ground does not know yet
		// (NaN: none). seen: in the player's view (it never goes while seen). False once it is gone.
		bool update(float seconds, ground_source& ground, float fallback_y, bool seen);
		void stop() { active_ = false; }

		bool active() const { return active_; }
		bool resting() const { return resting_; }
		bool touching() const { return touching_; }
		float age() const { return age_; }
		v3 pose_position() const;
		q4 pose_rotation() const { return rotation_; }
		const v3& centre() const { return centre_; }
		const v3& velocity() const { return velocity_; }
		const v3& spin() const { return spin_; }
		int bounces() const { return bounces_; }
		// The lowest corner's height above the ground under it (negative inside), last step.
		float clearance() const { return clearance_; }

	private:
		void step(float dt, ground_source& ground, float fallback_y);

		loose_config config_;
		loose_shape shape_;
		bool active_{};
		bool resting_{};
		bool touching_{};
		bool ever_known_{};
		float age_{};
		float still_for_{};
		float rest_age_{};
		float clearance_{};
		int bounces_{};
		v3 centre_;
		q4 rotation_;
		v3 velocity_;
		v3 spin_;
	};
}
