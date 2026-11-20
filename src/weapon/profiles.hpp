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
	// #88: true when that point was placed from the gun's model (its bolt bone at rest, out to the handle's side),
	// because the game's own reload never pulls the handle; false for one measured from the animation or none.
	bool handle_point_from_model(std::string_view unlock_name);
	// #97: the charging handle can be worked from either side (owner, mark 52: the FAMAS's under its carry handle, the
	// AK-12's ambidextrous): a second rack zone at the knob mirrored across the gun.
	bool handle_both_sides(std::string_view unlock_name);
	// #96: a turn bolt as the game's own cycle moves it (handle_probe --all-bones, shot): Wep_Slide turned about its own
	// axis (its origin, Wep_Root x y = pivot) by lift_degrees, knob up, then travel metres back. False for other guns.
	bool bolt_geometry_for(std::string_view unlock_name, float (&pivot)[2], float& lift_degrees, float& travel);
	// Where the game's own left hand takes this gun's magazine out (tools/handle_probe.py): offset from the seated
	// Wep_Mag in Wep_Root axes (+x left, +y up, +z barrel). False: not measured, the magazine zone's default.
	bool magazine_grab_for(std::string_view unlock_name, float (&offset)[3]);
	// #90: how far a held magazine's top sits above the fist's grasp centre: where the game's own hand takes this gun's
	// magazine (magazine_grab_for, its depth below the top), so the fist holds it by its body as the game does (FAMAS
	// 3.7 cm, M416 8.9 cm); 2 to 12 cm; 0 when not measured or the game's hand is above the top (P90): the drawn hand then
	// keeps the grip it has (the authored grip's own depth, or 7 cm in a plain fist).
	float magazine_hold_drop(std::string_view unlock_name);
	// The way this gun's magazine leaves its well, measured from the game's empty reload: degrees from straight down (gun
	// -y) in the gun's mid-plane, + forward along the barrel. False: straight down (the reference, AK-12 and SCAR-H).
	bool magazine_way_out_for(std::string_view unlock_name, float& degrees);
	// A belt-fed gun's lid fully open, as the game's own reload opens it (degrees about the gun's x axis).
	float lid_degrees_for(std::string_view unlock_name);

	// #69 (owner 2026-11-15): the charging handle's grab zones where a gun needs its own (the SCAR-H's higher: a
	// grab at the gun's bottom caught the handle; the AK-12's much larger and reaching under the gun, where the
	// off hand racks it); false for the rest (hand_config's).
	// shape starts as defaults' zones; only what a gun changes is written over it.
	bool rack_zones_for(std::string_view unlock_name, const hand_config& defaults, rack_zone_shape& shape);
}
