#pragma once

#include <string>

#include "input/gamepad.hpp"

// Weapon swapping in the game (docs/tasks/weapon-swap.md, holsters = 1): the gun hand's grip in a
// body holster (weapon/holsters) selects that weapon through the game's own slot keys (1 primary,
// 2 secondary, 3/4 gadgets, 6 knife), typed into its Raw Input keyboard (hooks::press_game_key), and
// checks the switch against the slot the game reports (re/notes/weapon.md). x64 only.
namespace bfvr::weapon
{
	void set_holsters(bool enabled);

	// Present thread, once per frame.
	void update_weapon_swap();

	// XInput hook thread: the gun-hand grip (RB) stays ours while the hand is in a holster.
	input::gamepad_state filter_swap_pad(const input::gamepad_state& pad);

	// Pipe "holster": state. "holster <primary|secondary|knife|gadget1|gadget2>" (research): switch as
	// if drawn from that holster.
	std::string holster_pipe_command(const std::string& argument);
}
