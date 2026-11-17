#pragma once

#include <string_view>

#include "weapon/reload.hpp"
#include "weapon/reload_hands.hpp"

namespace bfvr::weapon
{
	// The reload variant of a BF4 weapon from its unlock asset name (e.g. "U_AK12") and the game's
	// MagazineCapacity (FiringFunctionData Ammo, which counts the chambered round: 31 for a 30-round
	// AK-12): rock-in guns (weapon/gun_traits) are AK, the rest AR; the hold-open as the gun has it.
	reload_profile profile_for(std::string_view unlock_name, int game_capacity);

	// #64: a single-shot break-open launcher reloaded by hand (weapon/break_open): the standalone M320 from the gadget
	// stash ("U_SP_M320_HE" and its other rounds). Not the M320 under a rifle ("U_<rifle>_M320_..."), not yet.
	bool is_break_open(std::string_view unlock_name);

	// The charging handle's knob of a weapon in its Wep_Root frame (+z along the barrel, +y up, metres),
	// where it was measured; false for the rest (they use the variant's point).
	bool handle_point_for(std::string_view unlock_name, float (&point)[3]);

	// #69 (owner 2026-11-15): the charging handle's grab zones where a gun needs its own (the SCAR-H's higher: a
	// grab at the gun's bottom caught the handle; the AK-12's much larger and reaching under the gun, where the
	// off hand racks it); false for the rest (hand_config's).
	// shape starts as defaults' zones; only what a gun changes is written over it.
	bool rack_zones_for(std::string_view unlock_name, const hand_config& defaults, rack_zone_shape& shape);
}
