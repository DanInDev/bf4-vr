// Checks the foregrip table (src/weapon/foregrip, #91): unlock names seen live (Baku: SCAR-H angled, AK-12 folding;
// Shanghai 2026-11-21: the UMP-45's angled grip on the gun from the weapon crate, and the vertical, folding, stubby and
// ergo names in memory, re/notes/weapon.md), a bipod and the gun's other attachments. Only "potato" is assumed.
#include <cstdio>
#include <initializer_list>

#include "weapon/foregrip.hpp"

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
}

int main()
{
	expect(classify_foregrip("U_SCAR-H_Foregrip_Angled") == foregrip_kind::angled, "SCAR-H angled grip");
	expect(classify_foregrip("U_AK12_Foregrip_Folding") == foregrip_kind::vertical, "AK-12 folding grip is a post");
	expect(classify_foregrip("U_UMP45_Foregrip_Angled") == foregrip_kind::angled, "UMP-45 angled grip (crate gun)");
	expect(classify_foregrip("U_GalilACE53_Foregrip_Vertical") == foregrip_kind::vertical, "vertical grip");
	expect(classify_foregrip("U_SteyrAug_Foregrip_Folding") == foregrip_kind::vertical, "folding grip is a post");
	expect(classify_foregrip("U_Saiga_20k_Foregrip_Stubby") == foregrip_kind::stubby, "stubby grip");
	expect(classify_foregrip("U_GalilACE23_Foregrip_Ergo") == foregrip_kind::vertical, "ergo grip is a post");
	expect(classify_foregrip("U_FAMAS_Foregrip_Potato") == foregrip_kind::stubby, "potato grip (assumed name)");
	expect(classify_foregrip("u_scar-h_foregrip_angled") == foregrip_kind::angled, "case does not matter");
	for (const char* other : { "U_AK12", "U_SCAR-H", "U_AK12_Bipod", "U_SCAR-H_EOTech", "U_AK12_HeavyBarrel", "U_SCAR-H_MuzzleBrake",
			 "U_SCAR-H_TargetPointer", "U_Grass_Woodland", "U_M67_SP", "" })
	{
		expect(classify_foregrip(other) == foregrip_kind::none, other);
	}
	expect(classify_foregrip(nullptr) == foregrip_kind::none, "(null)");
	expect(foregrip_kind::none == foregrip_kind{}, "none is the default");
	if (failures)
	{
		std::printf("%d failure(s)\n", failures);
		return 1;
	}
	std::printf("foregrip: all passed\n");
	return 0;
}
