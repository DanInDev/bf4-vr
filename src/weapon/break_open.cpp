#include "weapon/break_open.hpp"

#include <algorithm>

namespace bfvr::weapon
{
	break_open::break_open(const launcher_options& options) : options_(options)
	{
		load(1, 0);
	}

	void break_open::load(int rounds_in_gun, int reserve)
	{
		state_ = {};
		state_.load = rounds_in_gun > 0 ? barrel_load::live : barrel_load::empty;
		state_.reserve = std::max(reserve, 0);
	}

	launcher_event break_open::trigger()
	{
		if (ready())
		{
			state_.load = barrel_load::spent;
			return launcher_event::shot;
		}
		if (!state_.open)
		{
			state_.open = true;
			return launcher_event::opened;
		}
		return launcher_event::none;
	}

	launcher_event break_open::tip(float muzzle_up_degrees)
	{
		if (state_.open && state_.load == barrel_load::spent && muzzle_up_degrees >= options_.tip_degrees)
		{
			state_.load = barrel_load::empty;
			return launcher_event::case_out;
		}
		return launcher_event::none;
	}

	launcher_event break_open::grab_barrel()
	{
		if (!state_.open)
		{
			return launcher_event::none;
		}
		if (state_.load == barrel_load::spent)
		{
			state_.load = barrel_load::empty;
			return launcher_event::case_out;
		}
		if (state_.load == barrel_load::live && !state_.holding)
		{
			state_.load = barrel_load::empty;
			state_.holding = true;
			return launcher_event::round_out;
		}
		return launcher_event::none;
	}

	launcher_event break_open::take_from_chest()
	{
		if (state_.holding || state_.reserve <= 0)
		{
			return launcher_event::none;
		}
		--state_.reserve;
		state_.holding = true;
		return launcher_event::round_taken;
	}

	launcher_event break_open::insert()
	{
		if (!state_.open || !state_.holding || state_.load != barrel_load::empty)
		{
			return launcher_event::none;
		}
		state_.holding = false;
		state_.load = barrel_load::live;
		return launcher_event::round_seated;
	}

	launcher_event break_open::let_go(bool at_chest)
	{
		if (!state_.holding)
		{
			return launcher_event::none;
		}
		state_.holding = false;
		if (at_chest || !options_.drop_discards_round)
		{
			++state_.reserve;
			return at_chest ? launcher_event::round_stowed : launcher_event::round_dropped;
		}
		return launcher_event::round_dropped;
	}

	launcher_event break_open::close()
	{
		if (!state_.open)
		{
			return launcher_event::none;
		}
		state_.open = false;
		return launcher_event::closed;
	}
}
