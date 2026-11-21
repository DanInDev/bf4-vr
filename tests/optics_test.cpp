// Checks the optic table (src/weapon/optics): the unlock names seen live in the campaign (AK-12 PK-AS and
// magnifier, SCAR-H EOTech), non-optic attachments, the PK-AS / PKA order and a gun's whole unlock set.
#include <cmath>
#include <cstdio>
#include <initializer_list>

#include "weapon/optics.hpp"

using namespace bfvr::weapon;

namespace
{
	int failures = 0;

	void expect(bool condition, const char* what)
	{
		if (!condition)
		{
			std::printf("FAIL: %s\n", what);
			++failures;
		}
	}

	bool kind(const char* name, optic_kind expected)
	{
		return classify_optic(name).kind == expected;
	}
}

int main()
{
	expect(kind("U_AK12_PK-AS", optic_kind::reflex), "PK-AS is a red dot");
	expect(kind("U_SCAR-H_EOTech", optic_kind::reflex), "EOTech is a holo");
	expect(kind("U_UMP45_RX01", optic_kind::reflex), "RX01 is BF4's Reflex (RDS), the crate UMP-45's red dot");
	expect(kind("U_AK12_Magnifier", optic_kind::magnifier), "the flip magnifier");
	expect(std::fabs(classify_optic("U_AK12_Magnifier").magnification - 1.73f) < 1e-4f, "the magnifier's measured zoom");
	expect(kind("U_AK12_PKA", optic_kind::magnified) && std::fabs(classify_optic("U_AK12_PKA").magnification - 3.4f) < 1e-4f, "PKA is a 3.4x, not the PK-AS");
	expect(kind("U_M16A4_ACOG", optic_kind::magnified) && std::fabs(classify_optic("U_M16A4_ACOG").magnification - 4.0f) < 1e-4f, "ACOG 4x");
	for (const char* other : { "U_AK12", "U_SCAR-H", "U_AK12_HeavyBarrel", "U_AK12_Foregrip_Folding", "U_SCAR-H_MuzzleBrake", "U_SCAR-H_TargetPointer",
			 "U_Grass_Woodland", "U_M67_SP", "U_Knife", "U_SP_Visor", "" })
	{
		expect(kind(other, optic_kind::none), "not an optic");
	}
	expect(kind(nullptr, optic_kind::none), "no name");

	gun_optics ak;
	for (const char* name : { "U_AK12", "U_AK12_Foregrip_Folding", "U_AK12_HeavyBarrel", "U_AK12_Magnifier", "U_AK12_PK-AS", "U_Grass_Woodland" })
	{
		add_optic(ak, name);
	}
	expect(ak.reflex && ak.magnifier && !ak.magnified, "the campaign AK-12: red dot and magnifier");
	gun_optics scar;
	for (const char* name : { "U_SCAR-H", "U_SCAR-H_EOTech", "U_SCAR-H_Foregrip_Angled", "U_SCAR-H_MuzzleBrake", "U_SCAR-H_TargetPointer" })
	{
		add_optic(scar, name);
	}
	expect(scar.reflex && !scar.magnifier && !scar.magnified, "the campaign SCAR-H: holo");
	gun_optics irons;
	add_optic(irons, "U_M9");
	expect(!irons.reflex && !irons.magnified && !irons.magnifier, "no optic: iron sights");
	gun_optics acog;
	add_optic(acog, "U_M16A4_ACOG");
	expect(acog.magnified && std::fabs(acog.magnification - 4.0f) < 1e-4f && !acog.reflex, "a 4x scope");

	// sight_ads: only the clear holo alone goes without the game's aim.
	expect(!needs_game_aim(scar), "the SCAR-H's EOTech is clear at the hip");
	expect(needs_game_aim(ak), "the AK-12's PK-AS is black at the hip (and carries the magnifier)");
	gun_optics pkas;
	add_optic(pkas, "U_AK12_PK-AS");
	expect(needs_game_aim(pkas), "a PK-AS alone");
	expect(needs_game_aim(irons) && needs_game_aim(acog), "iron sights and scopes keep the game's aim");
	gun_optics holo_scope;
	add_optic(holo_scope, "U_X_EOTech");
	add_optic(holo_scope, "U_X_ACOG");
	expect(needs_game_aim(holo_scope), "a holo with a scope");
	// #139: with the scope glass the zoom is in the optic's own glass: scopes and magnifiers go without the game's aim.
	expect(!needs_game_aim(acog, true), "glass: a scope needs no game aim");
	expect(!needs_game_aim(holo_scope, true), "glass: a clear holo with a scope");
	gun_optics holo_magnifier;
	add_optic(holo_magnifier, "U_X_EOTech");
	add_optic(holo_magnifier, "U_X_Magnifier");
	expect(holo_magnifier.magnifier && !needs_game_aim(holo_magnifier, true), "glass: a clear holo with a magnifier");
	expect(needs_game_aim(ak, true), "glass: the AK-12's PK-AS is still black at the hip");
	expect(needs_game_aim(irons, true), "glass: iron sights keep the game's aim");
	expect(!needs_game_aim(scar, true), "glass: a clear holo alone, as before");
	// #138: power from the game's zoom data (live FOVs, hip 45).
	expect(std::fabs(optic_power(45.0f, 27.0f) - 1.725f) < 0.01f, "the magnifier: 1.73x, as the game's aim showed");
	expect(std::fabs(optic_power(45.0f, 17.2f) - 2.74f) < 0.01f, "ACOG / PSO-1: 2.74x");
	expect(std::fabs(optic_power(45.0f, 8.7f) - 5.44f) < 0.02f, "SRS rifle scope: 5.44x");
	expect(optic_power(45.0f, 40.0f) < min_optic_power && optic_power(45.0f, 42.0f) < min_optic_power, "red dots, holos and irons count as 1x");
	expect(optic_power(0.0f, 17.2f) == 0.0f && optic_power(45.0f, 0.0f) == 0.0f && optic_power(45.0f, 200.0f) == 0.0f, "unread or nonsense FOVs give no power");
	// level_power: the selected level against the hip, its bounds and the 1x clamp.
	const float cz_magnifier[8]{ 45.0f, 40.0f, 27.0f };
	expect(level_power(cz_magnifier, 3, 8, 1) == 1.0f, "the holo level reports 1x (1.14 clamped)");
	expect(std::fabs(level_power(cz_magnifier, 3, 8, 2) - 1.725f) < 0.01f, "the magnifier level: 1.73x");
	expect(level_power(cz_magnifier, 3, 8, 0) == 0.0f, "level 0 (the hip) is no optic level");
	expect(level_power(cz_magnifier, 3, 8, 3) == 0.0f && level_power(cz_magnifier, 3, 8, -1) == 0.0f, "out of the levels: unknown");
	expect(level_power(cz_magnifier, 9, 8, 8) == 0.0f, "past the array: unknown");
	const float unread[8]{ 0.0f, 17.2f };
	expect(level_power(unread, 2, 8, 1) == 0.0f, "an unread hip FOV: unknown, the caller falls back");
	expect(level_power(nullptr, 2, 8, 1) == 0.0f, "no levels: unknown");
	const float acog_levels[8]{ 45.0f, 17.2f };
	expect(std::fabs(level_power(acog_levels, 2, 8, 1) - 2.74f) < 0.01f, "ACOG level: 2.74x, not clamped");
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("optics: all checks passed\n");
	return 0;
}
