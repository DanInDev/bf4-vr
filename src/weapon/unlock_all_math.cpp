#include "weapon/unlock_all.hpp"

namespace bfvr::weapon
{
	unlock_step unlock_all_step(bool wanted, bool level, bool flag, bool ours)
	{
		if (wanted && level)
		{
			return flag ? unlock_step::none : unlock_step::set;
		}
		// Only our own 1 is taken back: a 1 the game set itself stays.
		return flag && ours ? unlock_step::restore : unlock_step::none;
	}
}
