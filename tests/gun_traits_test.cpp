// Checks the per-gun traits (src/weapon/gun_traits, all-weapons.md step 1) and the reload profiles made from them
// (weapon/profiles): every crate gun has a row, the AK/AR split and the hold-open follow the real guns, and only the
// magazine-fed guns get the physical magazine reload.
#include <cmath>
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
	expect(covered == 51, "all 51 crate guns reload by hand");
	expect(physical_reload_covers(traits_for("U_870")) && physical_reload_covers(traits_for("U_M240")) && physical_reload_covers(traits_for("U_MP412Rex")),
		"tubes shell by shell, belts by lid and belt, the revolver by its frame");
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
	// Bolt actions and the Hawk's pump cycle by hand: a shot leaves the chamber empty (the trigger waits), a rack feeds.
	{
		reload scout(profile_for("U_Scout", 6));
		scout.load(6, 20);
		expect(scout.profile().manual_cycle && profile_for("U_HAWK", 6).manual_cycle, "Scout and Hawk cycle by hand");
		expect(!profile_for("U_AK12", 31).manual_cycle && !profile_for("U_M416", 31).manual_cycle && !profile_for("U_M1014", 8).manual_cycle,
			"automatic guns do not (the M1014 is semi-automatic)");
		expect(profile_for("U_870", 8).manual_cycle && profile_for("U_SPAS12", 8).manual_cycle, "the tube pumps cycle by hand");
		// 6 = 5 in the magazine and one chambered.
		expect(scout.fire() == reload_event::shot && !scout.state().chamber && scout.rounds_in_gun() == 5,
			"Scout: the shot leaves the chamber empty, the next round still in the magazine (the trigger waits)");
		expect(scout.fire() == reload_event::dry_fire, "Scout: no second shot without working the bolt");
		expect(scout.rack() == reload_event::chambered && scout.state().chamber && scout.rounds_in_gun() == 5, "Scout: the bolt worked, the next round is in");
		reload last(profile_for("U_SV98", 6));
		last.load(1, 20);
		expect(last.fire() == reload_event::shot && last.rack() == reload_event::racked_empty, "SV-98: the last round fired, the bolt cycles empty");
		reload off(profile_for("U_Scout", 6));
		off.set_manual_cycle(false);
		off.load(6, 20);
		expect(off.fire() == reload_event::shot && off.state().chamber, "manual_cycle off: the game's own cycle, the next round fed");
	}
	// Tube shotguns, shell by shell (family F).
	{
		reload remington(profile_for("U_870", 7)); // capacity 6 in the tube
		remington.load(0, 20);
		expect(remington.profile().tube && remington.state().magazine_in && remington.rounds_in_gun() == 0, "870: an empty tube, still in the gun");
		expect(remington.press_button(true) == reload_event::none && remington.grab_magazine() == reload_event::none, "870: the tube never comes out");
		expect(remington.take_from_chest() == reload_event::magazine_taken && remington.state().held_rounds == 1 && remington.state().reserve == 19,
			"870: one shell from the pouch");
		expect(remington.insert() == reload_event::shell_loaded && remington.state().magazine_rounds == 1 && !remington.state().holding, "870: into the loading port");
		for (int i = 0; i < 5; ++i)
		{
			remington.take_from_chest();
			remington.insert();
		}
		expect(remington.state().magazine_rounds == 6, "870: six in the tube");
		remington.take_from_chest();
		expect(remington.insert() == reload_event::none && remington.state().holding, "870: the tube full, the seventh stays in the hand");
		expect(remington.rack() == reload_event::chambered && remington.state().magazine_rounds == 5, "870: pumped, one chambered");
		remington.insert();
		expect(remington.state().magazine_rounds == 6 && remington.rounds_in_gun() == 7, "870: the held one tops the tube up (6 + 1)");
		expect(remington.fire() == reload_event::shot && !remington.state().chamber, "870: the shot waits for the pump");
		reload benelli(profile_for("U_M1014", 8));
		benelli.load(3, 10);
		expect(benelli.fire() == reload_event::shot && benelli.state().chamber, "M1014: semi-automatic, the next shell fed from the tube");
	}
	// The revolver: break open, speedloader, double action.
	{
		reload rex(profile_for("U_MP412Rex", 6)); // the game's 6 counts the round under the hammer: six chambers
		rex.load(2, 30);
		expect(rex.profile().revolver && rex.state().chamber && rex.rounds_in_gun() == 2, "REX: two live rounds, one under the hammer");
		expect(rex.fire() == reload_event::shot && rex.state().chamber && rex.fire() == reload_event::shot && !rex.state().chamber,
			"REX: double action, the cylinder turns the next round under the hammer");
		expect(rex.fire() == reload_event::dry_fire, "REX: empty chambers click");
		expect(rex.press_button(false) == reload_event::lid_opened && rex.state().lid_open, "REX: the latch breaks it open");
		expect(rex.fire() == reload_event::dry_fire, "REX: open, nothing fires");
		expect(rex.take_from_chest() == reload_event::magazine_taken && rex.state().held_rounds == 6 && rex.state().reserve == 24,
			"REX: a full speedloader, one round per chamber");
		expect(rex.insert() == reload_event::magazine_in && rex.state().magazine_rounds == 6 && !rex.state().holding, "REX: six chambers filled");
		expect(rex.close_lid() == reload_event::lid_closed && rex.state().chamber && rex.rounds_in_gun() == 6, "REX: shut, ready");
		rex.fire();
		rex.fire();
		rex.press_button(false);
		rex.take_from_chest();
		expect(rex.insert() == reload_event::magazine_in && rex.state().magazine_rounds == 6 && rex.state().reserve == 22,
			"REX: a speedloader on four live rounds fills the two empty chambers, the other four back to the pouch");
		expect(rex.press_button(true) == reload_event::none && rex.grab_magazine() == reload_event::none, "REX: the latch again does nothing, nothing to pull");
	}
	// Belt-fed (family H): lid, box, belt.
	{
		reload saw(profile_for("U_M249", 201)); // a 200-round box
		saw.load(3, 400);
		expect(saw.profile().belt && saw.state().belt_laid && !saw.state().lid_open && saw.state().chamber, "M249: as the game left it, lid shut on a laid belt");
		expect(saw.press_button(true) == reload_event::none, "M249: the box stays on while the lid is shut");
		expect(saw.open_lid() == reload_event::lid_opened && !saw.state().chamber && saw.rounds_in_gun() == 3, "M249: lid up, the waiting round back on the belt");
		expect(saw.fire() == reload_event::dry_fire, "M249: no shot with the lid up");
		expect(saw.press_button(true) == reload_event::magazine_out && !saw.state().belt_laid, "M249: lid up, the box comes off with its belt");
		saw.let_go(false);
		expect(saw.take_from_chest() == reload_event::magazine_taken && saw.state().held_rounds == 200, "M249: a full box from the pouch");
		expect(saw.insert() == reload_event::magazine_in && !saw.state().belt_laid, "M249: the box on, its belt hanging");
		expect(saw.close_lid() == reload_event::lid_closed && !saw.state().chamber, "M249: shut without the belt, nothing to fire");
		expect(saw.take_belt() == reload_event::none, "M249: the belt cannot be taken under a shut lid");
		saw.open_lid();
		expect(saw.take_belt() == reload_event::belt_taken && saw.lay_belt() == reload_event::belt_laid, "M249: the belt end taken and laid on the tray");
		expect(saw.close_lid() == reload_event::lid_closed && saw.state().chamber && saw.rounds_in_gun() == 200, "M249: lid shut on the belt, ready (open bolt, no charging)");
		expect(saw.fire() == reload_event::shot && saw.state().chamber && saw.rounds_in_gun() == 199, "M249: fires and feeds from the belt");
		saw.open_lid();
		saw.take_belt();
		expect(saw.drop_belt() == reload_event::belt_dropped && !saw.state().belt_laid, "M249: the belt let go off the tray hangs again");
	}
	// Charging handles (tools/handle_probe.py): measured guns have their own point and side, the rest the variant's.
	{
		float point[3]{};
		expect(handle_point_for("U_G36C", point) && point[1] > 0.04f && point[2] > 0.5f, "G36C: its handle on top, ahead (measured)");
		expect(handle_point_for("U_Scorpion", point) && point[0] > 0.0f, "CZ-3A1: on the left (+x)");
		expect(handle_point_for("U_SAIGA_20K", point) && point[0] < 0.0f, "Saiga: on the right, as an AK");
		expect(handle_point_for("U_AK12", point) && point[0] < 0.0f, "AK-12: the 11-03 point kept");
		expect(handle_point_for("U_SCAR-HSV", point) && point[0] > 0.0f, "SCAR-H SV: the SCAR-H's");
		expect(!handle_point_for("U_M416", point), "M416: its empty reload uses the bolt release: the variant's point");
		expect(handle_point_for("U_Scout", point) && point[0] < -0.05f && point[1] > 0.08f, "Scout: the bolt knob, out on the right and high");
		{
			// The P90: its handle well away from where its top magazine is taken (they were 2 cm apart with the AR default).
			float grab[3]{}, well_to_handle = 0.0f;
			const float seated_mag[3]{ 0.0f, 0.052f, 0.378f }; // its Wep_Mag in Wep_Root axes (handle_probe)
			expect(handle_point_for("U_P90", point) && magazine_grab_for("U_P90", grab), "P90: handle and grab measured");
			for (int k = 0; k < 3; ++k)
			{
				const float d = point[k] - (seated_mag[k] + grab[k]);
				well_to_handle += d * d;
			}
			expect(std::sqrt(well_to_handle) > 0.15f, "P90: handle more than 15 cm from the magazine grab");
		}
	}
	// Magazine grab points: the game's hand on the magazine, from the seated Wep_Mag.
	{
		float grab[3]{};
		expect(magazine_grab_for("U_M416", grab) && grab[1] < -0.07f, "M416: the long box, grabbed ~9 cm below the magwell");
		expect(magazine_grab_for("U_Scout", grab) && grab[1] > -0.03f, "Scout: the short box, grabbed close under the gun");
		expect(magazine_grab_for("U_SCAR-H", grab) && grab[0] > 0.0f, "SCAR-H: from the left");
		expect(magazine_grab_for("U_P90", grab) && grab[2] < -0.1f, "P90: taken behind its magazine, on top of the gun");
		expect(!magazine_grab_for("U_QBB95", grab) && !magazine_grab_for("U_870", grab), "QBB-95 (spread across takes) and tube guns: the default");
		int measured = 0;
		for (std::size_t i = 0; i < crate_gun_count(); ++i)
		{
			if (magazine_grab_for(crate_gun_at(i), grab))
			{
				++measured;
				expect(physical_reload_covers(traits_for(crate_gun_at(i))), "a grab point only for a gun with the physical magazine reload");
				expect(grab[1] < 0.03f && grab[1] > -0.15f, "every grab between 3 cm above (the P90's top magazine) and 15 cm below the magwell");
			}
		}
		expect(measured == 29, "29 crate guns have a measured grab point");
	}
	// The way out: the AK-12 and SCAR-H are the reference and stay straight down; only clearly slanted guns differ.
	{
		float degrees = 0.0f;
		expect(!magazine_way_out_for("U_AK12", degrees) && !magazine_way_out_for("U_SCAR-H", degrees) && !magazine_way_out_for("U_M416", degrees),
			"AK-12, SCAR-H, M416: straight down");
		expect(magazine_way_out_for("U_SAIGA_20K", degrees) && degrees > 15.0f, "Saiga: rocked forward");
		expect(magazine_way_out_for("U_P90", degrees) && degrees < -90.0f, "P90: back off the top");
		for (std::size_t i = 0; i < crate_gun_count(); ++i)
		{
			if (magazine_way_out_for(crate_gun_at(i), degrees))
			{
				expect(physical_reload_covers(traits_for(crate_gun_at(i))) && std::fabs(degrees) >= 10.0f && std::fabs(degrees) <= 110.0f,
					"a way out only for a hand-reload gun, and only where it is clearly off straight down");
			}
		}
	}
	if (failures == 0)
	{
		std::printf("gun_traits: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
