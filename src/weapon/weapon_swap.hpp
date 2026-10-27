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
	// Squad order by hand signal (weapon/hand_signal): off hand raised ahead above the eyes + grip = Q.
	void set_squad_signal(bool enabled);
	// grip_spot = 0 (default): the gun-hand grip (pad RB) never reaches the game. Its game action is
	// spot / command rose on release (re/notes/sound.md), so squeezing the grip while holding or
	// reloading the gun played call-outs.
	void set_grip_spot(bool enabled);
	// melee_gesture: a quick forward thrust of the gun hand is the game's melee (pad right-stick
	// click, the knife stab; weapon/melee_gesture, #15).
	void set_melee_gesture(bool enabled);

	// Present thread, once per frame.
	void update_weapon_swap();

	// XInput hook thread: the gun-hand grip (RB) stays ours while the hand is in a holster (always with
	// grip_spot = 0), the off-hand grip (LB) while the off hand gives the squad signal; adds the melee
	// gesture's right-stick click.
	input::gamepad_state filter_swap_pad(const input::gamepad_state& pad);

	// Pipe "holster": state. "holster <primary|secondary|knife|gadget1|gadget2>" (research): switch as
	// if drawn from that holster; "holster order": the squad order key.
	std::string holster_pipe_command(const std::string& argument);
}
