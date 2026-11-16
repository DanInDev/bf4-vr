#pragma once

// #65: the muzzle flash drawn at the drawn gun. BF4 places the flash's particles at its own (flat) gun's muzzle in the
// world; the move takes particles near that muzzle onto the drawn gun. After The Great Destroyer's src/effects.cpp
// (DrawQuadsMoved and its quad shader; reuse allowed by its maintainer). Engine-free; tests/muzzle_move_test.cpp.
namespace bfvr::stereo::muzzle_move
{
	// The rigid move from the game's frame onto the drawn one (x' = rows[0..2] . p + rows[3], and so on: three rows of
	// rotation | translation), the game's muzzle point and its barrel axis (unit), all in the game's world.
	struct move
	{
		float rows[12]{ 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
		float muzzle[3]{};
		float axis[3]{ 0, 0, 1 };
	};

	// game, drawn: the gun's body (Wep_Root) position and rotation (x y z w) in the game's world as the animation has it
	// and as the mod draws it: the move takes one onto the other. aim: Wep_Aim as the animation has it (the mod does
	// not move it), +z along the barrel; the flash's offset along it (FireEffects1p, ~0.9 m) gives the muzzle point.
	bool make(const float (&game_position)[3], const float (&game_rotation)[4], const float (&drawn_position)[3], const float (&drawn_rotation)[4],
		const float (&aim_position)[3], const float (&aim_rotation)[4], float flash_along, move& out);

	// How much of the move a quad centred at point gets: all of it within across_full of the barrel's line up to
	// along_full past the muzzle, none from across_none / along_none on, and from behind the muzzle back to behind
	// metres (the ejection port's puff) then off over 0.4 m. Times fade (0..1, the moment after firing).
	struct shape
	{
		float across_full{ 0.6f };
		float across_none{ 1.0f };
		float along_full{ 2.2f };
		float along_none{ 3.2f };
		float behind{ 0.6f };
	};
	float weight(const move& m, const float (&point)[3], const shape& s, float fade);

	// The point moved all the way.
	void apply(const move& m, const float (&point)[3], float (&out)[3]);
}
