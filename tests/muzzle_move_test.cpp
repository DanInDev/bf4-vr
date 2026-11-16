// Checks the muzzle flash move (src/stereo/muzzle_move, #65): the game's muzzle goes onto the drawn one, a point along
// the game's barrel lands along the drawn barrel, the identity move moves nothing, and the weight is full near the
// barrel's line, fades across and along it, and is zero far off, behind the gun and when the fade is over.
#include <cmath>
#include <cstdio>

#include "stereo/muzzle_move.hpp"

using namespace bfvr::stereo::muzzle_move;

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

	bool near(float a, float b, float tolerance = 1e-3f)
	{
		return std::fabs(a - b) <= tolerance;
	}
}

int main()
{
	// The game's gun at the chest pointing +z; the drawn one 40 cm right and 20 cm down, turned 90 deg to +x (yaw).
	const float game_position[3] = { 10.0f, 1.4f, 5.0f };
	const float game_rotation[4] = { 0, 0, 0, 1 };
	const float drawn_position[3] = { 10.4f, 1.2f, 5.0f };
	const float half = std::sqrt(0.5f);
	const float drawn_rotation[4] = { 0, half, 0, half }; // +90 deg about y: +z goes to +x
	move m;
	expect(make(game_position, game_rotation, drawn_position, drawn_rotation, game_position, game_rotation, 0.9f, m), "a move from two frames");
	expect(near(m.muzzle[0], 10.0f) && near(m.muzzle[1], 1.419f) && near(m.muzzle[2], 5.9f), "the game's muzzle 0.9 m along +z, 1.9 cm up");
	expect(near(m.axis[2], 1.0f), "the game's barrel axis is +z");
	float out[3];
	apply(m, m.muzzle, out);
	expect(near(out[0], 11.3f) && near(out[1], 1.219f) && near(out[2], 5.0f), "the game's muzzle lands 0.9 m along the drawn barrel (+x)");
	const float ahead[3] = { 10.0f, 1.4f, 6.5f }; // 1.5 m along the game's barrel
	apply(m, ahead, out);
	expect(near(out[0], 11.9f) && near(out[1], 1.2f) && near(out[2], 5.0f), "a point along the game's barrel lands along the drawn one");

	// Wep_Aim away from the gun's body: the muzzle comes from Wep_Aim, the move from the body.
	const float aim_position[3] = { 10.1f, 1.5f, 5.2f };
	move offset;
	expect(make(game_position, game_rotation, drawn_position, drawn_rotation, aim_position, game_rotation, 0.9f, offset), "a move with Wep_Aim off the body");
	expect(near(offset.muzzle[0], 10.1f) && near(offset.muzzle[1], 1.519f) && near(offset.muzzle[2], 6.1f), "the muzzle along Wep_Aim");
	apply(offset, offset.muzzle, out);
	expect(near(out[0], 11.5f) && near(out[1], 1.319f) && near(out[2], 4.9f), "the muzzle moved as the body is (0.1 left of it turns to 0.1 back)");

	// The identity move.
	move same;
	expect(make(game_position, game_rotation, game_position, game_rotation, game_position, game_rotation, 0.9f, same), "same frames");
	apply(same, ahead, out);
	expect(near(out[0], ahead[0]) && near(out[1], ahead[1]) && near(out[2], ahead[2]), "the same frame moves nothing");

	// Weights along and across the game's barrel.
	const shape s;
	const float at_muzzle[3] = { 10.0f, 1.42f, 5.9f };
	expect(near(weight(m, at_muzzle, s, 1.0f), 1.0f), "at the muzzle: all of it");
	expect(near(weight(m, at_muzzle, s, 0.25f), 0.25f), "the fade scales it");
	expect(weight(m, at_muzzle, s, 0.0f) == 0.0f, "faded out: nothing");
	const float across_half[3] = { 10.8f, 1.42f, 6.4f }; // 0.8 m off the line, 0.5 m along
	expect(near(weight(m, across_half, s, 1.0f), 0.5f, 0.01f), "0.8 m across: half (full to 0.6, none at 1.0)");
	const float far_along[3] = { 10.0f, 1.42f, 5.9f + 3.5f };
	expect(weight(m, far_along, s, 1.0f) == 0.0f, "3.5 m past the muzzle (the dirt a shot throws up): none");
	const float behind[3] = { 10.0f, 1.42f, 5.9f - 1.2f };
	expect(weight(m, behind, s, 1.0f) == 0.0f, "1.2 m behind the muzzle (behind the gun): none");
	const float port[3] = { 10.0f, 1.42f, 5.9f - 0.5f };
	expect(near(weight(m, port, s, 1.0f), 1.0f), "0.5 m behind the muzzle (the ejection port's puff): all of it");

	// Bad frames.
	const float zero[4] = { 0, 0, 0, 0 };
	move bad;
	expect(!make(game_position, zero, drawn_position, drawn_rotation, game_position, game_rotation, 0.9f, bad), "a zero rotation is refused");

	if (failures == 0)
	{
		std::printf("muzzle_move: all checks passed\n");
	}
	return failures == 0 ? 0 : 1;
}
