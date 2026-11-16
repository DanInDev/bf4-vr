#pragma once

// #64 (owner 2026-11-15): the M320 grenade launcher reloaded by hand, as the real one works and as the BF3 VR mod
// "The Great Destroyer" does it (src/m320.h, the M416's under-rifle M320; its maintainer allows reuse, credit it): a pull
// of the trigger while it is not ready to fire works the barrel release, and the barrel's back end swings out to the
// gun's left about a pin at its front. Nothing pulls the spent case: it slides out once the muzzle is tipped up far
// enough, or the off hand pulls it out by its rim. A new round comes from the chest pouch, goes into the open barrel's
// back end and is pushed home; a push on the barrel (or a roll of the wrist) swings it shut and it latches.
//
// The state of the launcher and its rounds, driven by hand events; engine-free. The game adapter turns poses into the
// events, writes rounds_in_gun() and the reserve back into the weapon and draws the barrel and the round. Tested by
// tests/break_open_test.cpp.
namespace bfvr::weapon
{
	enum class barrel_load
	{
		empty,
		live,  // a round that fires
		spent, // the case of a fired round, loose in the barrel
	};

	enum class launcher_event
	{
		none,           // the event did nothing in this state
		shot,
		opened,         // the release worked by a trigger pull while not ready: the barrel swings out
		case_out,       // the spent case slid out (muzzle up) or was pulled out by hand and let fall
		round_taken,    // a new round from the chest into the off hand
		round_seated,   // the held round pushed home in the open barrel
		round_out,      // a live round pulled back out of the open barrel into the hand
		round_stowed,   // the held round back at the chest (to the reserve)
		round_dropped,  // the held round let go elsewhere (lost, or back to the reserve: drop_discards_round)
		closed,         // swung shut and latched
	};

	struct launcher_options
	{
		bool drop_discards_round{ true };
		float tip_degrees{ 30.0f }; // muzzle above level at which a spent case slides out of the open barrel
	};

	struct launcher_state
	{
		bool open{};
		barrel_load load{ barrel_load::live };
		int reserve{};
		bool holding{}; // the off hand holds a live round
	};

	class break_open
	{
	public:
		explicit break_open(const launcher_options& options = {});
		void configure(const launcher_options& options) { options_ = options; }

		// The launcher as the game left it (switching weapons, level start): 1 or 0 rounds in it, the reserve. Shut,
		// nothing held. An empty launcher from the game holds no case (the game's own reload took it).
		void load(int rounds_in_gun, int reserve);
		// The game changed the reserve (resupply): take its number.
		void set_reserve(int reserve) { state_.reserve = reserve < 0 ? 0 : reserve; }
		const launcher_state& state() const { return state_; }
		// What the game should count in the gun: only a live round in a shut barrel fires.
		int rounds_in_gun() const { return state_.load == barrel_load::live ? 1 : 0; }
		// What the game should count in reserve: the pouch plus a round in the hand (it is not in the gun).
		int game_reserve() const { return state_.reserve + (state_.holding ? 1 : 0); }
		// Shut, latched and loaded: the trigger fires and goes to the game.
		bool ready() const { return !state_.open && state_.load == barrel_load::live; }

		// The trigger pulled: fires when ready, otherwise works the release and opens the barrel.
		launcher_event trigger();
		// Each frame while open: the muzzle's angle above level (degrees). A spent case slides out past tip_degrees.
		launcher_event tip(float muzzle_up_degrees);
		// The off hand grips at the open barrel's back end: a spent case is pulled out (and falls); a live round comes
		// back into the hand when the hand is free.
		launcher_event grab_barrel();
		// Off-hand grip closed at the chest pouch.
		launcher_event take_from_chest();
		// The held round's nose reaches the open, empty barrel and is pushed home.
		launcher_event insert();
		// Off-hand grip opened while holding a round: at_chest stows it, anywhere else drops it.
		launcher_event let_go(bool at_chest);
		// The barrel pushed (or rolled) shut.
		launcher_event close();

	private:
		launcher_options options_;
		launcher_state state_;
	};
}
