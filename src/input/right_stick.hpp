#pragma once

// Right-stick mapping for VR play (docs/tasks/input-locomotion.md, issue #22): horizontal turns the
// view (snap or smooth), vertical flicks change stance; the stick never aims. Engine-free; the game
// adapter adds turn to the aimer yaw and turns stance commands into game input.
// Tested by tests/right_stick_test.cpp.
namespace bfvr::input
{
	enum class turn_mode
	{
		off,
		snap,
		smooth,
	};

	enum class stance
	{
		stand,
		crouch,
		prone,
	};

	enum class stance_command
	{
		none,
		up,   // prone -> crouch -> stand
		down, // stand -> crouch -> prone
	};

	struct right_stick_config
	{
		float deadzone{ 0.2f };         // stick radius treated as centred; leaving it re-arms everything
		turn_mode turn{ turn_mode::snap };
		float snap_angle{ 0.5235988f }; // radians per snap (30 degrees)
		float snap_press{ 0.7f };       // |x| that fires a snap
		float snap_release{ 0.4f };     // |x| below which the next snap is armed
		float snap_cooldown{ 0.25f };   // seconds; a press during the cooldown fires when it ends
		float smooth_speed{ 2.6f };     // radians per second at full deflection (about 150 degrees)
		bool stance_flicks{ true };
		float stance_press{ 0.75f };    // |y| that sends a stance command
		float stance_release{ 0.35f };  // |y| below which the next command is armed
		float axis_bias{ 1.5f };        // an axis claims the stick once it exceeds the other by this factor
	};

	struct right_stick_output
	{
		float turn{}; // radians this frame, positive = turn right (adds to aimer yaw, re/notes/aim.md)
		stance_command stance{ stance_command::none };
	};

	// One gesture per stick movement: the first axis to dominate owns the stick until it returns to
	// the deadzone, so a diagonal flick never both turns and changes stance.
	class right_stick
	{
	public:
		explicit right_stick(const right_stick_config& config = {}) : config_(config) {}
		void configure(const right_stick_config& config) { config_ = config; }
		const right_stick_config& config() const { return config_; }

		// x right, y up, both in [-1, 1] (OpenXR thumbstick); seconds since the last update.
		right_stick_output update(float x, float y, float seconds);
		void reset();

	private:
		enum class axis
		{
			none,
			horizontal,
			vertical,
		};

		right_stick_config config_;
		axis owner_{ axis::none };
		bool snap_armed_{ true };
		bool stance_armed_{ true };
		float cooldown_{};
	};

	// The stance a command leads to; commands past the end (up while standing) keep the stance.
	stance next_stance(stance current, stance_command command);
}
