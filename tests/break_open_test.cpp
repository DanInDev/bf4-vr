// Checks the break-open launcher (src/weapon/break_open, #64, the M320): fire, the trigger opening an empty or spent
// launcher, the spent case sliding out only with the muzzle up (or pulled by hand), a round from the chest seated and
// latched, a live round pulled back out, stowing and dropping, the counts the game is given, and nothing happening
// out of order.
#include <cstdio>

#include "weapon/break_open.hpp"

using namespace bfvr::weapon;
using event = launcher_event;

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
	// A full reload: fire, the trigger opens it, the case slides out with the muzzle up, a round goes in, it shuts.
	{
		break_open m320;
		m320.load(1, 3);
		expect(m320.ready() && m320.rounds_in_gun() == 1, "loaded from the game: ready, one round");
		expect(m320.trigger() == event::shot, "the trigger fires when ready");
		expect(m320.state().load == barrel_load::spent && m320.rounds_in_gun() == 0, "a fired launcher holds a spent case, nothing to fire");
		expect(m320.trigger() == event::opened && m320.state().open, "the next pull works the release: open");
		expect(m320.trigger() == event::none, "a pull while open does nothing");
		expect(m320.tip(10.0f) == event::none && m320.state().load == barrel_load::spent, "muzzle 10 deg up: the case stays");
		expect(m320.tip(35.0f) == event::case_out && m320.state().load == barrel_load::empty, "muzzle 35 deg up: the case slides out");
		expect(m320.take_from_chest() == event::round_taken && m320.state().holding, "a round from the chest");
		expect(m320.state().reserve == 2 && m320.game_reserve() == 3, "the held round still counts in the game's reserve");
		expect(m320.take_from_chest() == event::none, "a second round while holding one: nothing");
		expect(m320.insert() == event::round_seated && !m320.state().holding, "the round pushed home");
		expect(!m320.ready() && m320.rounds_in_gun() == 1, "seated but open: counts as loaded, not ready to fire");
		expect(m320.close() == event::closed && m320.ready(), "shut: ready");
		expect(m320.game_reserve() == 2, "two left");
	}

	// The case pulled out by hand; a round into a barrel that still holds the case is refused.
	{
		break_open m320;
		m320.load(1, 1);
		m320.trigger();
		m320.trigger();
		m320.take_from_chest();
		expect(m320.insert() == event::none, "the case is still in: the round does not go in");
		m320.let_go(true);
		expect(m320.state().reserve == 1, "stowed back");
		expect(m320.grab_barrel() == event::case_out && m320.state().load == barrel_load::empty, "the hand pulls the case out");
		expect(m320.grab_barrel() == event::none, "an empty barrel: nothing to grab");
	}

	// An empty launcher from the game: the trigger opens it (no dry fire), no case to tip out.
	{
		break_open m320;
		m320.load(0, 2);
		expect(m320.state().load == barrel_load::empty, "empty from the game: no case");
		expect(m320.trigger() == event::opened, "the trigger opens an empty launcher");
		expect(m320.tip(80.0f) == event::none, "nothing slides out of an empty barrel");
	}

	// A live round pulled back out of the open barrel, then dropped (lost) or stowed.
	{
		break_open m320;
		m320.load(1, 0);
		expect(m320.grab_barrel() == event::none, "shut: the barrel cannot be grabbed");
		expect(m320.close() == event::none, "shut: closing does nothing");
		// Open it while loaded: only by firing first, so give it an open, loaded barrel this way.
		m320.trigger();
		m320.trigger();
		m320.grab_barrel();
		expect(m320.state().load == barrel_load::empty, "case out");
		m320.set_reserve(1);
		m320.take_from_chest();
		m320.insert();
		expect(m320.grab_barrel() == event::round_out && m320.state().holding, "a live round comes back into the hand");
		expect(m320.let_go(false) == event::round_dropped && m320.game_reserve() == 0, "dropped: lost");
	}

	// drop_discards_round off: a dropped round goes back to the reserve.
	{
		launcher_options keep;
		keep.drop_discards_round = false;
		break_open m320(keep);
		m320.load(0, 1);
		m320.take_from_chest();
		expect(m320.let_go(false) == event::round_dropped && m320.state().reserve == 1, "kept: the dropped round is back in the reserve");
		expect(m320.let_go(false) == event::none, "nothing held: let go does nothing");
	}

	// The tip angle is a setting; no reserve, no round.
	{
		launcher_options steep;
		steep.tip_degrees = 60.0f;
		break_open m320(steep);
		m320.load(1, 0);
		m320.trigger();
		m320.trigger();
		expect(m320.tip(45.0f) == event::none && m320.tip(61.0f) == event::case_out, "tip at 60 deg: 45 keeps it, 61 lets it slide");
		expect(m320.take_from_chest() == event::none, "no reserve: no round");
		m320.load(5, -3);
		expect(m320.rounds_in_gun() == 1 && m320.state().reserve == 0, "load clamps: one round, no negative reserve");
	}

	if (failures == 0)
	{
		std::printf("break_open: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
