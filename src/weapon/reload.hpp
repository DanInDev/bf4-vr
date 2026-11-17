#pragma once

#include <cstdint>

// Physical reloading (issue #14): the state of one gun and its magazines, driven by hand events.
// Engine-free; the game adapter turns controller poses into the events below, writes
// rounds_in_gun() and reserve back into the weapon and moves/hides the Mag bone. Tested by
// tests/reload_test.cpp.
//
//   AK variant: no release button. Grab the magazine with the off hand and pull it out; insert a new
//               one; rack the charging handle if the chamber is empty. A new magazine pushed
//               against a seated one knocks the old one out (flick), when the profile allows it.
//   AR variant: the button releases the magazine (it drops free unless the off hand is on it).
//               Firing the last round with a magazine in locks the bolt back; with a magazine in,
//               the button then works as the bolt catch and chambers a round. Running dry with the
//               magazine out leaves the bolt closed on an empty chamber: rack by hand.
//               Palming: with a new magazine held at the magwell, the button releases the seated one
//               into the same hand (both held); the new one then goes in and the old one stays in the
//               hand for the pouch.
//
// Magazines come from and go back to the chest: taking one fills it from the reserve, stowing one
// returns its rounds. A dropped magazine falls through the floor; drop_discards_ammo says whether
// its rounds are lost or go back into the reserve.
namespace bfvr::weapon
{
	enum class variant
	{
		ak,
		ar,
	};

	// Whether the last shot locks the bolt back (and the AR's bolt catch closes it): by the variant (AR yes, AK no) unless
	// the gun says otherwise (weapon/gun_traits: a UMP or a P90 has none, an M14 or an SVD has one).
	enum class hold_open
	{
		by_kind,
		yes,
		no,
	};

	struct reload_profile
	{
		variant kind{ variant::ar };
		int capacity{ 30 };
		bool flick{ true }; // AK: a new magazine can knock the old one out
		hold_open hold{ hold_open::by_kind };
	};

	bool locks_open(const reload_profile& profile);

	struct reload_options
	{
		bool drop_discards_ammo{ true };
	};

	// What an event did, for haptics, sounds and the Mag bone.
	enum class reload_event
	{
		none,             // the event did nothing in this state
		shot,
		dry_fire,         // trigger on an empty chamber
		bolt_locked,      // AR: the last round went, the follower keeps the bolt open (no sound)
		magazine_out,     // into the off hand
		magazine_dropped, // fell through the floor (AR release without the hand on it, or let go)
		magazine_stowed,  // back at the chest, rounds to the reserve
		magazine_taken,   // new one from the chest
		magazine_in,
		flicked_out,      // AK: the new magazine knocked the old one out; the new one is still in the hand (#47: seats next)
		chambered,        // bolt catch or rack fed a round
		racked_empty,     // rack without a round to feed
		round_ejected,    // rack with a round in the chamber: that round is lost (state() shows what was fed)
		magazine_palmed,  // AR palming: the seated magazine released into the hand that holds the new one
		racked_locked,    // AR: a rack on an empty magazine, the follower holds the bolt open (#78: sounds like the bolt)
	};

	// #78: the seated magazine left the gun (was_in, now out) and no magazine-out cue played for it this frame (a pull
	// into the hand, a flick or palming already sound): the release dropped it, which must still sound. Independent of
	// a magazine in the hand (the usual empty reload holds the new one while the old one drops).
	bool release_needs_out_cue(bool was_in, bool is_in, bool out_cue_played);

	struct gun_state
	{
		bool magazine_in{ true };
		int magazine_rounds{ 30 };
		bool chamber{ true };
		bool bolt_locked{};
		int reserve{};
		bool holding{}; // the off hand holds a magazine
		int held_rounds{};
		bool inserted_since_lock{}; // AR: the button chambers instead of releasing
		bool palmed{};   // AR palming: the off hand also holds the old magazine it caught
		int palmed_rounds{};
	};

	class reload
	{
	public:
		explicit reload(const reload_profile& profile = {}, const reload_options& options = {});
		void configure(const reload_options& options) { options_ = options; }
		const reload_profile& profile() const { return profile_; }

		// The gun as the game left it (switching weapons, level start): rounds in the gun counting
		// the chamber, and the reserve. Nothing held, bolt forward.
		void load(int rounds_in_gun, int reserve);
		// The game changed the reserve (ammo pickup, resupply): take its number.
		void set_reserve(int reserve) { state_.reserve = reserve < 0 ? 0 : reserve; }
		const gun_state& state() const { return state_; }
		int rounds_in_gun() const { return state_.magazine_rounds + (state_.chamber ? 1 : 0); }

		reload_event fire();
		// AR: magazine release; once the bolt is locked back and a new magazine went in, the bolt catch.
		// hand_on_magazine: the off hand is gripping at the magwell and catches the magazine.
		// palming: the off hand holds a new magazine beside the magwell and catches the old one with it
		// (a round in the chamber: the press releases even right after an insert, the bolt is forward).
		reload_event press_button(bool hand_on_magazine, bool palming = false);
		// Off-hand grip closed at the magwell (AK: pulls the magazine out).
		reload_event grab_magazine();
		// Off-hand grip closed at the chest.
		reload_event take_from_chest();
		// Off-hand grip opened while holding a magazine (both, when palmed); at_chest stows it, anywhere
		// else drops it.
		reload_event let_go(bool at_chest);
		// The held magazine reaches the magwell. Palmed: the new one goes in, the old one stays held.
		reload_event insert();
		// Charging handle pulled back and released.
		reload_event rack();

	private:
		reload_event drop(int rounds);
		reload_event feed();

		reload_profile profile_;
		reload_options options_;
		gun_state state_;
	};
}
