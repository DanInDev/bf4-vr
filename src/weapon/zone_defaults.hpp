#pragma once

#include <string_view>

#include "weapon/zone_tuning.hpp"

// #152 (owner 2026-11-24): grab zones tuned in the headset become the defaults for everyone. The tunes ship in the
// source (zone_defaults.cpp, written by tools/zones_to_defaults.py from a saved bfvr_zones.ini) and are read before
// the player's own bfvr_zones.ini, which overrides them zone by zone. Engine-free; tests/zone_tuning_test.cpp.
namespace bfvr::weapon::zone_tuning
{
	// The shipped tunes, in the bfvr_zones.ini format.
	std::string_view built_in_defaults();
	// defaults with every zone that overrides tunes away from "as built" taken from overrides.
	book with_overrides(const book& defaults, const book& overrides);
}
