// Checks the menu back gesture (src/input/menu_back, #34): the off-hand grip pressed again while a menu is
// up sends Escape once; if the menu is still up half a second later (a crate, which ignores Escape) it
// clicks Done; the grip still held from opening the crate does not count.
#include <cstdio>

#include "input/menu_back.hpp"

using namespace bfvr::input;
using action = menu_back::action;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}
}

int main()
{
	unsigned long long t = 1000;

	// The gadget stash: opened with the grip held; Escape does nothing there, Done closes it.
	menu_back stash;
	expect(stash.update(false, true, t) == action::none, "no menu, grip held (reaching for the crate): nothing");
	expect(stash.update(true, true, t += 16) == action::none, "the menu appears with the opening grip still held: nothing");
	expect(stash.update(true, false, t += 16) == action::none, "let go: nothing yet");
	expect(stash.update(true, true, t += 16) == action::escape, "pressed again: Escape first");
	expect(stash.update(true, true, t += 16) == action::none, "held: once only");
	expect(stash.update(true, false, t += 200) == action::none, "200 ms later, still up: wait");
	expect(stash.update(true, false, t += 300) == action::click_done, "500 ms later still up (Escape ignored): click Done");
	expect(stash.update(true, false, t += 16) == action::none, "Done clicked once");
	expect(stash.update(false, false, t += 16) == action::none, "the stash closed");

	// The pause menu: Escape resumes it, so Done is never clicked (it would hit the level).
	menu_back pause;
	t = 5000;
	expect(pause.update(true, false, t) == action::none, "pause menu up, grip free");
	expect(pause.update(true, true, t += 16) == action::escape, "first grip press: Escape");
	expect(pause.update(false, true, t += 100) == action::none, "the game resumed: the menu is gone");
	expect(pause.update(false, false, t += 600) == action::none, "no Done click after the menu left");

	// A new menu while the grip is held needs a release first.
	menu_back again;
	t = 9000;
	expect(again.update(true, true, t) == action::none, "a menu appears under a held grip: nothing");
	expect(again.update(true, true, t += 1000) == action::none, "still held a second later: nothing");
	expect(again.update(true, false, t += 16) == action::none, "let go");
	expect(again.update(true, true, t += 16) == action::escape, "pressed: Escape");

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("menu_back: all checks passed\n");
	return 0;
}
