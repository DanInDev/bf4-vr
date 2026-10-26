#pragma once

#include "stereo/tracking.hpp"

// Body-attached HUD gestures (docs/tasks/hud-gestures.md, issue #26), as pure functions and small
// state machines over poses in OpenXR LOCAL space (metres, radians). Head = the eye midpoint
// (eye_math::centre_pose); grip = stereo::hand_sample::grip. Engine-free; tested by
// tests/gestures_test.cpp.
//
// Grip axes (OpenXR): -z along the handle, away from the wrist. +x is the palm normal, pointing out
// of the palm for the left hand and into the palm for the right hand; for both hands held
// naturally +x points to the user's right. Verify on SteamVR's Touch bindings in the headset.
namespace bfvr::input::gestures
{
	using stereo::pose;

	// p.position + p.orientation * offset.
	void transform_point(const pose& p, const float (&offset)[3], float (&out)[3]);
	// Angle between the head's forward axis (-z) and the direction from the head to point.
	float gaze_angle(const pose& head, const float (&point)[3]);
	// Unit normal out of the palm (and the inner forearm): +x of the left grip, -x of the right.
	void palm_normal(const pose& grip, int hand, float (&out)[3]);

	// Looking at the inside of a forearm, like reading a watch worn on the inner wrist.
	struct forearm_glance_config
	{
		int hand{ 0 };                   // 0 = left (the off hand of a right-handed player)
		float offset[3]{ 0, 0, 0.12f };  // glance point from the grip pose, grip axes, left hand (x mirrored for the right)
		float show_cone{ 0.35f };        // gaze within 20 degrees of the point to show
		float hide_cone{ 0.61f };        // stays shown within 35 degrees
		float facing{ 0.5f };            // cos of the largest angle between palm normal and the direction to the eyes (60 degrees)
		float max_distance{ 0.8f };
		float dwell{ 0.25f };            // seconds the show condition must hold
		float hide_delay{ 0.3f };        // seconds outside the stay condition before hiding
	};

	// The instantaneous show condition.
	bool forearm_in_view(const pose& head, const pose& grip, const forearm_glance_config& config);

	class forearm_glance
	{
	public:
		explicit forearm_glance(const forearm_glance_config& config = {}) : config_(config) {}
		void configure(const forearm_glance_config& config) { config_ = config; }
		const forearm_glance_config& config() const { return config_; }
		// Returns whether the element is shown. tracked = false counts as looking away.
		bool update(const pose& head, const pose& grip, bool tracked, float seconds);
		bool visible() const { return visible_; }
		void reset();

	private:
		forearm_glance_config config_;
		bool visible_{};
		float timer_{};
	};

	// Turning the gun to look at its side and holding it steady: shows ammo until you look away.
	struct gun_inspect_config
	{
		int hand{ 1 };                          // gun hand
		float offset[3]{ 0, 0.03f, -0.10f };    // point on the gun from the grip pose, grip axes, right hand (x mirrored for the left)
		float show_cone{ 0.35f };               // gaze within 20 degrees of the gun to start
		float hide_cone{ 0.7f };                // look further than 40 degrees away to hide
		float max_distance{ 0.7f };
		float side_facing{ 0.6f };              // |cos| between the gun's side (grip x) and the direction to the eyes; aiming down the sights is near 0
		float max_angular_speed{ 0.6f };        // rad/s (about 35 degrees per second) counts as steady
		float max_speed{ 0.15f };               // m/s
		float smoothing{ 0.1f };                // seconds; low-pass on both speeds against tracking noise
		float hold{ 1.0f };                     // seconds steady before showing
		float hide_delay{ 0.3f };
	};

	// The instantaneous pose condition (without steadiness).
	bool gun_in_inspect_pose(const pose& head, const pose& grip, const gun_inspect_config& config);

	class gun_inspect
	{
	public:
		explicit gun_inspect(const gun_inspect_config& config = {}) : config_(config) {}
		void configure(const gun_inspect_config& config) { config_ = config; }
		const gun_inspect_config& config() const { return config_; }
		bool update(const pose& head, const pose& grip, bool tracked, float seconds);
		bool visible() const { return visible_; }
		// Seconds of steady inspection so far (for a fill-up indicator).
		float progress() const { return timer_; }
		void reset();

	private:
		gun_inspect_config config_;
		bool visible_{};
		float timer_{};
		bool has_last_{};
		pose last_;
		float speed_{};
		float angular_speed_{};
	};
}
