#pragma once

#include "weapon/reload_hands.hpp"

// Free hands (#35, setting free_hands, default off): the gun is held only while the gun hand squeezes
// its grip. Let go for release_seconds and it drops to the floor below the hand, lying there (drawn by
// body/arm_pose) while the game keeps the weapon; the trigger does nothing. Reach down to it and grip
// to pick it up again. Drawing another weapon (holsters) or the game switching weapons ends the drop.
// Engine-free; OpenXR LOCAL metres. Tested by tests/free_hands_test.cpp.
namespace bfvr::weapon
{
	struct free_hands_frame
	{
		float seconds{};      // since the last update
		bool enabled{};
		bool gun_in_hand{};   // a primary or secondary is drawn (the only items that can drop)
		bool weapon_changed{}; // a different weapon is now in hand
		bool hand_valid{};
		v3 gun_hand;          // the gun hand's palm (grip point)
		bool grip{};          // gun-hand grip held
		bool busy{};          // the grip belongs to something else now (holster draw, grenade pin, panel)
		float floor_y{};      // the floor's height, same space
		float hand_yaw{};     // the gun hand's yaw (atan2(-forward.x, -forward.z)): the dropped gun lies along it
	};

	enum class free_hands_event
	{
		none,
		dropped,
		picked_up,
	};

	struct free_hands_result
	{
		free_hands_event event{ free_hands_event::none };
		bool dropped{};
		v3 position;    // where the dropped gun lies (its Wep_Root), when dropped
		float yaw{};    // and which way its barrel points
		bool block_fire{};
		bool in_reach{}; // dropped and the hand is close enough to pick it up (a ring, a tick)
	};

	struct free_hands_config
	{
		float release_seconds{ 0.35f }; // grip open this long before the gun falls (a regrip is free)
		float pickup_reach{ 0.30f };    // the palm this close to the dropped gun picks it up
		float lie_height{ 0.04f };      // the gun's root above the floor when lying on its side
	};

	class free_hands
	{
	public:
		explicit free_hands(const free_hands_config& config = {}) : config_(config) {}
		free_hands_result update(const free_hands_frame& frame);
		void reset();
		const free_hands_config& config() const { return config_; }

	private:
		free_hands_config config_;
		bool dropped_{};
		v3 position_;
		float yaw_{};
		float open_for_{};
		bool last_grip_{ true };
	};
}
