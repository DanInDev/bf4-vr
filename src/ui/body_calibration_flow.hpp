#pragma once

#include <string>

#include "input/gamepad.hpp"
#include "ui/calibration_screen.hpp"

// The body calibration in the game (#46): after a level loads, a notice asks for a T-pose (arms out to
// the sides, both triggers held still for a second) while the body is not calibrated yet
// (body_calibration = first) or every time (always). The measurement (body/body_calibration) sets
// body_height and body_span, arm_length and shoulder_width, and is saved to bfvr.ini; body_height also
// scales the body zones and the shoulders' height (core).
namespace bfvr::ui
{
	// Present thread, once per frame.
	void update_body_calibration();
	// The notice text (empty: none): logs and the pipe; the headset shows the step screen instead.
	std::string body_calibration_notice();
	// #50: the calibration step screen's state; false while there is nothing to show.
	bool body_calibration_screen(calibration_view& view);
	// XInput hook thread: both triggers are the calibration's while the pose is being held.
	input::gamepad_state filter_calibration_pad(const input::gamepad_state& pad);
	// Pipe "calibrate": "start" asks again now, "stop" ends the prompt; else the state and the last result.
	std::string body_calibration_command(const std::string& argument);
}
