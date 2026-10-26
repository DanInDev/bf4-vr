#pragma once

// The left menu button shared by the game (Start: pause menu) and the settings panel (#7): a long
// press is ours, a short press is passed to the game when the button comes up. Engine-free; tested
// by tests/settings_panel_test.cpp.
namespace bfvr::ui
{
	struct menu_button_events
	{
		bool long_press{};  // once, when the hold time is reached
		bool short_press{}; // once, at release, when the hold time was not reached
	};

	class menu_button
	{
	public:
		explicit menu_button(float hold_seconds = 0.6f) : hold_(hold_seconds) {}
		menu_button_events update(bool down, float seconds);
		bool down() const { return down_; }

	private:
		float hold_;
		bool down_{};
		bool fired_{};
		float held_{};
	};
}
