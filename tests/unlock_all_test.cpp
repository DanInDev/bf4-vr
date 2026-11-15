// Checks unlock_all_weapons' decision (src/weapon/unlock_all_math): the flag is set only while wanted in
// a campaign level, set again after the game resets it, and only the mod's own 1 is taken back.
#include <cstdio>

#include "weapon/unlock_all.hpp"

using namespace bfvr::weapon;

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
	expect(unlock_all_step(true, true, false, false) == unlock_step::set, "wanted in a level, flag off: set");
	expect(unlock_all_step(true, true, true, true) == unlock_step::none, "already set by us: nothing");
	expect(unlock_all_step(true, true, false, true) == unlock_step::set, "the level-load sync reset it: set again");
	expect(unlock_all_step(true, false, true, true) == unlock_step::restore, "level ended or guard tripped: restore");
	expect(unlock_all_step(false, true, true, true) == unlock_step::restore, "setting off: restore");
	expect(unlock_all_step(false, true, true, false) == unlock_step::none, "the game's own 1 stays");
	expect(unlock_all_step(true, false, true, false) == unlock_step::none, "outside a level the game's own 1 stays");
	expect(unlock_all_step(false, false, false, false) == unlock_step::none, "off and unset: nothing");
	expect(unlock_all_step(true, false, false, false) == unlock_step::none, "front end or loading: never set");
	if (failures == 0)
	{
		std::printf("unlock_all: all passed\n");
	}
	return failures == 0 ? 0 : 1;
}
