#pragma once

#include <string>

#include "input/gamepad.hpp"

// Physical reloading in the game (issue #14, reload = physical): the reload state machine
// (weapon/reload) owns the ammo of the gun in hand, driven by the off hand (weapon/reload_hands)
// and the shots the game fires. BF4 build 179547, x64 only (re/notes/weapon.md):
//   - ammo is written into the in-process server's ServerSoldierWeapon; the client copies follow;
//   - the game's magazine never reaches 0 (that starts its own reload): it holds the rounds in the
//     gun plus a spare, and the trigger is held back while the chamber is empty;
//   - the game's reload button (pad X) is the AR release / bolt catch and never reaches the game.
namespace bfvr::weapon
{
	void set_physical_reload(bool enabled);
	void set_drop_discards_ammo(bool discard);

	// Present thread, once per frame: hands, shots, ammo writes, magazine drawing.
	void update_physical_reload();

	// XInput hook thread: removes what the physical reload consumes (X, the off-hand grip while it
	// handles a magazine, the trigger on an empty chamber).
	input::gamepad_state filter_pad(const input::gamepad_state& pad);

	// Pipe "reload": state and the last events. "reload <event>" (research builds) drives the state
	// machine directly: fire button grab take letgo stow insert rack.
	std::string reload_pipe_command(const std::string& argument);
}
