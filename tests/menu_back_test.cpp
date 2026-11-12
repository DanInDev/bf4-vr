// Checks the menu back gesture (src/input/menu_back, #34): the off-hand grip pressed again while a menu is
// up backs out once; the grip still held from opening the crate does not.
#include <cstdio>

#include "input/menu_back.hpp"

using namespace bfvr::input;

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
	// Opened with the grip held: the menu appears while it is still down.
	menu_back back;
	expect(!back.update(false, true), "no menu, grip held (reaching for the crate): nothing");
	expect(!back.update(true, true), "the menu appears with the opening grip still held: nothing");
	expect(!back.update(true, true), "still held: nothing");
	expect(!back.update(true, false), "let go: nothing yet");
	expect(back.update(true, true), "pressed again: back out");
	expect(!back.update(true, true), "held after the press: once only");
	expect(!back.update(true, false), "released");
	expect(back.update(true, true), "a second press while the menu is still up (it did not close): again");

	// The menu closes: nothing more, and the next menu needs a release first.
	expect(!back.update(false, true), "menu gone");
	expect(!back.update(true, true), "a new menu while the grip is held: nothing");
	expect(!back.update(true, false), "let go");
	expect(back.update(true, true), "pressed: back out of the new menu");

	// A menu opened without the grip (the pause menu from the menu button): the first press counts.
	menu_back pause;
	expect(!pause.update(false, false), "playing");
	expect(!pause.update(true, false), "pause menu up, grip free");
	expect(pause.update(true, true), "first grip press: back out (resume)");

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("menu_back: all checks passed\n");
	return 0;
}
