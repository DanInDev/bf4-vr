#include "weapon/server_soldier.hpp"

namespace bfvr::weapon
{
	std::uint64_t choose_server_soldier(const soldier_candidate* candidates, int count, const float (&client_feet)[3], float tolerance)
	{
		std::uint64_t best = 0;
		float best_squared = tolerance * tolerance;
		for (int i = 0; candidates && i < count; ++i)
		{
			const soldier_candidate& c = candidates[i];
			if (!c.address || !c.typed || !c.placed)
			{
				continue;
			}
			float squared = 0.0f;
			for (int axis = 0; axis < 3; ++axis)
			{
				const float d = c.position[axis] - client_feet[axis];
				squared += d * d;
			}
			if (squared <= best_squared)
			{
				best_squared = squared;
				best = c.address;
			}
		}
		return best;
	}
}
