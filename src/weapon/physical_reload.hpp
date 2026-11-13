#pragma once

#include <string>

#include "input/gamepad.hpp"

// Physical reloading in the game (issue #14, reload = physical): the reload state machine
// (weapon/reload) owns the ammo of the gun in hand, driven by the off hand (weapon/reload_hands)
// and the shots the game fires. BF4 build 179547, x64 only (re/notes/weapon.md):
//   - ammo is written into the in-process server's weapon (firing object); the client copies follow;
//   - the game's magazine never reaches 0 (that starts its own reload): it holds the rounds in the
//     gun plus a spare, and the trigger is held back while the chamber is empty;
//   - the right B button is the AR release / bolt catch and never reaches the game.
namespace bfvr::weapon
{
	void set_physical_reload(bool enabled);
	void set_drop_discards_ammo(bool discard);
	// AR palming (weapon/reload_hands): a new magazine at the magwell of a loaded gun turns aside in
	// the hand, B drops the old one into the same hand, turning the hand back seats the new one.
	void set_palm_reload(bool enabled);
	// #40, setting magazine_snap: the open off hand snaps onto the seated magazine with the game's grip.
	void set_magazine_snapping(bool enabled);
	// #38: while the off hand carries the gun, the free gun hand changes magazines too (false: it only racks).
	void set_free_hand_reload(bool enabled);
	// #47, setting magazine_flight: a discarded magazine flies on with the hand's velocity and rests on the floor.
	void set_magazine_flight(bool enabled);
	// The free gun hand's grip belongs to the reload this frame (it holds a magazine, racks, or is nearer a
	// reload zone than the pistol grip): free hands must not take the gun back with it.
	bool reload_claims_free_hand();
	// Off hand on the optic or forward rail + grip = the game's accessory toggle (D-pad up), in
	// either reload mode (weapon/accessory_touch).
	void set_accessory_touch(bool enabled);

	// Present thread, once per frame: hands, shots, ammo writes, magazine drawing.
	void update_physical_reload();

	// XInput hook thread: removes what the physical reload consumes (X, the off-hand grip while it
	// handles a magazine, the trigger on an empty chamber).
	input::gamepad_state filter_pad(const input::gamepad_state& pad);
	// The off-hand grip belongs to a reload step or an accessory touch this frame.
	bool reload_owns_off_hand();
	// Stricter: the off hand holds a magazine or racks (a hand merely inside a reload zone does not count).
	bool reload_engages_off_hand();

	// Pipe "reload": state and the last events. "reload <event>" (research builds) drives the state
	// machine directly: fire button grab take letgo stow insert rack.
	std::string reload_pipe_command(const std::string& argument);
}
