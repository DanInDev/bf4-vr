#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "input/gamepad.hpp"
#include "stereo/tracking.hpp"

// The in-VR settings panel in the game (issue #7): a long press on the left menu button, or both
// thumbstick clicks held together (stick_menu), opens it 0.8 m ahead at eye height where the user
// looks (world-locked); either controller points, its trigger clicks; Save writes bfvr.ini, Close
// or another long press closes. A short press stays the game's Start. While the panel is open the
// game sees an idle pad.
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

	// A one-line notice on a small head-locked quad below the view centre: the opener hint after a
	// level loads (menu_hint), and that SteamVR has the controllers while the session is visible but
	// not focused (focus_notice).
	constexpr int notice_width = 768;
	constexpr int notice_height = 80;
	bool vr_notice_shown();
	// Like vr_panel_pixels, for the notice (notice_width x notice_height).
	bool vr_notice_pixels(std::uint64_t& revision, std::vector<std::uint32_t>& pixels);

	// Theater: the session is visible but not focused (the runtime has the controllers). A menu
	// button or stick chord held when that happens is forgotten, not passed to the game as Start.
	void set_input_focus_lost(bool lost);
	// Seconds the left menu button has been held (0 when up), for the focus log.
	float menu_hold_seconds();

	// XInput hook thread: Start held back (a short press arrives at release), stick clicks held back
	// while they may be a chord, idle pad while open.
	input::gamepad_state filter_panel_pad(const input::gamepad_state& pad);

	// Pipe "panel": state; "open" | "close" | "hover <u> <v>" | "click <u> <v>" | "shot <path.bmp>".
	std::string panel_pipe_command(const std::string& argument);
}
