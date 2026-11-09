#pragma once

#include "weapon/reload_hands.hpp"

// Use by reaching (#15): the game's interact (pick up a weapon, open a door, a "use" prompt; keyboard
// E) while the off hand reaches out ahead, below the eyes and away from the gun, and squeezes its grip.
// Held for as long as the grip is, wherever the hand then goes (pulling a door). Below eye level so the
// squad signal (raised hand) never uses; away from the gun so a two-handed grab never does. With
// physical reload the pad's own interact (X, shared with reload) is not reachable, hence this.
// Engine-free; OpenXR LOCAL metres. Tested by tests/use_reach_test.cpp.
namespace bfvr::weapon
{
	struct use_frame
	{
		bool head_valid{};
		v3 head;          // between the eyes
		float head_yaw{}; // atan2(-forward.x, -forward.z)
		bool hand_valid{};
		v3 off_hand;
		bool gun_hand_valid{};
		v3 gun_hand;
		// The gun in hand: its pistol grip and handguard (the two-handed grab's line). When known, only a hand
		// near the gun is refused, not one merely near the gun hand.
		bool gun_line_valid{};
		v3 gun_grip;
		v3 gun_support;
		bool off_grip{};
		bool busy{}; // the off hand holds the gun, a magazine or a grenade, or the panel is open
	};

	struct use_result
	{
		bool use{};      // the interact key is down
		bool reaching{}; // the hand is in the use pose (or using): keep its grip from the game
	};

	struct use_config
	{
		// Headset 2026-11-09: the gadget stash would not open. A natural reach is ~30-35 cm ahead of the eyes
		// (40 needed a stretched arm), and a crate at hip height fell between this zone (down to 70 cm below
		// the eyes) and the ground pickup (from 95 cm). Every body zone (pouches, holsters, sling) sits at most
		// 15 cm ahead, so the zone starts at 28 cm and reaches down to the pickup.
		float min_forward{ 0.28f };     // ahead of the head along its yaw
		float max_up{ 0.0f };           // at or below eye level (the squad signal starts above it)
		float min_down{ -0.95f };       // not further below the eyes than this (below: the ground pickup)
		float max_side{ 0.60f };
		float min_gun_distance{ 0.30f }; // from the gun hand, when the gun's line is unknown: not the handguard
		// From the gun (grip to handguard, and past it towards the muzzle): the two-handed grab takes a hand
		// within 12.5 cm of the barrel line. Headset 2026-11-09: the off hand reaching ahead-right for the crates
		// came within 30 cm of the gun hand and was refused.
		float min_gun_line_distance{ 0.18f };
		float gun_line_beyond{ 0.20f };
		// Picking up from the ground (#35): reaching down below the hips counts too, ahead of the body
		// (the hip holsters sit beside it) and within arm's length.
		float pickup_below{ -0.95f };       // this far below the eyes or lower
		float pickup_min_forward{ 0.20f };
		float pickup_max_distance{ 0.90f }; // horizontally from the head
	};

	class use_reach
	{
	public:
		explicit use_reach(const use_config& config = {}) : config_(config) {}
		use_result update(const use_frame& frame);
		bool using_now() const { return using_; }

	private:
		bool in_pose(const use_frame& frame) const;

		use_config config_;
		bool last_grip_{};
		bool using_{};
	};
}
