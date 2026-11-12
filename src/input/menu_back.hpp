#pragma once

// #34 (owner, 2026-11-13): while a game menu is up over the level (a crate, the pause menu), pressing the
// off-hand grip again backs out of it (Escape): the crate closes the way it was opened, without aiming the
// laser at Done. The grip that opened the crate is usually still held when the menu appears, so a press
// counts only after the grip was let go once while the menu is up. Engine-free; tested by
// tests/menu_back_test.cpp.
namespace bfvr::input
{
	class menu_back
	{
	public:
		// Once per frame; true on the frame the back key should be pressed.
		bool update(bool menu_up, bool grip)
		{
			if (!menu_up)
			{
				armed_ = false;
				grip_was_ = grip;
				return false;
			}
			const bool pressed = armed_ && grip && !grip_was_;
			if (!grip)
			{
				armed_ = true;
			}
			if (pressed)
			{
				armed_ = false;
			}
			grip_was_ = grip;
			return pressed;
		}

	private:
		bool armed_{};
		bool grip_was_{};
	};
}
