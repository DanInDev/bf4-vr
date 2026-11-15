#pragma once

#include <string>

// unlock_all_weapons: every gun in the campaign weapon crate can be picked (docs/tasks/all-weapons.md).
// The game's own switch, SyncedBFSettings.AllUnlocksUnlocked, is held at 1 while a campaign level runs;
// the unlock check (0x140157D10) reads it before the player's unlocks. Memory only: nothing is saved,
// nothing goes to EA. The game's value is written back when the level ends, the setting goes off or the
// multiplayer guard trips (docs/SAFETY.md).
namespace bfvr::weapon
{
	enum class unlock_step
	{
		none,
		set,     // write 1
		restore, // write back the game's own value
	};

	// Engine-free. wanted: the setting; level: a campaign level runs and the guard has not tripped;
	// flag: the game's flag now; ours: the mod wrote the 1 that stands there.
	unlock_step unlock_all_step(bool wanted, bool level, bool flag, bool ours);

	void set_unlock_all_weapons(bool on);
	// Present thread, every frame (checks four times a second): the level-load settings sync from the
	// game's server writes the flag back to 0, so it is set again after that.
	void update_unlock_all_weapons();
	// Pipe "unlocks": the setting, the code check, the settings object, the flag, writes and restores.
	std::string unlock_all_report();
}
