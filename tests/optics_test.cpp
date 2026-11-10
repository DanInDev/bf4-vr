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
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("optics: all checks passed\n");
	return 0;
}
