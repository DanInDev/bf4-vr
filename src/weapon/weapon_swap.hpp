#pragma once

#include <string>

#include "body/body_size.hpp"
#include "input/gamepad.hpp"
#include "weapon/reload_hands.hpp"

// Weapon swapping in the game (docs/tasks/weapon-swap.md, holsters = 1): the gun hand's grip in a
// body holster (weapon/holsters) selects that weapon through the game's own slot keys (1 primary,
// 2 secondary, 3/4 gadgets, 6 knife), typed into its Raw Input keyboard (hooks::press_game_key), and
// checks the switch against the slot the game reports (re/notes/weapon.md). x64 only.
namespace bfvr::weapon
{
	void set_holsters(bool enabled);
	// #46, #50: the player's body against the reference the holsters, pouches and sling are laid out for.
	void set_body_size(const body::body_size& size);
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
	// free_hands (#35, weapon/free_hands): the gun is held only while the gun hand grips; let go and it
	// lies on the floor (drawn there, no firing) until gripped again.
	void set_free_hands(bool enabled);
	// Live setting slot_models_ahead (#41): the grenade and knife copies are drawn this far (metres) ahead of their
	// grab points along the body's facing, on the chest instead of inside it.
	void set_slot_models_ahead(float metres);
	float slot_models_ahead();
	// Live setting free_hands_release: the gun hand's grip open this long (seconds) before the gun drops or goes to
	// the off hand; 0 (default) at once.
	void set_free_hands_release(float seconds);
	// Live setting sling (#38): with free hands, a gun let go by both hands hangs at the chest, not on the floor.
	void set_sling(bool on);
	// Live setting sling_show_primary (#58): while another weapon is out, the primary hangs on the sling in 3D.
	void set_sling_show_primary(bool on);
	// Live setting slot_models_at_start (#41): at level start the grenade and the knife are selected for a moment
	// so their 3D copies stand at the pouch and the holster at once.
	void set_slot_models_at_start(bool on);
	// The gun lies on the floor (free hands): no shots, no two-handed hold, no reload on it.
	bool gun_dropped();
	// Empty hands (#38): the off hand holds the gun by the handguard; the gun hand is free (no fire).
	bool gun_in_off_hand();
	// ...by its pistol grip (picked up with the off hand): it fires with the off-hand trigger.
	bool gun_in_off_grip();
	// While the off hand reaches to use something (use_reach): from the head towards that hand, OpenXR LOCAL,
	// unit. The game's aim goes there so its use target is what the hand reaches for (#37 keeps it with the
	// body otherwise).
	bool use_reach_direction(float (&direction)[3]);
	// #70: for a moment around the squad order (Q) and a melee strike, where the head looks; while the C4's throw
	// (left trigger) is held, along the off hand's ray. OpenXR LOCAL, unit. false otherwise.
	bool action_aim_direction(float (&direction)[3]);
	// Live setting action_aim (#70).
	void set_action_aim(bool on);

	// Present thread, once per frame.
	void update_weapon_swap();

	// The holsters' lazy body yaw (radians, 0 = facing -z, + = left) for the chest rig's pouches;
	// false while the holsters are off or the head was never tracked.
	bool body_yaw(float& yaw);

	// A chest rig point (weapon/body_rig: body frame offset from the head) in OpenXR LOCAL, in the
	// holsters' body frame (the head's yaw while there is none); false without head tracking.
	bool body_rig_point(const v3& offset, v3& point);
	// The same, with the body's yaw it was placed at (atan2(-forward.x, -forward.z)).
	bool body_rig_point(const v3& offset, v3& point, float& yaw);

	// XInput hook thread: the gun-hand grip (RB) stays ours while the hand is in a holster, the
	// off-hand grip (LB) while the off hand gives the squad signal; adds the melee gesture's
	// right-stick click. (input::for_game keeps both grips from the game unless grips_to_game.)
	input::gamepad_state filter_swap_pad(const input::gamepad_state& pad);

	// Pipe "holster": state. "holster <primary|secondary|knife|gadget1|gadget2>" (research): switch as
	// if drawn from that holster; "holster order": the squad order key.
	std::string holster_pipe_command(const std::string& argument);
	// Held grenade (#36): select the game's grenade for a moment so stereo/extra_mesh can capture it, then
	// the gun again (only with a primary or secondary gun in hand and no switch running).
	void request_grenade_capture();
}
