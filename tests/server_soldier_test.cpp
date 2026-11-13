// Checks choose_server_soldier (src/weapon/server_soldier_math): the player's server soldier is the typed
// candidate standing at the client soldier's feet, never an untyped object or a squadmate a step away.
#include <cstdio>

#include "weapon/server_soldier.hpp"

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

	soldier_candidate at(std::uint64_t address, float x, float y, float z, bool typed = true)
	{
		soldier_candidate c;
		c.address = address;
		c.typed = typed;
		c.placed = true;
		c.position[0] = x;
		c.position[1] = y;
		c.position[2] = z;
		return c;
	}
}

int main()
{
	// Live 2026-11-13 (Baku): the client's feet, the player 0.00 m from them, a squadmate 1.53 m off.
	const float feet[3]{ -477.40f, 406.36f, -699.16f };
	const soldier_candidate live[]{ at(0x1783f8ce0, -478.93f, 406.36f, -699.16f), at(0x178405440, -440.0f, 403.0f, -650.0f),
		at(0x178423640, -477.40f, 406.36f, -699.16f) };
	expect(choose_server_soldier(live, 3, feet, 1.0f) == 0x178423640ull, "the soldier at the feet, not the squadmate");

	const soldier_candidate squad_only[]{ at(0x1783f8ce0, -478.93f, 406.36f, -699.16f) };
	expect(choose_server_soldier(squad_only, 1, feet, 1.0f) == 0, "a squadmate 1.5 m away is not the player");

	const soldier_candidate untyped[]{ at(0x6db96710, -477.40f, 406.36f, -699.16f, false) };
	expect(choose_server_soldier(untyped, 1, feet, 1.0f) == 0, "an object without the soldier's vtable never counts");

	soldier_candidate unplaced = at(0x178423640, -477.40f, 406.36f, -699.16f);
	unplaced.placed = false;
	expect(choose_server_soldier(&unplaced, 1, feet, 1.0f) == 0, "a candidate whose position could not be read never counts");

	// Two within tolerance (a squadmate pressed against the player): the nearer wins.
	const soldier_candidate close_pair[]{ at(1, -477.0f, 406.36f, -699.16f), at(2, -477.35f, 406.36f, -699.16f) };
	expect(choose_server_soldier(close_pair, 2, feet, 1.0f) == 2, "the nearer of two close soldiers");

	expect(choose_server_soldier(nullptr, 0, feet, 1.0f) == 0, "no candidates");

	if (failures == 0)
	{
		std::printf("server_soldier: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
