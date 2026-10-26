// Checks the physical reload state machine (src/weapon/reload): AK and AR variants, chest stow and
// take, drop with and without drop_discards_ammo, bolt lock, bolt catch, racking and the AK flick;
// the hand gestures (src/weapon/reload_hands) and the weapon profiles (src/weapon/profiles).
#include <cstdio>
#include <initializer_list>

#include "weapon/profiles.hpp"
#include "weapon/reload.hpp"
#include "weapon/reload_hands.hpp"

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
	void test_profiles()
	{
		const reload_profile ak12 = profile_for("U_AK12", 31);
		expect(ak12.kind == variant::ak && ak12.capacity == 30 && ak12.flick, "AK-12: AK, 30 rounds");
		const reload_profile scar = profile_for("U_SCAR-H", 21);
		expect(scar.kind == variant::ar && scar.capacity == 20 && !scar.flick, "SCAR-H: AR, 20 rounds");
		expect(profile_for("U_M416", 1).capacity == 1, "capacity never below 1");
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

	void test_hands_ak()
	{
		reload gun(ak);
		gun.load(11, 60);
		reload_hands hands;
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
		expect(has(r, reload_event::magazine_in) && r.draw == magazine_draw::with_gun, "bring it to the magwell to insert");
		hands.update(gun, at(away, false));
		expect(gun.rounds_in_gun() == 31, "chambered round kept: 30+1");
		hands.update(gun, at(slide, true));
		r = hands.update(gun, at({ slide.x, slide.y, slide.z + 0.07f }, false));
		expect(has(r, reload_event::round_ejected), "pull the slide back and let go racks");
		hands.update(gun, at(slide, true));
		r = hands.update(gun, at({ slide.x, slide.y, slide.z + 0.02f }, false));
		expect(r.count == 0, "a short pull does not rack");
	}

	void test_hands_ar_drop_and_fall()
	{
		reload gun(ar);
		gun.load(16, 30);
		reload_hands hands;
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

	void test_hands_flick()
	{
		reload gun(ak, { false });
		gun.load(11, 60);
		reload_hands hands;
		hands.update(gun, at(chest, true));
		hand_result r = hands.update(gun, at(magwell, true));
		expect(has(r, reload_event::flicked_in) && r.draw == magazine_draw::with_gun, "new magazine knocks the old one out");
		expect(gun.rounds_in_gun() == 31 && gun.state().reserve == 40, "old rounds back (discard off)");
		hand_frame lost = at(magwell, false);
		lost.hands_valid = false;
		reload_hands blind;
		r = blind.update(gun, lost);
		lost.grip = true;
		r = blind.update(gun, lost);
		expect(r.count == 0, "no gestures without hand tracking");
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
	test_profiles();
	test_hands_ak();
	test_hands_ar_drop_and_fall();
	test_hands_flick();
	if (failures == 0)
	{
		std::printf("reload: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
