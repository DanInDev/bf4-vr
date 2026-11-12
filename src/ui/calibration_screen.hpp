#pragma once

#include <vector>

#include "ui/settings_panel.hpp"

// The body calibration as a full step (#50): a wide head-locked screen with a T-pose figure, the three
// things to do (arms out, both triggers, hold still), a segmented loading bar while it measures, and the
// result. Engine-free: it lays out draw items for ui/panel_canvas; ui/vr_panel rasterises them and the
// theater shows them on their own quad. Tested by tests/calibration_screen_test.cpp.
namespace bfvr::ui
{
	constexpr int calibration_width = 1600;
	constexpr int calibration_height = 700;
	// Drawn see-through (the game stays visible, dimmed, behind the step).
	constexpr std::uint32_t calibration_backdrop = 0x0B0F14;

	struct calibration_view
	{
		enum class stage
		{
			pose,    // waiting for the T-pose, or measuring it
			confirm, // a height far from the stored one: a second T-pose must agree
			done,    // measured and saved
			kept,    // the second T-pose never came: the stored height stays
		};
		stage at{ stage::pose };
		bool arms_out{};      // the hands are where a T-pose has them (triggers aside)
		bool left_trigger{};
		bool right_trigger{};
		bool holding{};       // in the pose with both triggers: measuring
		bool lower_first{};   // confirm: still in the first pose, the arms must come down before the second
		float progress{};     // 0..1 of the hold
		float measured{};     // metres: the result (done) or the height to confirm (confirm)
		float stored{};       // metres: the height before (confirm, kept)
		float span{};         // metres, fingertips (done)
		int seconds_left{};   // until the prompt gives up
	};

	// 1, 2 or 3: the step the player is on (1 arms out, 2 both triggers, 3 hold still); 4 = all done.
	int calibration_step(const calibration_view& view);
	constexpr int calibration_segments = 36;
	int calibration_segments_lit(const calibration_view& view);

	std::vector<draw_item> calibration_screen(const calibration_view& view);
}
