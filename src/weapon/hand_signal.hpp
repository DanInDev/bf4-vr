#pragma once

#include "weapon/reload_hands.hpp"

// Squad order by hand signal (campaign "engage", keyboard Q): the off hand raised above the eyes and
// stretched out ahead, then a grip squeeze. Held above eye level, so the support hand on the gun
// (below the eyes while aiming) never gives the order. Engine-free; OpenXR LOCAL metres. Tested by
// tests/reload_test.cpp.
namespace bfvr::weapon
{
	struct signal_frame
	{
		bool head_valid{};
		v3 head;          // between the eyes
		float head_yaw{}; // atan2(-forward.x, -forward.z)
		bool hand_valid{};
		v3 off_hand;
		bool off_grip{};
	};

	struct signal_result
	{
		bool order{}; // send the squad order once
		bool raised{}; // the hand is in the signal pose: keep its grip from the game
	};

	struct signal_config
	{
		float min_forward{ 0.35f }; // ahead of the head along its yaw
		float min_up{ 0.05f };      // above eye level
		float max_side{ 0.40f };
		float cooldown{ 1.0f };     // seconds between orders
	};

	class hand_signal
	{
	public:
		explicit hand_signal(const signal_config& config = {}) : config_(config) {}
		signal_result update(const signal_frame& frame, float seconds);

	private:
		signal_config config_;
		bool last_grip_{};
		float cooldown_{};
	};
}
