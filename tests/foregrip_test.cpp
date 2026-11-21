// Checks the foregrip table (src/weapon/foregrip, #91): the unlock names seen live (SCAR-H angled, AK-12
// folding), the other kinds by BF4's naming, a bipod and the gun's other attachments.
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
	expect(classify_foregrip("U_UMP45_Foregrip") == foregrip_kind::vertical, "plain foregrip is vertical");
	expect(classify_foregrip("U_FAMAS_Foregrip_Potato") == foregrip_kind::stubby, "potato grip");
	expect(classify_foregrip("U_MX4_Foregrip_Stubby") == foregrip_kind::stubby, "stubby grip");
	expect(classify_foregrip("U_M416_Ergo") == foregrip_kind::vertical, "ergo grip is a post");
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
