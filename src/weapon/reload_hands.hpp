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
		magazine_draw draw{ magazine_draw::with_gun };
		v3 magazine_position; // for in_hand, falling and hidden
		q4 magazine_rotation;
	};

	struct hand_config
	{
		float reach_magazine{ 0.08f }; // off hand this close to the magwell grabs / catches / inserts
		float reach_slide{ 0.07f };
		float reach_chest{ 0.20f };
		float rearm_distance{ 0.12f }; // a magazine taken from the gun must leave this far before it can go in
		float rack_pull{ 0.05f };      // pulled back this far along the barrel before letting go = rack
		float fall_seconds{ 0.6f };    // a dropped magazine falls this long, then disappears
		// Where the magazine's top (the Wep_Mag origin) sits relative to the hand holding it, in the
		// controller's aim axes (x right, y up, -z forward): AR held from below, AK by the sides.
		v3 hold_ar{ 0.0f, 0.13f, -0.03f };
		v3 hold_ak{ 0.0f, 0.07f, -0.03f };
		// Charging handle in the gun frame (Wep_Root, +z along the barrel, metres): AR at the rear of the
		// receiver on top, AK on the receiver above the magazine (either side within the reach).
		v3 handle_ar{ 0.0f, 0.06f, 0.20f };
		v3 handle_ak{ 0.0f, 0.03f, 0.40f };
		float reach_handle{ 0.08f };
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
