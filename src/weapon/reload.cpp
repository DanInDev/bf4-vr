#include "weapon/reload.hpp"

#include <cmath>

#include <algorithm>

namespace bfvr::weapon
{
	int spare_rounds_for(float rounds_per_second, float frame_seconds)
	{
		if (!(rounds_per_second > 0.0f) || !(frame_seconds > 0.0f) || !std::isfinite(rounds_per_second) || !std::isfinite(frame_seconds))
		{
			return 2;
		}
		const float in_flight = rounds_per_second * frame_seconds * 3.0f;
		const int spares = static_cast<int>(std::ceil(in_flight)) + 1;
		return spares < 2 ? 2 : spares > 8 ? 8 : spares;
	}

	bool locks_open(const reload_profile& profile)
	{
		return profile.hold == hold_open::by_kind ? profile.kind == variant::ar : profile.hold == hold_open::yes;
	}

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
		// A hold-open gun left empty by the game sits with the bolt open on the empty magazine.
		state_.bolt_locked = !state_.chamber && locks_open(profile_);
	}

	// Bolt going forward: feeds the top round, or an AR's follower holds it open on an empty magazine.
	reload_event reload::feed()
	{
		if (profile_.belt && (state_.lid_open || !state_.belt_laid))
		{
			return reload_event::racked_empty; // no belt under a shut lid: nothing to feed
		}
		if (profile_.revolver && state_.lid_open)
		{
			return reload_event::racked_empty; // the frame open: nothing under the hammer
		}
		if (state_.magazine_in && state_.magazine_rounds > 0)
		{
			--state_.magazine_rounds;
			state_.chamber = true;
			return reload_event::chambered;
		}
		if (state_.magazine_in && locks_open(profile_))
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
		if (!state_.chamber || (turning() && state_.bolt_up)) // #96: an unlocked bolt does not fire
		{
			return reload_event::dry_fire;
		}
		state_.chamber = false;
		if (profile_.manual_cycle)
		{
			return reload_event::shot; // the next round waits for the hand on the bolt or the pump (rack)
		}
		// Without a magazine the bolt closes on an empty chamber (no follower to hold it open).
		return feed() == reload_event::bolt_locked ? reload_event::bolt_locked : reload_event::shot;
	}

	reload_event reload::press_button(bool hand_on_magazine, bool palming)
	{
		if (profile_.kind != variant::ar || profile_.tube) // a tube has no release
		{
			return reload_event::none;
		}
		if (profile_.revolver)
		{
			return open_lid(); // the latch: the frame breaks open (shut again by a flick or a push, not the button)
		}
		if (profile_.belt)
		{
			// The box comes off only with the lid up (the belt lies under it) and takes its belt along.
			if (!state_.lid_open || !state_.magazine_in || state_.belt_held)
			{
				return reload_event::none;
			}
			state_.belt_laid = false;
			const int box = state_.magazine_rounds;
			state_.magazine_in = false;
			state_.magazine_rounds = 0;
			if (hand_on_magazine && !state_.holding)
			{
				state_.holding = true;
				state_.held_rounds = box;
				return reload_event::magazine_out;
			}
			return drop(box);
		}
		palming = palming && state_.holding && !state_.palmed;
		// The first press after a new magazine is the bolt catch: it chambers from the locked bolt or an
		// empty chamber, and with a round already chambered (a tactical reload) the bolt is forward and
		// nothing happens. Only the next press releases the magazine (headset 2026-11-04: it ejected).
		// A hand placed to palm means the release.
		// Without a hold-open there is no bolt catch to press (a UMP, a P90): the button only frees the magazine.
		if (state_.magazine_in && state_.inserted_since_lock && locks_open(profile_))
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
		if (!state_.magazine_in || pulled_by_hand())
		{
			return reload_event::none; // #98: no release button to press
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
		// An AR magazine is latched until the button releases it (#98: unless the gun has none to press).
		if ((profile_.kind != variant::ak && !pulled_by_hand()) || profile_.tube || !state_.magazine_in || state_.holding)
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
		// A tube: one shell; a revolver's speedloader: one round per chamber (the magazine's capacity plus the chamber).
		state_.held_rounds = profile_.tube ? 1 : std::min(profile_.capacity + (profile_.revolver ? 1 : 0), state_.reserve);
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
		if (profile_.revolver)
		{
			// The speedloader at the open cylinder: it fills the empty chambers; what does not fit goes back to the pouch.
			const int chambers = profile_.capacity + 1;
			if (!state_.lid_open || state_.magazine_rounds >= chambers)
			{
				return reload_event::none;
			}
			const int loaded = std::min(chambers - state_.magazine_rounds, state_.held_rounds);
			state_.magazine_rounds += loaded;
			state_.reserve += state_.held_rounds - loaded;
			state_.holding = false;
			state_.held_rounds = 0;
			return reload_event::magazine_in;
		}
		if (profile_.tube)
		{
			// The shell into the loading port: one more in the tube, unless it is full (the shell stays in the hand).
			if (!state_.magazine_in || state_.magazine_rounds >= profile_.capacity)
			{
				return reload_event::none;
			}
			state_.magazine_rounds += state_.held_rounds;
			state_.holding = false;
			state_.held_rounds = 0;
			return reload_event::shell_loaded;
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
		state_.inserted_since_lock = !profile_.belt;
		if (profile_.belt)
		{
			state_.belt_laid = false; // a new box: its belt hangs out until laid on the tray
		}
		return result;
	}

	reload_event reload::open_lid()
	{
		if (!(profile_.belt || profile_.revolver) || state_.lid_open)
		{
			return reload_event::none;
		}
		state_.lid_open = true;
		if (state_.chamber)
		{
			// Open bolt: the round waiting on the tray is still on the belt.
			state_.chamber = false;
			++state_.magazine_rounds;
		}
		return reload_event::lid_opened;
	}

	reload_event reload::empty_cylinder()
	{
		if (!profile_.revolver || !state_.lid_open)
		{
			return reload_event::none;
		}
		const int rounds = state_.magazine_rounds + (state_.chamber ? 1 : 0);
		state_.magazine_rounds = 0;
		state_.chamber = false;
		if (rounds > 0)
		{
			drop(rounds);
		}
		return reload_event::cylinder_emptied; // also with only spent cases in it: they fall out all the same
	}

	reload_event reload::close_lid()
	{
		if (!(profile_.belt || profile_.revolver) || !state_.lid_open)
		{
			return reload_event::none;
		}
		state_.lid_open = false;
		if (!state_.chamber)
		{
			feed(); // on a laid belt the next round is ready (the open bolt is still cocked)
		}
		return reload_event::lid_closed;
	}

	reload_event reload::take_belt()
	{
		// #95: an empty box has no belt left to take (its box comes off with the lid up).
		if (!profile_.belt || !state_.lid_open || !state_.magazine_in || state_.belt_held || state_.holding || state_.magazine_rounds == 0)
		{
			return reload_event::none;
		}
		state_.belt_held = true;
		state_.belt_laid = false; // hanging from the box, or lifted off the tray
		return reload_event::belt_taken;
	}

	reload_event reload::lay_belt()
	{
		if (!state_.belt_held || !state_.lid_open || !state_.magazine_in)
		{
			return reload_event::none;
		}
		state_.belt_held = false;
		state_.belt_laid = true;
		return reload_event::belt_laid;
	}

	reload_event reload::drop_belt()
	{
		if (!state_.belt_held)
		{
			return reload_event::none;
		}
		state_.belt_held = false;
		return reload_event::belt_dropped;
	}

	reload_event reload::bolt_lift()
	{
		if (!turning() || state_.bolt_up)
		{
			return reload_event::none;
		}
		state_.bolt_up = true;
		return reload_event::bolt_opened;
	}

	reload_event reload::bolt_back()
	{
		if (!turning() || !state_.bolt_up || state_.bolt_back)
		{
			return reload_event::none;
		}
		state_.bolt_back = true;
		const bool live = state_.chamber; // a spent case goes without a word; a live round is lost
		state_.chamber = false;
		return live ? reload_event::round_ejected : reload_event::none;
	}

	reload_event reload::bolt_forward()
	{
		if (!turning() || !state_.bolt_back)
		{
			return reload_event::none;
		}
		state_.bolt_back = false;
		state_.bolt_locked = false;
		const reload_event fed = feed();
		return fed == reload_event::bolt_locked ? reload_event::racked_empty : fed; // a bolt gun has no hold-open
	}

	reload_event reload::bolt_down()
	{
		if (!turning() || !state_.bolt_up || state_.bolt_back)
		{
			return reload_event::none;
		}
		state_.bolt_up = false;
		return reload_event::bolt_closed;
	}

	reload_event reload::rack()
	{
		state_.bolt_up = state_.bolt_back = false; // #96: a full rack ends with the bolt locked
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
