// Checks the soldier radius keeper (src/body/soldier_radius, #53): it writes the wanted radius into the
// level's physics data, remembers the game's own value per asset and puts it back when the setting
// returns to 0, and leaves anything that is no radius alone.
#include <cmath>
#include <cstdio>

#include "body/soldier_radius.hpp"

using namespace bfvr::body;

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

	bool near(float a, float b)
	{
		return std::fabs(a - b) < 1e-4f;
	}
}

int main()
{
	constexpr std::uintptr_t level_one = 0x6892F4B0, level_two = 0x7000A000; // fit the 32-bit build too

	radius_keeper keeper;
	expect(!keeper.update(level_one, 0.35f, 0.0f).write && near(keeper.original(), 0.35f), "setting 0: the game's own, nothing written, 0.35 remembered");
	radius_action a = keeper.update(level_one, 0.35f, 0.20f);
	expect(a.write && near(a.value, 0.20f), "setting 0.20: write 0.20");
	expect(!keeper.update(level_one, 0.20f, 0.20f).write, "already 0.20: nothing more");
	a = keeper.update(level_one, 0.20f, 0.0f);
	expect(a.write && near(a.value, 0.35f), "back to 0: the game's 0.35 written back");
	expect(!keeper.update(level_one, 0.35f, 0.0f).write, "restored: nothing more");

	a = keeper.update(level_one, 0.35f, 0.02f);
	expect(a.write && near(a.value, soldier_radius_min), "below the range: clamped to 0.10");
	a = keeper.update(level_one, 0.10f, 0.9f);
	expect(a.write && near(a.value, soldier_radius_max), "above the range: clamped to 0.35");

	// A new level brings a new asset: its own value is remembered, not the first level's.
	radius_keeper levels;
	levels.update(level_one, 0.35f, 0.15f);
	a = levels.update(level_two, 0.40f, 0.15f);
	expect(a.write && near(a.value, 0.15f) && levels.asset() == level_two && near(levels.original(), 0.40f), "a new level's asset: its 0.40 remembered, 0.15 written");
	a = levels.update(level_two, 0.15f, 0.0f);
	expect(a.write && near(a.value, 0.40f), "back to 0 there: its own 0.40 restored");

	// Wrong pointers or no soldier: nothing written.
	radius_keeper guard;
	expect(!guard.update(0, 0.35f, 0.2f).write, "no asset (no soldier): nothing");
	expect(!guard.update(level_one, 123456.0f, 0.2f).write, "a value that is no radius: left alone");
	expect(!guard.update(level_one, std::nanf(""), 0.2f).write, "NaN: left alone");
	expect(guard.asset() == 0, "nothing remembered from a bad read");

	// Only the player's physics asset (the spawn builds AI characters' shapes too).
	expect(is_player_physics("Gameplay/Soldiers/DefaultSoldierPhysics_SP"), "the player's asset (campaign)");
	expect(is_player_physics("Gameplay/Soldiers/DefaultSoldierPhysics"), "the same without the _SP suffix");
	expect(!is_player_physics("Characters/Soldiers/AISoldierPhysics"), "AI soldiers: left alone");
	expect(!is_player_physics(""), "no name: left alone");

	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("soldier_radius: all checks passed\n");
	return 0;
}
