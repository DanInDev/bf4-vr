#pragma once

#include <string>

#include "input/gamepad.hpp"
#include "weapon/reload_hands.hpp"

// Weapon swapping in the game (docs/tasks/weapon-swap.md, holsters = 1): the gun hand's grip in a
// body holster (weapon/holsters) selects that weapon through the game's own slot keys (1 primary,
// 2 secondary, 3/4 gadgets, 6 knife), typed into its Raw Input keyboard (hooks::press_game_key), and
// checks the switch against the slot the game reports (re/notes/weapon.md). x64 only.
namespace bfvr::weapon
{
	void set_holsters(bool enabled);
	// Squad order by hand signal (weapon/hand_signal): off hand raised ahead above the eyes + grip = Q.
	void set_squad_signal(bool enabled);
	// melee_gesture: a quick forward thrust of the gun hand is the game's melee (pad right-stick
	// click, the knife stab; weapon/melee_gesture, #15).
	void set_melee_gesture(bool enabled);
	// knife_grip: with the knife out, the blade points along the gun-hand controller
	// (body::set_knife_in_hand).
	void set_knife_grip(bool enabled);
	// use_reach: the off hand reaching out ahead (away from the gun) + grip holds the game's interact
	// (E: pick up, doors, use prompts; weapon/use_reach, #15).
	void set_use_reach(bool enabled);
	// c4_trigger: with the C4 out, the off-hand (left) trigger reaches the game: it throws a C4.
	void set_c4_trigger(bool enabled);

	// Present thread, once per frame.
	void update_weapon_swap();

	// The holsters' lazy body yaw (radians, 0 = facing -z, + = left) for the chest rig's pouches;
	// false while the holsters are off or the head was never tracked.
	bool body_yaw(float& yaw);

	// A chest rig point (weapon/body_rig: body frame offset from the head) in OpenXR LOCAL, in the
	// holsters' body frame (the head's yaw while there is none); false without head tracking.
	bool body_rig_point(const v3& offset, v3& point);

	// XInput hook thread: the gun-hand grip (RB) stays ours while the hand is in a holster, the
	// off-hand grip (LB) while the off hand gives the squad signal; adds the melee gesture's
	// right-stick click. (input::for_game keeps both grips from the game unless grips_to_game.)
	input::gamepad_state filter_swap_pad(const input::gamepad_state& pad);

	// Pipe "holster": state. "holster <primary|secondary|knife|gadget1|gadget2>" (research): switch as
	// if drawn from that holster; "holster order": the squad order key.
	std::string holster_pipe_command(const std::string& argument);
}
