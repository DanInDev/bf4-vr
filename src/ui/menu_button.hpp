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
		float held() const { return down_ ? held_ : 0.0f; }

	private:
		float hold_;
		bool down_{};
		bool fired_{};
		float held_{};
	};

	struct stick_chord_events
	{
		bool started{}; // both clicks went down: the hold is counting
		bool fired{};   // once per chord, when the hold time is reached
		// once, when both clicks are up again after a chord that did not reach the hold time and began
		// with both clicks fresh (not one already held for the game, e.g. sprinting): mark_tap
		bool tapped{};
	};

	// Both thumbstick clicks held together (stick_menu): the panel's second opener, which SteamVR
	// never takes (on Quest through SteamVR the left menu button also summons its dashboard). The
	// game never sees a chord: a click alone waits up to grace seconds for the other one and reaches
	// the game late (as a tap of tap seconds if it was already released); once both are down, neither
	// reaches the game until both are up.
	class stick_chord
	{
	public:
		explicit stick_chord(float hold_seconds = 0.5f, float grace_seconds = 0.1f, float tap_seconds = 0.1f)
			: hold_(hold_seconds), grace_(grace_seconds), tap_(tap_seconds) {}
		// stick 0 = left, 1 = right.
		stick_chord_events update(bool left, bool right, float seconds);
		// What the game sees of that stick click this frame.
		bool to_game(int stick) const;
		bool chord() const { return chord_; }
		float held() const { return chord_ && both_ ? held_ : 0.0f; }

	private:
		struct click
		{
			bool down{};
			bool passing{}; // held alone past the grace time: the game has it
			float alone{};  // seconds held while waiting for the other click
			float tap{};    // seconds left of a late tap
		};
		float hold_;
		float grace_;
		float tap_;
		click clicks_[2]{};
		bool chord_{}; // both went down; ends when both are up
		bool both_{};
		bool fired_{};
		bool fresh_{};
		float held_{};
	};

	// The opener hint (menu_hint): shown for show seconds once a level runs, again after each level
	// load (the level was gone for at least rearm seconds); dismissed when the panel opens.
	class level_hint
	{
	public:
		explicit level_hint(float show_seconds = 8.0f, float rearm_seconds = 2.0f) : show_(show_seconds), rearm_(rearm_seconds) {}
		// True while the hint is shown.
		bool update(bool level_running, float seconds);
		void dismiss() { left_ = 0.0f; }

	private:
		float show_;
		float rearm_;
		float left_{};
		float away_{ 1e9f };
		bool running_{};
	};
}
