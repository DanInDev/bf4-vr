#pragma once

#include "weapon/body_rig.hpp"
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
		// The off hand draws from the zones marked off_hand (the gadgets on its side) while it is free.
		bool off_hand_valid{};
		v3 off_hand;
		bool off_grip{};
		bool off_busy{};    // reload, grenade, two-handed hold, menu
		bool left_handed{}; // zones mirrored
		body::body_size size{}; // #46, #50: the player's body against the reference the zones are laid out for
	};

	struct holster_result
	{
		holster draw{ holster::none }; // grip pressed in this zone: switch to it
		holster zone{ holster::none }; // the zone the gun hand is in (claim the grip, haptics, hint)
		holster off_zone{ holster::none }; // the zone the off hand is in
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
		bool off_hand{}; // the off hand may draw here too
	};

	// The chest rig (grab zones v2, #57, owner 2026-11-13): primary at the sling where a let-go rifle
	// hangs (belly, gun side), secondary on the gun-side hip, knife high on the off-side chest (cross
	// draw), gadget 1 over the gun-side shoulder, gadget 2 over the off-side shoulder. Grenades (high
	// gun-side chest) and magazines (low chest down to the off-side hip) are the off hand's pouches
	// (body_rig.hpp). The sling and the hip are 30 cm apart; the nearest centre wins inside both radii.
	struct holster_config
	{
		holster_zone zones[5]{
			{ holster::primary, sling_point, 0.14f },                     // at the sling (the gun hand only)
			{ holster::secondary, { 0.22f, -0.75f, 0.0f }, 0.14f },       // gun-side hip
			{ holster::knife, { -0.10f, -0.27f, -0.10f }, 0.11f },        // high chest, off-hand side
			// #71 (owner 2026-11-16: barely worked): 31 cm, twice the old 15.5 / 14 cm. The edges just touch the knife's and
			// the grenade pouch's; the cheek (aiming) and the mouth (the pin with the teeth) stay outside.
			{ holster::gadget1, { 0.20f, 0.05f, 0.15f }, 0.31f, true },   // over the gun-side shoulder, behind the head
			{ holster::gadget2, { -0.20f, 0.05f, 0.15f }, 0.31f, true },  // over the off-side shoulder
		};
		float yaw_slack{ 0.6f }; // the body turns when the head is this far (radians) off it
	};

	class holsters
	{
	public:
		explicit holsters(const holster_config& config = {}) : config_(config) {}
		holster_result update(const holster_frame& frame);
		float body_yaw() const { return body_yaw_; }
		bool body_known() const { return body_known_; }
		void reset();

	private:
		holster_config config_;
		bool last_grip_{};
		bool last_off_grip_{};
		bool body_known_{};
		float body_yaw_{};
	};

	const char* holster_name(holster which);

	// #49: what a grip at a holster does. Another weapon: the game switches to it. The weapon already
	// selected but hanging on the sling (free hands): it comes to the hand at once (a gun on the floor is
	// picked up where it lies). Otherwise nothing.
	enum class draw_action
	{
		none,
		switch_weapon,
		take_into_hand,
	};
	draw_action draw_for(bool already_selected, bool on_sling);
}
