// Checks the physical reload state machine (src/weapon/reload): AK and AR variants, chest stow and
// take, drop with and without drop_discards_ammo, bolt lock, bolt catch, racking and the AK flick.
#include <cstdio>
#include <initializer_list>

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

	reload_profile ak{ variant::ak, 30, true };
	reload_profile ar{ variant::ar, 30, true };

	// Every round the player still owns: in the gun, in the hand, in the reserve.
	int owned(const reload& gun)
	{
		return gun.rounds_in_gun() + gun.state().held_rounds + gun.state().reserve;
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
		expect(gun.press_button(false) == reload_event::magazine_dropped, "button releases (bolt is not locked, so no catch)");
		gun.take_from_chest();
		expect(gun.state().held_rounds == 0 && !gun.state().holding, "reserve already used up");
		reload again(ar);
		again.load(1, 30);
		again.press_button(false);
		again.fire();
		again.take_from_chest();
		again.insert();
		expect(again.rack() == reload_event::chambered && again.state().chamber && again.state().magazine_rounds == 29, "manual rack chambers");
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
		expect(gun.insert() == reload_event::flicked_in, "flick knocks the old magazine out");
		expect(gun.rounds_in_gun() == 31 && gun.state().reserve == 40, "new magazine in, old rounds back (discard off)");
		expect(owned(gun) == before, "flick with discard off keeps every round");
		reload strict(ak);
		strict.load(11, 60);
		strict.take_from_chest();
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
				case 1: result = gun.press_button((seed >> 8) & 1); break;
				case 2: result = gun.grab_magazine(); break;
				case 3: result = gun.take_from_chest(); break;
				case 4: result = gun.let_go((seed >> 9) & 1); break;
				case 5: result = gun.insert(); break;
				case 6: result = gun.rack(); break;
				}
				// A shot (bolt_locked from the trigger is one too) or a racked-out round costs one.
				const bool spends = result == reload_event::shot || result == reload_event::round_ejected
					|| (action == 0 && result == reload_event::bolt_locked);
				const int lost = before - owned(gun);
				if (lost != (spends ? 1 : 0))
				{
					expect(false, "rounds only leave by a shot or a rack");
					break;
				}
				total -= lost;
				const gun_state& s = gun.state();
				if (s.magazine_rounds < 0 || s.magazine_rounds > profile.capacity || s.held_rounds < 0 || s.reserve < 0 || (!s.magazine_in && s.magazine_rounds != 0) || (!s.holding && s.held_rounds != 0))
				{
					expect(false, "state stays consistent");
					break;
				}
			}
			expect(total == owned(gun), "bookkeeping matches");
		}
	}
}

int main()
{
	test_load();
	test_ar_empty_reload();
	test_ar_tactical_catch_and_stow();
	test_ar_dry_without_magazine();
	test_ak();
	test_ak_flick();
	test_drop_option();
	test_hand_rules();
	test_conservation();
	if (failures == 0)
	{
		std::printf("reload: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
