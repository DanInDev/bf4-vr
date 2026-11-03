#pragma once

#include <string_view>

#include "weapon/reload.hpp"

namespace bfvr::weapon
{
	// The reload variant of a BF4 weapon from its unlock asset name (e.g. "U_AK12") and the game's
	// MagazineCapacity (FiringFunctionData Ammo, which counts the chambered round: 31 for a 30-round
	// AK-12). Kalashnikov-pattern rifles are AK; every other magazine weapon is AR for now.
	reload_profile profile_for(std::string_view unlock_name, int game_capacity);

	// The charging handle's knob of a weapon in its Wep_Root frame (+z along the barrel, +y up, metres),
	// where it was measured; false for the rest (they use the variant's point).
	bool handle_point_for(std::string_view unlock_name, float (&point)[3]);
}
