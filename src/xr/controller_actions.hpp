#pragma once

#include <openxr/openxr.h>

#include "input/gamepad.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::xr
{
	// OpenXR actions that turn the VR controllers into an Xbox-style pad.
	// Suggested bindings target the Oculus Touch profile, which Meta Link and
	// SteamVR (Steam Link) both expose for Quest controllers.
	//
	//   left stick  -> left stick (move)     right stick -> right stick (look)
	//   left/right trigger -> LT / RT        left/right grip -> LB / RB
	//   A B (right), X Y (left) -> A B X Y   stick clicks -> LS / RS
	//   left menu button -> Start
	//
	// Grip and aim poses of both hands are tracked too (arm IK).
	class controller_actions
	{
	public:
		// Creates the actions on the instance and attaches them to the session. Returns false
		// (and logs) on failure; the theater keeps running without controller input then.
		bool create(XrInstance instance, XrSession session);
		// Call once per frame while the session is running.
		input::gamepad_state sync(XrSession session);
		// Grip and aim poses relative to base at the given display time; uses the state of the
		// last sync.
		stereo::hand_sample locate(XrSpace base, XrTime time) const;
		// Destroys the actions; call before the instance is destroyed. The pose spaces belong to
		// the session and go with it.
		void destroy();

	private:
		XrActionSet action_set_{ XR_NULL_HANDLE };
		XrAction move_{ XR_NULL_HANDLE };
		XrAction look_{ XR_NULL_HANDLE };
		XrAction left_trigger_{ XR_NULL_HANDLE };
		XrAction right_trigger_{ XR_NULL_HANDLE };
		XrAction left_grip_{ XR_NULL_HANDLE };
		XrAction right_grip_{ XR_NULL_HANDLE };
		XrAction a_{ XR_NULL_HANDLE };
		XrAction b_{ XR_NULL_HANDLE };
		XrAction x_{ XR_NULL_HANDLE };
		XrAction y_{ XR_NULL_HANDLE };
		XrAction left_stick_click_{ XR_NULL_HANDLE };
		XrAction right_stick_click_{ XR_NULL_HANDLE };
		XrAction start_{ XR_NULL_HANDLE };
		XrAction grip_pose_{ XR_NULL_HANDLE };
		XrAction aim_pose_{ XR_NULL_HANDLE };
		XrSpace grip_spaces_[2]{ XR_NULL_HANDLE, XR_NULL_HANDLE }; // 0 = left, 1 = right
		XrSpace aim_spaces_[2]{ XR_NULL_HANDLE, XR_NULL_HANDLE };
		bool left_grip_held_{};
		bool right_grip_held_{};
	};
}
