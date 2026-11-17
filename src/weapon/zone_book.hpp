#pragma once

#include <string>

#include "weapon/zone_tuning.hpp"

// The per-gun grab-zone tunes (weapon/zone_tuning) of this process, shared by the panel's sliders (core, writer)
// and the physical reload (reader), and their file bfvr_zones.ini beside bfvr.ini, read on first use.
namespace bfvr::weapon::zone_book
{
	zone_tuning::gun_tunes get(const std::string& gun);
	zone_tuning::tune get(const std::string& gun, zone_tuning::zone which);
	void set(const std::string& gun, zone_tuning::zone which, const zone_tuning::tune& value);

	// Setting zone_tunes: saved tunes move the zones (off: every gun as built, to compare).
	void set_enabled(bool on);
	bool enabled();

	// The gun in hand (physical reload, every frame), for the panel's sliders; "" when none.
	void set_current_gun(const std::string& gun);
	std::string current_gun();

	// Reads the file again over nothing ("" or "error: ..."), writes every tune ("saved <path>" or "error: ...").
	std::string load();
	std::string save();
	// Pipe "zones": the file, and the live tunes in its format.
	std::string report();
}
