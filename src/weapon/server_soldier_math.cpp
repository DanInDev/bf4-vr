#include "weapon/server_soldier.hpp"

namespace bfvr::weapon
{
	bool keep_server_soldier(bool typed, bool same_client, float distance, float frame_seconds, float& off_seconds)
	{
		if (!typed || !same_client)
		{
			off_seconds = 0.0f;
			return false;
		}
		off_seconds = distance > drift_metres ? off_seconds + (frame_seconds > 0.0f ? frame_seconds : 0.0f) : 0.0f;
		return off_seconds <= drift_seconds;
	}

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
