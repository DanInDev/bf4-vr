#pragma once

#include <string>

// #34, the owner's wish (2026-11-16): our own weapon crate instead of the game's menu, which needs BF4 in front for
// its clicks. A gun goes into a slot the way the level scripts hand out kits (re/notes/weapon.md "Own crate"): a
// CustomizeSoldierData with that one gun is applied to the player's server soldier (ServerSoldierEntity vtable slot
// 100, the call ServerCustomizeSoldierEntity makes on its apply event). Research stage: pipe `givegun`.
namespace bfvr::weapon
{
	// Pipe "givegun <debug id> [slot]", e.g. "givegun U_M416 0": finds the gun's SoldierWeaponUnlockAsset and queues
	// the customization for the game thread; waits up to 2 s for it to run.
	std::string give_gun_pipe_command(const std::string& argument);

	// Game thread (camera hook): runs a queued customization.
	void own_crate_on_game_thread();
}
