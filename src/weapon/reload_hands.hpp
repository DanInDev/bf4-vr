#pragma once

#include "weapon/reload.hpp"

// Physical reload gestures (issue #14): turns the off hand, its grip, the reload button and the
// gun's magazine and slide positions into reload events, and says where to draw the magazine.
// Engine-free, all positions in one space (OpenXR LOCAL, metres). Tested by tests/reload_test.cpp.
namespace bfvr::weapon
{
	struct v3
	{
		float x{}, y{}, z{};
	};

	struct q4
	{
		float x{}, y{}, z{}, w{ 1 };
	};

	struct hand_frame
	{
		float seconds{};      // since the last update
		bool hands_valid{};   // both drawn hands and the gun points are known
		v3 off_hand;          // the off hand (holds magazines, racks)
		q4 off_rotation;      // its aim rotation (a held magazine turns with it)
		bool has_palm{};
		v3 off_palm;          // the off hand's grip point: grabs are measured from here (else from off_hand)
		v3 gun_hand;
		q4 gun_rotation;      // aim rotation; -z points down the barrel
		v3 magwell;           // where the gun holds its magazine (Wep_Mag)
		q4 magazine_rotation; // the seated magazine's rotation
		bool has_slide{};
		v3 slide;             // bolt / charging handle (Wep_Slide), when the gun frame is unknown
		bool has_root{};
		v3 root;              // the gun's frame (Wep_Root: +z along the barrel, +y up)
		q4 root_rotation;
		v3 chest;             // magazine pouch: below the head
		bool grip{};          // off-hand grip held
		bool button{};        // reload button held (AR: magazine release / bolt catch)
	};

	enum class magazine_draw
	{
		with_gun,
		in_hand,
		falling,
		hidden,
	};

	struct hand_result
	{
		static constexpr int max_events = 4;
		reload_event events[max_events]{};
		int count{};
		bool off_hand_busy{}; // the off-hand grip belongs to the reload (hand in a reload zone): keep it from the game
		// The zone the open off hand is in (a haptic tick on entering it); none while holding or racking.
		enum class zone { none, magazine, handle, chest } in_zone{ zone::none };
		// Zone centres and reaches (grab_debug): magazine body, charging handle, chest pouch.
		v3 zone_centre[3];
		float zone_reach[3]{};
		bool has_handle{};
		magazine_draw draw{ magazine_draw::with_gun };
		v3 magazine_position; // for in_hand, falling and hidden
		q4 magazine_rotation;
	};

	struct hand_config
	{
		float reach_magazine{ 0.14f }; // off hand this close to the magwell grabs / catches; a held magazine this close seats
		float reach_slide{ 0.12f };
		float reach_chest{ 0.30f };
		float rearm_distance{ 0.18f }; // a magazine taken from the gun must leave this far before it can go in
		float rack_pull{ 0.04f };      // pulled back this far along the barrel before letting go = rack
		float fall_seconds{ 0.6f };    // a dropped magazine falls this long, then disappears
		// Where the magazine's top (the Wep_Mag origin) sits relative to the hand holding it, in the
		// controller's aim axes (x right, y up, -z forward): AR held from below, AK by the sides.
		v3 hold_ar{ 0.0f, 0.13f, -0.03f };
		v3 hold_ak{ 0.0f, 0.07f, -0.03f };
		// Charging handle in the gun frame (Wep_Root, +z along the barrel, metres): AR at the rear of the
		// receiver on top, AK on the receiver above the magazine (either side within the reach).
		v3 handle_ar{ 0.0f, 0.06f, 0.20f };
		v3 handle_ak{ 0.0f, 0.03f, 0.40f };
		float reach_handle{ 0.12f };
		// The magazine is grabbed by its body, this far below where it seats (gun frame -y): on an AK the
		// charging handle sits only ~3 cm from the magwell, the hand on the magazine well below it.
		float magazine_grab_drop{ 0.08f };
		// Ahead of the magwell along the barrel is the handguard (the two-handed grip), not the magazine.
		float magazine_ahead_limit{ 0.03f };
	};

	class reload_hands
	{
	public:
		explicit reload_hands(const hand_config& config = {}) : config_(config) {}
		hand_result update(reload& gun, const hand_frame& frame);
		void reset();

	private:
		void emit(hand_result& result, reload_event event);
		void start_fall(v3 from, q4 rotation);

		hand_config config_;
		bool last_grip_{};
		bool last_button_{};
		bool racking_{};
		v3 rack_start_;
		bool insert_armed_{};
		q4 held_relative_; // the held magazine's rotation relative to the gun hand
		bool falling_{};
		float fall_time_{};
		v3 fall_start_;
		q4 fall_rotation_;
	};
}
