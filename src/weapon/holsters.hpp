#pragma once

#include "weapon/reload_hands.hpp"

// Weapon swapping by body holsters (docs/tasks/weapon-swap.md): the gun hand reaches to a spot on the
// body and squeezes its grip to draw the weapon kept there. Zones live in a body frame that follows
// the head's yaw lazily (looking over the shoulder does not move the primary). Engine-free; OpenXR
// LOCAL metres. Tested by tests/reload_test.cpp.
namespace bfvr::weapon
{
	enum class holster
	{
		none,
		primary,
		secondary,
		knife,
		gadget1,
		gadget2,
	};

	struct holster_frame
	{
		float seconds{};
		bool head_valid{};
		v3 head;          // between the eyes
		float head_yaw{}; // radians, atan2(-forward.x, -forward.z): 0 = looking along -z, + = turned left
		bool hand_valid{};
		v3 gun_hand;
		bool gun_grip{};
		bool left_handed{}; // zones mirrored
	};

	struct holster_result
	{
		holster draw{ holster::none }; // grip pressed in this zone: switch to it
		holster zone{ holster::none }; // the zone the hand is in (claim the grip, haptics, hint)
		// Zone centres and radii in config order (grab_debug); valid when the head was.
		bool has_centres{};
		v3 centres[5];
		float radii[5]{};
	};

	struct holster_zone
	{
		holster which;
		v3 offset; // body frame: x toward the gun side, y up, z back; from the head
		float radius;
	};

	struct holster_config
	{
		holster_zone zones[5]{
			{ holster::primary, { 0.20f, 0.05f, 0.15f }, 0.155f },    // over the gun-side shoulder, behind the head
			{ holster::secondary, { 0.22f, -0.75f, 0.0f }, 0.14f },   // gun-side hip
			{ holster::knife, { -0.12f, -0.30f, -0.12f }, 0.14f },    // chest, off-hand side
			{ holster::gadget1, { -0.20f, -0.70f, -0.05f }, 0.11f },  // off-hand belt, front
			{ holster::gadget2, { -0.20f, -0.70f, 0.18f }, 0.11f },   // off-hand belt, back
		};
		float yaw_slack{ 0.6f }; // the body turns when the head is this far (radians) off it
	};

	class holsters
	{
	public:
		explicit holsters(const holster_config& config = {}) : config_(config) {}
		holster_result update(const holster_frame& frame);
		float body_yaw() const { return body_yaw_; }
		void reset();

	private:
		holster_config config_;
		bool last_grip_{};
		bool body_known_{};
		float body_yaw_{};
	};

	const char* holster_name(holster which);
}
