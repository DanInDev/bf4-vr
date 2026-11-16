#pragma once

// Menu cursor moved with a thumbstick (#34, owner 2026-11-16: "instead of a pointer, use the joystick"). The game's
// menus over a level (the gadget stash, the pause menu) ignore its own pad and the arrow keys, but take the
// mod's mouse clicks with BF4 in front or behind other windows (re/notes/weapon.md "Crate menu clicks"). This
// cursor gives those clicks a position without the laser: the stick moves it over the flat screen, the trigger
// clicks. Engine-free; tested by tests/stick_cursor_test.cpp.
namespace bfvr::input
{
	struct stick_cursor_config
	{
		float deadzone{ 0.15f }; // stick radius that moves nothing (drift of a resting stick)
		float speed{ 0.6f };     // screen widths per second at full tilt
		float exponent{ 2.0f };  // response curve: a small tilt moves slowly, for placing it on a button
		float max_step{ 0.1f };  // seconds; a longer frame (a stall, a loading screen) moves no further
	};

	class stick_cursor
	{
	public:
		explicit stick_cursor(const stick_cursor_config& config = {}) : config_(config) {}

		// x right, y up, each -1..1; seconds since the last update; aspect = screen width / height, so the cursor
		// moves as far up as sideways for the same tilt. Returns true while the stick is out of its dead zone.
		bool update(float x, float y, float seconds, float aspect);
		void reset(float u = 0.5f, float v = 0.5f);
		// Texture coordinates of the screen, (0, 0) top left, as input::pointer_hit.
		float u() const { return u_; }
		float v() const { return v_; }

	private:
		stick_cursor_config config_;
		float u_{ 0.5f };
		float v_{ 0.5f };
	};
}
