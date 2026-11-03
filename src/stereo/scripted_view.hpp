#pragma once

// Whether the game is moving the camera by itself: a scripted scene (BF4's campaign cutscenes are
// mostly first person, the camera riding the soldier's animated head) or the death fall. In play the
// rendered camera always looks where the aimer points; when the animation drives it, it turns away.
// Engine-free; tested by tests/scripted_view_test.cpp.
namespace bfvr::stereo
{
	struct scripted_config
	{
		float enter_degrees{ 10.0f };  // camera this far from the aim ...
		float enter_seconds{ 0.25f };  // ... for this long: scripted
		float leave_degrees{ 4.0f };   // back within this ...
		float leave_seconds{ 0.5f };   // ... for this long: play again
	};

	struct scripted_input
	{
		bool soldier{};          // a local soldier exists
		bool dead{};             // its health is 0
		bool aim_known{};        // the aimer could be read
		float deviation_degrees{}; // between the rendered camera's forward and the aim
	};

	class scripted_view
	{
	public:
		explicit scripted_view(const scripted_config& config = {}) : config_(config) {}
		bool update(const scripted_input& input, float seconds);
		bool active() const { return active_; }

	private:
		scripted_config config_;
		bool active_{};
		float timer_{};
	};

	// Angle (degrees) between the camera's forward (camera yaw convention: atan2(x, z), pitch up
	// positive, forward = -row 2 of the view transform) and the aimer's yaw/pitch (camera yaw = -aimer yaw).
	float aim_deviation_degrees(const float (&camera_forward)[3], float aimer_yaw, float aimer_pitch);
}
