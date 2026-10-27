#pragma once

#include <string>

#include "input/gamepad.hpp"

namespace bfvr::input
{
	// Game side of the right-stick mapping (issue #22, right_stick.hpp). With right_stick = turn in
	// bfvr.ini the right stick no longer aims: horizontal turns the view (aimer yaw, re/notes/aim.md),
	// vertical flicks change stance through the pad's crouch/jump buttons, chosen from the soldier's
	// real stance (ClientSoldierEntity +0x4F0: 0 stand, 1 crouch, 2 prone).
	void install_vr_controls();
	// turn: snap | smooth | off.
	void configure_turn(const std::string& turn, int snap_degrees, bool stance_flicks);

	// XInput thread, every poll: the pad the game sees (stick replaced, stance presses added).
	gamepad_state for_game(const gamepad_state& pad);

	// Controller aim (#12): aim = controller. The camera hook renders from a level body yaw (camera
	// yaw convention, atan2(look.x, look.z)); snap turns change it; the aimer follows the controller.
	// The right stick then always turns (and flicks stance), whatever right_stick says.
	void set_controller_aim(bool enabled);
	bool controller_aim_active();
	// Game thread: the body yaw, taken from the game's camera yaw the first time.
	float body_camera_yaw(float game_yaw);
	// Game thread: points the aimer along a world direction (bullets go there).
	void aim_along(const float (&direction)[3]);
	// Controller aim's shot direction in OpenXR LOCAL (weapon/two_handed: the barrel line, converged);
	// without one the camera hook uses the gun hand's aim ray.
	void set_aim_direction(bool valid, const float (&direction)[3]);
	bool aim_direction(float (&direction)[3]);

	// The game walks relative to its own camera (the body, before the head pose: measured on the
	// monitor in both aim modes), so a player who turns in the room would walk the old way. The left
	// stick is turned by the head's (or the gun hand's) yaw in LOCAL space: move_direction =
	// head | gun | body (body: the game's own behaviour).
	enum class move_reference
	{
		body,
		head,
		gun,
	};
	void set_move_reference(move_reference reference);

	// Holds the aimer pitch at 0 every frame (the head alone pitches the view, #5).
	void set_neutral_pitch(bool enabled);

	// Game thread (view-params copy): adds the turn collected since the last call to the aimer yaw.
	void apply_view_turn();

	// Pipe: "rightstick look|turn" switches live; no argument reports.
	std::string set_right_stick_mode(const std::string& mode);
}
