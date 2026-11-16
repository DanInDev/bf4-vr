#pragma once

#include <string_view>

#include "weapon/reload.hpp"
#include "weapon/reload_hands.hpp"

namespace bfvr::weapon
{
	// The reload variant of a BF4 weapon from its unlock asset name (e.g. "U_AK12") and the game's
	// MagazineCapacity (FiringFunctionData Ammo, which counts the chambered round: 31 for a 30-round
	// AK-12). Kalashnikov-pattern rifles are AK; every other magazine weapon is AR for now.
	reload_profile profile_for(std::string_view unlock_name, int game_capacity);

	// The charging handle's knob of a weapon in its Wep_Root frame (+z along the barrel, +y up, metres),
	// where it was measured; false for the rest (they use the variant's point).
	bool handle_point_for(std::string_view unlock_name, float (&point)[3]);

	// #69 (owner 2026-11-15): the charging handle's grab zones where a gun needs its own (the SCAR-H's higher: a
	// grab at the gun's bottom caught the handle; the AK-12's much larger and reaching under the gun, where the
	// off hand racks it); false for the rest (hand_config's).
	bool rack_zones_for(std::string_view unlock_name, rack_zone_shape& shape);
}
