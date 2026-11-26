#pragma once

#include <string>

#include "input/gamepad.hpp"

// Ladders by hand (#134, setting ladder_hands): the game adapter of weapon/ladder_climb. Reads the soldier's
// character state (Climbing), the ladder it is on and the level's ladders (a background scan for their
// ClientLadderComponent), the palms and grips; drives the left stick the game climbs by, raises the climbing
// state's speed while the hands climb, and holds the drawn hands on their rungs (body::set_rung_hold).
// re/notes/ladders.md. BF4 x64 only; elsewhere it does nothing.
namespace bfvr::weapon
{
	void set_ladder_hands(bool on);

	// Present thread, once per frame, before update_weapon_swap (the rungs come before use, pickup and free hands).
	void update_ladders();

	// Rig arm 0 = left, 1 = right: the hand holds a rung or reaches for one: its grip is nobody else's.
	bool ladder_claims_hand(int hand);

	// XInput hook thread: the left stick the hands climb by (or walk onto the ladder a hand reached for).
	input::gamepad_state filter_ladder_pad(const input::gamepad_state& pad);

	// Game thread (camera hook): while the soldier is on a ladder or walking onto one, the game's aim goes level into
	// the ladder (world axes, unit). BF4 reads the climb stick relative to the soldier's facing and drops him off past
	// 30 degrees (ClimbingStateData.DropOffAngle; monitor 2026-11-26: a pull with him facing 30 degrees off took him
	// down and off), while in VR the facing follows the player's body round the room. The view is unaffected.
	bool ladder_aim_world(float (&direction)[3]);

	// Pipe "ladder": state, ladder, holds, the stick, counters; "ladder scan" scans for the level's ladders now.
	std::string ladder_command(const std::string& argument);
}
