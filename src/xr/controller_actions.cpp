#include "xr/controller_actions.hpp"

#include <windows.h>
#include <Xinput.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "common/log.hpp"
#include "input/handedness.hpp"

namespace bfvr::xr
{
	namespace
	{
		// Grip is analog on Touch; hysteresis keeps LB/RB from chattering around one threshold.
		constexpr float grip_press = 0.55f;
		constexpr float grip_release = 0.45f;

		bool ok(XrInstance instance, XrResult result, const char* what)
		{
			if (XR_SUCCEEDED(result))
			{
				return true;
			}
			char text[XR_MAX_RESULT_STRING_SIZE]{};
			if (XR_FAILED(xrResultToString(instance, result, text)))
			{
				std::snprintf(text, sizeof(text), "XrResult %d", static_cast<int>(result));
			}
			log::write("controllers: %s failed: %s", what, text);
			return false;
		}

		std::int16_t to_axis(float value)
		{
			return static_cast<std::int16_t>(std::lround(std::clamp(value, -1.0f, 1.0f) * 32767.0f));
		}

		std::uint8_t to_trigger(float value)
		{
			return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
		}

		bool held(float value, bool& latch)
		{
			latch = latch ? value > grip_release : value > grip_press;
			return latch;
		}
	}

	bool controller_actions::create(XrInstance instance, XrSession session)
	{
		XrActionSetCreateInfo set_info{ XR_TYPE_ACTION_SET_CREATE_INFO };
		strcpy_s(set_info.actionSetName, "gamepad");
		strcpy_s(set_info.localizedActionSetName, "Gamepad");
		if (!ok(instance, xrCreateActionSet(instance, &set_info, &action_set_), "xrCreateActionSet"))
		{
			return false;
		}

		struct action_spec
		{
			XrAction* action;
			const char* name;
			XrActionType type;
			const char* path;
		};
		const action_spec specs[] = {
			{ &move_, "move", XR_ACTION_TYPE_VECTOR2F_INPUT, "/user/hand/left/input/thumbstick" },
			{ &look_, "look", XR_ACTION_TYPE_VECTOR2F_INPUT, "/user/hand/right/input/thumbstick" },
			{ &left_trigger_, "left_trigger", XR_ACTION_TYPE_FLOAT_INPUT, "/user/hand/left/input/trigger/value" },
			{ &right_trigger_, "right_trigger", XR_ACTION_TYPE_FLOAT_INPUT, "/user/hand/right/input/trigger/value" },
			{ &left_grip_, "left_bumper", XR_ACTION_TYPE_FLOAT_INPUT, "/user/hand/left/input/squeeze/value" },
			{ &right_grip_, "right_bumper", XR_ACTION_TYPE_FLOAT_INPUT, "/user/hand/right/input/squeeze/value" },
			{ &a_, "button_a", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/right/input/a/click" },
			{ &b_, "button_b", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/right/input/b/click" },
			{ &x_, "button_x", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/left/input/x/click" },
			{ &y_, "button_y", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/left/input/y/click" },
			{ &left_stick_click_, "left_stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/left/input/thumbstick/click" },
			{ &right_stick_click_, "right_stick_click", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/right/input/thumbstick/click" },
			{ &start_, "start", XR_ACTION_TYPE_BOOLEAN_INPUT, "/user/hand/left/input/menu/click" },
		};

		std::vector<XrActionSuggestedBinding> bindings;
		for (const action_spec& spec : specs)
		{
			XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
			info.actionType = spec.type;
			strcpy_s(info.actionName, spec.name);
			strcpy_s(info.localizedActionName, spec.name);
			if (!ok(instance, xrCreateAction(action_set_, &info, spec.action), "xrCreateAction"))
			{
				return false;
			}

			XrPath path{};
			if (!ok(instance, xrStringToPath(instance, spec.path, &path), "xrStringToPath"))
			{
				return false;
			}
			bindings.push_back({ *spec.action, path });
		}

		// Pose actions: one per pose kind, per hand through subaction paths.
		XrPath hands[2]{};
		if (!ok(instance, xrStringToPath(instance, "/user/hand/left", &hands[0]), "xrStringToPath")
			|| !ok(instance, xrStringToPath(instance, "/user/hand/right", &hands[1]), "xrStringToPath"))
		{
			return false;
		}
		struct pose_spec
		{
			XrAction* action;
			const char* name;
			const char* paths[2];
		};
		const pose_spec poses[] = {
			{ &grip_pose_, "grip_pose", { "/user/hand/left/input/grip/pose", "/user/hand/right/input/grip/pose" } },
			{ &aim_pose_, "aim_pose", { "/user/hand/left/input/aim/pose", "/user/hand/right/input/aim/pose" } },
			{ &haptic_, "haptic", { "/user/hand/left/output/haptic", "/user/hand/right/output/haptic" } },
		};
		for (const pose_spec& spec : poses)
		{
			XrActionCreateInfo info{ XR_TYPE_ACTION_CREATE_INFO };
			info.actionType = spec.action == &haptic_ ? XR_ACTION_TYPE_VIBRATION_OUTPUT : XR_ACTION_TYPE_POSE_INPUT;
			strcpy_s(info.actionName, spec.name);
			strcpy_s(info.localizedActionName, spec.name);
			info.countSubactionPaths = 2;
			info.subactionPaths = hands;
			if (!ok(instance, xrCreateAction(action_set_, &info, spec.action), "xrCreateAction"))
			{
				return false;
			}
			for (const char* binding : spec.paths)
			{
				XrPath path{};
				if (!ok(instance, xrStringToPath(instance, binding, &path), "xrStringToPath"))
				{
					return false;
				}
				bindings.push_back({ *spec.action, path });
			}
		}

		XrPath profile{};
		if (!ok(instance, xrStringToPath(instance, "/interaction_profiles/oculus/touch_controller", &profile), "xrStringToPath"))
		{
			return false;
		}
		XrInteractionProfileSuggestedBinding suggested{ XR_TYPE_INTERACTION_PROFILE_SUGGESTED_BINDING };
		suggested.interactionProfile = profile;
		suggested.countSuggestedBindings = static_cast<std::uint32_t>(bindings.size());
		suggested.suggestedBindings = bindings.data();
		if (!ok(instance, xrSuggestInteractionProfileBindings(instance, &suggested), "xrSuggestInteractionProfileBindings"))
		{
			return false;
		}

		XrSessionActionSetsAttachInfo attach{ XR_TYPE_SESSION_ACTION_SETS_ATTACH_INFO };
		attach.countActionSets = 1;
		attach.actionSets = &action_set_;
		if (!ok(instance, xrAttachSessionActionSets(session, &attach), "xrAttachSessionActionSets"))
		{
			return false;
		}

		hand_paths_[0] = hands[0];
		hand_paths_[1] = hands[1];

		// Without pose spaces the pad still works; only the arms stay animated.
		for (int hand = 0; hand < 2; ++hand)
		{
			XrActionSpaceCreateInfo space_info{ XR_TYPE_ACTION_SPACE_CREATE_INFO };
			space_info.subactionPath = hands[hand];
			space_info.poseInActionSpace.orientation.w = 1.0f;
			space_info.action = grip_pose_;
			if (!ok(instance, xrCreateActionSpace(session, &space_info, &grip_spaces_[hand]), "xrCreateActionSpace"))
			{
				break;
			}
			space_info.action = aim_pose_;
			if (!ok(instance, xrCreateActionSpace(session, &space_info, &aim_spaces_[hand]), "xrCreateActionSpace"))
			{
				break;
			}
		}

		log::write("controllers: gamepad actions ready (Oculus Touch bindings)");
		return true;
	}

	stereo::hand_sample controller_actions::locate(XrSpace base, XrTime time) const
	{
		stereo::hand_sample sample;
		constexpr XrSpaceLocationFlags tracked = XR_SPACE_LOCATION_POSITION_VALID_BIT | XR_SPACE_LOCATION_ORIENTATION_VALID_BIT;
		const auto locate_one = [&](XrSpace space, stereo::pose& out)
		{
			XrSpaceLocation location{ XR_TYPE_SPACE_LOCATION };
			if (space == XR_NULL_HANDLE || XR_FAILED(xrLocateSpace(space, base, time, &location)) || (location.locationFlags & tracked) != tracked)
			{
				return false;
			}
			const XrPosef& pose = location.pose;
			out = { { pose.position.x, pose.position.y, pose.position.z },
				{ pose.orientation.x, pose.orientation.y, pose.orientation.z, pose.orientation.w } };
			return true;
		};
		for (int hand = 0; hand < 2; ++hand)
		{
			sample.valid[hand] = locate_one(grip_spaces_[hand], sample.grip[hand]) && locate_one(aim_spaces_[hand], sample.aim[hand]);
		}
		return input::left_handed() ? input::swapped(sample) : sample;
	}

	input::gamepad_state controller_actions::sync(XrSession session)
	{
		input::gamepad_state pad;
		pad.connected = true;
		if (action_set_ == XR_NULL_HANDLE)
		{
			return pad;
		}

		const XrActiveActionSet active{ action_set_, XR_NULL_PATH };
		XrActionsSyncInfo sync_info{ XR_TYPE_ACTIONS_SYNC_INFO };
		sync_info.countActiveActionSets = 1;
		sync_info.activeActionSets = &active;
		// XR_SESSION_NOT_FOCUSED (e.g. system menu open) succeeds with every action inactive: a neutral pad.
		const XrResult synced = xrSyncActions(session, &sync_info);
		if (XR_FAILED(synced))
		{
			return pad;
		}
		focused_ = synced != XR_SESSION_NOT_FOCUSED;

		const auto read_float = [&](XrAction action)
		{
			XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO };
			info.action = action;
			XrActionStateFloat state{ XR_TYPE_ACTION_STATE_FLOAT };
			return XR_SUCCEEDED(xrGetActionStateFloat(session, &info, &state)) && state.isActive ? state.currentState : 0.0f;
		};
		const auto read_bool = [&](XrAction action)
		{
			XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO };
			info.action = action;
			XrActionStateBoolean state{ XR_TYPE_ACTION_STATE_BOOLEAN };
			return XR_SUCCEEDED(xrGetActionStateBoolean(session, &info, &state)) && state.isActive && state.currentState;
		};
		const auto read_vector = [&](XrAction action)
		{
			XrActionStateGetInfo info{ XR_TYPE_ACTION_STATE_GET_INFO };
			info.action = action;
			XrActionStateVector2f state{ XR_TYPE_ACTION_STATE_VECTOR2F };
			return XR_SUCCEEDED(xrGetActionStateVector2f(session, &info, &state)) && state.isActive ? state.currentState : XrVector2f{};
		};

		const XrVector2f move = read_vector(move_);
		const XrVector2f look = read_vector(look_);
		pad.left_x = to_axis(move.x);
		pad.left_y = to_axis(move.y);
		pad.right_x = to_axis(look.x);
		pad.right_y = to_axis(look.y);
		pad.left_trigger = to_trigger(read_float(left_trigger_));
		pad.right_trigger = to_trigger(read_float(right_trigger_));

		std::uint16_t buttons = 0;
		if (held(read_float(left_grip_), left_grip_held_)) buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
		if (held(read_float(right_grip_), right_grip_held_)) buttons |= XINPUT_GAMEPAD_RIGHT_SHOULDER;
		if (read_bool(a_)) buttons |= XINPUT_GAMEPAD_A;
		if (read_bool(b_)) buttons |= XINPUT_GAMEPAD_B;
		if (read_bool(x_)) buttons |= XINPUT_GAMEPAD_X;
		if (read_bool(y_)) buttons |= XINPUT_GAMEPAD_Y;
		if (read_bool(left_stick_click_)) buttons |= XINPUT_GAMEPAD_LEFT_THUMB;
		if (read_bool(right_stick_click_)) buttons |= XINPUT_GAMEPAD_RIGHT_THUMB;
		if (read_bool(start_)) buttons |= XINPUT_GAMEPAD_START;
		pad.buttons = buttons;
		return input::left_handed() ? input::mirrored(pad) : pad;
	}

	void controller_actions::vibrate(XrSession session, const input::haptic_output& output)
	{
		if (haptic_ == XR_NULL_HANDLE)
		{
			return;
		}
		// Output index 1 is the gun hand: the right controller, or the left one when left-handed.
		const bool left_handed = input::left_handed();
		for (int hand = 0; hand < 2; ++hand)
		{
			const int controller = (hand == 1) != left_handed ? 1 : 0;
			const float amplitude = output.amplitude[hand];
			XrHapticActionInfo info{ XR_TYPE_HAPTIC_ACTION_INFO };
			info.action = haptic_;
			info.subactionPath = hand_paths_[controller];
			if (amplitude > 0.0f)
			{
				// Renewed every frame; long enough to bridge to the next one.
				XrHapticVibration vibration{ XR_TYPE_HAPTIC_VIBRATION };
				vibration.amplitude = amplitude;
				vibration.duration = 50'000'000; // 50 ms
				vibration.frequency = XR_FREQUENCY_UNSPECIFIED;
				xrApplyHapticFeedback(session, &info, reinterpret_cast<const XrHapticBaseHeader*>(&vibration));
				vibrating_[controller] = true;
			}
			else if (vibrating_[controller])
			{
				xrStopHapticFeedback(session, &info);
				vibrating_[controller] = false;
			}
		}
	}

	void controller_actions::destroy()
	{
		if (action_set_ != XR_NULL_HANDLE)
		{
			// Destroying the set also destroys its actions.
			xrDestroyActionSet(action_set_);
		}
		*this = controller_actions{};
	}
}
