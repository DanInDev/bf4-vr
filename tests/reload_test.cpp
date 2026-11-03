// Checks the physical reload state machine (src/weapon/reload): AK and AR variants, chest stow and
// take, drop with and without drop_discards_ammo, bolt lock, bolt catch, racking and the AK flick;
// the hand gestures (src/weapon/reload_hands), the weapon profiles (src/weapon/profiles) and the
// accessory toggle by touch (src/weapon/accessory_touch) and the grenade gestures
// (src/weapon/grenade_hands), the weapon holsters (src/weapon/holsters) and the squad hand signal
// (src/weapon/hand_signal).
#include <cmath>
#include <cstdio>
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
		// Headset 2026-11-03: the button after a new magazine dropped it instead of chambering.
		expect(gun.press_button(false) == reload_event::chambered && gun.state().chamber, "empty chamber, new magazine: the button chambers (bolt not locked)");
		expect(gun.press_button(false) == reload_event::magazine_dropped && gun.state().chamber, "then it releases, the round stays chambered");
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

		reload third(ak);
		third.load(11, 60);
		reload_hands far(flat_hold());
		far.update(third, palm_at(away, false));
		r = far.update(third, palm_at(body + v3{ 0.0f, -0.16f, -0.03f }, true));
		expect(r.count == 0, "16 cm below the magazine is out of reach");
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

	void test_hands_flick()
	{
		reload gun(ak, { false });
		gun.load(11, 60);
		reload_hands hands(flat_hold());
		hands.update(gun, at(chest, true));
		hand_result r = hands.update(gun, at(magwell, true));
		expect(has(r, reload_event::flicked_in) && r.draw == magazine_draw::with_gun, "new magazine knocks the old one out");
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
		expect(draw_at(h, frame, { 0.2f, 0.05f, 0.15f }) == holster::primary, "over the right shoulder: primary");
		expect(draw_at(h, frame, { 0.24f, -0.72f, 0.02f }) == holster::secondary, "right hip: secondary");
		expect(draw_at(h, frame, { -0.10f, -0.27f, -0.10f }) == holster::knife, "high left chest: knife");
		expect(draw_at(h, frame, { -0.2f, 0.05f, 0.15f }) == holster::gadget1, "over the left shoulder: gadget 1");
		expect(draw_at(h, frame, { -0.22f, -0.72f, 0.02f }) == holster::gadget2, "left hip: gadget 2");
		expect(draw_at(h, frame, magazine_pouch) == holster::none && draw_at(h, frame, grenade_pouch) == holster::none, "the off hand's pouches are no holsters");
		expect(draw_at(h, frame, { 0.15f, -0.25f, -0.35f }) == holster::none, "aiming position: nothing");
		frame.gun_hand = { 0.15f, -0.25f, -0.35f };
		frame.gun_grip = true;
		h.update(frame);
		frame.gun_hand = { 0.2f, 0.05f, 0.15f };
		expect(h.update(frame).draw == holster::none, "a grip held into the zone does not draw");
		frame.gun_grip = false;
		expect(h.update(frame).zone == holster::primary, "zone reported while the hand is there");

		holster_frame left = frame;
		left.left_handed = true;
		holsters mirrored;
		expect(draw_at(mirrored, left, { -0.2f, 0.05f, 0.15f }) == holster::primary, "left-handed: primary over the left shoulder");
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
		// Body yaw 0.97 rad (turned left): the primary sits right-back of that body.
		const float b = turning.body_yaw();
		const v3 primary{ 0.2f * std::cos(b) + 0.15f * std::sin(b), 0.05f, -0.2f * std::sin(b) + 0.15f * std::cos(b) };
		expect(draw_at(turning, turn, primary) == holster::primary, "zones turn with the body");
		// grab_debug rings: the reported centres are where the zones are (turned body, both hands).
		turn.gun_hand = { 5.0f, 5.0f, 5.0f };
		const holster_result rings = turning.update(turn);
		expect(rings.has_centres && std::fabs(rings.centres[0].x - primary.x) < 1e-4f && std::fabs(rings.centres[0].z - primary.z) < 1e-4f
			&& std::fabs(rings.radii[0] - 0.155f) < 1e-6f, "ring centre and radius of the primary holster");
		holster_frame mirrored_ring = left;
		mirrored_ring.gun_hand = { 5.0f, 5.0f, 5.0f };
		holsters mirrored_rings;
		const holster_result left_rings = mirrored_rings.update(mirrored_ring);
		expect(left_rings.has_centres && std::fabs(left_rings.centres[2].x - 0.10f) < 1e-4f, "left-handed: the knife ring on the right chest");
		holster_frame blind = frame;
		blind.head_valid = false;
		expect(draw_at(h, blind, { 0.2f, 0.05f, 0.15f }) == holster::none, "no head tracking: no holsters");

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
		expect(off_draw(off, frame, { -0.2f, 0.05f, 0.15f }, false) == holster::gadget1, "off hand over its shoulder: gadget 1");
		expect(off_draw(off, frame, { -0.22f, -0.75f, 0.0f }, false) == holster::gadget2, "off hand at its hip: gadget 2");
		expect(off_draw(off, frame, { -0.22f, -0.75f, 0.0f }, true) == holster::none, "a busy off hand draws nothing");
		expect(off_draw(off, frame, { 0.2f, 0.05f, 0.15f }, false) == holster::none, "the primary is the gun hand's");
		expect(off_draw(off, frame, { -0.10f, -0.27f, -0.10f }, false) == holster::none, "the knife is the gun hand's (cross draw)");

		// Chest rig: the pouches sit in the body frame, apart from each other and from the off hand's holsters.
		const v3 head{ 0.1f, 1.6f, -0.2f };
		const v3 pouch = body_point(head, 1.5707963f, grenade_pouch, false);
		expect(std::fabs(pouch.x - (head.x + grenade_pouch.z)) < 1e-5f && std::fabs(pouch.z - (head.z - grenade_pouch.x)) < 1e-5f,
			"body turned left 90 degrees: the pouch offsets turn with it");
		const auto gap = [](v3 a, v3 b) { return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) + (a.z - b.z) * (a.z - b.z)); };
		const hand_config reach;
		const grenade_config grenade_reach;
		expect(gap(grenade_pouch, magazine_pouch) > reach.reach_chest + grenade_reach.reach_belt, "grenade and magazine pouches do not overlap");
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
		expect(!squeeze({ -0.1f, 0.15f, -0.2f }).order, "hand raised but close to the head: no order");
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
		ak_gun.insert();
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
		short_gun.insert();
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
}

// Each physical reload event plays the stage of the game's reload patch matched to it (re/notes/sound.md).
static void test_reload_sound_cues()
{
	using namespace bfvr::weapon;
	sound_cue cues[2]{};
	expect(cues_for(reload_event::magazine_out, cues) == 1 && cues[0].event == reload_sound::magazine_out && cues[0].delay_ms == 0, "cue: magazine out");
	expect(cues_for(reload_event::magazine_in, cues) == 2 && cues[0].event == reload_sound::magazine_in && cues[1].event == reload_sound::magazine_seat
		&& cues[1].delay_ms > 0, "cue: magazine in, then seated");
	expect(cues_for(reload_event::flicked_in, cues) == 2 && cues[0].event == reload_sound::magazine_in, "cue: flicked in");
	expect(cues_for(reload_event::chambered, cues) == 1 && cues[0].event == reload_sound::bolt, "cue: chambered");
	expect(cues_for(reload_event::racked_empty, cues) == 1 && cues[0].event == reload_sound::bolt, "cue: racked empty");
	expect(cues_for(reload_event::magazine_stowed, cues) == 1 && cues[0].event == reload_sound::settle, "cue: stowed");
	for (const reload_event silent : { reload_event::none, reload_event::shot, reload_event::dry_fire, reload_event::magazine_taken, reload_event::magazine_dropped })
	{
		expect(cues_for(silent, cues) == 0, "cue: no sound");
	}
}

int main()
{
	test_reload_sound_cues();
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
	test_hands_reach_and_overlap();
	test_hands_ar_drop_and_fall();
	test_hands_flick();
	test_accessory_touch();
	test_grenade_throw();
	test_grenade_drop_and_put_back();
	test_holsters();
	test_hand_signal();
	test_hold_and_handle();
	test_seating();
	if (failures == 0)
	{
		std::printf("reload: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
