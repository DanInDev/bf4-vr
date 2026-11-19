#pragma once

// Driving a car (drive_controls = wheel): a virtual steering wheel held with the grips, the right trigger
// as throttle and the left as brake / reverse. The wheel turns with the hands around its centre in the
// seat's frame (OpenXR LOCAL: x right, y up, the car facing -z): two hands turn it by the line between
// them (no centre needed, so a wheel drawn a little off still steers true), one hand by its angle around
// the centre. Let go, it springs back to straight. Engine-free; tested by tests/drive_wheel_test.cpp.
namespace bfvr::input
{
	struct wheel_config
	{
		float lock{ 1.571f };        // wheel turn (rad) for a full lock: 90 degrees
		float return_rate{ 4.0f };   // rad/s back to straight when no hand holds it
		float min_radius{ 0.04f };   // a single hand closer to the centre than this does not turn it
		float trigger_dead{ 0.06f }; // trigger travel (0..1) ignored
		float stick_dead{ 0.15f };   // the left stick takes over below/above this
	};

	struct wheel_hand
	{
		bool held{};   // grip squeezed
		float x{}, y{}; // position in the wheel plane relative to its centre (metres; x right, y up)
	};

	struct wheel_input
	{
		float seconds{};
		wheel_hand hands[2];       // 0 = left, 1 = right
		float throttle{}, brake{}; // right and left trigger, 0..1
		float stick_x{}, stick_y{}; // left stick, -1..1 (y forward): a fallback while the wheel is free
	};

	struct wheel_output
	{
		float steer{};  // -1 full left .. 1 full right (the game's left stick x)
		float drive{};  // -1 brake / reverse .. 1 throttle (the game's left stick y)
		float angle{};  // wheel turn, rad, positive anticlockwise (to the left) as the driver sees it
		int hands{};    // hands on the wheel
	};

	class drive_wheel
	{
	public:
		explicit drive_wheel(const wheel_config& config = {}) : config_(config) {}
		void configure(const wheel_config& config) { config_ = config; }
		wheel_output update(const wheel_input& in);
		void reset();
		float angle() const { return angle_; }

	private:
		wheel_config config_;
		float angle_{};
		bool was_held_[2]{};
		bool pair_was_{};
		float hand_angle_[2]{};
		float pair_angle_{};
	};
}
