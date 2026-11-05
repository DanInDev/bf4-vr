#pragma once

// Torso yaw from the tracked head and hands (#13). The game body only turns with the stick; a player
// who turns in the room (or looks far to the side) would otherwise keep the shoulders facing the old
// way while the hands reach across the chest. The torso follows the head (dragged past max_twist, settling
// onto it over settle_seconds) and leans towards two hands held together in front (a two-handed gun);
// never more than max_twist from the head. Engine-free; tested by
// tests/torso_test.cpp. Angles in radians about the tracking space's up axis, left (counter-clockwise
// seen from above) positive, 0 = the body's forward.
namespace bfvr::body
{
	struct torso_config
	{
		float max_twist{ 0.70f };    // the head may turn ~40 degrees from the torso before it drags it along
		float follow_seconds{ 0.25f }; // time constant of the easing
		float settle_seconds{ 1.2f };  // the chest settles onto where the head faces this slowly
		float min_hand_reach{ 0.15f }; // hands' midpoint at least this far from the head (horizontally) to count
		float reach_blend{ 0.12f };    // and counts fully this much further out
		// The hands count only while both are out in front: each within hand_cone of where the head faces
		// (fading out by hand_cone_edge) and at least hand_min_reach out. One gun hand swung to the side or
		// round the head (headset 2026-11-05: a "lasso" spun the body) or a hand at the chest has no pull.
		float hand_cone{ 0.785f };      // 45 degrees
		float hand_cone_edge{ 1.22f };  // 70 degrees
		float hand_min_reach{ 0.12f };
		// ...and close together, as on a two-handed gun: a one-handed gun out in front with the off hand
		// low or at the chest is no body direction.
		float hands_apart{ 0.30f };      // counts fully up to this far apart (metres)
		float hands_apart_edge{ 0.45f }; // and not at all from here
	};

	struct torso_hands
	{
		bool valid[2]{};
		// Each hand relative to the head, tracking axes (x right, y up, z back), metres.
		float offset[2][3]{};
	};

	class torso_yaw
	{
	public:
		explicit torso_yaw(const torso_config& config = {}) : config_(config) {}
		float update(float head_yaw, const torso_hands& hands, float seconds);
		float yaw() const { return yaw_; }
		void reset(float yaw = 0.0f)
		{
			yaw_ = yaw;
			base_ = yaw;
			lean_ = 0.0f;
		}

	private:
		torso_config config_;
		float yaw_{};
		float base_{}; // from the head alone: it drags this only past the twist limit
		float lean_{}; // towards two hands held together in front (radians from base_), eased
	};

	// Yaw of a rotation's forward (-z) direction, same convention as torso_yaw.
	float yaw_of(float qx, float qy, float qz, float qw);
	// Where the neck sits from the eyes' midpoint for a head facing head_yaw: `back` metres behind it
	// along the way it faces (tracking axes, x right, z back; y is 0). The torso turns about that point.
	void neck_offset(float head_yaw, float back, float (&out)[3]);
	// a - b wrapped into [-pi, pi].
	float angle_between(float a, float b);
}
