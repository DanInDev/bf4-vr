#pragma once

#include <string>
#include <string_view>

// #80, the owner's wish (2026-11-16): our own weapon crate instead of the game's menu, which needs BF4 in front for
// its clicks. A gun goes into a slot the way the level scripts hand out kits (re/notes/weapon.md "Own crate"): a
// CustomizeSoldierData with that one gun is applied to the player's server soldier (ServerSoldierEntity vtable slot
// 100, the call ServerCustomizeSoldierEntity makes on its apply event). Research stage: pipe `givegun`.
namespace bfvr::weapon
{
	// Pipe "givegun <debug id> [slot] [attachment ids]", e.g. "givegun U_SteyrAug 0 U_SteyrAug_Magnifier": finds the
	// gun's SoldierWeaponUnlockAsset (and each attachment's BFUnlockAsset, put in its UnlockAssets) and queues the
	// customization for the game thread; waits up to 2 s for it to run.
	std::string give_gun_pipe_command(const std::string& argument);
	// Pipe "unlockfind <text>": the attachment unlocks (BFUnlockAsset) whose name contains the text.
	std::string unlock_find_pipe_command(const std::string& text);

	// Settings crate_primary / crate_secondary, every frame (from own_crate_on_game_thread): a gun picked on the panel
	// goes into slot 0 / 1 and into the hand once the pick has settled (the asset is looked up in the background, the
	// call runs on the game thread).
	void own_crate_settings(std::string_view primary, std::string_view secondary);

	// Game thread (camera hook): runs a queued customization.
	void own_crate_on_game_thread();
}
