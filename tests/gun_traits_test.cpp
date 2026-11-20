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
		{
			// #95: an empty box has no belt to take; its box comes straight off with the lid up.
			reload empty(profile_for("U_M249", 200));
			empty.load(0, 200);
			empty.open_lid();
			expect(empty.state().magazine_rounds == 0 && empty.take_belt() == reload_event::none && !empty.state().belt_held, "M249 empty: no belt to take");
			expect(empty.press_button(true) == reload_event::magazine_out, "M249 empty: lid up, the box comes off");
		}
	}
	// #98: no release button on the P90 and the AK 5C (owner, marks 31 and 1): the hand pulls the magazine out, B does not drop it.
	for (const char* name : { "U_P90", "U_AK5C" })
	{
		const reload_profile profile = profile_for(name, 51);
		expect(profile.kind == variant::ar && profile.pull_out, (std::string(name) + ": pulled out by hand").c_str());
		reload gun(profile);
		gun.load(10, 100);
		expect(gun.press_button(false) == reload_event::none && gun.state().magazine_in, (std::string(name) + ": B does not drop it").c_str());
		expect(gun.press_button(true) == reload_event::none && gun.state().magazine_in, (std::string(name) + ": nor with the hand on it").c_str());
		expect(gun.grab_magazine() == reload_event::magazine_out && gun.state().holding && gun.state().held_rounds == 9, // 10 in the gun: 9 + the chambered one
			(std::string(name) + ": the hand takes it out").c_str());
		gun.configure({ true, false }); // setting magazine_pull_out off: B as before
		gun.let_go(false);
		gun.take_from_chest();
		gun.insert();
		gun.press_button(true); // the first press after a new magazine is the bolt catch where the gun has one
		gun.press_button(true);
		expect(!gun.state().magazine_in, (std::string(name) + ": setting off, B releases it again").c_str());
	}
	{
		reload m416(profile_for("U_M416", 31));
		m416.load(10, 100);
		expect(!profile_for("U_M416", 31).pull_out && m416.grab_magazine() == reload_event::none, "M416: latched until B, as before");
		expect(!profile_for("U_AK12", 31).pull_out && profile_for("U_AK12", 31).kind == variant::ak, "AK-12: the AK variant, not pull_out");
	}
	// #96: a turn bolt in its four steps (SRS, FY-JS, Scout, SV-98): lift, back, forward, down; fires only locked.
	for (const char* name : { "U_SRS", "U_FY-JS", "U_Scout", "U_SV98" })
	{
		const std::string n(name);
		const reload_profile profile = profile_for(name, 6);
		expect(profile.bolt_turn && profile.manual_cycle, (n + ": a turn bolt worked by hand").c_str());
		reload gun(profile);
		gun.load(6, 20); // 5 in the magazine, one chambered
		expect(gun.fire() == reload_event::shot && gun.fire() == reload_event::dry_fire, (n + ": one shot, then the bolt must be worked").c_str());
		expect(gun.bolt_back() == reload_event::none && gun.bolt_forward() == reload_event::none && gun.bolt_down() == reload_event::none,
			(n + ": locked, the bolt does not come back").c_str());
		expect(gun.bolt_lift() == reload_event::bolt_opened && gun.state().bolt_up, (n + ": lifted").c_str());
		expect(gun.bolt_back() == reload_event::none && gun.state().bolt_back, (n + ": back, the spent case out").c_str());
		expect(gun.bolt_down() == reload_event::none, (n + ": back, it cannot turn down").c_str());
		expect(gun.bolt_forward() == reload_event::chambered && gun.state().magazine_rounds == 4, (n + ": forward feeds the next round").c_str());
		expect(gun.fire() == reload_event::dry_fire && gun.state().chamber, (n + ": not turned down: the trigger does nothing").c_str());
		expect(gun.bolt_down() == reload_event::bolt_closed && !gun.state().bolt_up, (n + ": down: locked").c_str());
		expect(gun.fire() == reload_event::shot, (n + ": locked, it fires").c_str());
		gun.rack(); // a full cycle at once (pipe): chambered and locked
		gun.bolt_lift();
		expect(gun.bolt_back() == reload_event::round_ejected && !gun.state().chamber, (n + ": back on a live round ejects it").c_str());
		gun.bolt_forward();
		gun.bolt_down();
		gun.set_manual_cycle(false);
		expect(gun.bolt_lift() == reload_event::none, (n + ": manual_cycle off: the game cycles it, no steps").c_str());
	}
	{
		reload m416(profile_for("U_M416", 31));
		expect(!m416.turning() && m416.bolt_lift() == reload_event::none, "M416: no turn bolt");
	}
	// #97: handles worked from either side on the real gun; one-sided and reversible ones not.
	for (const char* both : { "U_FAMAS", "U_AK12", "U_P90", "U_G36C", "U_QBB95" })
	{
		expect(handle_both_sides(both), (std::string(both) + ": its handle from either side").c_str());
	}
	for (const char* one : { "U_SCAR-H", "U_SG553LB", "U_CZ805", "U_SteyrAug", "U_M416", "U_AK74M", "U_MagpulPDR" })
	{
		expect(!handle_both_sides(one), (std::string(one) + ": one side (or reversible, or centred)").c_str());
	}
	// Charging handles (tools/handle_probe.py): measured guns have their own point and side, the rest the variant's.
	{
		float point[3]{};
		expect(handle_point_for("U_G36C", point) && point[1] > 0.04f && point[2] > 0.5f, "G36C: its handle on top, ahead (measured)");
		expect(handle_point_for("U_Scorpion", point) && point[0] > 0.0f, "CZ-3A1: on the left (+x)");
		expect(handle_point_for("U_SAIGA_20K", point) && point[0] < 0.0f, "Saiga: on the right, as an AK");
		expect(handle_point_for("U_AK12", point) && point[0] < 0.0f, "AK-12: the 11-03 point kept");
		expect(handle_point_for("U_SCAR-HSV", point) && point[0] > 0.0f, "SCAR-H SV: the SCAR-H's");
		// #88: the 11 guns whose reload never pulls the handle, placed from the model, on the owner's side.
		for (const char* right : { "U_AK5C", "U_AEK971", "U_SG553LB", "U_M240", "U_M249" })
		{
			expect(handle_point_for(right, point) && point[0] < 0.0f && handle_point_from_model(right), "model: on the right (-x)");
		}
		for (const char* left : { "U_UMP45", "U_SteyrAug" })
		{
			expect(handle_point_for(left, point) && point[0] > 0.0f && handle_point_from_model(left), "model: front left (+x)");
		}
		for (const char* top : { "U_M16A4", "U_M416", "U_MK11", "U_QBZ951" })
		{
			expect(handle_point_for(top, point) && point[0] == 0.0f && point[1] > 0.05f && handle_point_from_model(top), "model: on top");
		}
		expect(handle_point_for("U_M416", point) && point[2] < 0.25f, "M416: the rear T-handle");
		expect(!handle_point_from_model("U_FAMAS") && !handle_point_from_model("U_AK12"), "measured guns are not from the model");
		expect(handle_point_for("U_M416", point) && handle_point_from_model("U_M416"), "M416: its empty reload uses the bolt release: placed from the model (#88)");
		// #90: a held magazine by its body, where the game's own hand takes it.
		expect(std::fabs(magazine_hold_drop("U_FAMAS") - 0.037f) < 1e-4f, "FAMAS: held 3.7 cm below its top (its short box)");
		expect(std::fabs(magazine_hold_drop("U_M416") - 0.089f) < 1e-4f, "M416: 8.9 cm (a STANAG)");
		expect(magazine_hold_drop("U_SAR21") == 0.12f, "SAR-21: its 13.5 cm clamped to 12");
		expect(magazine_hold_drop("U_P90") == 0.0f && magazine_hold_drop("U_MX4") == 0.0f, "P90 (grab above the top), MX4 (not measured): none, the grip's own");
		expect(magazine_hold_drop("U_RFB_SP") == 0.02f, "RFB: 1.4 cm clamped to 2");
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
