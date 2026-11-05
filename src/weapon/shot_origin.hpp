#pragma once

#include <string>

// Shots from the gun (controller aim): the game starts every shot at its weapon's shot frame
// (ClientWeapon +0x40), which it sets each frame at the camera along the aim (setter 0x1401473B0,
// re/notes/weapon.md "Where shots start"). For the local player's current weapon this frame is moved to
// the drawn gun's sight line, along its barrel, so bullets leave the gun and land on the sight's dot at
// any range. BF4 x64 only.
namespace bfvr::weapon
{
	bool install_shot_origin_hook();
	// Live setting shots_from_gun.
	void set_shots_from_gun(bool on);
	std::string shot_origin_report();
}
