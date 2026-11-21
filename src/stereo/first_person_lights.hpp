#pragma once

#include <cstdint>
#include <string>

// #92: the weapon's flashlight and laser light where the drawn gun points. The game places them right (the accessory
// update reads the pose the mod edits, re/notes/weapon.md "Flashlight and laser"), but its renderer re-bases every
// first-person light (flag +0xDA) from one camera into the drawn view (FUN_140CC9C50: L x inverse(from) x to). The mod
// turns the drawn view by the head, the "from" camera has no head in it, so the light swung with the head away from the
// gun (owner's marks 8, 12 and 14, 2026-11-19). The hook re-bases from the drawn view itself: the light stays at its own
// world matrix, at the gun. BF4 x64 only.
namespace bfvr::stereo
{
	bool install_first_person_light_hook();
	// Live setting light_at_gun.
	void set_light_at_gun(bool on);
	// Pipe "fplight [hex light address]": the hook's counters and both cameras; with a light, its matrix as the game
	// would draw it (re-based between the two cameras) and as the hook draws it.
	std::string first_person_light_report(std::uintptr_t light);
}
