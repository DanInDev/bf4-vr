#include "weapon/death_watch.hpp"

namespace bfvr::weapon
{
	life_event death_watch::update(bool health_known, float health)
	{
		if (!health_known)
		{
			return life_event::none; // no soldier (the death camera, a load): wait for one
		}
		if (health > 0.0f)
		{
			alive_seen_ = true;
			if (dead_)
			{
				dead_ = false;
				return life_event::respawned;
			}
			return life_event::none;
		}
		if (alive_seen_ && !dead_)
		{
			dead_ = true;
			++deaths_;
			return life_event::died;
		}
		return life_event::none;
	}
}
