#pragma once

#include <string>

#include "body/arm_pose.hpp"

// Weapons from the ground with either hand (#54). BF4 takes a pickup with ConceptPickUp (keyboard R held)
// while its prompt shows ("HOLD TO PICKUP AK-12": within the pickup's InteractionRadius, 2.5 m in Baku).
// Held for 1.5 s through the game's Raw Input it took the AK-12 into slot 1, the game not even in front
// (live 2026-11-13). R is also reload, so it is held only while a hand that has just squeezed reaches a
// pickup. The reach is the BF3 single-player VR mod's magnetic take, which its user approved (ideas only,
// that code has no licence): a fresh squeeze within 35 cm of the gun, or a hand lowered beside the body
// over it (up to 1.2 m above it, 50 cm around). re/notes/weapon.md "Weapons from the ground".
namespace bfvr::weapon
{
	// Engine-free part, OpenXR LOCAL metres (tests/weapon_pickup_test.cpp).
	struct pickup_point
	{
		float position[3]{};
		float radius{}; // the game's InteractionRadius: the soldier must be this close
	};

	struct pickup_hand
	{
		bool valid{};
		float palm[3]{};
		bool grip{};
		bool busy{}; // holds the gun, a magazine, a grenade, works the panel: its squeeze is not a take
	};

	struct pickup_frame
	{
		float seconds{};
		bool head_valid{};
		float head[3]{};
		pickup_hand hands[2]; // 0 = off hand, 1 = gun hand
		const pickup_point* pickups{};
		int count{};
		bool weapon_changed{}; // the weapon in hand changed this frame (the game took it)
	};

	struct pickup_config
	{
		float sphere{ 0.35f };        // a palm this close to the gun takes it
		float lowered{ 0.50f };       // or a palm this far below the head ...
		float column_up{ 1.20f };     // ... up to this far above the gun ...
		float column_down{ 0.30f };   // ... or this far below it (prone, a gun on a crate) ...
		float column_around{ 0.50f }; // ... and this close to it horizontally
		float max_hold{ 2.0f };       // seconds R stays down at most (the game takes it in about one)
	};

	struct pickup_result
	{
		bool hold{};     // R down
		int hand{ -1 };  // the hand taking
		int target{ -1 };
		bool started{}; // a take began this frame
	};

	// The pickup a palm reaches (nearest), or -1. Only pickups within their radius of the head count.
	int pickup_in_reach(const pickup_config& config, const float (&palm)[3], bool head_valid, const float (&head)[3], const pickup_point* pickups,
		int count);

	class pickup_grab
	{
	public:
		explicit pickup_grab(const pickup_config& config = {}) : config_(config) {}
		pickup_result update(const pickup_frame& frame);

	private:
		pickup_config config_;
		bool was_grip_[2]{};
		int hand_{ -1 };
		int target_{ -1 };
		float held_{};
	};

	// Live setting weapon_pickup (default on).
	void set_weapon_pickup(bool on);
	// Present thread, from weapon_swap's update (its lock held), after the hands are known. Returns whether R is
	// held this frame: the off hand's use reach then stays off. busy: per hand (0 off, 1 gun).
	bool update_weapon_pickup(const body::weapon_points& points, bool points_fresh, bool head_valid, const float (&head)[3], bool off_grip,
		bool gun_grip, const bool (&busy)[2], bool weapon_changed, float seconds);
	// Pipe "pickup": the census (pickups found, nearest to each palm), the state and the takes.
	std::string weapon_pickup_report();
}
