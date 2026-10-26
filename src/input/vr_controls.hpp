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

	// XInput thread, every poll: the pad the game sees (stick replaced, stance presses added).
	gamepad_state for_game(const gamepad_state& pad);

	// Game thread (view-params copy): adds the turn collected since the last call to the aimer yaw.
	void apply_view_turn();

	// Pipe: "rightstick look|turn" switches live; no argument reports.
	std::string set_right_stick_mode(const std::string& mode);
}
