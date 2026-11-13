#pragma once

// #34 (owner, 2026-11-13): while a game menu is up over the level (a crate, the pause menu), pressing the
// off-hand grip again backs out of it. Escape resumes the pause menu, but the gadget stash ignores Escape,
// the pad's B and a real Escape key (monitor 2026-11-13): it closes only with its Done button. So the
// press sends Escape first and, if a menu is still up half a second later, clicks Done. Clicking Done at
// once would be wrong: after a crate closes, the Escape would open the pause menu in the level.
// The grip that opened the crate is usually still held when the menu appears, so a press counts only after
// the grip was let go once while the menu is up. Engine-free; tested by tests/menu_back_test.cpp.
namespace bfvr::input
{
	// The stash's Done button in the game image (texture coordinates, 2160x2160, re/notes/weapon.md).
	inline constexpr float menu_done_u = 0.648f;
	inline constexpr float menu_done_v = 0.736f;

	class menu_back
	{
	public:
		enum class action
		{
			none,
			escape,     // press Escape now
			click_done, // the menu is still up: click its Done button
		};

		static constexpr unsigned long long done_delay_ms = 500;

		// Once per frame.
		action update(bool menu_up, bool grip, unsigned long long now_ms)
		{
			if (!menu_up)
			{
				armed_ = false;
				done_due_ = 0;
				grip_was_ = grip;
				return action::none;
			}
			action result = action::none;
			if (done_due_ && now_ms >= done_due_)
			{
				done_due_ = 0;
				result = action::click_done;
			}
			const bool pressed = armed_ && grip && !grip_was_;
			if (!grip)
			{
				armed_ = true;
			}
			if (pressed)
			{
				armed_ = false;
				done_due_ = now_ms + done_delay_ms;
				result = action::escape;
			}
			grip_was_ = grip;
			return result;
		}

	private:
		bool armed_{};
		bool grip_was_{};
		unsigned long long done_due_{};
	};
}
