#pragma once

// Torso yaw from the tracked head and hands (#13). The game body only turns with the stick; a player
// who turns in the room (or looks far to the side) would otherwise keep the shoulders facing the old
// way while the hands reach across the chest. The torso faces between the hands when both are out in
// front, never more than max_twist from the head, and eases there. Engine-free; tested by
// tests/torso_test.cpp. Angles in radians about the tracking space's up axis, left (counter-clockwise
// seen from above) positive, 0 = the body's forward.
namespace bfvr::body
{
	struct torso_config
	{
		float max_twist{ 0.70f };    // the head may turn ~40 degrees from the torso before it drags it along
		float follow_seconds{ 0.25f }; // time constant of the easing
		float min_hand_reach{ 0.15f }; // hands' midpoint at least this far from the head (horizontally) to count
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
		void reset(float yaw = 0.0f) { yaw_ = yaw; }

	private:
		torso_config config_;
		float yaw_{};
	};

	// Yaw of a rotation's forward (-z) direction, same convention as torso_yaw.
	float yaw_of(float qx, float qy, float qz, float qw);
	// a - b wrapped into [-pi, pi].
	float angle_between(float a, float b);
}
