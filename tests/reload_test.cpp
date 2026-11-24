// Checks the physical reload state machine (src/weapon/reload): AK and AR variants, chest stow and
// take, drop with and without drop_discards_ammo, bolt lock, bolt catch, racking and the AK flick;
// the hand gestures (src/weapon/reload_hands), the weapon profiles (src/weapon/profiles) and the
// accessory toggle by touch (src/weapon/accessory_touch) and the grenade gestures
// (src/weapon/grenade_hands), the weapon holsters (src/weapon/holsters) and the squad hand signal
// (src/weapon/hand_signal).
#include <cmath>
#include <utility>
#include <cstdio>
#include <vector>
#include <initializer_list>

#include "weapon/accessory_touch.hpp"
#include "weapon/grenade_hands.hpp"
#include "weapon/hand_signal.hpp"
#include "weapon/body_rig.hpp"
#include "weapon/holsters.hpp"
#include "weapon/profiles.hpp"
#include "weapon/reload.hpp"
#include "weapon/reload_hands.hpp"
#include "weapon/game_reload_sounds.hpp"

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

	reload_profile ak{ variant::ak, 30, true };
	reload_profile ar{ variant::ar, 30, true };

	// Every round the player still owns: in the gun, in the hand, in the reserve.
	int owned(const reload& gun)
	{
		return gun.rounds_in_gun() + gun.state().held_rounds + gun.state().palmed_rounds + gun.state().reserve;
	}

	void empty(reload& gun)
	{
		while (gun.state().chamber)
		{
			gun.fire();
		}
	}

	void test_load()
	{
		reload gun(ar);
		gun.load(31, 90);
		expect(gun.state().chamber && gun.state().magazine_rounds == 30 && gun.state().reserve == 90, "load 30+1");
		gun.load(0, 90);
		expect(!gun.state().chamber && gun.state().bolt_locked, "AR loaded empty: bolt open");
		reload k(ak);
		k.load(0, 0);
		expect(!k.state().chamber && !k.state().bolt_locked, "AK loaded empty: bolt closed");
		gun.load(99, -5);
		expect(gun.rounds_in_gun() == 31 && gun.state().reserve == 0, "load clamps");
	}

	void test_ar_empty_reload()
	{
		reload gun(ar);
		gun.load(2, 60);
		expect(gun.fire() == reload_event::shot, "AR shot 1");
		expect(gun.fire() == reload_event::bolt_locked, "AR last round locks the bolt");
		expect(gun.fire() == reload_event::dry_fire, "AR locked: dry");
		expect(gun.press_button(false) == reload_event::magazine_dropped, "AR button releases the empty magazine");
		expect(!gun.state().magazine_in && gun.state().bolt_locked, "bolt stays locked without a magazine");
		expect(gun.press_button(false) == reload_event::none, "button without a magazine does nothing");
		expect(gun.take_from_chest() == reload_event::magazine_taken && gun.state().held_rounds == 30 && gun.state().reserve == 30, "take a full magazine");
		expect(gun.insert() == reload_event::magazine_in, "insert");
		expect(!gun.state().chamber && gun.state().bolt_locked, "insert does not chamber");
		expect(gun.press_button(false) == reload_event::chambered, "second press = bolt catch");
		expect(gun.state().chamber && gun.state().magazine_rounds == 29 && !gun.state().bolt_locked, "chambered from the new magazine");
		expect(gun.press_button(false) == reload_event::magazine_dropped && gun.state().chamber, "next press releases again, chamber kept");
	}

	void test_ar_tactical_catch_and_stow()
	{
		reload gun(ar);
		gun.load(16, 30); // 15 + 1
		expect(gun.press_button(true) == reload_event::magazine_out && gun.state().held_rounds == 15, "hand on the magazine catches it");
		expect(gun.state().chamber, "round stays chambered");
		expect(gun.let_go(true) == reload_event::magazine_stowed && gun.state().reserve == 45, "stow returns its rounds");
		expect(gun.take_from_chest() == reload_event::magazine_taken && gun.state().held_rounds == 30, "take from 45");
		expect(gun.insert() == reload_event::magazine_in && gun.rounds_in_gun() == 31, "30+1 after tactical reload");
		expect(gun.state().reserve == 15, "reserve left");
	}

	void test_ar_dry_without_magazine()
	{
		reload gun(ar);
		gun.load(11, 30);
		gun.press_button(false); // magazine gone, round chambered
		expect(gun.fire() == reload_event::shot, "fire the chambered round without a magazine");
		expect(!gun.state().bolt_locked && !gun.state().chamber, "no magazine: bolt closes empty");
		gun.take_from_chest();
		gun.insert();
		// Headset 2026-11-03: the button after a new magazine dropped it instead of chambering.
		expect(gun.press_button(false) == reload_event::chambered && gun.state().chamber, "empty chamber, new magazine: the button chambers (bolt not locked)");
		expect(gun.press_button(false) == reload_event::magazine_dropped && gun.state().chamber, "then it releases, the round stays chambered");
		gun.take_from_chest();
		expect(gun.state().held_rounds == 0 && !gun.state().holding, "reserve already used up");
		// Headset 2026-11-04: a tactical reload (round chambered), new magazine, the button ejected it again.
		reload tactical(ar);
		tactical.load(11, 60);
		expect(tactical.press_button(false) == reload_event::magazine_dropped && tactical.state().chamber, "tactical: release, round stays");
		tactical.take_from_chest();
		tactical.insert();
		expect(tactical.press_button(false) == reload_event::none && tactical.state().magazine_in && tactical.state().magazine_rounds == 30,
			"tactical: the first press after the new magazine keeps it (bolt already forward)");
		expect(tactical.press_button(false) == reload_event::magazine_dropped, "tactical: the next press releases");
		reload again(ar);
		again.load(1, 30);
		again.press_button(false);
		again.fire();
		again.take_from_chest();
		again.insert();
		expect(again.rack() == reload_event::chambered && again.state().chamber && again.state().magazine_rounds == 29, "manual rack chambers");
		// #78 (owner, headset): racking an AR whose magazine is empty made no sound. The rack locks the bolt open.
		reload empty(ar);
		empty.load(1, 30);
		expect(empty.fire() == reload_event::bolt_locked, "the last round locks the bolt (no rack sound for a shot)");
		const reload_event racked = empty.rack();
		expect(racked == reload_event::racked_locked && empty.state().bolt_locked && !empty.state().chamber, "#78: a rack on the empty magazine is its own event");
		sound_cue rack_cues[2]{};
		expect(cues_for(racked, rack_cues) == 1 && rack_cues[0].event == reload_sound::bolt, "#78: and it sounds like the bolt");
		// The bolt catch pressed with an empty magazine seated: on a real AR the follower holds the catch up, the bolt
		// does not move, so there is nothing to hear (review 2026-11-16: stated, not an oversight).
		reload seated(ar);
		seated.load(1, 30);
		seated.fire();                 // empty, bolt locked
		seated.press_button(false);    // the empty one drops
		seated.take_from_chest();
		seated.insert();               // a full one: the catch chambers
		const reload_event caught = seated.press_button(false);
		expect(caught == reload_event::chambered, "the bolt catch on a full magazine chambers (bolt cue)");
		reload empty_catch(ar);
		empty_catch.load(1, 0);
		empty_catch.fire();            // bolt locked on the empty magazine still seated
		empty_catch.press_button(true); // hold it in the hand (magazine out)
		empty_catch.insert();          // the same empty magazine back in
		const reload_event dropped = empty_catch.press_button(false);
		sound_cue catch_cues[2]{};
		// Review 2: the owner asked for the bolt release to sound every time; a firm press drops the bolt on an empty
		// magazine too. It rides home on an empty chamber.
		expect(dropped == reload_event::racked_empty && !empty_catch.state().bolt_locked && !empty_catch.state().chamber
				&& cues_for(dropped, catch_cues) == 1 && catch_cues[0].event == reload_sound::bolt,
			"#78: the catch pressed on an empty magazine drops the bolt home, with the bolt sound");
		expect(empty_catch.fire() == reload_event::dry_fire, "and the chamber stays empty");
		// Review 2: releasing the empty magazine while the off hand already holds the new one (the usual empty reload)
		// was silent: the drop event has no cue and the fallback skipped it while a hand held a magazine.
		expect(release_needs_out_cue(true, false, false), "#78: the seated magazine left the gun, nothing sounded yet: magazine out");
		expect(!release_needs_out_cue(true, false, true), "already sounded (pulled into the hand, flicked, palmed): not twice");
		expect(!release_needs_out_cue(false, false, false), "a magazine dropped from the hand, none was seated: silent");
		expect(!release_needs_out_cue(true, true, false), "still seated: silent");
		reload swap(ar);
		swap.load(1, 30);
		swap.fire();
		swap.take_from_chest();        // the new one in the off hand first
		const bool was_in = swap.state().magazine_in;
		const reload_event released = swap.press_button(false);
		expect(released == reload_event::magazine_dropped && swap.state().holding, "the empty one drops while the hand holds the new one");
		expect(release_needs_out_cue(was_in, swap.state().magazine_in, false), "#78: and it sounds like a magazine coming out");
	}

	void test_ak()
	{
		reload gun(ak);
		gun.load(1, 60);
		expect(gun.fire() == reload_event::shot, "AK last round");
		expect(!gun.state().bolt_locked && !gun.state().chamber, "AK has no hold-open");
		expect(gun.press_button(false) == reload_event::none, "AK has no release button");
		expect(gun.grab_magazine() == reload_event::magazine_out && gun.state().held_rounds == 0, "AK: pull the magazine out");
		expect(gun.let_go(false) == reload_event::magazine_dropped, "drop it");
		gun.take_from_chest();
		expect(gun.insert() == reload_event::magazine_in && !gun.state().chamber, "insert, chamber still empty");
		expect(gun.fire() == reload_event::dry_fire, "must rack first");
		expect(gun.rack() == reload_event::chambered && gun.rounds_in_gun() == 30, "rack: 29+1");
		expect(gun.rack() == reload_event::round_ejected && gun.rounds_in_gun() == 29, "rack with a round in loses it");
	}

	void test_ak_flick()
	{
		reload gun(ak, { false });
		gun.load(11, 60); // 10 + 1
		gun.take_from_chest();
		const int before = owned(gun);
		expect(gun.insert() == reload_event::flicked_out, "flick knocks the old magazine out (#47: first step)");
		expect(!gun.state().magazine_in && gun.state().holding && gun.rounds_in_gun() == 1, "the new one is still in the hand, only the chambered round in the gun");
		expect(gun.insert() == reload_event::magazine_in, "second step: the new one seats");
		expect(gun.rounds_in_gun() == 31 && gun.state().reserve == 40, "new magazine in, old rounds back (discard off)");
		expect(owned(gun) == before, "flick with discard off keeps every round");
		reload strict(ak);
		strict.load(11, 60);
		strict.take_from_chest();
		strict.insert();
		strict.insert();
		expect(strict.state().reserve == 30, "flick with discard on loses the old rounds");
		reload_profile no_flick = ak;
		no_flick.flick = false;
		reload plain(no_flick);
		plain.load(11, 60);
		plain.take_from_chest();
		expect(plain.insert() == reload_event::none && plain.state().holding, "no flick: insert refused while a magazine is in");
		reload r(ar);
		r.load(11, 60);
		r.take_from_chest();
		expect(r.insert() == reload_event::none, "AR never flicks");
	}

	void test_drop_option()
	{
		reload keep(ar, { false });
		keep.load(21, 0);
		keep.press_button(false);
		expect(keep.state().reserve == 20, "discard off: dropped rounds go to the reserve");
		reload lose(ar, { true });
		lose.load(21, 0);
		lose.press_button(false);
		expect(lose.state().reserve == 0, "discard on: dropped rounds are gone");
		lose.configure({ false });
		lose.take_from_chest();
		expect(!lose.state().holding, "nothing to take from an empty reserve");
	}

	// AR palming: B with a new magazine in the hand drops the old one into the same hand.
	void test_ar_palming()
	{
		reload gun(ar);
		gun.load(31, 60);
		gun.fire();
		gun.fire();
		gun.fire();
		expect(gun.take_from_chest() == reload_event::magazine_taken && gun.state().reserve == 30, "a new magazine from the pouch");
		expect(gun.press_button(true, true) == reload_event::magazine_palmed, "B palms the old one into the hand");
		expect(gun.state().palmed && gun.state().palmed_rounds == 27 && !gun.state().magazine_in && gun.state().holding && gun.state().chamber,
			"both magazines in the hand, the round stays chambered");
		expect(gun.press_button(true, true) == reload_event::none, "a third magazine does not fit");
		expect(gun.insert() == reload_event::magazine_in, "the new one goes in");
		expect(gun.state().magazine_rounds == 30 && gun.state().holding && gun.state().held_rounds == 27 && !gun.state().palmed,
			"the old one stays in the hand");
		expect(gun.insert() == reload_event::none, "the old one cannot go into a loaded AR");
		expect(gun.let_go(true) == reload_event::magazine_stowed && gun.state().reserve == 57 && !gun.state().holding, "stowed at the pouch");

		// Right after an insert the first press is the bolt catch, but with the bolt forward on a round a
		// hand placed to palm means the release.
		gun.take_from_chest();
		expect(gun.press_button(true, true) == reload_event::magazine_palmed, "palming right after an insert releases");

		// Run dry: the bolt locks on the empty magazine, palming takes it, B chambers after the new one.
		reload dry(ar);
		dry.load(1, 60);
		expect(dry.fire() == reload_event::bolt_locked, "the last round locks the bolt");
		dry.take_from_chest();
		expect(dry.press_button(true, true) == reload_event::magazine_palmed && dry.state().palmed_rounds == 0, "the empty one palmed");
		dry.insert();
		expect(dry.press_button(false) == reload_event::chambered && dry.state().chamber, "then B is the bolt catch");

		// Letting go with both: both drop (or both stow).
		reload both(ar, { false });
		both.load(31, 60);
		both.take_from_chest();
		both.press_button(true, true);
		expect(both.let_go(false) == reload_event::magazine_dropped && both.state().reserve == 90 && !both.state().palmed,
			"both dropped, rounds back to the reserve without discard");

		// Without a magazine in the hand the flag changes nothing; an AK has no release.
		reload plain(ar);
		plain.load(31, 60);
		expect(plain.press_button(false, true) == reload_event::magazine_dropped, "no new magazine held: an ordinary release");
		reload k(ak);
		k.load(31, 60);
		k.take_from_chest();
		expect(k.press_button(true, true) == reload_event::none, "AK: no palming by button");
	}

	void test_hand_rules()
	{
		reload gun(ak);
		gun.load(31, 90);
		expect(gun.let_go(true) == reload_event::none, "let go with nothing held");
		expect(gun.insert() == reload_event::none, "insert with nothing held");
		gun.take_from_chest();
		expect(gun.take_from_chest() == reload_event::none, "one magazine per hand");
		expect(gun.grab_magazine() == reload_event::none, "cannot pull the magazine with a full hand");
		gun.let_go(true);
		expect(gun.state().reserve == 90, "taking and stowing again is neutral");
		reload r(ar);
		r.load(31, 0);
		expect(r.grab_magazine() == reload_event::none, "AR magazine is latched");
	}

	// Random-ish sequence: with discard off, rounds only leave through shots and racked rounds.
	void test_conservation()
	{
		for (const reload_profile& profile : { ak, ar })
		{
			reload gun(profile, { false });
			gun.load(31, 120);
			int total = owned(gun);
			unsigned seed = 12345;
			for (int step = 0; step < 5000; ++step)
			{
				seed = seed * 1103515245u + 12345u;
				const int before = owned(gun);
				const unsigned action = (seed >> 16) % 7;
				reload_event result = reload_event::none;
				switch (action)
				{
				case 0: result = gun.fire(); break;
				case 1: result = gun.press_button((seed >> 8) & 1, (seed >> 10) & 1); break;
				case 2: result = gun.grab_magazine(); break;
				case 3: result = gun.take_from_chest(); break;
				case 4: result = gun.let_go((seed >> 9) & 1); break;
				case 5: result = gun.insert(); break;
				case 6: result = gun.rack(); break;
				}
				// A shot (bolt_locked from the trigger is one too) or a racked-out round costs one.
				const bool spends = result == reload_event::shot || result == reload_event::round_ejected
					|| (action == 0 && result == reload_event::bolt_locked);
				// (racked_locked: a rack that found the magazine empty spends nothing)
				const int lost = before - owned(gun);
				if (lost != (spends ? 1 : 0))
				{
					expect(false, "rounds only leave by a shot or a rack");
					break;
				}
				total -= lost;
				const gun_state& s = gun.state();
				if (s.magazine_rounds < 0 || s.magazine_rounds > profile.capacity || s.held_rounds < 0 || s.reserve < 0 || (!s.magazine_in && s.magazine_rounds != 0) || (!s.holding && s.held_rounds != 0)
					|| (!s.palmed && s.palmed_rounds != 0) || (s.palmed && (!s.holding || s.magazine_in)))
				{
					expect(false, "state stays consistent");
					break;
				}
			}
			expect(total == owned(gun), "bookkeeping matches");
		}
	}
	void test_profiles()
	{
		const reload_profile ak12 = profile_for("U_AK12", 31);
		expect(ak12.kind == variant::ak && ak12.capacity == 30 && ak12.flick, "AK-12: AK, 30 rounds");
		const reload_profile scar = profile_for("U_SCAR-H", 21);
		expect(scar.kind == variant::ar && scar.capacity == 20 && !scar.flick, "SCAR-H: AR, 20 rounds");
		expect(profile_for("U_M416", 1).capacity == 1, "capacity never below 1");
		// #64: the standalone M320 is a break-open launcher; rifles and the M320 under a rifle are not.
		expect(is_break_open("U_SP_M320_HE") && is_break_open("U_SP_M320_SMK"), "the stash's M320 is break-open");
		expect(!is_break_open("U_SCAR-H") && !is_break_open("U_M416_M320_HE") && !is_break_open("U_SP_C4"), "rifles, the underslung M320 and C4 are not");
	}

	v3 operator+(v3 a, v3 b)
	{
		return { a.x + b.x, a.y + b.y, a.z + b.z };
	}

	bool close(float a, float b, float tolerance = 1e-4f)
	{
		return std::fabs(a - b) <= tolerance;
	}

	// Positions in metres: gun ahead and below the head, barrel along -z.
	constexpr v3 magwell{ 0.0f, -0.2f, -0.4f };
	constexpr v3 slide{ 0.05f, -0.12f, -0.3f };
	constexpr v3 chest{ 0.0f, -0.35f, -0.1f };
	constexpr v3 away{ -0.3f, -0.3f, -0.3f };

	hand_frame at(v3 hand, bool grip, bool button = false)
	{
		hand_frame frame;
		frame.seconds = 1.0f / 90.0f;
		frame.hands_valid = true;
		frame.off_hand = hand;
		frame.gun_hand = { 0.1f, -0.2f, -0.3f };
		frame.magwell = magwell;
		frame.has_slide = true;
		frame.slide = slide;
		frame.chest = chest;
		frame.grip = grip;
		frame.button = button;
		return frame;
	}

	// The magazine top right at the hand (the tests below place the hand at the magwell).
	hand_config flat_hold()
	{
		hand_config config;
		config.hold_ar = config.hold_ak = { 0.0f, 0.0f, 0.0f };
		return config;
	}

	bool has(const hand_result& result, reload_event event)
	{
		for (int i = 0; i < result.count; ++i)
		{
			if (result.events[i] == event)
			{
				return true;
			}
		}
		return false;
	}

	// Headset 2026-11-14: putting a magazine back was hard to find. The stow zone is 1.6x the grab reach: 18 cm from
	// the pouch puts it back (a grab there takes nothing), 25 cm drops it.
	void test_stow_zone()
	{
		const auto let_go_at = [&](v3 hand)
		{
			reload gun(ar);
			gun.load(11, 60);
			reload_hands hands(flat_hold());
			hands.update(gun, at(chest, true)); // a magazine from the pouch
			hands.update(gun, at(away, true));
			hand_result r = hands.update(gun, at(hand, true));
			const bool zone = r.stow_zone;
			r = hands.update(gun, at(hand, false));
			return std::make_pair(zone, has(r, reload_event::magazine_stowed));
		};
		const auto near = let_go_at({ chest.x - 0.18f, chest.y, chest.z });
		expect(near.first && near.second, "18 cm from the pouch: in the stow zone, put back");
		const auto far = let_go_at({ chest.x - 0.25f, chest.y, chest.z });
		expect(!far.first && !far.second, "25 cm from the pouch: not stowed");
		reload gun(ar);
		gun.load(11, 60);
		reload_hands hands(flat_hold());
		hands.update(gun, at(away, false));
		const hand_result r = hands.update(gun, at({ chest.x - 0.18f, chest.y, chest.z }, true));
		expect(!has(r, reload_event::magazine_taken), "a grab 18 cm from the pouch takes nothing (the grab reach is unchanged)");
	}

	// #57: the magazine zone runs from the low chest pouch down to the off-side hip (a capsule).
	void test_magazine_zone_to_hip()
	{
		const v3 hip{ -0.2f, -0.65f, 0.0f };
		const auto take_at = [&](v3 hand, float reach)
		{
			reload gun(ar);
			gun.load(11, 60);
			reload_hands hands(flat_hold());
			hand_frame open = at(away, false);
			open.has_chest_hip = true;
			open.chest_hip = hip;
			open.chest_reach = reach;
			hands.update(gun, open);
			hand_frame grab = at(hand, true);
			grab.has_chest_hip = true;
			grab.chest_hip = hip;
			grab.chest_reach = reach;
			return has(hands.update(gun, grab), reload_event::magazine_taken);
		};
		expect(take_at(chest, 0), "at the chest pouch: a magazine");
		expect(take_at(hip, 0), "at the off-side hip: a magazine");
		expect(take_at({ -0.1f, -0.5f, -0.05f }, 0), "halfway down to the hip: a magazine");
		expect(take_at({ hip.x - 0.1f, hip.y, hip.z + 0.05f }, 0), "a hand's width outside the hip: still within reach");
		expect(!take_at({ 0.22f, -0.65f, 0.0f }, 0), "the gun-side hip (pistol): no magazine");
		expect(!take_at({ hip.x, hip.y - 0.2f, hip.z }, 0), "20 cm below the hip: nothing");
		expect(take_at({ hip.x, hip.y - 0.2f, hip.z }, 0.22f), "a larger magazine_reach takes it there too");
		// Stowing works along the whole zone as well.
		reload gun(ar);
		gun.load(11, 60);
		reload_hands hands(flat_hold());
		const auto with_hip = [&](hand_frame f)
		{
			f.has_chest_hip = true;
			f.chest_hip = hip;
			return f;
		};
		hands.update(gun, with_hip(at(away, false)));
		hands.update(gun, with_hip(at(chest, true)));
		hands.update(gun, with_hip(at(hip, true)));
		const hand_result r = hands.update(gun, with_hip(at(hip, false)));
		expect(has(r, reload_event::magazine_stowed), "let go at the hip stows the magazine");
		expect(r.has_chest_hip && close(r.chest_hip.y, hip.y) && close(r.zone_reach[2], hand_config{}.reach_chest), "the zone's far end and reach reported for grab_debug");
	}

	void test_hands_ak()
	{
		reload gun(ak);
		gun.load(11, 60);
		reload_hands hands(flat_hold());
		hands.update(gun, at(away, false));
		hand_result r = hands.update(gun, at(magwell, true));
		expect(has(r, reload_event::magazine_out) && r.draw == magazine_draw::in_hand, "grip at the magwell pulls the AK magazine");
		expect(r.off_hand_busy, "grip is the reload's while holding");
		r = hands.update(gun, at(magwell, true));
		expect(gun.state().holding, "not pushed straight back in");
		r = hands.update(gun, at(chest, true));
		r = hands.update(gun, at(chest, false));
		expect(has(r, reload_event::magazine_stowed) && gun.state().reserve == 70, "let go at the chest stows 10 rounds");
		expect(r.draw == magazine_draw::hidden, "no magazine drawn after stowing");
		r = hands.update(gun, at(chest, true));
		expect(has(r, reload_event::magazine_taken) && gun.state().held_rounds == 30, "grip at the chest takes a magazine");
		r = hands.update(gun, at(magwell, true));
		expect(has(r, reload_event::magazine_in) && r.draw == magazine_draw::in_hand, "bring it to the magwell to insert; it slides home");
		for (int i = 0; i < 12; ++i)
		{
			r = hands.update(gun, at(magwell, true));
		}
		expect(r.draw == magazine_draw::with_gun, "seated with the gun after the slide-in");
		hands.update(gun, at(away, false));
		expect(gun.rounds_in_gun() == 31, "chambered round kept: 30+1");
		hands.update(gun, at(slide, true));
		r = hands.update(gun, at({ slide.x, slide.y, slide.z + 0.07f }, false));
		expect(has(r, reload_event::round_ejected), "pull the slide back and let go racks");
		hands.update(gun, at(slide, true));
		r = hands.update(gun, at({ slide.x, slide.y, slide.z + 0.02f }, false));
		expect(r.count == 0, "a short pull does not rack");
	}

	// An AK as the game holds it (Wep_Root +z along the barrel, here pointing away along -z): the charging
	// handle (handle_ak, 3 cm above the receiver) sits right above the magwell. The grab is measured
	// from the palm, 6 cm ahead of the drawn wrist.
	void test_hands_reach_and_overlap()
	{
		hand_frame frame = at(away, false);
		frame.has_root = true;
		frame.root = { 0.0f, -0.2f, 0.0f };
		frame.root_rotation = { 0.0f, 1.0f, 0.0f, 0.0f };
		frame.magwell = frame.root + v3{ 0.0f, 0.0f, -0.40f };
		const v3 handle = frame.root + v3{ 0.0f, 0.03f, -0.40f };
		const v3 body = frame.magwell + v3{ 0.0f, -0.08f, 0.0f };
		const auto palm_at = [&](v3 palm, bool grip)
		{
			hand_frame f = frame;
			f.grip = grip;
			f.has_palm = true;
			f.off_palm = palm;
			f.off_hand = palm + v3{ 0.0f, 0.0f, 0.06f };
			return f;
		};

		reload gun(ak);
		gun.load(11, 60);
		reload_hands hands(flat_hold());
		hands.update(gun, palm_at(away, false));
		hand_result r = hands.update(gun, palm_at(body + v3{ 0.06f, -0.04f, 0.03f }, true));
		expect(has(r, reload_event::magazine_out), "palm on the magazine body, 9 cm off its middle, pulls it (not the handle above)");

		reload fourth(ak);
		fourth.load(11, 60);
		reload_hands guard(flat_hold());
		guard.update(fourth, palm_at(away, false));
		r = guard.update(fourth, palm_at(body + v3{ 0.0f, 0.0f, -0.06f }, true));
		expect(r.count == 0 && !r.off_hand_busy && fourth.state().magazine_in, "palm ahead of the magwell is on the handguard: no magazine, the grip is the two-handed grab's");

		reload second(ak);
		second.load(11, 60);
		reload_hands racker(flat_hold());
		racker.update(second, palm_at(away, false));
		racker.update(second, palm_at(handle + v3{ 0.04f, 0.03f, 0.0f }, true));
		r = racker.update(second, palm_at(handle + v3{ 0.04f, 0.03f, 0.05f }, false));
		expect(has(r, reload_event::round_ejected) && second.state().magazine_in, "palm at the handle racks, the magazine stays in");

		// Headset 2026-11-04: an underhand rack missed the AK's handle. Its zone now sits out on the
		// handle's side (here world +x) and is larger: 10 cm out and 6 cm below the knob still racks.
		reload under(ak);
		under.load(0, 60);
		under.take_from_chest();
		under.insert(); // knocks the empty one out (#47)
		under.insert(); // seats the new one: an empty chamber, it needs charging
		reload_hands underhand(flat_hold());
		underhand.update(under, palm_at(away, false));
		r = underhand.update(under, palm_at(handle + v3{ 0.10f, -0.06f, 0.0f }, true));
		expect(r.racking && under.state().magazine_in, "underhand: the palm out beside and below the handle starts the rack");
		r = underhand.update(under, palm_at(handle + v3{ 0.10f, -0.06f, 0.05f }, false));
		expect(has(r, reload_event::chambered), "underhand: pulled back and let go, it chambers");

		reload third(ak);
		third.load(11, 60);
		reload_hands far(flat_hold());
		far.update(third, palm_at(away, false));
		r = far.update(third, palm_at(body + v3{ 0.0f, -0.16f, -0.03f }, true));
		expect(r.count == 0, "16 cm below the magazine is out of reach");
	}

	// #41 (owner 2026-11-15): the grenade and knife copies are drawn ahead of their grab points, on the chest.
	void test_slot_model_point()
	{
		const v3 point{ 0.1f, -0.3f, -0.1f };
		const v3 ahead = slot_model_point(point, 0.0f, 0.08f);
		expect(close(ahead.x, 0.1f) && close(ahead.y, -0.3f) && close(ahead.z, -0.18f), "slot model: facing -z, 8 cm further along -z");
		const v3 left = slot_model_point(point, 1.5707963f, 0.08f);
		expect(close(left.x, 0.02f) && close(left.z, -0.1f), "slot model: turned 90 degrees left, 8 cm along -x");
		const v3 same = slot_model_point(point, 0.7f, 0.0f);
		expect(close(same.x, point.x) && close(same.z, point.z), "slot model: 0 ahead keeps the grab point");
	}

	// Owner 2026-11-15: the charging handle has an overhand zone at its level, out on its side and a little
	// above, and a larger underhand zone below it, short along the barrel (the handguard ahead stays the
	// two-handed grab's). Gun frame as the identity here: +x left, +y up, +z along the barrel.
	// #99: a pistol held two-handed: the off hand cupping the gun hand sits on the magazine well in the grip; with the
	// magazine seated and nothing in that hand, the reload does not take it (the cup does), and a magazine brought there
	// still goes in.
	void test_pistol_cup_zone()
	{
		reload pistol(ar);
		pistol.load(16, 45);
		reload_hands hands(flat_hold());
		hand_frame f = at(magwell, false);
		f.cup_grip = true;
		hand_result r = hands.update(pistol, f);
		expect(!r.off_hand_busy && r.in_zone != hand_result::zone::magazine, "pistol cup: the open hand at the grip is not the magazine's");
		f.grip = true;
		r = hands.update(pistol, f);
		expect(!pistol.state().holding && pistol.state().magazine_in && !r.off_hand_busy, "pistol cup: closing it there takes nothing");
		hand_frame plain = at(magwell, false);
		reload rifle(ar);
		rifle.load(16, 45);
		reload_hands rifle_hands(flat_hold());
		expect(rifle_hands.update(rifle, plain).off_hand_busy, "a rifle: the open hand at the magazine is the reload's");
		pistol.press_button(false); // B drops the old one
		hand_frame open_hand = at(chest, false);
		open_hand.cup_grip = true;
		hands.update(pistol, open_hand); // let go first: a new grip press at the pouch
		hand_frame g = at(chest, true);
		g.cup_grip = true;
		hands.update(pistol, g);
		expect(pistol.state().holding, "pistol cup: a new magazine from the pouch");
		g = at(magwell, true);
		g.cup_grip = true;
		r = hands.update(pistol, g);
		expect(pistol.state().magazine_in && !pistol.state().holding, "pistol cup: brought to the well it goes in");
	}

	// #96: the SRS's turn bolt by the hand (its game cycle: Wep_Slide turned 56.4 degrees about its -z through (0, 0.006),
	// then 10.3 cm back). Gun frame = the reload frame here. The hand takes the knob down, turns it up, draws it back,
	// pushes it forward and turns it down; each step's event in order, and the trigger only works at the end.
	void test_turn_bolt_hands()
	{
		const float lift = 56.4f * 0.017453293f;
		const v3 pivot{ 0.0f, 0.006f, 0.0f };
		const v3 knob_up{ -0.084f, 0.116f, 0.243f }; // where the game's hand rides it back (profiles)
		const v3 knob_rest = bolt_knob_turned(knob_up, pivot, -lift);
		expect(close(bolt_lift_of(knob_rest, pivot, knob_rest, lift), 0.0f, 1e-4f) && close(bolt_lift_of(knob_up, pivot, knob_rest, lift), 1.0f, 1e-3f),
			"bolt: the hand's turn about the bore: 0 at the knob's rest, 1 at the game's lifted knob");
		expect(knob_rest.y < knob_up.y && knob_rest.x < knob_up.x, "bolt: down is lower and further out on the right (-x) than up");
		reload_profile srs = ar;
		srs.manual_cycle = true;
		srs.bolt_turn = true;
		srs.hold = hold_open::no;
		reload gun(srs);
		gun.load(6, 20);
		expect(gun.fire() == reload_event::shot && !gun.state().chamber, "bolt: one shot, nothing fed");
		reload_hands hands(flat_hold());
		const auto bolt_frame = [&](v3 hand, bool grip)
		{
			hand_frame f = at(hand, grip);
			f.has_root = true;
			f.root = {};
			f.has_handle_point = true;
			f.handle_point = knob_up;
			f.bolt_turn = true;
			f.bolt_pivot = pivot;
			f.bolt_lift_radians = lift;
			f.bolt_travel = 0.103f;
			return f;
		};
		std::vector<reload_event> seen;
		hand_result r;
		const auto step = [&](v3 hand, bool grip)
		{
			r = hands.update(gun, bolt_frame(hand, grip));
			for (int i = 0; i < r.count; ++i)
			{
				seen.push_back(r.events[i]);
			}
			return r;
		};
		step(away, false);
		step(knob_rest, false);
		expect(r.in_zone == hand_result::zone::handle && r.bolt_drawn, "bolt: the open hand at the knob is in its zone");
		step(knob_rest, true);
		expect(r.bolt_held && !gun.state().bolt_up, "bolt: taken, still locked");
		step(knob_rest + v3{ 0.0f, 0.0f, -(0.06f) }, true);
		expect(r.bolt_pull == 0.0f && !gun.state().bolt_back, "bolt: locked, pulling does not draw it back");
		for (int k = 1; k <= 10; ++k)
		{
			step(bolt_knob_turned(knob_rest, pivot, lift * k / 10.0f), true);
		}
		expect(gun.state().bolt_up && close(r.bolt_turn, lift, 1e-3f), "bolt: turned up the game's 56.4 degrees: unlocked");
		for (int k = 1; k <= 10; ++k)
		{
			step(knob_up + v3{ 0.0f, 0.0f, -(0.0103f * k) }, true);
		}
		expect(gun.state().bolt_back && r.rack_at_rear == false && close(r.bolt_pull, 0.103f, 1e-4f), "bolt: drawn fully back");
		expect(gun.fire() == reload_event::dry_fire, "bolt: back, the trigger does nothing");
		step(knob_up + v3{ 0.0f, 0.0f, -(0.103f) }, false);
		step(knob_up + v3{ 0.0f, 0.0f, -(0.103f) } + v3{ 0.2f, 0.0f, 0.0f }, false);
		expect(gun.state().bolt_back && close(r.bolt_pull, 0.103f, 1e-4f), "bolt: let go at the rear, it stays there");
		step(r.knob_now, false);
		step(r.knob_now, true);
		for (int k = 9; k >= 0; --k)
		{
			step(knob_up + v3{ 0.0f, 0.0f, -(0.0103f * k) }, true);
		}
		expect(!gun.state().bolt_back && gun.state().chamber && gun.state().bolt_up, "bolt: pushed forward, a round chambered, still up");
		expect(gun.fire() == reload_event::dry_fire && gun.state().chamber, "bolt: not turned down: no shot");
		for (int k = 9; k >= 0; --k)
		{
			step(bolt_knob_turned(knob_rest, pivot, lift * k / 10.0f), true);
		}
		expect(!gun.state().bolt_up && r.bolt_turn < 0.2f * lift, "bolt: turned down: locked");
		step(knob_rest, false);
		const std::vector<reload_event> order{ reload_event::bolt_opened, reload_event::chambered, reload_event::bolt_closed };
		std::vector<reload_event> filtered;
		for (const reload_event e : seen)
		{
			if (e != reload_event::none)
			{
				filtered.push_back(e);
			}
		}
		expect(filtered == order, "bolt: events in order: opened, (spent case out), chambered, closed");
		expect(gun.fire() == reload_event::shot, "bolt: locked again, it fires");
	}

	void test_rack_zones()
	{
		const hand_config config;
		const q4 straight{};
		const v3 knob{ -0.02f, 0.06f, 0.53f }; // the AK-12's, on the gun's right
		const float side = rack_side(knob, true);
		expect(side == -1.0f && rack_side({ 0.035f, 0.04f, 0.57f }, false) == 1.0f && rack_side({ 0.0f, 0.06f, 0.2f }, false) == 0.0f
				&& rack_side({ 0.0f, 0.03f, 0.4f }, true) == -1.0f,
			"side: from the knob's x (SCAR-H left, AK-12 right); a centred AK handle counts as the right");
		const auto zone = [&](v3 offset) { return rack_zone_of(knob + offset, knob, straight, side, config).zone; };
		expect(zone({ -0.03f, 0.03f, 0.0f }) == rack_zone::over, "over: out on the handle's side, above it");
		expect(zone({ 0.0f, 0.0f, 0.0f }) == rack_zone::over, "over: on the knob");
		expect(zone({ 0.03f, 0.03f, 0.0f }) == rack_zone::over, "over: from across the top");
		expect(zone({ -0.03f, 0.13f, 0.0f }) == rack_zone::none, "over: 10 cm above the zone is out");
		expect(zone({ -0.02f, -0.05f, 0.0f }) == rack_zone::under, "under: below the knob");
		expect(zone({ -0.06f, -0.17f, 0.02f }) == rack_zone::under, "under: 17 cm below and 6 cm out still racks");
		expect(zone({ -0.02f, -0.24f, 0.0f }) == rack_zone::none, "under: 24 cm below is out");
		expect(zone({ -0.02f, -0.06f, 0.08f }) == rack_zone::none, "under: 8 cm ahead along the barrel is the handguard's");
		expect(zone({ -0.02f, -0.06f, -0.08f }) == rack_zone::none, "under: 8 cm behind is out");
		expect(zone({ -0.03f, -0.02f, 0.0f }) == rack_zone::under, "both zones, below the knob's height: underhand");
		expect(zone({ -0.03f, 0.005f, 0.0f }) == rack_zone::over, "both zones, at the knob's height: overhand");
		// Turned gun: the zones turn with it (barrel along world -z, the handle's side along world +x).
		const q4 turned{ 0.0f, 1.0f, 0.0f, 0.0f };
		const v3 knob_world{ 0.02f, 0.06f, -0.53f };
		expect(rack_zone_of(knob_world + v3{ 0.02f, -0.12f, 0.0f }, knob_world, turned, side, config).zone == rack_zone::under, "turned gun: below is under");
		expect(rack_zone_of(knob_world + v3{ 0.03f, 0.03f, 0.0f }, knob_world, turned, side, config).zone == rack_zone::over, "turned gun: out on the side and up is over");

		// Through reload_hands: the zone the grab starts in is the stroke's hold.
		hand_frame frame = at(away, false);
		frame.has_root = true;
		frame.root = { 0.0f, -0.2f, 0.0f };
		frame.root_rotation = turned;
		frame.magwell = frame.root + v3{ 0.0f, 0.0f, -0.45f };
		frame.has_handle_point = true;
		frame.handle_point = knob;
		const v3 handle = frame.root + v3{ 0.02f, 0.06f, -0.53f };
		const auto palm_at = [&](v3 palm, bool grip)
		{
			hand_frame f = frame;
			f.grip = grip;
			f.has_palm = true;
			f.off_palm = palm;
			f.off_hand = palm;
			return f;
		};
		const auto stroke = [&](v3 offset, bool& underhand, bool& chambered)
		{
			reload gun(ak);
			gun.load(0, 60);
			gun.take_from_chest();
			gun.insert();
			gun.insert(); // seated, nothing chambered: the underhand zone is there
			reload_hands hands(flat_hold());
			hands.update(gun, palm_at(away, false));
			hand_result r = hands.update(gun, palm_at(handle + offset, true));
			underhand = r.racking && r.rack_underhand;
			const bool racking = r.racking;
			r = hands.update(gun, palm_at(handle + offset + v3{ 0.0f, 0.0f, 0.06f }, true));
			underhand = underhand && r.rack_underhand;
			r = hands.update(gun, palm_at(handle + offset + v3{ 0.0f, 0.0f, 0.06f }, false));
			chambered = has(r, reload_event::chambered);
			return racking;
		};
		bool underhand = false, chambered = false;
		expect(stroke({ 0.03f, 0.03f, 0.0f }, underhand, chambered) && !underhand && chambered, "overhand grab: racks, held overhand");
		expect(stroke({ 0.03f, -0.15f, 0.0f }, underhand, chambered) && underhand && chambered, "underhand grab 15 cm below: racks, held underhand to the release");
		reload busy_gun(ak);
		busy_gun.load(0, 60);
		busy_gun.take_from_chest();
		busy_gun.insert();
		busy_gun.insert();
		reload_hands busy(flat_hold());
		const hand_result open = busy.update(busy_gun, palm_at(handle + v3{ 0.03f, -0.15f, 0.0f }, false));
		expect(open.off_hand_busy && open.in_zone == hand_result::zone::handle && open.has_under_zone, "open hand in the underhand zone: the grip is the reload's (no handguard grab there)");

		// Per-gun magazine grab (weapon/profiles magazine_grab_for): the magazine zone sits at the measured offset from the
		// seated Wep_Mag, turned with the gun; unmeasured guns keep magazine_grab_drop straight below it.
		{
			reload_hands plain(flat_hold());
			const hand_result below = plain.update(busy_gun, palm_at(away, false));
			const v3 drop{ below.zone_centre[0].x - frame.magwell.x, below.zone_centre[0].y - frame.magwell.y, below.zone_centre[0].z - frame.magwell.z };
			expect(std::fabs(drop.x) < 1e-4f && std::fabs(drop.y + config.magazine_grab_drop) < 1e-4f && std::fabs(drop.z) < 1e-4f,
				"no measured grab: the zone is magazine_grab_drop below the magwell");
			hand_frame measured = palm_at(away, false);
			measured.has_magazine_grab = true;
			measured.magazine_grab = { 0.033f, -0.052f, 0.022f }; // the SCAR-H's, left of and below its Wep_Mag
			reload_hands per_gun(flat_hold());
			const hand_result grab = per_gun.update(busy_gun, measured);
			const v3 offset{ grab.zone_centre[0].x - frame.magwell.x, grab.zone_centre[0].y - frame.magwell.y, grab.zone_centre[0].z - frame.magwell.z };
			// The turned gun (180 degrees about y): gun +x is world -x, gun +z is world -z.
			expect(std::fabs(offset.x + 0.033f) < 1e-4f && std::fabs(offset.y + 0.052f) < 1e-4f && std::fabs(offset.z + 0.022f) < 1e-4f,
				"measured grab: the zone moves to the gun's offset, turned with the gun");
		}

		// #69 (owner 2026-11-15): per-gun zones. The AK-12's reach well under the gun; the SCAR-H's overhand zone
		// sits higher, so a grab at the gun's bottom is not the handle.
		rack_zone_shape ak12, scar, none;
		expect(rack_zones_for("U_AK12", config, ak12) && rack_zones_for("U_SCAR-H", config, scar) && !rack_zones_for("U_M416", config, none), "per-gun zones for the AK-12 and SCAR-H only");
		expect(scar.under_radii.y == config.rack_under_radii.y && scar.under_offset.y == config.rack_under_offset.y, "the SCAR-H keeps the default underhand zone (from hand_config, one source)");
		// The AK-12's overhand reach (12 cm, default 9): 10.5 cm from the zone's centre, out on the handle's side.
		const v3 ak_over_centre = knob + v3{ -0.03f, 0.03f, 0.0f };
		expect(rack_zone_of(ak_over_centre + v3{ -0.105f, 0.0f, 0.0f }, knob, straight, side, config).over_distance > config.rack_over_reach, "default reach: 10.5 cm from the centre is outside");
		hand_config with_ak = config;
		with_ak.rack_over_offset = ak12.over_offset;
		with_ak.rack_over_reach = ak12.over_reach;
		with_ak.rack_under_offset = ak12.under_offset;
		with_ak.rack_under_radii = ak12.under_radii;
		expect(rack_zone_of(knob + v3{ -0.04f, -0.30f, 0.0f }, knob, straight, side, with_ak).zone == rack_zone::under, "AK-12: 30 cm below the knob is still the underhand zone");
		expect(rack_zone_of(knob + v3{ -0.04f, -0.30f, 0.0f }, knob, straight, side, config).zone == rack_zone::none, "default zones: 30 cm below is out");
		expect(rack_zone_of(ak_over_centre + v3{ -0.105f, 0.0f, 0.0f }, knob, straight, side, with_ak).zone == rack_zone::over, "AK-12: 10.5 cm from the overhand centre is in its 12 cm reach");
		const v3 scar_knob{ 0.035f, 0.04f, 0.57f };
		const float scar_side = rack_side(scar_knob, false);
		hand_config with_scar = config;
		with_scar.rack_over_offset = scar.over_offset;
		with_scar.rack_over_reach = scar.over_reach;
		const v3 low = scar_knob + v3{ 0.03f, -0.025f, 0.0f }; // a hand just under the knob's height (the gun's bottom grab)
		// Overhand reach only (the underhand region exists only while the gun needs charging, decided in reload_hands).
		expect(rack_zone_of(low, scar_knob, straight, scar_side, config).over_distance < config.rack_over_reach, "SCAR-H, default zones: a hand just under the knob was in the overhand zone");
		expect(!(rack_zone_of(low, scar_knob, straight, scar_side, with_scar).over_distance < with_scar.rack_over_reach), "SCAR-H zones: not any more");
		expect(rack_zone_of(scar_knob + v3{ 0.03f, 0.06f, 0.0f }, scar_knob, straight, scar_side, with_scar).zone == rack_zone::over, "SCAR-H: above and beside the knob is overhand");

		// Through reload_hands: frame.has_rack_shape selects the gun's zones.
		reload big(ak);
		big.load(0, 60);
		big.take_from_chest();
		big.insert();
		big.insert();
		reload_hands big_hands(flat_hold());
		hand_frame shaped = palm_at(handle + v3{ 0.04f, -0.30f, 0.0f }, false);
		shaped.has_rack_shape = true;
		shaped.rack_shape = ak12;
		const hand_result deep = big_hands.update(big, shaped);
		expect(deep.in_zone == hand_result::zone::handle && deep.under_radii.y == ak12.under_radii.y, "frame zones: the AK-12's reach 30 cm under the knob");

		// Loaded and chambered, the space under the handle is the handguard's again (SCAR-H, monitor 2026-11-15);
		// the overhand zone still racks.
		reload loaded(ak);
		loaded.load(11, 60);
		reload_hands ready(flat_hold());
		const hand_result below = ready.update(loaded, palm_at(handle + v3{ 0.03f, -0.15f, 0.0f }, false));
		expect(!below.off_hand_busy && below.in_zone == hand_result::zone::none && !below.has_under_zone, "loaded: no underhand zone, the grip is the two-handed grab's");
		bool loaded_under = false, loaded_chambered = false;
		reload overhand_gun(ak);
		overhand_gun.load(11, 60);
		reload_hands overhand(flat_hold());
		overhand.update(overhand_gun, palm_at(away, false));
		hand_result over = overhand.update(overhand_gun, palm_at(handle + v3{ 0.03f, 0.03f, 0.0f }, true));
		loaded_under = over.rack_underhand;
		over = overhand.update(overhand_gun, palm_at(handle + v3{ 0.03f, 0.03f, 0.06f }, false));
		loaded_chambered = has(over, reload_event::round_ejected);
		expect(!loaded_under && loaded_chambered, "loaded: the overhand zone still racks (a round ejected)");
		gun_state open_bolt;
		open_bolt.chamber = false;
		expect(needs_rack(open_bolt), "needs_rack: nothing chambered");
		gun_state locked;
		locked.bolt_locked = true;
		expect(needs_rack(locked) && !needs_rack(gun_state{}), "needs_rack: the bolt held open; a chambered gun does not");

		// #97: a handle reachable from both sides (FAMAS, AK-12): the mirrored knob (gun x flipped) racks too, held on that side.
		const v3 mirrored_world = frame.root + v3{ -0.02f, 0.06f, -0.53f }; // gun (0.02, 0.06, 0.53) through the turned frame
		const auto mirrored_stroke = [&](bool both_sides, float& side_seen, float& knob_x)
		{
			reload loaded(ak);
			loaded.load(31, 60); // chambered: only the overhand zone (no underhand fallback to reach across)
			reload_hands hands(flat_hold());
			hand_frame f0 = palm_at(away, false);
			f0.handle_both_sides = both_sides;
			hands.update(loaded, f0);
			hand_frame f1 = palm_at(mirrored_world + v3{ -0.03f, 0.03f, 0.0f }, true);
			f1.handle_both_sides = both_sides;
			hand_result r = hands.update(loaded, f1);
			side_seen = r.rack_side;
			knob_x = r.handle_in_gun.x;
			const bool racking = r.racking;
			hand_frame f2 = palm_at(mirrored_world + v3{ -0.03f, 0.03f, 0.06f }, false);
			f2.handle_both_sides = both_sides;
			r = hands.update(loaded, f2);
			return racking && has(r, reload_event::round_ejected);
		};
		// #97 review: the side is chosen from zones that can rack: a chambered gun's underhand zone cannot.
		rack_zone_test own_over, across_under, across_over, own_none;
		own_over.zone = rack_zone::over; own_over.over_distance = 0.08f;
		across_under.zone = rack_zone::under; across_under.over_distance = 0.05f;
		across_over.zone = rack_zone::over; across_over.over_distance = 0.05f;
		expect(!prefer_mirrored(own_over, across_under, false), "mirror: a loaded gun keeps the own overhand zone over the far underhand one");
		expect(prefer_mirrored(own_over, across_under, true), "mirror: an empty gun may take the nearer underhand zone across");
		expect(prefer_mirrored(own_over, across_over, false) && !prefer_mirrored(across_over, own_over, false), "mirror: the nearer overhand zone wins");
		expect(prefer_mirrored(own_none, across_over, false) && !prefer_mirrored(own_over, own_none, false), "mirror: only a zone that is there");
		float side_seen = 0.0f, knob_x = 0.0f;
		expect(!mirrored_stroke(false, side_seen, knob_x), "one-sided handle (M416-like): the gun's other side does not rack");
		expect(mirrored_stroke(true, side_seen, knob_x) && side_seen == 1.0f && close(knob_x, 0.02f, 1e-5f),
			"both sides: the mirrored knob racks, held on the gun's left");
	}

	// #40: the open hand snaps onto the seated magazine by distance; not while holding, gripping or with no
	// magazine in, and not on the handguard.
	void test_magazine_snap()
	{
		hand_frame frame = at(away, false);
		frame.has_root = true;
		frame.root = { 0.0f, -0.2f, 0.0f };
		frame.root_rotation = { 0.0f, 1.0f, 0.0f, 0.0f };
		frame.magwell = frame.root + v3{ 0.0f, 0.0f, -0.40f };
		const v3 body = frame.magwell + v3{ 0.0f, -0.08f, 0.0f };
		const auto palm_at = [&](v3 palm, bool grip)
		{
			hand_frame f = frame;
			f.grip = grip;
			f.has_palm = true;
			f.off_palm = palm;
			f.off_hand = palm + v3{ 0.0f, 0.0f, 0.06f };
			return f;
		};
		reload gun(ak);
		gun.load(11, 60);
		reload_hands hands(flat_hold());
		hand_result r = hands.update(gun, palm_at(away, false));
		expect(r.magazine_snap == 0.0f, "away: no snap");
		r = hands.update(gun, palm_at(body + v3{ 0.095f, 0.0f, 0.0f }, false));
		expect(r.in_zone == hand_result::zone::magazine && r.magazine_snap > 0.0f && r.magazine_snap < 0.05f, "at the zone's edge: barely");
		r = hands.update(gun, palm_at(body + v3{ 0.075f, 0.0f, 0.0f }, false));
		expect(std::fabs(r.magazine_snap - 0.5f) < 0.01f, "half way between the edge and full: half");
		r = hands.update(gun, palm_at(body + v3{ 0.03f, 0.0f, 0.0f }, false));
		expect(std::fabs(r.magazine_snap - 1.0f) < 1e-4f, "within 5 cm: full");
		r = hands.update(gun, palm_at(body + v3{ 0.03f, 0.0f, 0.0f }, true));
		expect(has(r, reload_event::magazine_out) && std::fabs(r.magazine_snap - 1.0f) < 1e-4f, "the grab takes it: the snapped hold stays, no jump");
		r = hands.update(gun, palm_at(body + v3{ 0.0f, -0.075f, 0.0f }, true));
		expect(gun.state().holding && std::fabs(r.magazine_snap - 0.5f) < 0.01f, "pulled 7.5 cm out: the hold is half way back to the controller");
		r = hands.update(gun, palm_at(body + v3{ 0.0f, -0.12f, 0.0f }, true));
		expect(r.magazine_snap == 0.0f, "out of the zone: the controller's hold");
		r = hands.update(gun, palm_at(body + v3{ 0.0f, -0.03f, 0.0f }, true));
		expect(r.magazine_snap == 0.0f, "brought back while still holding it: no snap again");
		r = hands.update(gun, palm_at(chest, true));
		r = hands.update(gun, palm_at(chest, false));
		r = hands.update(gun, palm_at(body + v3{ 0.03f, 0.0f, 0.0f }, false));
		expect(r.magazine_snap == 0.0f, "no magazine in the gun: nothing to snap to");

		reload guarded(ak);
		guarded.load(11, 60);
		reload_hands on_guard(flat_hold());
		r = on_guard.update(guarded, palm_at(body + v3{ 0.0f, 0.0f, -0.06f }, false));
		expect(r.magazine_snap == 0.0f, "on the handguard: no snap");

		// An AR's magazine stays in the gun under the grip alone (the release button takes it): the hand
		// stays snapped while squeezing, and keeps the hold when B drops it into the hand.
		reload rifle(ar);
		rifle.load(21, 60);
		reload_hands ar_hands(flat_hold());
		ar_hands.update(rifle, palm_at(away, false));
		r = ar_hands.update(rifle, palm_at(body + v3{ 0.03f, 0.0f, 0.0f }, false));
		expect(std::fabs(r.magazine_snap - 1.0f) < 1e-4f, "AR: snapped");
		r = ar_hands.update(rifle, palm_at(body + v3{ 0.03f, 0.0f, 0.0f }, true));
		expect(rifle.state().magazine_in && std::fabs(r.magazine_snap - 1.0f) < 1e-4f, "AR: squeezing keeps the snap, the magazine waits for the release");
		hand_frame release = palm_at(body + v3{ 0.03f, 0.0f, 0.0f }, true);
		release.button = true;
		r = ar_hands.update(rifle, release);
		expect(rifle.state().holding && std::fabs(r.magazine_snap - 1.0f) < 1e-4f, "AR: B drops it into the snapped hand, no jump");
	}

	void test_hands_ar_drop_and_fall()
	{
		reload gun(ar);
		gun.load(16, 30);
		reload_hands hands(flat_hold());
		hands.update(gun, at(away, false));
		hand_result r = hands.update(gun, at(away, false, true));
		expect(has(r, reload_event::magazine_dropped) && r.draw == magazine_draw::falling, "X drops the AR magazine, it falls");
		const float start = r.magazine_position.y;
		for (int i = 0; i < 20; ++i)
		{
			r = hands.update(gun, at(away, false));
		}
		expect(r.draw == magazine_draw::falling && r.magazine_position.y < start - 0.1f, "falling down");
		for (int i = 0; i < 60; ++i)
		{
			r = hands.update(gun, at(away, false));
		}
		expect(r.draw == magazine_draw::hidden, "gone after the fall");
		gun.load(16, 30);
		hands.reset();
		hands.update(gun, at(magwell, true));
		r = hands.update(gun, at(magwell, true, true));
		expect(has(r, reload_event::magazine_out) && r.draw == magazine_draw::in_hand, "X with the hand on the magazine catches it");
		hands.update(gun, at(away, true));
		r = hands.update(gun, at(away, false));
		expect(has(r, reload_event::magazine_dropped) && r.draw == magazine_draw::falling, "let go away from the chest drops it");
		r = hands.update(gun, at(away, true));
		expect(!r.off_hand_busy && r.count == 0, "a grip away from gun and chest stays the game's");
		r = hands.update(gun, at(magwell, false));
		expect(r.off_hand_busy, "hand at the magwell: the grip is claimed before it closes");
	}

	// Headset 2026-11-14: a released magazine not caught dropped straight down. It is pushed out of the well
	// (eject_speed along the gun's down axis) and keeps the gun's motion: a flick of the gun throws it further.
	void test_eject_flight()
	{
		{
			reload gun(ar);
			gun.load(16, 30);
			reload_hands hands(flat_hold());
			hands.update(gun, at(away, false));
			hand_result r = hands.update(gun, at(away, false, true));
			const float start = r.magazine_position.y;
			for (int i = 0; i < 6; ++i)
			{
				r = hands.update(gun, at(away, false));
			}
			expect(r.draw == magazine_draw::falling && r.magazine_position.y < start - 0.08f, "ejected: 8 cm out in 0.07 s (gravity alone: 2 cm)");
		}
		{
			// The gun swung to the right at 2 m/s as the button goes.
			reload gun(ar);
			gun.load(16, 30);
			reload_hands hands(flat_hold());
			hand_frame f = at(away, false);
			for (int i = 0; i < 10; ++i)
			{
				f.magwell.x = magwell.x + 2.0f * f.seconds * static_cast<float>(i);
				hands.update(gun, f);
			}
			f.magwell.x += 2.0f * f.seconds;
			f.button = true;
			hand_result r = hands.update(gun, f);
			const float start_x = r.magazine_position.x;
			f.button = false;
			for (int i = 0; i < 9; ++i)
			{
				r = hands.update(gun, f);
			}
			expect(r.draw == magazine_draw::falling && r.magazine_position.x > start_x + 0.12f, "flicked: it flies on with the gun's swing");
		}
	}

	// The falling magazine, whichever mesh draws it (the rig's own, or the extra one while the gun holds its magazine).
	bool falling_at(const hand_result& r, v3& where)
	{
		if (r.draw == magazine_draw::falling)
		{
			where = r.magazine_position;
			return true;
		}
		if (r.extra == hand_result::extra_magazine::falling)
		{
			where = r.extra_position;
			return true;
		}
		return false;
	}

	// #47: a magazine let go while the hand swings keeps flying that way; with a floor it lands and rests.
	void test_hands_toss()
	{
		reload gun(ar);
		gun.load(16, 30);
		reload_hands hands(flat_hold());
		hands.update(gun, at(chest, true)); // takes a magazine from the pouch
		hand_result r{};
		v3 hand = away;
		for (int i = 0; i < 30; ++i) // the hand settles away from the chest (the test jumps there in one frame)
		{
			hands.update(gun, at(hand, true));
		}
		for (int i = 0; i < 12; ++i) // swinging right at 1.8 m/s
		{
			hand.x += 0.02f;
			hand_frame f = at(hand, true);
			f.floor_valid = true;
			f.floor_y = -1.6f;
			r = hands.update(gun, f);
		}
		hand_frame let_go = at(hand, false);
		let_go.floor_valid = true;
		let_go.floor_y = -1.6f;
		r = hands.update(gun, let_go);
		expect(has(r, reload_event::magazine_dropped), "toss: let go away from the chest drops it");
		const float released_x = hand.x;
		for (int i = 0; i < 18; ++i) // 0.2 s
		{
			hand_frame f = at(hand, false);
			f.floor_valid = true;
			f.floor_y = -1.6f;
			r = hands.update(gun, f);
		}
		v3 at_now{};
		expect(falling_at(r, at_now) && at_now.x > released_x + 0.25f, "toss: 0.2 s later it is >25 cm further right (the hand's 1.8 m/s)");
		for (int i = 0; i < 90; ++i) // 1 s more: on the floor
		{
			hand_frame f = at(hand, false);
			f.floor_valid = true;
			f.floor_y = -1.6f;
			r = hands.update(gun, f);
		}
		expect(falling_at(r, at_now) && close(at_now.y, -1.6f + 0.08f, 1e-3f), "toss: it rests on the floor, still drawn");

		// Flight off: the old straight drop.
		reload still_gun(ar);
		still_gun.load(16, 30);
		reload_hands old(flat_hold());
		old.update(still_gun, at(chest, true));
		hand = away;
		for (int i = 0; i < 12; ++i)
		{
			hand.x += 0.02f;
			hand_frame f = at(hand, true);
			f.throw_flight = false;
			old.update(still_gun, f);
		}
		hand_frame off = at(hand, false);
		off.throw_flight = false;
		old.update(still_gun, off);
		for (int i = 0; i < 18; ++i)
		{
			hand_frame f = at(hand, false);
			f.throw_flight = false;
			r = old.update(still_gun, f);
		}
		expect(falling_at(r, at_now) && close(at_now.x, hand.x, 1e-3f), "flight off: straight down");
	}

	// #61: with the game's world and a ground, a tossed magazine is a rigid body on that ground: it lands on a
	// raised step it was tossed onto (not the STAGE floor), lies there long after the old 4 s while looked at,
	// and goes once not looked at after 30 s. The world here is LOCAL shifted by 100 m and turned 90 degrees.
	class step_ground : public bfvr::physics::ground_source
	{
	public:
		float floor{}, step{}, step_from_x{};
		bfvr::physics::ground_plane below(const v3& p) override
		{
			return { bfvr::physics::ground_plane::kind::plane, { p.x, p.x > step_from_x ? step : floor, p.z }, { 0, 1, 0 } };
		}
	};

	void test_hands_loose_on_ground()
	{
		const v3 origin{ 100.0f, 50.0f, -20.0f };
		const q4 to_local{ 0.0f, 0.7071068f, 0.0f, 0.7071068f }; // 90 degrees about y
		step_ground ground;
		const auto world_frame = [&](v3 hand, bool grip, bool looking)
		{
			hand_frame f = at(hand, grip);
			f.has_world = true;
			f.world_origin = origin;
			f.world_to_local = to_local;
			f.ground = &ground;
			f.floor_valid = true;
			f.floor_y = -1.6f;
			f.has_view = true;
			f.view_position = { 0.0f, 0.0f, 0.0f };
			f.view_forward = looking ? v3{ 0.0f, -1.0f, -0.3f } : v3{ 0.0f, 1.0f, 0.0f };
			return f;
		};
		// The world's floor is the STAGE floor's height; the step covers the world beyond x = 99.5 at 0.6 m higher.
		ground.floor = origin.y - 1.6f;
		ground.step = origin.y - 1.0f;
		ground.step_from_x = 1e9f;
		reload gun(ar);
		gun.load(16, 30);
		reload_hands hands(flat_hold());
		hands.update(gun, world_frame(chest, true, true));
		v3 hand = away;
		for (int i = 0; i < 30; ++i)
		{
			hands.update(gun, world_frame(hand, true, true));
		}
		hand_result r = hands.update(gun, world_frame(hand, false, true));
		expect(has(r, reload_event::magazine_dropped) && r.loose, "ground: let go, it is in the world");
		for (int i = 0; i < 180 && !r.loose_resting; ++i)
		{
			r = hands.update(gun, world_frame(hand, false, true));
		}
		v3 lying{};
		expect(r.loose_resting && falling_at(r, lying), "ground: comes to rest within 2 s, drawn");
		expect(lying.y > -1.6f && lying.y < -1.6f + 0.1f, "ground: lies on the floor under it, in LOCAL");
		for (int i = 0; i < 90 * 40; ++i)
		{
			r = hands.update(gun, world_frame(hand, false, true));
		}
		expect(r.loose && falling_at(r, lying), "ground: still there after 40 s while looked at");
		r = hands.update(gun, world_frame(hand, false, false));
		expect(!r.loose, "ground: gone once not looked at");

		// On a raised step: the ground under it, not the STAGE floor.
		ground.step_from_x = -1e9f; // the whole world is the step
		reload gun2(ar);
		gun2.load(16, 30);
		reload_hands hands2(flat_hold());
		hands2.update(gun2, world_frame(chest, true, true));
		for (int i = 0; i < 30; ++i)
		{
			hands2.update(gun2, world_frame(hand, true, true));
		}
		r = hands2.update(gun2, world_frame(hand, false, true));
		for (int i = 0; i < 180 && !r.loose_resting; ++i)
		{
			r = hands2.update(gun2, world_frame(hand, false, true));
		}
		expect(r.loose_resting && falling_at(r, lying) && lying.y > -1.0f && lying.y < -1.0f + 0.1f, "step: lies on the step, 60 cm above the STAGE floor");
	}

	void test_hands_flick()
	{
		reload gun(ak, { false });
		gun.load(11, 60);
		reload_hands hands(flat_hold());
		hand_result r = hands.update(gun, at(chest, true));
		expect(r.draw == magazine_draw::in_hand && r.extra == hand_result::extra_magazine::seated, "extra meshes: a new one taken, the old one still drawn in the gun");
		r = hands.update(gun, at(magwell, true));
		expect(has(r, reload_event::flicked_out) && r.draw == magazine_draw::in_hand, "#47 step one: the new magazine knocks the old one out and stays in the hand");
		expect(r.extra == hand_result::extra_magazine::falling && r.extra_position.y <= magwell.y + 1e-4f, "extra meshes: the knocked-out one falls from the magwell");
		bool seated = false;
		for (int i = 0; i < 60 && !seated; ++i) // held at the magwell: no second motion
		{
			r = hands.update(gun, at(magwell, true));
			seated = has(r, reload_event::magazine_in);
		}
		expect(!seated && gun.state().holding, "#47 step two needs its own motion: held still at the magwell, the new one does not seat (owner 2026-11-15)");
		for (int i = 1; i <= 6 && !seated; ++i) // pushing on up past the magwell, 1 cm a frame (monitor 2026-11-16)
		{
			r = hands.update(gun, at({ magwell.x, magwell.y + 0.01f * static_cast<float>(i), magwell.z }, true));
			seated = has(r, reload_event::magazine_in);
		}
		r = hands.update(gun, at(magwell, true)); // and back down to the magwell without leaving it downwards
		seated = seated || has(r, reload_event::magazine_in);
		expect(!seated, "#47: pushing on up past the magwell after the flick is no second motion");
		r = hands.update(gun, at({ magwell.x, magwell.y - 0.03f, magwell.z }, true));
		r = hands.update(gun, at(magwell, true));
		expect(!has(r, reload_event::magazine_in), "3 cm away and back is not yet the second motion");
		r = hands.update(gun, at({ magwell.x, magwell.y - 0.07f, magwell.z }, true));
		int seat_frame = -1;
		for (int i = 0; i < 30 && !seated; ++i)
		{
			r = hands.update(gun, at(magwell, true));
			if (has(r, reload_event::magazine_in))
			{
				seated = true;
				seat_frame = i;
			}
		}
		expect(seated && seat_frame == 0, "#47 step two: brought 7 cm away and pushed in again, the new one seats");
		for (int i = 0; i < 200 && r.extra != hand_result::extra_magazine::none; ++i)
		{
			r = hands.update(gun, at(magwell, false));
		}
		expect(r.extra == hand_result::extra_magazine::none, "extra meshes: the fall ends");
		expect(gun.rounds_in_gun() == 31 && gun.state().reserve == 40, "old rounds back (discard off)");
		hand_frame lost = at(magwell, false);
		lost.hands_valid = false;
		reload_hands blind(flat_hold());
		r = blind.update(gun, lost);
		lost.grip = true;
		r = blind.update(gun, lost);
		expect(r.count == 0, "no gestures without hand tracking");
	}
	void test_accessory_touch()
	{
		accessory_frame frame;
		frame.hands_valid = true;
		frame.has_optic = true;
		frame.optic = { 0.0f, -0.12f, -0.35f };
		frame.has_rail = true;
		frame.rail = { -0.03f, -0.18f, -0.7f };
		frame.magwell = magwell;
		accessory_touch touch;
		frame.off_hand = away;
		accessory_result r = touch.update(frame);
		expect(!r.busy && !r.toggle, "hand away: nothing");
		frame.off_hand = { 0.0f, -0.10f, -0.36f };
		r = touch.update(frame);
		expect(r.busy && !r.toggle, "hand on the optic claims the grip before it closes");
		frame.grip = true;
		r = touch.update(frame);
		expect(r.toggle, "grip on the optic toggles");
		r = touch.update(frame);
		expect(!r.toggle && r.busy, "held grip toggles once");
		frame.grip = false;
		touch.update(frame);
		frame.off_hand = { -0.02f, -0.17f, -0.68f };
		frame.grip = true;
		expect(touch.update(frame).toggle, "grip on the front rail toggles");
		frame.grip = false;
		touch.update(frame);
		frame.off_hand = { 0.0f, -0.17f, -0.39f }; // 6 cm from the optic, 3 cm from the magwell
		frame.grip = true;
		r = touch.update(frame);
		expect(!r.busy && !r.toggle, "closer to the magwell: the reload's");
		frame.grip = false;
		touch.update(frame);
		frame.off_hand = { 0.0f, -0.10f, -0.36f };
		frame.holding = true;
		frame.grip = true;
		expect(!touch.update(frame).toggle, "no toggling with a magazine in the hand");
		frame.holding = false;
		frame.has_optic = frame.has_rail = false;
		frame.grip = false;
		touch.update(frame);
		frame.grip = true;
		expect(!touch.update(frame).toggle, "a gun without optic or rail bones has nothing to touch");
	}
	struct grenade_rig
	{
		grenade_hands hands;
		grenade_frame frame;
		grenade_rig()
		{
			frame.seconds = 1.0f / 90.0f;
			frame.hands_valid = true;
			frame.belt = { 0.0f, -0.7f, -0.1f };
			frame.off_hand = { -0.25f, -0.4f, -0.3f };
			frame.gun_hand = { 0.15f, -0.25f, -0.35f };
		}
		grenade_result step() { return hands.update(frame); }
		// Moves the off hand to target over the given time (90 Hz), returns the last result.
		grenade_result move(v3 target, float seconds)
		{
			const v3 start = frame.off_hand;
			const int steps = static_cast<int>(seconds * 90.0f + 0.5f);
			grenade_result r;
			for (int i = 1; i <= steps; ++i)
			{
				const float f = static_cast<float>(i) / static_cast<float>(steps);
				frame.off_hand = { start.x + (target.x - start.x) * f, start.y + (target.y - start.y) * f, start.z + (target.z - start.z) * f };
				r = step();
			}
			return r;
		}
	};

	void test_grenade_throw()
	{
		grenade_rig g;
		g.step();
		g.frame.off_hand = g.frame.belt;
		grenade_result r = g.step();
		expect(r.off_busy && !r.holding, "hand at the belt claims the grip");
		g.frame.off_grip = true;
		r = g.step();
		expect(r.event == grenade_event::taken && r.holding, "grip at the belt takes a grenade");
		r = g.move({ -0.1f, -0.3f, -0.3f }, 0.3f);
		// Gun hand grips next to the grenade and pulls 7 cm away.
		g.frame.gun_hand = { -0.05f, -0.28f, -0.3f };
		r = g.step();
		expect(r.gun_busy, "gun hand at the grenade claims its grip");
		g.frame.gun_grip = true;
		r = g.step();
		expect(!r.armed, "gripping the pin alone does not arm");
		g.frame.gun_hand = { 0.03f, -0.25f, -0.3f };
		r = g.step();
		expect(r.event == grenade_event::pin_pulled && r.armed, "pulling the pin away arms");
		g.frame.gun_grip = false;
		g.step();
		// Throw: 0.5 m forward-up in 0.1 s (about 5 m/s), let go while moving.
		r = g.move({ -0.1f, -0.1f, -0.75f }, 0.1f);
		g.frame.off_grip = false;
		r = g.step();
		expect(r.event == grenade_event::thrown, "fast release throws");
		const float speed = std::sqrt(r.velocity.x * r.velocity.x + r.velocity.y * r.velocity.y + r.velocity.z * r.velocity.z);
		expect(speed > 3.5f && speed < 6.5f && r.velocity.z < -3.0f && r.velocity.y > 1.0f, "throw velocity follows the hand");
		expect(!r.holding && !r.off_busy, "hand empty after the throw");
	}

	// Owner 2026-11-24: the pin by hand was hard. A squeeze made a moment before the hand reaches the grenade (the
	// usual motion: let go of the gun, close the hand on the way to the ring) takes the pin; an old one does not.
	void test_grenade_pin_early_squeeze()
	{
		for (int pass = 0; pass < 2; ++pass)
		{
			const bool late = pass == 1;
			grenade_rig g;
			g.frame.gun_grip = true; // the gun hand holds the gun
			g.step();
			g.frame.off_hand = g.frame.belt;
			g.frame.off_grip = true;
			g.step();
			g.move({ -0.1f, -0.3f, -0.3f }, 0.3f);
			g.frame.gun_grip = false; // lets go of the gun
			g.step();
			g.frame.gun_grip = true; // and closes 20 cm short of the grenade
			g.frame.gun_hand = { 0.1f, -0.28f, -0.3f };
			g.step();
			for (int i = 0; i < (late ? 60 : 18); ++i) // 0.2 s on the way, or held 0.67 s before arriving
			{
				g.step();
			}
			g.frame.gun_hand = { -0.05f, -0.28f, -0.3f };
			g.step();
			g.frame.gun_hand = { 0.03f, -0.25f, -0.3f };
			const grenade_result r = g.step();
			if (late)
			{
				expect(!r.armed, "a squeeze held long before arriving (holding the gun) does not take the pin");
			}
			else
			{
				expect(r.event == grenade_event::pin_pulled && r.armed, "squeezed on the way, arriving at the grenade: pin pulled");
			}
		}
	}

	// Headset 2026-11-14: looking down tipped every zone forward and down with the eyes. The level head is where
	// the eyes would be with the head level, about the neck: the same for any pitch and roll, turning with the yaw.
	void test_level_head()
	{
		const v3 neck{ 0.3f, 1.5f, -0.2f };
		const auto eyes_for = [&](float yaw_degrees, float pitch_degrees, float roll_degrees, q4& q)
		{
			const float y = 0.5f * yaw_degrees * 0.017453293f, p = 0.5f * pitch_degrees * 0.017453293f, r = 0.5f * roll_degrees * 0.017453293f;
			const q4 qy{ 0.0f, std::sin(y), 0.0f, std::cos(y) }, qp{ std::sin(p), 0.0f, 0.0f, std::cos(p) }, qr{ 0.0f, 0.0f, std::sin(r), std::cos(r) };
			const auto mul = [](q4 a, q4 b) { return q4{ a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y, a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
				a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w, a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z }; };
			q = mul(mul(qy, qp), qr);
			const v3 v{ 0.0f, eye_above_neck, -eye_ahead_of_neck };
			const float tx = 2.0f * (q.y * v.z - q.z * v.y), ty = 2.0f * (q.z * v.x - q.x * v.z), tz = 2.0f * (q.x * v.y - q.y * v.x);
			return v3{ neck.x + v.x + q.w * tx + (q.y * tz - q.z * ty), neck.y + v.y + q.w * ty + (q.z * tx - q.x * tz),
				neck.z + v.z + q.w * tz + (q.x * ty - q.y * tx) };
		};
		q4 level_q, down_q, side_q;
		const v3 level = eyes_for(0.0f, 0.0f, 0.0f, level_q);
		const v3 down = eyes_for(0.0f, -60.0f, 0.0f, down_q);
		expect(level.y - down.y > 0.08f, "looking down 60 degrees drops the eyes ~10 cm (the problem)");
		const v3 a = level_head(level, level_q), b = level_head(down, down_q);
		expect(close(a.x, level.x, 1e-3f) && close(a.y, level.y, 1e-3f) && close(a.z, level.z, 1e-3f), "level: the eyes themselves");
		expect(close(b.x, level.x, 1e-3f) && close(b.y, level.y, 1e-3f) && close(b.z, level.z, 1e-3f), "looking down 60 degrees: where the eyes are when level");
		const v3 tilted = eyes_for(90.0f, -45.0f, 20.0f, side_q);
		const v3 c = level_head(tilted, side_q);
		expect(close(c.y, level.y, 1e-3f) && close(c.x, neck.x - eye_ahead_of_neck, 1e-3f) && close(c.z, neck.z, 1e-3f),
			"turned 90 degrees left, down and rolled: level, ahead of the neck to the left");
	}

	// The pin pulled with the teeth (owner's idea, 2026-11-14): the grenade held to the mouth a moment, then yanked
	// away, arms it; passing by the mouth or holding it there without the yank does not; the hands' pull still works.
	void test_grenade_teeth()
	{
		grenade_rig g;
		g.frame.has_mouth = true;
		g.frame.mouth = { 0.0f, -0.08f, -0.09f };
		g.step();
		g.frame.off_hand = g.frame.belt;
		g.step();
		g.frame.off_grip = true;
		grenade_result r = g.step();
		expect(r.event == grenade_event::taken, "teeth: a grenade taken");
		// Swept past the mouth quickly: no bite.
		r = g.move({ 0.0f, -0.08f, -0.12f }, 0.05f);
		r = g.move({ -0.25f, -0.35f, -0.35f }, 0.1f);
		expect(!r.armed, "teeth: swept past the mouth, still pinned");
		r = g.move({ 0.0f, -0.09f, -0.12f }, 0.2f);
		for (int i = 0; i < 27; ++i) // 0.3 s at the mouth
		{
			r = g.step();
		}
		expect(r.pin_bitten && !r.armed, "teeth: held at the mouth, the pin is in the teeth");
		r = g.move({ -0.25f, -0.3f, -0.35f }, 0.15f);
		expect(r.armed, "teeth: yanked away, armed");
		bool pulled = false;
		grenade_rig h;
		h.frame.has_mouth = true;
		h.frame.mouth = { 0.0f, -0.08f, -0.09f };
		h.step();
		h.frame.off_hand = h.frame.belt;
		h.step();
		h.frame.off_grip = true;
		h.step();
		h.move({ 0.0f, -0.09f, -0.12f }, 0.2f);
		for (int i = 0; i < 27; ++i)
		{
			h.step();
		}
		// Moved away by the hand pull path still counts once: the event comes once.
		for (int i = 0; i < 30; ++i)
		{
			const v3 at{ -0.02f * static_cast<float>(i), -0.09f - 0.01f * static_cast<float>(i), -0.12f };
			h.frame.off_hand = at;
			const grenade_result step = h.step();
			pulled = pulled || step.event == grenade_event::pin_pulled;
		}
		expect(pulled, "teeth: the pin_pulled event comes (its sound and buzz)");
	}

	// Empty hands (#38): with the gun dropped or slung the gun hand takes the grenade, the off hand pulls the
	// pin, the gun hand throws. Without either_hand the gun hand at the belt takes nothing.
	void test_grenade_either_hand()
	{
		grenade_rig plain;
		plain.step();
		plain.frame.gun_hand = plain.frame.belt;
		plain.frame.gun_grip = true;
		grenade_result r = plain.step();
		expect(!r.holding, "with the gun in hand the gun hand never takes a grenade");

		grenade_rig g;
		g.frame.either_hand = true;
		g.step();
		g.frame.gun_hand = g.frame.belt;
		r = g.step();
		expect(r.gun_busy && !r.holding, "the free gun hand at the belt claims its grip");
		g.frame.gun_grip = true;
		r = g.step();
		expect(r.event == grenade_event::taken && r.holding && r.hand == 1, "the free gun hand takes the grenade");
		g.frame.gun_hand = { 0.1f, -0.3f, -0.3f };
		g.step();
		g.frame.off_hand = { 0.05f, -0.28f, -0.3f };
		r = g.step();
		expect(r.off_busy, "the off hand at the grenade claims its grip");
		g.frame.off_grip = true;
		g.step();
		g.frame.off_hand = { -0.03f, -0.25f, -0.3f };
		r = g.step();
		expect(r.event == grenade_event::pin_pulled && r.armed, "the off hand pulls the pin");
		g.frame.off_grip = false;
		g.step();
		// Throw with the gun hand: 0.5 m forward-up in 0.1 s.
		const v3 start = g.frame.gun_hand;
		const v3 end{ 0.1f, -0.1f, -0.75f };
		for (int i = 1; i <= 9; ++i)
		{
			const float f = static_cast<float>(i) / 9.0f;
			g.frame.gun_hand = { start.x + (end.x - start.x) * f, start.y + (end.y - start.y) * f, start.z + (end.z - start.z) * f };
			g.step();
		}
		g.frame.gun_grip = false;
		r = g.step();
		expect(r.event == grenade_event::thrown && r.velocity.z < -3.0f && close(r.position.x, 0.1f, 1e-3f), "the gun hand throws it, from where it is");
	}

	void test_grenade_drop_and_put_back()
	{
		grenade_rig g;
		g.frame.off_hand = g.frame.belt;
		g.step();
		g.frame.off_grip = true;
		g.step();
		g.move({ -0.1f, -0.3f, -0.3f }, 0.4f);
		g.frame.off_grip = false;
		grenade_result r = g.step();
		expect(r.event == grenade_event::put_back, "pin in: back to the pouch");
		g.frame.off_hand = g.frame.belt;
		g.frame.off_grip = true;
		g.step();
		g.move({ -0.1f, -0.3f, -0.3f }, 0.4f);
		g.frame.gun_hand = { -0.06f, -0.3f, -0.3f };
		g.frame.gun_grip = true;
		g.step();
		g.frame.gun_hand = { 0.02f, -0.3f, -0.3f };
		g.step();
		g.frame.gun_grip = false;
		for (int i = 0; i < 20; ++i)
		{
			g.step(); // hand still
		}
		g.frame.off_grip = false;
		r = g.step();
		expect(r.event == grenade_event::dropped, "pin out, slow release drops it");
		// A throw whose hand stops 0.1 s before the fingers open still counts.
		grenade_rig late;
		late.frame.off_hand = late.frame.belt;
		late.step();
		late.frame.off_grip = true;
		late.step();
		late.move({ -0.1f, -0.3f, -0.3f }, 0.4f);
		late.frame.gun_hand = { -0.06f, -0.3f, -0.3f };
		late.frame.gun_grip = true;
		late.step();
		late.frame.gun_hand = { 0.02f, -0.3f, -0.3f };
		late.step();
		late.frame.gun_grip = false;
		late.move({ -0.1f, -0.1f, -0.8f }, 0.1f);
		for (int i = 0; i < 9; ++i)
		{
			late.step();
		}
		late.frame.off_grip = false;
		expect(late.step().event == grenade_event::thrown, "fastest motion in the last 0.15 s is the throw");
		grenade_rig none;
		none.frame.have_grenade = false;
		none.frame.off_hand = none.frame.belt;
		none.step();
		none.frame.off_grip = true;
		expect(none.step().event == grenade_event::none, "no grenades left: nothing to take");
		grenade_rig lost;
		lost.frame.off_hand = lost.frame.belt;
		lost.step();
		lost.frame.off_grip = true;
		lost.step();
		lost.frame.hands_valid = false;
		expect(lost.step().event == grenade_event::put_back, "tracking lost: the grenade goes back, never thrown");
	}
	holster draw_at(holsters& h, holster_frame frame, v3 hand)
	{
		frame.gun_hand = hand;
		frame.gun_grip = false;
		h.update(frame);
		frame.gun_grip = true;
		const holster drawn = h.update(frame).draw;
		frame.gun_grip = false;
		h.update(frame);
		return drawn;
	}

	void test_holsters()
	{
		holster_frame frame;
		frame.head_valid = frame.hand_valid = true;
		holsters h;
		// #57 layout: primary at the sling, secondary at the gun-side hip, gadget 1 over the gun-side
		// shoulder, gadget 2 over the off-side shoulder, knife high on the off-side chest.
		expect(draw_at(h, frame, sling_point) == holster::primary, "at the sling: primary");
		expect(draw_at(h, frame, { 0.12f, -0.47f, -0.15f }) == holster::primary, "a little off the sling: primary");
		expect(draw_at(h, frame, { 0.24f, -0.72f, 0.02f }) == holster::secondary, "right hip: secondary");
		expect(draw_at(h, frame, { -0.10f, -0.27f, -0.10f }) == holster::knife, "high left chest: knife");
		expect(draw_at(h, frame, { 0.2f, 0.05f, 0.15f }) == holster::gadget1, "over the right shoulder: gadget 1");
		expect(draw_at(h, frame, { -0.2f, 0.05f, 0.15f }) == holster::gadget2, "over the left shoulder: gadget 2");
		expect(draw_at(h, frame, { -0.22f, -0.72f, 0.02f }) == holster::none, "left hip: no holster any more (magazines)");
		expect(draw_at(h, frame, magazine_pouch) == holster::none && draw_at(h, frame, grenade_pouch) == holster::none, "the off hand's pouches are no holsters");
		expect(draw_at(h, frame, { 0.15f, -0.25f, -0.35f }) == holster::none, "aiming position: nothing");
		// #71 (owner 2026-11-16: the shoulder spots barely worked): the gadget zones at least twice as large (31 cm).
		expect(draw_at(h, frame, { 0.20f, 0.33f, 0.15f }) == holster::gadget1, "#71: 28 cm above the gun-side shoulder spot: gadget 1");
		expect(draw_at(h, frame, { 0.45f, 0.05f, 0.30f }) == holster::gadget1, "#71: far out and back over the gun-side shoulder: gadget 1");
		expect(draw_at(h, frame, { -0.48f, 0.05f, 0.15f }) == holster::gadget2, "#71: 28 cm out from the off-side shoulder spot: gadget 2");
		expect(draw_at(h, frame, { 0.08f, -0.12f, -0.12f }) == holster::none, "#71: the gun hand at the cheek (aiming): no gadget");
		expect(draw_at(h, frame, { 0.0f, -0.08f, -0.08f }) == holster::none, "#71: at the mouth (the pin with the teeth): no gadget");
		expect(draw_at(h, frame, { 0.0f, 0.0f, -0.15f }) == holster::none, "#71: in front of the face: no gadget");
		// The pistol and the rifle: 30 cm apart; a reach for the pistol around the hip never draws the rifle.
		{
			const v3 hip{ 0.22f, -0.75f, 0.0f };
			const v3 around[] = { { 0.0f, 0.0f, 0.0f }, { 0.08f, 0.0f, 0.0f }, { 0.0f, -0.08f, 0.0f }, { 0.0f, 0.0f, 0.08f }, { 0.0f, 0.0f, -0.08f },
				{ -0.05f, 0.05f, 0.0f }, { 0.0f, 0.08f, 0.0f } };
			bool pistol_only = true;
			for (const v3& d : around)
			{
				pistol_only &= draw_at(h, frame, { hip.x + d.x, hip.y + d.y, hip.z + d.z }) == holster::secondary;
			}
			expect(pistol_only, "within 8 cm of the hip: always the pistol");
			bool rifle_only = true;
			for (const v3& d : around)
			{
				rifle_only &= draw_at(h, frame, { sling_point.x - d.x, sling_point.y - d.y, sling_point.z - d.z }) == holster::primary;
			}
			expect(rifle_only, "within 8 cm of the sling: always the rifle");
		}
		frame.gun_hand = { 0.15f, -0.25f, -0.35f };
		frame.gun_grip = true;
		h.update(frame);
		frame.gun_hand = sling_point;
		expect(h.update(frame).draw == holster::none, "a grip held into the zone does not draw");
		frame.gun_grip = false;
		expect(h.update(frame).zone == holster::primary, "zone reported while the hand is there");
		// #49: a grip at the selected weapon's own holster with the hand empty takes it, not "already in hand".
		expect(draw_for(false, false) == draw_action::switch_weapon && draw_for(false, true) == draw_action::switch_weapon,
			"another weapon: switch to it");
		expect(draw_for(true, true) == draw_action::take_into_hand, "the selected weapon on the sling: into the hand");
		expect(draw_for(true, false) == draw_action::none, "the selected weapon in the hand or on the floor: nothing");

		holster_frame left = frame;
		left.left_handed = true;
		holsters mirrored;
		expect(draw_at(mirrored, left, { -sling_point.x, sling_point.y, sling_point.z }) == holster::primary, "left-handed: primary at the sling on the left");
		expect(draw_at(mirrored, left, { -0.22f, -0.75f, 0.0f }) == holster::secondary, "left-handed: pistol on the left hip");
		expect(draw_at(mirrored, left, { -0.2f, 0.05f, 0.15f }) == holster::gadget1, "left-handed: gadget 1 over the left shoulder");
		expect(draw_at(mirrored, left, { 0.2f, 0.05f, 0.15f }) == holster::gadget2, "left-handed: gadget 2 over the right shoulder");
		expect(draw_at(mirrored, left, { 0.10f, -0.27f, -0.10f }) == holster::knife, "left-handed: knife on the right chest");

		holsters turning;
		holster_frame turn = frame;
		turning.update(turn);
		turn.head_yaw = 0.3f;
		turning.update(turn);
		expect(std::fabs(turning.body_yaw()) < 1e-5f, "small head turn: body stays");
		for (int i = 0; i <= 20; ++i)
		{
			turn.head_yaw = 1.5707963f * static_cast<float>(i) / 20.0f;
			turning.update(turn);
		}
		expect(std::fabs(turning.body_yaw() - (1.5707963f - 0.6f)) < 1e-4f, "big head turn: body follows to the slack");
		// Body yaw 0.97 rad (turned left): gadget 1 sits right-back of that body.
		const float b = turning.body_yaw();
		const v3 shoulder{ 0.2f * std::cos(b) + 0.15f * std::sin(b), 0.05f, -0.2f * std::sin(b) + 0.15f * std::cos(b) };
		expect(draw_at(turning, turn, shoulder) == holster::gadget1, "zones turn with the body");
		// grab_debug rings: the reported centres are where the zones are (turned body, both hands).
		turn.gun_hand = { 5.0f, 5.0f, 5.0f };
		const holster_result rings = turning.update(turn);
		const int gadget1_zone = static_cast<int>(holster::gadget1) - static_cast<int>(holster::primary);
		expect(rings.has_centres && std::fabs(rings.centres[gadget1_zone].x - shoulder.x) < 1e-4f && std::fabs(rings.centres[gadget1_zone].z - shoulder.z) < 1e-4f
			&& std::fabs(rings.radii[gadget1_zone] - 0.31f) < 1e-6f, "ring centre and radius of the gun-side shoulder holster (#71: 31 cm)");
		expect(std::fabs(rings.radii[0] - 0.14f) < 1e-6f, "the primary's ring at the sling is 14 cm");
		holster_frame mirrored_ring = left;
		mirrored_ring.gun_hand = { 5.0f, 5.0f, 5.0f };
		holsters mirrored_rings;
		const holster_result left_rings = mirrored_rings.update(mirrored_ring);
		expect(left_rings.has_centres && std::fabs(left_rings.centres[2].x - 0.10f) < 1e-4f, "left-handed: the knife ring on the right chest");
		holster_frame blind = frame;
		blind.head_valid = false;
		expect(draw_at(h, blind, sling_point) == holster::none, "no head tracking: no holsters");

		// The off hand draws the gadgets on its side, and only while it is free.
		const auto off_draw = [](holsters& zones, holster_frame f, v3 hand, bool busy)
		{
			f.hand_valid = false;
			f.off_hand_valid = true;
			f.off_hand = hand;
			f.off_busy = busy;
			f.off_grip = false;
			zones.update(f);
			f.off_grip = true;
			const holster drawn = zones.update(f).draw;
			f.off_grip = false;
			zones.update(f);
			return drawn;
		};
		holsters off;
		expect(off_draw(off, frame, { -0.2f, 0.05f, 0.15f }, false) == holster::gadget2, "off hand over its shoulder: gadget 2");
		expect(off_draw(off, frame, { 0.2f, 0.05f, 0.15f }, false) == holster::gadget1, "off hand across to the gun-side shoulder: gadget 1");
		expect(off_draw(off, frame, { -0.2f, 0.05f, 0.15f }, true) == holster::none, "a busy off hand draws nothing");
		expect(off_draw(off, frame, { -0.22f, -0.75f, 0.0f }, false) == holster::none, "off hand at its hip: magazines, no holster");
		expect(off_draw(off, frame, sling_point, false) == holster::none, "the primary at the sling is the gun hand's");
		expect(off_draw(off, frame, { -0.10f, -0.27f, -0.10f }, false) == holster::none, "the knife is the gun hand's (cross draw)");
		expect(off_draw(off, frame, grenade_pouch, false) == holster::none
				&& off_draw(off, frame, { grenade_pouch.x, grenade_pouch.y + 0.06f, grenade_pouch.z }, false) == holster::none,
			"#71: the off hand at the grenade pouch (and 6 cm above it) takes no gadget");

		// Chest rig: the pouches sit in the body frame, apart from each other and from the off hand's holsters.
		const v3 head{ 0.1f, 1.6f, -0.2f };
		const v3 pouch = body_point(head, 1.5707963f, grenade_pouch, false);
		expect(std::fabs(pouch.x - (head.x + grenade_pouch.z)) < 1e-5f && std::fabs(pouch.z - (head.z - grenade_pouch.x)) < 1e-5f,
			"body turned left 90 degrees: the pouch offsets turn with it");
		const auto gap = [](v3 a, v3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };
		const hand_config reach;
		const grenade_config grenade_reach;
		expect(gap(grenade_pouch, magazine_pouch) > reach.reach_chest + grenade_reach.reach_belt, "grenade and magazine pouches do not overlap");

		// #46: a calibrated body 10 % smaller pulls every point 10 % closer to the head; 1 leaves them.
		const bfvr::body::body_size smaller_body = bfvr::body::size_for({ 0.9f * bfvr::body::reference_stature });
		const v3 small = body_point(head, 0.0f, magazine_pouch, false, smaller_body);
		expect(std::fabs(small.y - (head.y + 0.9f * magazine_pouch.y)) < 1e-5f && std::fabs(small.x - (head.x + 0.9f * magazine_pouch.x)) < 1e-5f
				&& std::fabs(small.z - (head.z + 0.9f * magazine_pouch.z)) < 1e-5f, "body scale 0.9: the pouch 10 % closer");
		const v3 same = body_point(head, 0.0f, magazine_pouch, false);
		expect(std::fabs(same.y - (head.y + magazine_pouch.y)) < 1e-5f && std::fabs(same.z - (head.z + magazine_pouch.z)) < 1e-5f, "reference body: as laid out");
		holster_frame scaled = frame;
		scaled.head = {};
		scaled.head_yaw = 0.0f;
		scaled.size = smaller_body;
		holsters smaller;
		const holster_result placed = smaller.update(scaled);
		const int knife_zone = static_cast<int>(holster::knife) - static_cast<int>(holster::primary);
		const int hip_zone = static_cast<int>(holster::secondary) - static_cast<int>(holster::primary);
		expect(placed.has_centres && std::fabs(placed.centres[knife_zone].y - 0.9f * -0.27f) < 1e-4f, "body scale 0.9: the knife holster 10 % higher up the chest");

		// #50: measures set by hand move only their zones: a deeper chest moves the pouches and the knife
		// forward (-z), lower hips the hip holsters down; the over-shoulder zones stay with the height.
		const bfvr::body::body_size custom = bfvr::body::size_for({ 0.0f, 0.0f, 0.15f, 0.85f });
		const v3 deep = body_point(head, 0.0f, magazine_pouch, false, custom);
		expect(std::fabs(deep.z - (head.z + 1.5f * magazine_pouch.z)) < 1e-5f && std::fabs(deep.y - (head.y + magazine_pouch.y)) < 1e-5f, "chest 0.15 m: the pouch 50 % further forward, same height");
		holster_frame custom_frame = scaled;
		custom_frame.size = custom;
		holsters fitted;
		const holster_result moved = fitted.update(custom_frame);
		expect(moved.has_centres && std::fabs(moved.centres[hip_zone].y - -0.85f) < 1e-4f, "hips 0.85 m below the eyes: the hip holster there");
		expect(std::fabs(moved.centres[0].y - placed.centres[0].y / 0.9f) < 1e-4f, "the primary over the shoulder stays at the reference height");
	}
	void test_hand_signal()
	{
		hand_signal signal;
		signal_frame frame;
		frame.head_valid = frame.hand_valid = true;
		const auto squeeze = [&](v3 hand)
		{
			frame.off_hand = hand;
			frame.off_grip = false;
			signal.update(frame, 0.02f);
			frame.off_grip = true;
			const signal_result r = signal.update(frame, 0.02f);
			frame.off_grip = false;
			signal.update(frame, 0.02f);
			return r;
		};
		expect(squeeze({ -0.1f, 0.15f, -0.5f }).order, "arm raised ahead + grip: order");
		expect(!squeeze({ -0.1f, 0.15f, -0.5f }).order, "cooldown: no second order at once");
		for (int i = 0; i < 60; ++i)
		{
			signal.update(frame, 0.02f);
		}
		expect(squeeze({ 0.0f, 0.2f, -0.45f }).order, "after the cooldown: order again");
		for (int i = 0; i < 60; ++i)
		{
			signal.update(frame, 0.02f);
		}
		expect(!squeeze({ -0.1f, -0.15f, -0.45f }).order, "support hand on the gun (below the eyes): no order");
		for (int i = 0; i < 60; ++i)
		{
			signal.update(frame, 0.02f);
		}
		// Headset 2026-11-14: a fist raised just above the head, not stretched out, gave no order.
		expect(squeeze({ -0.1f, 0.12f, -0.15f }).order, "fist raised over the head, 15 cm ahead: order");
		for (int i = 0; i < 60; ++i)
		{
			signal.update(frame, 0.02f);
		}
		expect(!squeeze({ -0.1f, 0.15f, -0.05f }).order, "hand beside the head (5 cm ahead): no order");
		expect(!squeeze({ -0.1f, -0.05f, -0.45f }).order, "hand ahead but below the eyes (a raised gun's support hand): no order");
		expect(!squeeze({ -0.6f, 0.15f, -0.4f }).order, "hand out to the side: no order");
		frame.head_yaw = 1.5707963f; // looking along -x
		expect(squeeze({ -0.5f, 0.15f, 0.0f }).order, "follows the head's yaw");
		frame.off_hand = { -0.5f, 0.15f, 0.0f };
		expect(signal.update(frame, 0.02f).raised, "raised pose reported (grip claimed)");
		frame.head_valid = false;
		expect(!signal.update(frame, 0.02f).raised, "no head tracking: nothing");
	}
	void test_hold_and_handle()
	{
		// AR: held from below, the magazine top 13 cm above the hand (controller up = +y).
		reload gun(ar);
		gun.load(0, 60);
		gun.press_button(false); // empty magazine out
		reload_hands hands;
		hands.update(gun, at(chest, true));
		hand_frame frame = at({ magwell.x, magwell.y - 0.13f, magwell.z + 0.03f }, true);
		hand_result r = hands.update(gun, frame);
		expect(has(r, reload_event::magazine_in), "AR: the magazine top, 13 cm above the hand, meets the magwell");
		// The held magazine turns with the off hand: hand rolled 90 deg about z, top now to the left.
		reload gun2(ar);
		gun2.load(0, 60);
		reload_hands hands2;
		hand_frame rolled = at(chest, true);
		rolled.off_rotation = { 0.0f, 0.0f, 0.70710678f, 0.70710678f };
		hands2.update(gun2, rolled);
		r = hands2.update(gun2, rolled);
		expect(r.draw == magazine_draw::in_hand && r.magazine_position.x < chest.x - 0.1f, "held magazine follows the off hand's rotation");
		// Charging handle from the gun frame: AK above the magazine, AR at the rear.
		reload ak_gun(ak);
		ak_gun.load(0, 60);
		ak_gun.take_from_chest();
		ak_gun.insert(); // knocks the empty magazine out (#47: two steps)
		ak_gun.insert(); // seats the new one
		reload_hands rack_hands;
		hand_frame f = at(away, false);
		f.has_root = true;
		f.root = { 0.0f, -0.2f, 0.0f };
		f.root_rotation = { 0.0f, 1.0f, 0.0f, 0.0f }; // gun frame +z = world -z (barrel ahead)
		rack_hands.update(ak_gun, f);
		f.off_hand = { 0.0f, -0.17f, -0.40f }; // handle: root + (0, 0.03, 0.40) turned
		f.grip = true;
		r = rack_hands.update(ak_gun, f);
		expect(r.racking && r.rack_pull == 0.0f && close(r.handle_in_gun.z, 0.40f), "racking starts on the handle, nothing pulled yet");
		f.off_hand = { 0.0f, -0.17f, -0.37f };
		r = rack_hands.update(ak_gun, f);
		expect(r.racking && close(r.rack_pull, 0.03f), "the pull follows the hand back along the barrel");
		expect(!r.rack_at_rear, "a short pull is not at the rear yet");
		f.off_hand = { 0.0f, -0.17f, -0.20f };
		r = rack_hands.update(ak_gun, f);
		expect(close(r.rack_pull, 0.08f), "the pull stops at the slide's travel");
		expect(r.rack_at_rear, "the rear-stop cue fires when the pull counts");
		r = rack_hands.update(ak_gun, f);
		expect(!r.rack_at_rear, "the rear-stop cue fires once per stroke");
		f.off_hand = { 0.0f, -0.17f, -0.45f };
		r = rack_hands.update(ak_gun, f);
		expect(r.rack_pull == 0.0f, "pushing forward does not pull");
		f.grip = false;
		r = rack_hands.update(ak_gun, f);
		expect(has(r, reload_event::chambered), "AK: once at the rear, letting go chambers even after riding the handle forward");
		expect(!r.racking && r.rack_pull == 0.0f, "letting go returns the handle");

		// A stroke that never reached the rear does not rack, and the next stroke starts over.
		reload short_gun(ak);
		short_gun.load(0, 60);
		short_gun.take_from_chest();
		short_gun.insert(); // knocks the empty magazine out (#47: two steps)
		short_gun.insert(); // seats the new one
		reload_hands short_hands;
		f.grip = false;
		f.off_hand = away;
		short_hands.update(short_gun, f);
		f.off_hand = { 0.0f, -0.17f, -0.40f };
		f.grip = true;
		short_hands.update(short_gun, f);
		f.off_hand = { 0.0f, -0.17f, -0.375f };
		r = short_hands.update(short_gun, f);
		expect(!r.rack_at_rear, "2.5 cm is short of the rear");
		f.grip = false;
		r = short_hands.update(short_gun, f);
		expect(r.count == 0 && !short_gun.state().chamber, "a short stroke does not chamber");
		f.off_hand = { 0.0f, -0.17f, -0.40f };
		f.grip = true;
		short_hands.update(short_gun, f);
		f.off_hand = { 0.0f, -0.17f, -0.355f };
		r = short_hands.update(short_gun, f);
		expect(r.rack_at_rear, "a new full stroke reaches the rear again");
	}

	// Empty hands (#38): the off hand holds the gun by the handguard, the free gun hand (passed as the
	// reloading hand, handle_only) racks; it never takes the magazine or one from the pouch.
	void test_handle_only()
	{
		reload gun(ak);
		gun.load(31, 60);
		reload_hands hands(flat_hold());
		hand_frame f = at(magwell, true);
		f.handle_only = true;
		hands.update(gun, at(away, false));
		hand_result r = hands.update(gun, f);
		expect(r.count == 0 && !gun.state().holding && gun.state().magazine_in, "handle only: a grip at the magwell takes nothing");
		hands.update(gun, at(away, false));
		hand_frame pouch = at(chest, true);
		pouch.handle_only = true;
		r = hands.update(gun, pouch);
		expect(r.count == 0 && !gun.state().holding, "handle only: a grip at the pouch takes nothing");
		hand_frame rack = at(away, false);
		rack.handle_only = true;
		rack.has_root = true;
		rack.root = { 0.0f, -0.2f, 0.0f };
		rack.root_rotation = { 0.0f, 1.0f, 0.0f, 0.0f };
		hands.update(gun, rack);
		rack.off_hand = { 0.0f, -0.17f, -0.40f };
		rack.grip = true;
		r = hands.update(gun, rack);
		expect(r.racking, "handle only: the handle still racks");
	}

	// #38: the gun in the off hand, the free gun hand's grip near the gun: the pistol grip takes the gun back
	// only when it is the nearer target; a hand nearer the magazine reloads.
	void test_grip_before_reload()
	{
		hand_result last;
		last.zone_centre[0] = { 0.0f, -0.08f, 0.0f }; // magazine body
		last.zone_reach[0] = 0.10f;
		last.zone_centre[1] = { 0.0f, 0.03f, 0.0f }; // handle (unknown this time)
		last.zone_reach[1] = 0.085f;
		last.zone_centre[2] = { 0.0f, -0.35f, 0.4f }; // pouch
		last.zone_reach[2] = 0.13f;
		const v3 grip{ 0.0f, -0.07f, 0.13f };
		expect(grip_before_reload({ 0.0f, -0.07f, 0.12f }, grip, 0.12f, last), "at the pistol grip: take the gun back");
		expect(!grip_before_reload({ 0.0f, -0.09f, 0.02f }, grip, 0.12f, last), "at the magazine: reload");
		expect(!grip_before_reload({ 0.0f, 0.2f, 0.13f }, grip, 0.12f, last), "out of the grip's reach: no take-back");
		last.has_handle = true;
		expect(!grip_before_reload({ 0.0f, 0.03f, 0.05f }, { 0.0f, 0.0f, 0.12f }, 0.12f, last), "nearer the handle than the grip: rack");
		expect(grip_before_reload({ 0.0f, -0.07f, 0.10f }, grip, 0.12f, hand_result{}), "no zones placed yet: the grip");
		// #95: a belt carried by the gun hand (the gun in the off hand) past the pistol grip stays in the hand.
		hand_result belt = last;
		belt.belt_draw = 2;
		belt.belt_zone = true;
		expect(!grip_before_reload({ 0.0f, -0.07f, 0.12f }, grip, 0.12f, belt), "a held belt at the pistol grip: still the belt's grip");
		hand_result lid = last;
		lid.lid_held = true;
		expect(!grip_before_reload({ 0.0f, -0.07f, 0.12f }, grip, 0.12f, lid), "a held lid: still the lid's grip");
		hand_result near = last;
		near.belt_zone = true;
		expect(!grip_before_reload({ 0.0f, -0.07f, 0.12f }, grip, 0.12f, near), "at the lid or tray: the reload's grip");
	}

	// AR palming by hand: the new magazine at the magwell turns aside, B catches the old one, turning
	// the hand back seats the new one; the old one stays in the hand for the pouch.
	// #98: a pull-out gun (P90, AK 5C) has no palming: a fresh magazine at its loaded magwell neither gets ready nor
	// seats, B does nothing, and it goes back to the pouch; the old one comes out by hand once the hand is free.
	void test_pull_out_no_palming()
	{
		const q4 gun_frame{ 0.0f, 1.0f, 0.0f, 0.0f };
		const auto frame_at = [&](v3 hand, bool grip, bool button) {
			hand_frame f = at(hand, grip, button);
			f.has_root = true;
			f.root = { 0.0f, -0.2f, -0.2f };
			f.root_rotation = gun_frame;
			f.magazine_rotation = gun_frame;
			f.palming = true;
			return f;
		};
		reload_profile p90 = ar;
		p90.hold = hold_open::no;
		p90.pull_out = true;
		reload gun(p90);
		gun.load(31, 60);
		reload_hands hands(flat_hold());
		hand_result r = hands.update(gun, frame_at(chest, true, false));
		expect(has(r, reload_event::magazine_taken), "pull-out: a new magazine from the pouch");
		bool ever_ready = false;
		for (int i = 0; i < 30; ++i)
		{
			r = hands.update(gun, frame_at(magwell, true, i == 20));
			ever_ready = ever_ready || r.palm != hand_result::palm_stage::none;
		}
		expect(!ever_ready && gun.state().magazine_in && gun.state().holding && gun.state().magazine_rounds == 30,
			"pull-out: at the loaded magwell no palming, B does nothing, the old one stays");
		hands.update(gun, frame_at(chest, true, false));
		r = hands.update(gun, frame_at(chest, false, false));
		expect(has(r, reload_event::magazine_stowed) && !gun.state().holding, "pull-out: the new one goes back to the pouch");
		expect(gun.grab_magazine() == reload_event::magazine_out, "pull-out: then the old one comes out by hand");
	}

	void test_hands_palming()
	{
		const q4 gun_frame{ 0.0f, 1.0f, 0.0f, 0.0f }; // Wep_Root +z = world -z, +x (the gun's left) = world -x
		const auto palm_frame = [&](v3 hand, bool grip, bool button) {
			hand_frame f = at(hand, grip, button);
			f.has_root = true;
			f.root = { 0.0f, -0.2f, -0.2f };
			f.root_rotation = gun_frame;
			f.magazine_rotation = gun_frame; // seated square in the gun
			f.palming = true;
			return f;
		};
		reload gun(ar);
		gun.load(31, 60);
		gun.fire();
		gun.fire();
		gun.fire();
		reload_hands hands(flat_hold());
		hand_result r = hands.update(gun, palm_frame(chest, true, false));
		expect(has(r, reload_event::magazine_taken), "palming: a new magazine from the pouch");
		r = hands.update(gun, palm_frame(magwell, true, false));
		expect(r.palm == hand_result::palm_stage::ready && r.palm_ready_now && r.count == 0, "at the magwell of a loaded gun it gets ready, nothing goes in");
		for (int i = 0; i < 120; ++i)
		{
			r = hands.update(gun, palm_frame(magwell, true, false));
		}
		expect(!r.palm_ready_now && close(r.magazine_tilt, 40.0f * 0.017453293f, 1e-3f), "it turns aside over a few frames");
		expect(r.draw == magazine_draw::in_hand && r.in_fist && r.magazine_position.x < magwell.x - 0.03f, "the new one is drawn turned off the magwell, towards the gun's left");
		expect(r.extra == hand_result::extra_magazine::seated, "extra meshes: while the new one is held, the old one is still drawn in the gun");
		r = hands.update(gun, palm_frame(magwell, true, true));
		expect(has(r, reload_event::magazine_palmed) && r.palm == hand_result::palm_stage::caught && gun.state().palmed, "B drops the old one into the hand");
		expect(r.extra == hand_result::extra_magazine::in_fist, "extra meshes: the caught one is drawn in the fist");
		r = hands.update(gun, palm_frame(magwell, true, false));
		expect(r.count == 0 && !gun.state().magazine_in && r.palm_misalign_degrees > 30.0f, "held still, the turned new one does not seat");

		// Turn the hand back as a wrist rolls, about the forearm, here 30 degrees off the barrel (world -z
		// = the gun's +z, tipped towards +y): 46 degrees of roll lean it back about 40 about the barrel.
		const float half = 0.5f * 46.0f * 0.017453293f;
		const v3 forearm{ 0.0f, 0.5f, -0.8660254f };
		hand_frame part = palm_frame(magwell, true, false);
		const float quarter = 0.5f * 20.0f * 0.017453293f;
		part.off_rotation = { forearm.x * std::sin(quarter), forearm.y * std::sin(quarter), forearm.z * std::sin(quarter), std::cos(quarter) };
		r = hands.update(gun, part);
		expect(r.count == 0 && r.palm_misalign_degrees > 20.0f && r.palm_misalign_degrees < 30.0f, "half way round it still leans, nothing seats");
		hand_frame turned = palm_frame(magwell, true, false);
		turned.off_rotation = { forearm.x * std::sin(half), forearm.y * std::sin(half), forearm.z * std::sin(half), std::cos(half) };
		r = hands.update(gun, turned);
		expect(has(r, reload_event::magazine_in), "rolled back about the forearm, the new one seats");
		expect(gun.state().magazine_in && gun.state().magazine_rounds == 30 && gun.state().holding && gun.state().held_rounds == 27,
			"the gun has the new one, the hand the old one");
		for (int i = 0; i < 20; ++i)
		{
			r = hands.update(gun, turned);
		}
		expect(r.draw == magazine_draw::with_gun && !r.in_fist && r.palm == hand_result::palm_stage::none, "the one mesh shows the seated magazine");
		expect(r.extra == hand_result::extra_magazine::in_fist, "extra meshes: the old one stays drawn in the hand");
		hands.update(gun, palm_frame(chest, true, false));
		r = hands.update(gun, palm_frame(chest, false, false));
		expect(has(r, reload_event::magazine_stowed) && gun.state().reserve == 57, "the old one stowed at the pouch keeps its rounds");

		// Ready ends when the hand leaves the magwell; palming off, or no gun frame, never starts.
		reload g2(ar);
		g2.load(31, 60);
		reload_hands h2(flat_hold());
		h2.update(g2, palm_frame(chest, true, false));
		h2.update(g2, palm_frame(magwell, true, false));
		r = h2.update(g2, palm_frame(away, true, false));
		expect(r.palm == hand_result::palm_stage::none, "leaving the magwell ends ready");
		reload g3(ar);
		g3.load(31, 60);
		reload_hands h3(flat_hold());
		hand_frame off = palm_frame(chest, true, false);
		off.palming = false;
		h3.update(g3, off);
		off.off_hand = magwell;
		r = h3.update(g3, off);
		expect(r.palm == hand_result::palm_stage::none, "palm_reload off: no palming");
		off.button = true;
		r = h3.update(g3, off);
		expect(has(r, reload_event::magazine_dropped) && !g3.state().palmed, "palm_reload off: B drops the old one as before");
	}

	// A magazine pushed in slides from the hand into the magwell (no pop), then rides with the gun.
	void test_seating()
	{
		reload gun(ar);
		gun.load(0, 60);
		gun.press_button(false);
		reload_hands hands(flat_hold());
		hands.update(gun, at(chest, true));
		const v3 hand{ magwell.x, magwell.y - 0.07f, magwell.z };
		hand_result r = hands.update(gun, at(hand, true));
		expect(has(r, reload_event::magazine_in) && r.draw == magazine_draw::in_hand, "inserted 7 cm out: drawn sliding");
		expect(close(r.magazine_position.y, hand.y, 0.03f), "the slide starts at the hand");
		for (int i = 0; i < 4; ++i)
		{
			r = hands.update(gun, at(hand, true));
		}
		expect(r.draw == magazine_draw::in_hand && r.magazine_position.y > hand.y + 0.02f && r.magazine_position.y < magwell.y,
			"half way the magazine is between hand and magwell");
		for (int i = 0; i < 10; ++i)
		{
			r = hands.update(gun, at(hand, true));
		}
		expect(r.draw == magazine_draw::with_gun, "then it is the gun's again");
	}

	// Grip lock: the held magazine rides the magwell's rail with the hand locked on it, seats only when
	// pushed home, and the hand stays on it while the grip is held.
	hand_frame locked_at(v3 hand, bool grip, bool lock = true)
	{
		hand_frame frame = at(hand, grip);
		frame.has_root = true;
		frame.root = { magwell.x, magwell.y, magwell.z + 0.2f };
		frame.grip_lock = lock;
		return frame;
	}

	// Headset 2026-11-14: the AK's new magazine jittered beside the magwell and never went in. Locked, the magazine is
	// no longer the fist's, and the controller's fixed guess of where the hand holds it sat far from the drawn one:
	// off the rail, unlocked, the fist's again, locked again. Locked, the hand keeps the hold it locked with.
	void test_grip_lock_keeps_the_hold()
	{
		hand_config config = flat_hold();
		config.hold_ar = config.hold_ak = { 0.25f, 0.0f, 0.0f }; // the controller's guess: 25 cm aside the drawn fist
		reload gun(ak);
		gun.load(30, 60);
		reload_hands hands(config);
		hands.update(gun, locked_at(chest, true));
		const v3 hand{ magwell.x + 0.02f, magwell.y - 0.30f, magwell.z };
		hand_frame f = locked_at(hand, true);
		f.has_held = true; // the drawn fist holds it 6 cm under the magwell
		f.held_top = { magwell.x + 0.02f, magwell.y - 0.06f, magwell.z };
		hand_result r = hands.update(gun, f);
		expect(r.guided, "hold: locked on from the fist's magazine");
		bool stayed = true, flicked = false, seated = false, seated_early = false;
		float y = hand.y;
		int pull_back = 0;
		int past = 0;
		for (int i = 0; i < 120 && !seated; ++i)
		{
			// Now drawn on the rail (no fist); the hand pushes up 1 cm a frame. After the flick (#47, two motions) it
			// comes down 8 cm and pushes up again.
			if (flicked && past < 12)
			{
				y += 0.01f; // after the flick the hand pushes on 12 cm (the flick comes 3 cm below the magwell: ~9 cm past it, as on the monitor 2026-11-16)
				++past;
			}
			else if (flicked && pull_back < 22)
			{
				y -= 0.01f;
				++pull_back;
			}
			else
			{
				y += 0.01f;
			}
			hand_frame g = locked_at({ hand.x, y, hand.z }, true);
			r = hands.update(gun, g);
			stayed = stayed && (r.guided || r.hand_lock > 0.0f || has(r, reload_event::magazine_in) || flicked);
			const bool now_seated = has(r, reload_event::magazine_in);
			seated_early = seated_early || (now_seated && flicked && pull_back < 22);
			flicked = flicked || has(r, reload_event::flicked_out);
			seated = seated || now_seated;
		}
		expect(stayed, "hold: stays locked while pushed up (no flicker off the rail)");
		expect(!seated_early, "hold: after the flick, pushing on past the magwell and coming back without the pull-back does not seat (#47)");
		expect(flicked && seated, "hold: the AK's old magazine is knocked out, then, pulled back and pushed in again, the new one seats");
	}

	// Owner 2026-11-16: gripping the magazine in the gun felt floaty and did not always take the fingers onto it. Now it
	// locks the hand on it like the handguard: a grip on the seated magazine holds the hand there (the game's grip)
	// until the grip opens or the controller is far off.
	void test_seated_hold()
	{
		reload gun(ar);
		gun.load(31, 60);
		reload_hands hands(flat_hold());
		hands.update(gun, locked_at(away, false));
		const v3 on_body{ magwell.x, magwell.y - 0.08f, magwell.z };
		hand_result r = hands.update(gun, locked_at(on_body, true));
		expect(r.count == 0 && gun.state().magazine_in, "seated hold: an AR's magazine stays in the gun when gripped");
		expect(r.hand_lock == 1.0f && r.draw == magazine_draw::with_gun && close(r.lock_position.y, magwell.y), "seated hold: the grip locks the hand on it");
		r = hands.update(gun, locked_at({ magwell.x + 0.08f, magwell.y - 0.10f, magwell.z }, true));
		expect(r.hand_lock == 1.0f && close(r.lock_position.y, magwell.y), "seated hold: the controller 8 cm off, the hand stays on it");
		r = hands.update(gun, locked_at({ magwell.x + 0.30f, magwell.y - 0.08f, magwell.z }, true));
		expect(r.hand_lock == 0.0f, "seated hold: 30 cm off, it lets go");
		r = hands.update(gun, locked_at(on_body, false));
		r = hands.update(gun, locked_at(on_body, true));
		expect(r.hand_lock == 1.0f, "seated hold: gripped again, locked again");
		r = hands.update(gun, locked_at(on_body, false));
		expect(r.hand_lock == 0.0f, "seated hold: the open hand lets go");
		reload_hands unlocked(flat_hold());
		unlocked.update(gun, locked_at(away, false, false));
		r = unlocked.update(gun, locked_at(on_body, true, false));
		expect(r.hand_lock == 0.0f, "seated hold: with grip_lock off nothing locks");
	}

	// Owner 2026-11-16: a new magazine brought to an AR with the old one still in should lock in a place where it can be
	// palmed: once palming is ready, the new magazine and the hand stay put on the gun until the controller leaves.
	void test_palm_lock()
	{
		const q4 gun_frame{ 0.0f, 1.0f, 0.0f, 0.0f };
		const auto palm_frame = [&](v3 hand, bool grip, bool button) {
			hand_frame f = at(hand, grip, button);
			f.has_root = true;
			f.root = { 0.0f, -0.2f, -0.2f };
			f.root_rotation = gun_frame;
			f.magazine_rotation = gun_frame;
			f.palming = true;
			f.grip_lock = true;
			return f;
		};
		reload gun(ar);
		gun.load(31, 60);
		gun.fire();
		reload_hands hands(flat_hold());
		hand_result r = hands.update(gun, palm_frame(chest, true, false));
		expect(has(r, reload_event::magazine_taken), "palm lock: a new magazine from the pouch");
		r = hands.update(gun, palm_frame(magwell, true, false));
		expect(r.palm == hand_result::palm_stage::ready && r.hand_lock == 1.0f, "palm lock: ready at the magwell, the hand locked");
		for (int i = 0; i < 60; ++i)
		{
			r = hands.update(gun, palm_frame(magwell, true, false));
		}
		const v3 held = r.magazine_position;
		r = hands.update(gun, palm_frame({ magwell.x + 0.05f, magwell.y - 0.04f, magwell.z }, true, false));
		expect(r.palm == hand_result::palm_stage::ready && r.hand_lock == 1.0f && close(r.magazine_position.x, held.x, 1e-3f) && close(r.magazine_position.y, held.y, 1e-3f),
			"palm lock: the controller 6 cm off, the new magazine stays where it was on the gun");
		r = hands.update(gun, palm_frame({ magwell.x + 0.05f, magwell.y - 0.04f, magwell.z }, true, true));
		expect(has(r, reload_event::magazine_palmed), "palm lock: B still catches the old one");
		reload gun2(ar);
		gun2.load(31, 60);
		gun2.fire();
		reload_hands hands2(flat_hold());
		hands2.update(gun2, palm_frame(chest, true, false));
		r = hands2.update(gun2, palm_frame(magwell, true, false));
		r = hands2.update(gun2, palm_frame({ magwell.x + 0.30f, magwell.y, magwell.z }, true, false));
		expect(r.palm == hand_result::palm_stage::none && r.hand_lock == 0.0f, "palm lock: the controller 30 cm off, it lets go");
	}

	void test_grip_lock()
	{
		{
			reload gun(ar);
			gun.load(0, 60);
			gun.press_button(false);
			reload_hands hands(flat_hold());
			hands.update(gun, locked_at(chest, true));
			const v3 aside{ magwell.x + 0.03f, magwell.y - 0.08f, magwell.z };
			hand_result r = hands.update(gun, locked_at(aside, true));
			expect(r.guided && r.count == 0 && r.hand_lock == 1.0f, "lock: 8 cm under the magwell and 3 cm aside it locks on, not yet in");
			expect(close(r.magazine_position.x, magwell.x) && close(r.lock_position.y, magwell.y - 0.08f, 1e-3f) && !r.in_fist,
				"lock: drawn upright on the rail, not in the fist");
			r = hands.update(gun, locked_at({ magwell.x + 0.02f, magwell.y - 0.05f, magwell.z }, true));
			expect(r.guided && close(r.rail_depth, 0.05f, 1e-3f) && r.count == 0, "lock: rides the rail up");
			r = hands.update(gun, locked_at({ magwell.x, magwell.y - 0.02f, magwell.z }, true));
			expect(has(r, reload_event::magazine_in) && r.hand_lock == 1.0f, "lock: pushed home it seats, the hand on it");
			for (int i = 0; i < 20; ++i)
			{
				r = hands.update(gun, locked_at({ magwell.x, magwell.y - 0.02f, magwell.z }, true));
			}
			expect(r.draw == magazine_draw::with_gun && r.hand_lock == 1.0f && close(r.lock_position.y, magwell.y), "lock: seated, the hand stays on it while gripping");
			r = hands.update(gun, locked_at({ magwell.x, magwell.y - 0.02f, magwell.z }, false));
			expect(r.hand_lock == 0.0f, "lock: the open hand lets go of it");
		}
		{
			reload gun(ar);
			gun.load(0, 60);
			gun.press_button(false);
			reload_hands hands(flat_hold());
			hands.update(gun, locked_at(chest, true));
			hand_result r = hands.update(gun, locked_at({ magwell.x + 0.03f, magwell.y - 0.06f, magwell.z }, true));
			expect(r.guided, "leave: locked on first");
			r = hands.update(gun, locked_at({ magwell.x + 0.2f, magwell.y - 0.06f, magwell.z }, true));
			expect(!r.guided && r.in_fist && r.hand_lock == 0.0f && r.count == 0, "leave: 20 cm off the rail it is the fist's again");
		}
		{
			reload gun(ar);
			gun.load(0, 60);
			gun.press_button(false);
			reload_hands hands(flat_hold());
			hands.update(gun, locked_at(chest, true));
			hand_frame turned = locked_at({ magwell.x, magwell.y - 0.05f, magwell.z }, true);
			turned.off_rotation = { 0.0f, 0.0f, 0.7071068f, 0.7071068f };
			const hand_result r = hands.update(gun, turned);
			expect(!r.guided && r.count == 0, "lock: turned 90 degrees it neither locks on nor goes in");
		}
		{
			reload gun(ar);
			gun.load(0, 60);
			gun.press_button(false);
			reload_hands hands(flat_hold());
			hands.update(gun, locked_at(chest, true, false));
			const hand_result r = hands.update(gun, locked_at({ magwell.x, magwell.y - 0.07f, magwell.z }, true, false));
			expect(has(r, reload_event::magazine_in) && !r.guided && r.hand_lock == 0.0f, "lock off: in at 7 cm as before");
		}
		{
			reload gun(ak);
			gun.load(30, 60);
			reload_hands hands(flat_hold());
			hands.update(gun, locked_at(away, false));
			const v3 on_body{ magwell.x, magwell.y - 0.08f, magwell.z };
			hand_result r = hands.update(gun, locked_at(on_body, true));
			expect(has(r, reload_event::magazine_out) && r.guided && r.hand_lock == 1.0f, "pull out: taken by hand it rides the rail out");
			r = hands.update(gun, locked_at({ magwell.x + 0.02f, magwell.y - 0.02f, magwell.z }, true));
			expect(r.guided && r.count == 0, "pull out: pushed back up it does not seat again");
			r = hands.update(gun, locked_at({ magwell.x, magwell.y - 0.25f, magwell.z }, true));
			expect(!r.guided && r.in_fist, "pull out: past the rail's end it is the fist's");
		}
		{
			// The way out (weapon/profiles magazine_way_out_for): the magazine rides out along it, 30 degrees forward here.
			const float a = 30.0f * 0.017453293f;
			const v3 out{ 0.0f, -std::cos(a), std::sin(a) };
			const auto along = [&](float depth) { return v3{ magwell.x + out.x * depth, magwell.y + out.y * depth, magwell.z + out.z * depth }; };
			const auto slanted = [&](v3 hand, bool grip)
			{
				hand_frame f = locked_at(hand, grip);
				f.has_way_out = true;
				f.way_out_degrees = 30.0f;
				return f;
			};
			reload gun(ak);
			gun.load(30, 60);
			reload_hands hands(flat_hold());
			hand_result r = hands.update(gun, slanted(away, false));
			expect(close(r.zone_centre[0].z, magwell.z + out.z * 0.08f, 1e-3f) && close(r.zone_centre[0].y, magwell.y + out.y * 0.08f, 1e-3f),
				"way out: no measured grab, the zone sits along the way out");
			r = hands.update(gun, slanted(along(0.08f), true));
			expect(has(r, reload_event::magazine_out) && r.guided, "way out: taken 4 cm ahead of the magwell it is the magazine's, not the handguard's");
			// The magazine keeps the hand's offset from the grab: 3 cm further along, 3 cm out of the well.
			r = hands.update(gun, slanted(along(0.11f), true));
			expect(r.guided && close(r.magazine_position.z, magwell.z + out.z * 0.03f, 1e-3f) && close(r.magazine_position.y, magwell.y + out.y * 0.03f, 1e-3f),
				"way out: it rides the slanted rail");
			r = hands.update(gun, slanted({ magwell.x, magwell.y - 0.12f, magwell.z + 0.04f }, true));
			expect(r.guided && close(r.magazine_position.z, magwell.z + out.z * r.rail_depth, 1e-3f), "way out: a hand straight below still puts it on the slanted rail");
		}
		{
			// The P90: back off the top. Without a measured grab its zone is behind the magazine, not inside the gun.
			reload gun(ar);
			gun.load(30, 60);
			reload_hands hands(flat_hold());
			hand_frame f = locked_at(away, false);
			f.has_way_out = true;
			f.way_out_degrees = -102.0f;
			const hand_result r = hands.update(gun, f);
			expect(r.zone_centre[0].z < magwell.z - 0.07f && r.zone_centre[0].y > magwell.y + 0.01f, "P90: the zone behind and a little above the magazine");
		}
		{
			// Monitor 2026-11-17, P90: taken 16 cm behind its magwell, the magazine came out and seated again the next
			// frame: in the frame it came out, the re-arm still measured from the fist, 18 cm off.
			for (const bool lock : { true, false })
			{
				reload gun(ar);
				gun.load(30, 60);
				reload_hands hands(flat_hold());
				hand_result last;
				int held_frames = 0;
				const auto p90 = [&](v3 hand, bool grip, bool button)
				{
					hand_frame f = locked_at(hand, grip, lock);
					f.button = button;
					// As in the game: the held magazine where the drawn fist holds it (arm_pose attach_held_magazine). Right after
					// the pull-out the drawn hand is still easing from the controller onto the locked magazine: its fist 21 cm
					// from the magwell for a few frames.
					// Then the hand lock has brought the drawn fist onto the magazine on the rail: at the magwell.
					if (gun.state().holding)
					{
						f.has_held = true;
						f.held_top = held_frames++ < 2 ? v3{ hand.x, hand.y, hand.z - 0.05f } : magwell;
						f.held_rotation = {};
					}
					f.has_way_out = true;
					f.way_out_degrees = -101.0f;
					f.has_magazine_grab = true;
					f.magazine_grab = { 0.001f, 0.018f, -0.158f };
					return f;
				};
				const v3 grab_at{ magwell.x + 0.001f, magwell.y + 0.018f, magwell.z - 0.158f };
				last = hands.update(gun, p90(away, false, false));
				last = hands.update(gun, p90(grab_at, true, false));
				last = hands.update(gun, p90(grab_at, true, true));
				const bool out = has(last, reload_event::magazine_out);
				bool reseated = false;
				for (int i = 0; i < 5; ++i)
				{
					last = hands.update(gun, p90(grab_at, true, i < 2));
					reseated = reseated || has(last, reload_event::magazine_in);
				}
				expect(out && !reseated && gun.state().holding, lock ? "P90, locked: the magazine taken out stays out" : "P90, lock off: the magazine taken out stays out");
			}
		}
		{
			// The measured grab ahead of the magwell (the AK-12's, 3.9 cm): a hand on it is not the handguard's.
			reload gun(ak);
			gun.load(30, 60);
			reload_hands hands(flat_hold());
			hand_frame f = locked_at(away, false);
			f.has_magazine_grab = true;
			f.magazine_grab = { -0.016f, -0.041f, 0.039f };
			hands.update(gun, f);
			f = locked_at({ magwell.x - 0.016f, magwell.y - 0.041f, magwell.z + 0.039f }, true);
			f.has_magazine_grab = true;
			f.magazine_grab = { -0.016f, -0.041f, 0.039f };
			const hand_result r = hands.update(gun, f);
			expect(has(r, reload_event::magazine_out), "AK-12: the hand at its measured grab takes the magazine");
		}
		{
			// Monitor 2026-11-17, Hawk: its measured handle is the fore-end, so gripping it there to hold the gun started a
			// rack and the two-handed hold never came. A pump gun: the grip there is the hold's; the held hand pumps it.
			reload_profile pump_profile = ar;
			pump_profile.manual_cycle = true;
			pump_profile.hold = hold_open::no;
			reload gun(pump_profile);
			gun.load(5, 20);
			reload_hands hands(flat_hold());
			// locked_at: the root 0.2 ahead of the magwell along +z, the barrel along +z (back is -z).
			const v3 fore_end{ magwell.x, magwell.y + 0.08f, magwell.z + 0.2f + 0.45f };
			const auto on_pump = [&](float back_cm, bool grip)
			{
				hand_frame f = locked_at({ fore_end.x, fore_end.y, fore_end.z - back_cm * 0.01f }, grip);
				f.has_handle_point = true;
				f.handle_point = { 0.0f, 0.08f, 0.45f };
				f.pump = true;
				return f;
			};
			hands.update(gun, on_pump(0.0f, false));
			hand_result r = hands.update(gun, on_pump(0.0f, true));
			expect(!r.off_hand_busy && !r.racking && r.in_zone != hand_result::zone::handle, "pump: the grip on the fore-end is the two-handed hold's, not a rack");
			r = hands.update(gun, on_pump(6.0f, true));
			expect(!r.pumping && r.count == 0, "pump: locked while a round is chambered (no live shell lost to a drifting hand)");
			hands.update(gun, on_pump(0.0f, true));
			expect(gun.fire() == reload_event::shot && !gun.state().chamber, "pump: the shot leaves the chamber empty");
			r = hands.update(gun, on_pump(0.0f, true));
			r = hands.update(gun, on_pump(6.0f, true));
			expect(r.pumping && r.rack_at_rear && !r.off_hand_busy && close(r.rack_pull, 0.06f, 1e-3f), "pump: back 6 cm, the rear stop, still the hold's hand");
			r = hands.update(gun, on_pump(0.5f, true));
			expect(has(r, reload_event::chambered) && gun.state().chamber, "pump: forward again, the next shell chambered");
			r = hands.update(gun, on_pump(6.0f, true));
			expect(!r.pumping && r.count == 0, "pump: locked again on the fresh round");
		}
		{
			// A tube gun: an empty hand at the loading port is free (the two-handed hold, no magazine to take); a shell
			// from the pouch brought to the port goes in, one at a time.
			reload_profile tube_profile = ar;
			tube_profile.tube = true;
			tube_profile.capacity = 6;
			tube_profile.hold = hold_open::no;
			reload gun(tube_profile);
			gun.load(0, 20);
			reload_hands hands(flat_hold());
			const auto tube_at = [&](v3 hand, bool grip)
			{
				hand_frame f = locked_at(hand, grip);
				f.tube = true;
				return f;
			};
			hands.update(gun, tube_at(away, false));
			hand_result r = hands.update(gun, tube_at(magwell, true));
			expect(!r.off_hand_busy && r.count == 0, "tube: an empty hand gripping at the loading port is free");
			for (int shell = 0; shell < 2; ++shell)
			{
				hands.update(gun, tube_at(away, false));
				hands.update(gun, tube_at(chest, false));
				r = hands.update(gun, tube_at(chest, true));
				expect(has(r, reload_event::magazine_taken) && gun.state().held_rounds == 1, "tube: one shell from the pouch");
				r = hands.update(gun, tube_at(magwell, true));
				expect(has(r, reload_event::shell_loaded) && !gun.state().holding, "tube: brought to the port, it goes in");
				hands.update(gun, tube_at(magwell, false));
			}
			expect(gun.state().magazine_rounds == 2 && !r.guided, "tube: two shells in, no rail lock");
		}
		{
			// Belt-fed (M249 points, re/notes/weapon.md): the lid taken at its grab point and swung up about its hinge,
			// the belt end taken from the box mouth and laid on the tray, the lid pushed shut: ready.
			reload_profile belt_profile = ar;
			belt_profile.belt = true;
			belt_profile.capacity = 200;
			belt_profile.hold = hold_open::no;
			belt_profile.flick = false;
			reload gun(belt_profile);
			gun.load(1, 400);
			reload_hands hands(flat_hold());
			const v3 hinge{ 0.0f, 0.058f, 0.475f };
			const auto lid_grab_at = [&](float lid)
			{
				const float a = lid * 102.6f * 0.017453293f;
				const float y = 0.017f, z = -0.18f; // the closed grab (Scope1 rest) from the hinge
				return v3{ 0.0f, hinge.y + y * std::cos(a) - z * std::sin(a), hinge.z + y * std::sin(a) + z * std::cos(a) };
			};
			float drawn = 0.0f;
			const auto belt_frame = [&](v3 hand, bool grip)
			{
				hand_frame f = at(hand, grip);
				f.has_root = true;
				f.root = {};
				f.magwell = { 0.0f, -0.03f, 0.44f };
				f.belt = true;
				f.has_belt_points = true;
				f.lid_hinge = hinge;
				f.lid_grab = lid_grab_at(drawn);
				f.feed_tray = { -0.006f, 0.051f, 0.418f };
				f.belt_end = { 0.074f, 0.004f, 0.418f };
				return f;
			};
			const auto step = [&](v3 hand, bool grip)
			{
				const hand_result r = hands.update(gun, belt_frame(hand, grip));
				drawn = r.lid;
				return r;
			};
			step(away, false);
			hand_result r = step(lid_grab_at(0.0f) + v3{ 0.0f, 0.03f, 0.0f }, false);
			expect(r.belt_zone && !r.lid_held, "belt: near the lid the hand is the lid's (the accessory touch stands down, #95)");
			r = step(lid_grab_at(0.0f), true);
			expect(r.lid_held && r.off_hand_busy && r.belt_zone, "belt: the lid taken at its grab point");
			r = step(lid_grab_at(0.6f), true);
			expect(close(r.lid, 0.6f, 0.02f) && has(r, reload_event::lid_opened) && !gun.state().chamber, "belt: swung 60 % up it follows the hand, open");
			// #95: the drawn hand is held on the lid's grab point (gun axes), wherever the controller is.
			r = step(lid_grab_at(0.6f) + v3{ 0.03f, 0.0f, 0.0f }, true);
			expect(r.lid_held && close(r.handle_in_gun.x, lid_grab_at(drawn).x, 1e-4f) && close(r.handle_in_gun.y, lid_grab_at(drawn).y, 1e-4f)
				&& close(r.handle_in_gun.z, lid_grab_at(drawn).z, 1e-4f), "belt: the held lid holds the hand at its grab point");
			r = step(lid_grab_at(0.6f), false);
			for (int i = 0; i < 10; ++i)
			{
				r = step(away, false);
			}
			expect(gun.state().lid_open && close(r.lid, 1.0f, 1e-3f), "belt: let go past halfway, it swings fully up");
			// The box is still on, its belt laid: lift it off the tray, carry it off, lay it again.
			step({ -0.006f, 0.051f, 0.418f }, false);
			r = step({ -0.006f, 0.051f, 0.418f }, true);
			expect(has(r, reload_event::belt_taken) && gun.state().belt_held && r.belt_draw == 2, "belt: the laid belt lifted off the tray");
			r = step({ 0.074f, -0.10f, 0.418f }, true);
			expect(gun.state().belt_held && !gun.state().belt_laid, "belt: carried off, still in the hand");
			r = step({ 0.0f, 0.051f, 0.42f }, true);
			expect(has(r, reload_event::belt_laid) && gun.state().belt_laid && r.belt_draw == 0, "belt: brought to the tray, laid");
			step({ 0.0f, 0.051f, 0.42f }, false);
			step(away, false);
			r = step(lid_grab_at(1.0f), true);
			expect(r.lid_held, "belt: the open lid taken again");
			r = step(lid_grab_at(0.3f), true);
			r = step(lid_grab_at(0.3f), false);
			for (int i = 0; i < 10; ++i)
			{
				r = step(away, false);
			}
			expect(!gun.state().lid_open && gun.state().chamber && close(r.lid, 0.0f, 1e-3f), "belt: pushed under halfway, it shuts on the belt: ready");
		}
		{
			// The revolver: B breaks it open, a speedloader from the pouch at the open cylinder, a flick of the gun up shuts it.
			reload_profile rex_profile = ar;
			rex_profile.revolver = true;
			rex_profile.capacity = 5;
			rex_profile.hold = hold_open::no;
			rex_profile.flick = false;
			reload gun(rex_profile);
			gun.load(0, 30);
			reload_hands hands(flat_hold());
			float drawn = 0.0f;
			q4 gun_turn{};
			const auto rex_frame = [&](v3 hand, bool grip, bool button)
			{
				hand_frame f = at(hand, grip, button);
				f.has_root = true;
				f.root = {};
				f.root_rotation = gun_turn;
				f.magwell = magwell;
				f.revolver = true;
				f.has_belt_points = true;
				f.lid_hinge = { 0.0f, 0.026f, 0.137f };
				f.lid_grab = { 0.0f, 0.026f - 0.11f * std::sin(drawn * 1.4835f), 0.137f + 0.11f * std::cos(drawn * 1.4835f) };
				f.lid_degrees = 85.0f;
				return f;
			};
			const auto step = [&](v3 hand, bool grip, bool button)
			{
				const hand_result r = hands.update(gun, rex_frame(hand, grip, button));
				drawn = r.lid;
				return r;
			};
			step(away, false, false);
			hand_result r = step(away, false, true);
			expect(has(r, reload_event::lid_opened) && gun.state().lid_open, "REX: B breaks it open");
			for (int i = 0; i < 10; ++i)
			{
				r = step(away, false, false);
			}
			expect(close(r.lid, 1.0f, 1e-3f), "REX: the frame swings fully open");
			step(chest, false, false);
			r = step(chest, true, false);
			expect(has(r, reload_event::magazine_taken) && gun.state().held_rounds == 6, "REX: a speedloader from the pouch");
			r = step(magwell, true, false);
			expect(has(r, reload_event::magazine_in) && gun.state().magazine_rounds == 6, "REX: at the open cylinder, six rounds in");
			step(magwell, false, false);
			r = step(away, false, false);
			expect(gun.state().lid_open && !has(r, reload_event::lid_closed), "REX: still open while the gun is held still");
			// A flick up: the barrel pitched up 0.1 rad in one frame (6 /s).
			gun_turn = { -std::sin(0.05f), 0.0f, 0.0f, std::cos(0.05f) };
			r = step(away, false, false);
			const bool closed_on_flick = has(r, reload_event::lid_closed);
			r = step(away, false, false); // #132: the flick ends (held there): it shuts now
			expect((closed_on_flick || has(r, reload_event::lid_closed)) && gun.state().chamber && gun.rounds_in_gun() == 6, "REX: a flick up shuts it, ready");
			// #132 (owner 2026-11-24): open, tip it muzzle up and the cylinder empties.
			gun_turn = {};
			step(away, false, false);
			const int reserve_before = gun.state().reserve;
			r = step(away, false, true); // B opens it again
			for (int i = 0; i < 10; ++i)
			{
				r = step(away, false, false);
			}
			expect(gun.state().lid_open && gun.rounds_in_gun() == 6, "REX: open again, six in");
			const float up = 0.5f * 1.2f; // 69 degrees up, about -x, raised over three frames (not a flick: under the rate)
			bool emptied = false, closed = false;
			for (int i = 1; i <= 40; ++i)
			{
				const float a = up * std::fmin(1.0f, i / 30.0f);
				gun_turn = { -std::sin(a), 0.0f, 0.0f, std::cos(a) };
				r = step(away, false, false);
				emptied = emptied || has(r, reload_event::cylinder_emptied);
				closed = closed || has(r, reload_event::lid_closed);
			}
			expect(emptied && !closed && gun.rounds_in_gun() == 0 && gun.state().reserve == reserve_before, "REX: tipped muzzle up, the cylinder empties (once; the live rounds fall to the floor, drop_discards_ammo), it stays open");
			// A fast swing up to steep is the tip, not a flick close.
			gun_turn = {};
			for (int i = 0; i < 3; ++i)
			{
				r = step(away, false, false);
			}
			gun_turn = { -std::sin(0.6f), 0.0f, 0.0f, std::cos(0.6f) };
			r = step(away, false, false);
			expect(!has(r, reload_event::lid_closed) && gun.state().lid_open, "REX: snapped steeply up it stays open");
		}
		{
			// #132: a hand gripping at the cylinder and moving a little does not open it; swung out a third of the way it does.
			reload_profile rex_profile = ar;
			rex_profile.revolver = true;
			rex_profile.capacity = 5;
			rex_profile.hold = hold_open::no;
			rex_profile.flick = false;
			reload gun(rex_profile);
			gun.load(6, 30);
			reload_hands hands(flat_hold());
			const v3 hinge{ 0.0f, 0.026f, 0.137f };
			const v3 grab{ 0.0f, 0.026f - 0.11f, 0.137f };
			const auto rex_at = [&](float degrees, bool grip)
			{
				const float a = degrees * 0.017453293f;
				hand_frame f = at({ hinge.x, hinge.y - 0.11f * std::cos(a), hinge.z - 0.11f * std::sin(a) }, grip, false);
				f.has_root = true;
				f.root = {};
				f.root_rotation = {};
				f.magwell = magwell;
				f.revolver = true;
				f.has_belt_points = true;
				f.lid_hinge = hinge;
				f.lid_grab = grab;
				f.lid_degrees = 85.0f;
				return f;
			};
			hands.update(gun, rex_at(0.0f, false));
			hands.update(gun, rex_at(0.0f, true));
			hand_result r = hands.update(gun, rex_at(15.0f, true)); // a sixth of the way
			expect(!gun.state().lid_open && !has(r, reload_event::lid_opened), "REX: a small swing at the cylinder does not open it");
			r = hands.update(gun, rex_at(40.0f, true));
			expect(gun.state().lid_open, "REX: swung out past a third it opens");
		}
		{
			// AK quick reload under the lock: a fresh magazine on the rail of a loaded AK knocks the old one out.
			reload gun(ak);
			gun.load(30, 60);
			reload_hands hands(flat_hold());
			hands.update(gun, locked_at(chest, true));
			hand_result r = hands.update(gun, locked_at({ magwell.x + 0.02f, magwell.y - 0.06f, magwell.z }, true));
			expect(r.guided && (r.rail_probe_flags & 4), "AK loaded: the well counts as free (the flick), it locks on");
			r = hands.update(gun, locked_at({ magwell.x, magwell.y - 0.02f, magwell.z }, true));
			expect(has(r, reload_event::flicked_out), "AK loaded: pushed home on the rail, the old one is knocked out");
		}
	}
}

// Each physical reload event plays the stage of the game's reload patch matched to it (re/notes/sound.md).
static void test_reload_sound_cues()
{
	using namespace bfvr::weapon;
	sound_cue cues[2]{};
	// The gun's sound folder from its firing sound (census 2026-11-17: the unlock names missed 18 of 51 reload sets).
	expect(sound_folder_of("sound/Weapons/Handheld/Remington870/Weapon_Shotgun_Remington870_SPC") == "Remington870"
			&& sound_folder_of("Sound/Weapons/Handheld/M16A3/Weapon_Assault_M16A4_SPC") == "M16A3" && sound_folder_of("Sound/Character/x").empty(),
		"sound folder: from the firing sound's path, whatever the unlock is called");
	expect(reload_set_belongs("Sound/Weapons/Handheld/Remington870/Weapon_Shotgun_Remington870_Reload_SPC", "Remington870")
			&& reload_set_belongs("Sound/Weapons/Handheld/Mk11/Weapon_Sniper_Mk11_Reload_SPC", "MK11")
			&& !reload_set_belongs("Sound/Weapons/AI/Mk11/Weapon_Sniper_Mk11_Reload_SPC", "Mk11")
			&& !reload_set_belongs("Sound/Weapons/Handheld/AK12/Weapon_Assault_AK12_Reload_SPC", "AK")
			&& !reload_set_belongs("Sound/Weapons/Handheld/Mk11/Weapon_Sniper_Mk11_SPC", "Mk11"),
		"reload set: its folder ignoring case, a reload set, not the AI one, not a folder that only starts the same");
	{
		const std::uint32_t pump[] = { reload_sound::magazine_seat, reload_sound::settle, reload_sound::bolt, 0x11111111u };
		expect(!patch_has_event(pump, 4, reload_sound::magazine_in) && patch_has_event(pump, 4, reload_sound::bolt),
			"patch: the pump shotgun's has no magazine-in (the synthesised one plays), it has the bolt");
	}
	// Each patch's own events (sound_probe match, 2026-11-17).
	expect(patch_kind_of("Sound/Weapons/SourcePatches/Handheld/Weapon_SourcePatch_Reload_Shotgun_PumpAction") == reload_patch::pump
			&& patch_kind_of("Weapon_SourcePatch_Reload_LMG") == reload_patch::lmg && patch_kind_of("Weapon_SourcePatch_Reload_AR") == reload_patch::ar
			&& patch_kind_of("Weapon_SourcePatch_Reload_5") == reload_patch::tube5 && patch_kind_of("") == reload_patch::ar,
		"patch: told apart by name");
	expect(cues_for(reload_event::shell_loaded, reload_patch::pump, cues) == 1 && cues[0].event == reload_sound::pump_shell_in,
		"pump shotgun: a shell in is its own shell sound (the AR magazine-in it lacks played nothing)");
	expect(cues_for(reload_event::lid_closed, reload_patch::lmg, cues) == 1 && cues[0].event == reload_sound::lmg_lid_close
			&& cues_for(reload_event::lid_opened, reload_patch::pistol, cues) == 1 && cues[0].event == reload_sound::magazine_out,
		"LMG lid and revolver frame: their own sounds");
	expect(cues_for(reload_event::magazine_out, reload_patch::lmg, cues) == 1 && cues[0].event == reload_sound::magazine_out,
		"what a patch shares with the AR stays the AR's");
	expect(rear_stop_event(reload_patch::pump) == reload_sound::pump_back && rear_stop_event(reload_patch::ar) == 0u
			&& rear_stop_event(reload_patch::sniper) == 0u,
		"rear stop: the pump's own; none for the AR or the sniper (its event stayed silent there)");
	expect(cues_for(reload_event::magazine_taken, reload_patch::pump, cues) == 0 && cues_for(reload_event::magazine_taken, reload_patch::ar, cues) == 1,
		"a shell from the pouch: the synthesised cue on a pump (its settle event is silent), the AR's settle on a rifle");
	expect(cues_for(reload_event::magazine_out, cues) == 1 && cues[0].event == reload_sound::magazine_out && cues[0].delay_ms == 0, "cue: magazine out");
	expect(cues_for(reload_event::magazine_in, cues) == 2 && cues[0].event == reload_sound::magazine_in && cues[1].event == reload_sound::magazine_seat
		&& cues[1].delay_ms > 0, "cue: magazine in, then seated");
	expect(cues_for(reload_event::flicked_out, cues) == 1 && cues[0].event == reload_sound::magazine_out, "cue: AK old magazine knocked out");
	expect(cues_for(reload_event::chambered, cues) == 1 && cues[0].event == reload_sound::bolt, "cue: chambered");
	expect(cues_for(reload_event::racked_empty, cues) == 1 && cues[0].event == reload_sound::bolt, "cue: racked empty");
	expect(cues_for(reload_event::magazine_stowed, cues) == 1 && cues[0].event == reload_sound::settle, "cue: stowed");
	// Headset 2026-11-04: the synthesised cloth at the pouch did not sound right; the game's settle does.
	expect(cues_for(reload_event::magazine_taken, cues) == 1 && cues[0].event == reload_sound::settle, "cue: taken from the pouch");
	for (const reload_event silent : { reload_event::none, reload_event::shot, reload_event::dry_fire, reload_event::magazine_dropped })
	{
		expect(cues_for(silent, cues) == 0, "cue: no sound");
	}
}

	void test_throw_along_aim()
	{
		const auto near = [](float a, float b) { return std::fabs(a - b) < 1e-3f; };
		// #85: a throw straight ahead (LOCAL -z), the hand 30 cm right, 40 cm below and 20 cm ahead of the eyes.
		throw_in_frame t = throw_along_aim({ 0.3f, -0.4f, -0.2f }, { 0.0f, 0.0f, -8.0f });
		expect(t.valid && near(t.aim.z, -1.0f) && near(t.speed.z, 8.0f) && near(t.speed.x, 0.0f) && near(t.speed.y, 0.0f),
			"#85: ahead: the aim along the throw, all of the speed forward");
		expect(near(t.position.x, -0.3f) && near(t.position.y, -0.4f) && near(t.position.z, 0.2f),
			"#85: the hand 30 cm right (x left = -0.3), 40 cm below, 20 cm ahead of the eyes");
		// Thrown to the right (LOCAL +x) and up 45 deg: the aim follows; a hand at the eyes starts at the eyes.
		t = throw_along_aim({ 0.0f, 0.0f, 0.0f }, { 5.0f, 5.0f, 0.0f });
		expect(t.valid && near(t.aim.x, 0.7071f) && near(t.aim.y, 0.7071f) && near(t.speed.z, 7.0711f), "#85: up and right: the aim there, speed kept");
		expect(near(t.position.x, 0.0f) && near(t.position.y, 0.0f) && near(t.position.z, 0.0f), "#85: no offset");
		// A hand to the left of a throw to the right is behind it in that frame.
		t = throw_along_aim({ -0.3f, 0.0f, 0.0f }, { 6.0f, 0.0f, 0.0f });
		expect(near(t.position.z, -0.3f) && near(t.position.x, 0.0f), "#85: offset expressed along the throw");
		// Straight up: no right from the throw, still valid.
		t = throw_along_aim({ 0.0f, 0.0f, 0.0f }, { 0.0f, 4.0f, 0.0f });
		expect(t.valid && near(t.aim.y, 1.0f) && std::isfinite(t.position.x), "#85: straight up is still a throw");
		expect(!throw_along_aim({}, {}).valid, "#85: no speed, no throw");
	}

	void test_quick_throw_switch()
	{
		// #83: the game's quick throw selects the grenade for ~200 ms; during a physical throw that is no weapon change.
		expect(hide_quick_throw_switch(true, 6, 0), "#83: throw pending, slot 6 over the rifle: keep the rifle");
		expect(hide_quick_throw_switch(true, 6, 2), "#83: also over a gadget (C4)");
		expect(!hide_quick_throw_switch(false, 6, 0), "no throw pending: the grenade really was selected");
		expect(!hide_quick_throw_switch(true, 0, 0), "back on the rifle: nothing to hide");
		expect(!hide_quick_throw_switch(true, 6, 6), "the grenade was already the weapon");
		expect(!hide_quick_throw_switch(true, 6, -1), "no previous weapon known");
	}

static void test_spare_rounds()
{
	// #89: a 900-1200 rpm gun (15-20 rounds a second) at the headset's 90 fps: 2, as before (the HUD reads 2 high).
	expect(spare_rounds_for(20.0f, 1.0f / 90.0f) == 2 && spare_rounds_for(15.0f, 1.0f / 72.0f) == 2, "spares: headset frame rate, 2");
	// At 20 fps two spares were fired before the block reached the game (monitor): now 4.
	expect(spare_rounds_for(15.0f, 1.0f / 20.0f) == 4, "spares: 900 rpm at 20 fps, 4");
	expect(spare_rounds_for(20.0f, 1.0f / 8.0f) == 8, "spares: very slow frames, at most 8");
	expect(spare_rounds_for(0.0f, 1.0f / 20.0f) == 2 && spare_rounds_for(20.0f, 0.0f) == 2, "spares: nothing known, 2");
}

int main()
{
	test_spare_rounds();
	test_reload_sound_cues();
	test_load();
	test_ar_empty_reload();
	test_ar_tactical_catch_and_stow();
	test_ar_dry_without_magazine();
	test_ar_palming();
	test_ak();
	test_ak_flick();
	test_drop_option();
	test_hand_rules();
	test_conservation();
	test_profiles();
	test_magazine_zone_to_hip();
	test_stow_zone();
	test_hands_ak();
	test_magazine_snap();
	test_hands_reach_and_overlap();
	test_rack_zones();
	test_pistol_cup_zone();
	test_turn_bolt_hands();
	test_slot_model_point();
	test_hands_ar_drop_and_fall();
	test_eject_flight();
	test_hands_flick();
	test_hands_toss();
	test_hands_loose_on_ground();
	test_accessory_touch();
	test_grenade_throw();
	test_grenade_pin_early_squeeze();
	test_grenade_either_hand();
	test_grenade_teeth();
	test_level_head();
	test_grenade_drop_and_put_back();
	test_holsters();
	test_hand_signal();
	test_hold_and_handle();
	test_handle_only();
	test_grip_before_reload();
	test_seating();
	test_hands_palming();
	test_pull_out_no_palming();
	test_grip_lock();
	test_seated_hold();
	test_palm_lock();
	test_grip_lock_keeps_the_hold();
	test_quick_throw_switch();
	test_throw_along_aim();
	if (failures == 0)
	{
		std::printf("reload: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
