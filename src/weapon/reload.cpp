#include "weapon/reload.hpp"

#include <algorithm>

namespace bfvr::weapon
{
	bool release_needs_out_cue(bool was_in, bool is_in, bool out_cue_played)
	{
		return was_in && !is_in && !out_cue_played;
	}

	reload::reload(const reload_profile& profile, const reload_options& options) : profile_(profile), options_(options)
	{
		load(profile_.capacity + 1, 0);
	}

	void reload::load(int rounds_in_gun, int reserve)
	{
		state_ = {};
		rounds_in_gun = std::clamp(rounds_in_gun, 0, profile_.capacity + 1);
		state_.chamber = rounds_in_gun > 0;
		state_.magazine_rounds = std::max(rounds_in_gun - 1, 0);
		state_.reserve = std::max(reserve, 0);
		// An AR left empty by the game sits with the bolt open on the empty magazine.
		state_.bolt_locked = !state_.chamber && profile_.kind == variant::ar;
	}

	// Bolt going forward: feeds the top round, or an AR's follower holds it open on an empty magazine.
	reload_event reload::feed()
	{
		if (state_.magazine_in && state_.magazine_rounds > 0)
		{
			--state_.magazine_rounds;
			state_.chamber = true;
			return reload_event::chambered;
		}
		if (state_.magazine_in && profile_.kind == variant::ar)
		{
			state_.bolt_locked = true;
			state_.inserted_since_lock = false;
			return reload_event::bolt_locked;
		}
		return reload_event::racked_empty;
	}

	reload_event reload::drop(int rounds)
	{
		if (!options_.drop_discards_ammo)
		{
			state_.reserve += rounds;
		}
		return reload_event::magazine_dropped;
	}

	reload_event reload::fire()
	{
		if (!state_.chamber)
		{
			return reload_event::dry_fire;
		}
		state_.chamber = false;
		// Without a magazine the bolt closes on an empty chamber (no follower to hold it open).
		return feed() == reload_event::bolt_locked ? reload_event::bolt_locked : reload_event::shot;
	}

	reload_event reload::press_button(bool hand_on_magazine, bool palming)
	{
		if (profile_.kind != variant::ar)
		{
			return reload_event::none;
		}
		palming = palming && state_.holding && !state_.palmed;
		// The first press after a new magazine is the bolt catch: it chambers from the locked bolt or an
		// empty chamber, and with a round already chambered (a tactical reload) the bolt is forward and
		// nothing happens. Only the next press releases the magazine (headset 2026-11-04: it ejected).
		// A hand placed to palm means the release.
		if (state_.magazine_in && state_.inserted_since_lock)
		{
			state_.inserted_since_lock = false;
			if (!state_.chamber)
			{
				state_.bolt_locked = false;
				const reload_event fed = feed();
				if (fed == reload_event::bolt_locked)
				{
					// #78: an empty magazine. A firm press on the catch drops the bolt anyway; it rides home empty.
					state_.bolt_locked = false;
					return reload_event::racked_empty;
				}
				return fed;
			}
			if (!palming)
			{
				return reload_event::none;
			}
		}
		if (!state_.magazine_in)
		{
			return reload_event::none;
		}
		const int rounds = state_.magazine_rounds;
		state_.magazine_in = false;
		state_.magazine_rounds = 0;
		if (palming)
		{
			state_.palmed = true;
			state_.palmed_rounds = rounds;
			return reload_event::magazine_palmed;
		}
		if (hand_on_magazine && !state_.holding)
		{
			state_.holding = true;
			state_.held_rounds = rounds;
			return reload_event::magazine_out;
		}
		return drop(rounds);
	}

	reload_event reload::grab_magazine()
	{
		// An AR magazine is latched until the button releases it.
		if (profile_.kind != variant::ak || !state_.magazine_in || state_.holding)
		{
			return reload_event::none;
		}
		state_.holding = true;
		state_.held_rounds = state_.magazine_rounds;
		state_.magazine_in = false;
		state_.magazine_rounds = 0;
		return reload_event::magazine_out;
	}

	reload_event reload::take_from_chest()
	{
		if (state_.holding || state_.reserve <= 0)
		{
			return reload_event::none;
		}
		state_.held_rounds = std::min(profile_.capacity, state_.reserve);
		state_.reserve -= state_.held_rounds;
		state_.holding = true;
		return reload_event::magazine_taken;
	}

	reload_event reload::let_go(bool at_chest)
	{
		if (!state_.holding)
		{
			return reload_event::none;
		}
		state_.holding = false;
		const int rounds = state_.held_rounds + state_.palmed_rounds;
		state_.held_rounds = 0;
		state_.palmed = false;
		state_.palmed_rounds = 0;
		if (at_chest)
		{
			state_.reserve += rounds;
			return reload_event::magazine_stowed;
		}
		return drop(rounds);
	}

	reload_event reload::insert()
	{
		if (!state_.holding)
		{
			return reload_event::none;
		}
		if (state_.palmed && !state_.magazine_in)
		{
			state_.magazine_in = true;
			state_.magazine_rounds = state_.held_rounds;
			state_.held_rounds = state_.palmed_rounds;
			state_.palmed = false;
			state_.palmed_rounds = 0;
			state_.inserted_since_lock = true;
			return reload_event::magazine_in;
		}
		if (state_.magazine_in)
		{
			if (profile_.kind != variant::ak || !profile_.flick)
			{
				return reload_event::none;
			}
			// #47: two steps. The old one is knocked out now; the new one stays in the hand and seats on the
			// next insert.
			drop(state_.magazine_rounds);
			state_.magazine_in = false;
			state_.magazine_rounds = 0;
			return reload_event::flicked_out;
		}
		const reload_event result = reload_event::magazine_in;
		state_.magazine_in = true;
		state_.magazine_rounds = state_.held_rounds;
		state_.holding = false;
		state_.held_rounds = 0;
		state_.inserted_since_lock = true;
		return result;
	}

	reload_event reload::rack()
	{
		const bool ejected = state_.chamber;
		state_.chamber = false;
		state_.bolt_locked = false;
		const reload_event fed = feed();
		if (ejected)
		{
			return reload_event::round_ejected;
		}
		// #78: the hand pulled the handle; the bolt caught open is a rack, not a shot, and must sound like one.
		return fed == reload_event::bolt_locked ? reload_event::racked_locked : fed;
	}
}
