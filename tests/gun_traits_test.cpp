// Checks the per-gun traits (src/weapon/gun_traits, all-weapons.md step 1) and the reload profiles made from them
// (weapon/profiles): every crate gun has a row, the AK/AR split and the hold-open follow the real guns, and only the
// magazine-fed guns get the physical magazine reload.
#include <cstdio>
#include <string>

#include "weapon/crate_guns.hpp"
#include "weapon/gun_traits.hpp"
#include "weapon/profiles.hpp"
#include "weapon/reload.hpp"

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
	// Every crate gun (the own crate's list, #80) has a row, in the same order.
	const std::string values = crate::values;
	std::size_t start = values.find('|') + 1;
	std::size_t index = 0;
	bool same = true;
	while (start != 0 && start <= values.size())
	{
		const std::size_t bar = values.find('|', start);
		const std::string gun = values.substr(start, bar == std::string::npos ? std::string::npos : bar - start);
		if (crate_gun_at(index) != gun)
		{
			std::printf("FAIL: row %zu is %s, the crate list has %s\n", index, std::string(crate_gun_at(index)).c_str(), gun.c_str());
			same = false;
		}
		++index;
		start = bar == std::string::npos ? 0 : bar + 1;
	}
	expect(same && index == crate_gun_count() && crate_gun_count() == 51, "51 rows, the crate's guns in the crate's order");

	// Which guns keep the physical magazine reload.
	int covered = 0;
	for (std::size_t i = 0; i < crate_gun_count(); ++i)
	{
		covered += physical_reload_covers(traits_for(crate_gun_at(i))) ? 1 : 0;
	}
	expect(covered == 40, "40 magazine-fed guns keep the physical reload; 5 tube, 5 belt and the revolver go to the game's");
	expect(!physical_reload_covers(traits_for("U_870")) && !physical_reload_covers(traits_for("U_M240")) && !physical_reload_covers(traits_for("U_MP412Rex")),
		"tube, belt and revolver: the game's reload until their families are built");
	expect(physical_reload_covers(traits_for("U_P90")) && physical_reload_covers(traits_for("U_HAWK")) && physical_reload_covers(traits_for("U_SV98")),
		"the P90's top magazine, the Hawk's box and a bolt action's box stay physical");
	expect(physical_reload_covers(traits_for("U_SomethingNew")), "an unknown gun keeps today's behaviour (box, button, hold-open)");

	// AK or AR.
	expect(profile_for("U_SAIGA_20K", 10).kind == variant::ak, "the crate's Saiga is rocked in (the old list named U_Saiga12)");
	expect(profile_for("U_A91", 31).kind == variant::ak && profile_for("U_M39EBR", 21).kind == variant::ak, "A-91 and M39 rock in");
	expect(profile_for("U_M416", 31).kind == variant::ar && profile_for("U_UMP45", 26).kind == variant::ar, "box guns are AR");

	// Hold-open.
	{
		reload ump(profile_for("U_UMP45", 26));
		ump.load(1, 50);
		expect(ump.fire() == reload_event::shot && !ump.state().bolt_locked, "UMP-45: no hold-open, the bolt rides home empty");
		ump.press_button(false); // the empty one drops
		ump.take_from_chest();
		ump.insert();
		expect(ump.press_button(false) == reload_event::magazine_dropped && !ump.state().chamber,
			"UMP-45: no bolt catch, the button only frees the magazine again");
		ump.take_from_chest();
		ump.insert();
		expect(ump.rack() == reload_event::chambered, "UMP-45: a rack chambers");
	}
	{
		reload m39(profile_for("U_M39EBR", 21));
		m39.load(1, 40);
		expect(m39.fire() == reload_event::bolt_locked && m39.state().bolt_locked, "M39: rocked in, but the last shot locks the bolt back");
		expect(m39.grab_magazine() == reload_event::magazine_out, "M39: the magazine comes out by hand");
	}
	{
		reload ak(profile_for("U_AK12", 31));
		ak.load(1, 60);
		expect(ak.fire() == reload_event::shot && !ak.state().bolt_locked, "AK-12: no hold-open, as before");
		reload scar(profile_for("U_SCAR-H", 21));
		scar.load(1, 40);
		expect(scar.fire() == reload_event::bolt_locked, "SCAR-H: hold-open, as before");
	}
	if (failures == 0)
	{
		std::printf("gun_traits: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
