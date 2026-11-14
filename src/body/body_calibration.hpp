#pragma once

#include "body/arm_ik.hpp"

// Body calibration (#46, #50): stand straight, arms out to the sides (a T-pose), hold both triggers for
// two seconds; the mod measures your size and fits the soldier's arms, shoulders and body zones to it.
// Engine-free, OpenXR LOCAL metres (y up); tested by tests/body_calibration_test.cpp.
namespace bfvr::body::calibration
{
	struct sample
	{
		float seconds{};      // since the last sample
		bool head_valid{};
		ik::vec3 head;        // between the eyes
		bool hands_valid{};   // both grips tracked
		ik::vec3 left;        // grip (palm centre)
		ik::vec3 right;
		bool left_trigger{};
		bool right_trigger{};
		bool floor_valid{};   // the floor's height in LOCAL is known (an OpenXR STAGE space)
		float floor_y{};
	};

	struct config
	{
		float hold_seconds{ 2.0f };      // #50: averaged over a longer hold (two runs of one person gave 1.86 and 1.97 m)
		// A floor height this far from the span's: the floor is wrong (not set up), the span wins. 10 cm let a
		// short span (arms not straight) beat a right floor: the owner (1.88 m) measured 1.76 m (2026-11-14).
		float max_floor_disagreement{ 0.30f };
		float min_span{ 1.0f };          // grip to grip, horizontally
		float max_below_eyes{ 0.50f };   // hands at shoulder height: this far below the eyes at most
		float max_above_eyes{ 0.05f };
		float max_level_difference{ 0.15f };
		float max_off_centre{ 0.35f };   // the hands' midpoint this close to the head (out to the sides, not ahead)
		float max_motion{ 0.06f };       // a hand moving this far from where the hold began starts it again
	};

	struct measurement
	{
		bool valid{};
		float span{};            // grip to grip, metres
		float stature{};         // standing height
		float eye_height{};      // eyes above the floor
		bool from_floor{};       // stature from the floor (else estimated from the span)
		bool floor_doubted{};    // a floor was known but disagreed with the span, so the span was used
		float standing_head_y{}; // the head's height in LOCAL while standing straight
	};

	// What a measurement turns into. Anchored to the tuning made for the reference body (1.78 m: arm_length
	// 1.15, shoulders at the default anchor, the body zones as laid out), scaled for another size.
	struct body_shape
	{
		float scale{ 1.0f };          // body zones (pouches, holsters, sling) and the shoulders' height
		float arm_length{ 1.15f };    // live setting arm_length
		float shoulder_width{ 0.0f }; // live setting shoulder_width (per side, metres)
	};

	struct reference
	{
		float stature{ 1.78f };
		float arm_length{ 1.15f };
		float shoulder_anchor{ 0.18f }; // body/arm_pose shoulder_anchor.x: the shoulder joint, per side
	};

	// Anthropometry: stature from the eyes (crown 11 cm above them), from the span (fingertips ~9 cm past the
	// grip on either side), the shoulder joints 0.2 x stature apart, the arm reach from them to the grip.
	float stature_from_eyes(float eye_height);
	float stature_from_span(float span);
	float arm_reach(float span, float stature);
	body_shape shape_for(const measurement& measured, const reference& ref = {});

	// #50: a new height this far from the stored one (stored > 0) is kept only once a second T-pose agrees.
	inline constexpr float confirm_tolerance = 0.05f;
	bool needs_confirmation(float stored_height, float measured_height, float tolerance = confirm_tolerance);

	class t_pose
	{
	public:
		enum class state
		{
			waiting, // not in the pose (or not holding both triggers)
			holding, // in the pose, progress rising
			done,    // measured (result())
		};

		explicit t_pose(const config& settings = {}) : config_(settings) {}
		state update(const sample& now);
		bool in_pose(const sample& now) const;
		float progress() const { return config_.hold_seconds > 0.0f ? held_ / config_.hold_seconds : 1.0f; }
		const measurement& result() const { return result_; }
		void reset();

	private:
		config config_;
		state state_{ state::waiting };
		float held_{};
		ik::vec3 start_left_;
		ik::vec3 start_right_;
		// Sums over the hold.
		float span_sum_{};
		float head_sum_{};
		float floor_sum_{};
		int samples_{};
		int floor_samples_{};
		measurement result_;
	};
}
