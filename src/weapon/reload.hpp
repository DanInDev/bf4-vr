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
		// A bolt action or a pump (gun_traits cycle): a shot leaves the spent case in the chamber, nothing fed, until
		// the bolt or the fore-end is worked by hand (rack). The trigger is blocked meanwhile (empty chamber).
		bool manual_cycle{};
		// A tube magazine (gun_traits feed tube: the 870, M1014, SPAS-12, QBS-09, UTAS): the magazine never leaves the gun;
		// a shell from the pouch is pushed into the loading port, one at a time (insert: shell_loaded), up to capacity.
		bool tube{};
		// Belt-fed (gun_traits feed belt: M249, M240, MG4, Pecheneg, Type 88): a box under the gun, its belt laid on the
		// feed tray under a lid. Fires only with the lid shut on a laid belt; open bolt, so no charging after a reload.
		bool belt{};
		// A top-break revolver (gun_traits feed cylinder: the MP-412 REX): the latch (the release button) breaks the frame
		// open (lid_open; the extractor throws the cases), a speedloader from the pouch fills the empty chambers at the open
		// cylinder, the frame shut (a flick up or a push) brings the next chamber under the hammer: double action.
		bool revolver{};
		// #98: no release button (gun_traits release none on a box or top feed: the P90, the AK 5C; owner 2026-11-19): the
		// button never frees the magazine, the hand pulls it out (grab_magazine), as an AK's; the bolt catch still works.
		bool pull_out{};
		// #96: a turn bolt (gun_traits cycle bolt: the SRS, FY-JS, Scout, SV-98), worked in its four real steps by hand:
		// lifted out of its locking lugs, back (extracts and ejects), forward (feeds), down (locks). Fires only locked.
		bool bolt_turn{};
	};

	bool locks_open(const reload_profile& profile);

	struct reload_options
	{
		bool drop_discards_ammo{ true };
		bool pull_out{ true }; // setting magazine_pull_out: off, a pull_out gun's button frees the magazine as before
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
		shell_loaded,     // tube: one shell pushed into the loading port
		lid_opened,       // belt: the feed cover swung up (a round waiting on the tray goes back on the belt)
		lid_closed,       // belt: the cover shut (on a laid belt the gun is ready)
		belt_taken,       // belt: the hand took the belt end hanging out of the box
		belt_laid,        // belt: laid on the feed tray
		belt_dropped,     // belt: let go off the tray, it hangs from the box again
		bolt_opened,      // turn bolt: lifted out of its lugs, free to come back (#96)
		bolt_closed,      // turn bolt: turned down into its lugs, locked: the gun can fire
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
		bool lid_open{};   // belt: the feed cover is up
		bool belt_laid{ true }; // belt: the box's belt lies on the feed tray
		bool belt_held{};  // belt: the off hand holds the belt end
		bool bolt_up{};    // turn bolt: lifted (unlocked); the trigger does nothing (#96)
		bool bolt_back{};  // turn bolt: drawn back (chamber open)
	};

	class reload
	{
	public:
		explicit reload(const reload_profile& profile = {}, const reload_options& options = {});
		void configure(const reload_options& options) { options_ = options; }
		bool pulled_by_hand() const { return profile_.pull_out && options_.pull_out; } // #98
		const reload_profile& profile() const { return profile_; }
		// Setting manual_cycle, live: a gun that cycles by hand can be switched to the game's automatic cycle.
		void set_manual_cycle(bool on) { profile_.manual_cycle = on; }

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
		// Charging handle pulled back and released (a turn bolt: all four steps at once).
		reload_event rack();
		// #96, a turn bolt by hand (turning()): lift, back, forward, down. Each does nothing out of order.
		bool turning() const { return profile_.bolt_turn && profile_.manual_cycle; }
		reload_event bolt_lift();
		reload_event bolt_back();
		reload_event bolt_forward();
		reload_event bolt_down();
		// Belt-fed: the lid swung up or shut; the belt end taken from the box, laid on the tray, or let go elsewhere.
		reload_event open_lid();
		reload_event close_lid();
		reload_event take_belt();
		reload_event lay_belt();
		reload_event drop_belt();

	private:
		reload_event drop(int rounds);
		reload_event feed();

		reload_profile profile_;
		reload_options options_;
		gun_state state_;
	};

	// #89: the rounds the game's magazine keeps beyond ours, so it never reaches 0 (its own reload) before the trigger
	// block reaches the game, about three frames after our last round: the shots a gun firing this many rounds per
	// second fires in three frames of this length, plus one; never fewer than 2 (as before: the game's HUD counter
	// reads this many high, so a normal frame rate keeps it at 2), at most 8.
	int spare_rounds_for(float rounds_per_second, float frame_seconds);
}
