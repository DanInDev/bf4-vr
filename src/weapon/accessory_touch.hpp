#pragma once

#include "weapon/reload_hands.hpp"

// Accessory toggle by touch: the off hand on the gun's optic (Wep_Scope1) or forward rail
// (Wep_Extra1) plus a grip press sends the game's "toggle accessory" (pad D-pad up: magnifier,
// variable zoom, light, laser). Engine-free; OpenXR LOCAL metres. Tested by tests/reload_test.cpp.
namespace bfvr::weapon
{
	struct accessory_frame
	{
		bool hands_valid{};
		v3 off_hand;
		bool has_optic{};
		v3 optic;
		bool has_rail{};
		v3 rail;
		v3 magwell;          // a hand closer to the magwell belongs to the reload
		bool holding{};      // the off hand holds a magazine: no toggling
		bool grip{};         // off-hand grip held
	};

	struct accessory_result
	{
		bool toggle{}; // send one D-pad up tap
		bool busy{};   // hand on an accessory: keep the grip from the game
	};

	class accessory_touch
	{
	public:
		explicit accessory_touch(float reach = 0.05f) : reach_(reach) {}
		accessory_result update(const accessory_frame& frame);

	private:
		float reach_;
		bool last_grip_{};
	};
}
