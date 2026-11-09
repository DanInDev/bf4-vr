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
	// Live setting aim_with_body (#37): while shots take their frame from the gun, the game's own aim stays
	// with the body (the stick turn, level), so the game never turns its first-person body with the gun.
	void set_aim_with_body(bool on);
	// True while the game's aim should stay with the body: the setting is on and the shot frame was moved to
	// the gun within the last half second (the hook works for the weapon in hand).
	bool aim_stays_with_body();
	std::string shot_origin_report();
}
