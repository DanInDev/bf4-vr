#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "input/gamepad.hpp"
#include "stereo/tracking.hpp"

// The in-VR settings panel in the game (issue #7): a long press on the left menu button opens it
// 0.75 m ahead of the head (world-locked); either controller points, its trigger clicks; Save writes
// bfvr.ini, Close or another long press closes. A short press stays the game's Start. While the
// panel is open the game sees an idle pad.
namespace bfvr::ui
{
	// Present thread, once per frame, before the theater submits.
	void update_vr_panel();

	struct panel_view
	{
		bool open{};
		stereo::pose pose; // OpenXR LOCAL; the quad faces +z (towards the head)
		float width{};     // metres
		float height{};
	};
	panel_view vr_panel_view();

	// Present thread: copies the panel image (top-down BGRA, settings_panel::width x height) when it
	// is newer than revision, and updates revision.
	bool vr_panel_pixels(std::uint64_t& revision, std::vector<std::uint32_t>& pixels);

	// XInput hook thread: Start held back (a short press arrives at release), idle pad while open.
	input::gamepad_state filter_panel_pad(const input::gamepad_state& pad);

	// Pipe "panel": state; "open" | "close" | "hover <u> <v>" | "click <u> <v>" | "shot <path.bmp>".
	std::string panel_pipe_command(const std::string& argument);
}
