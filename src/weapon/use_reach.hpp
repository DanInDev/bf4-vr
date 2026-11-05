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
		float min_forward{ 0.40f };     // ahead of the head along its yaw
		float max_up{ 0.0f };           // at or below eye level (the squad signal starts above it)
		float min_down{ -0.70f };       // not further below the eyes than this
		float max_side{ 0.60f };
		float min_gun_distance{ 0.30f }; // from the gun hand: not the handguard
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
