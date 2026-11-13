#pragma once

#include "weapon/magazine_flight.hpp"
#include "weapon/reload.hpp"
#include "weapon/vec.hpp"

// Physical reload gestures (issue #14): turns the off hand, its grip, the reload button and the
// gun's magazine and slide positions into reload events, and says where to draw the magazine.
// Engine-free, all positions in one space (OpenXR LOCAL, metres). Tested by tests/reload_test.cpp.
namespace bfvr::weapon
{
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
		// Where the drawn fist holds the magazine (top, rotation), when the hand drawing knows; otherwise
		// the hold offsets below place it from the off hand.
		bool has_held{};
		v3 held_top;
		q4 held_rotation;
		float held_tilt{};    // radians the drawn magazine was turned aside (hand_result::magazine_tilt applied)
		bool palming{};       // AR palming allowed (setting palm_reload)
		// The charging handle's knob in the gun frame (Wep_Root axes) when known for this gun
		// (weapon/profiles); otherwise hand_config's per-variant point.
		bool has_handle_point{};
		v3 handle_point;
		bool grip{};          // off-hand grip held
		bool button{};        // reload button held (AR: magazine release / bolt catch)
		// Empty hands (#38): the off hand holds the gun by the handguard and the free gun hand is passed as
		// off_hand: it may only rack (no magazine or pouch grabs; the magazine stays drawn with the gun).
		bool handle_only{};
		// #47: a discarded magazine flies on with the velocity it left with and comes to rest on the floor
		// (OpenXR STAGE floor in LOCAL); off, or no floor known: the old short straight drop.
		bool throw_flight{ true };
		bool floor_valid{};
		float floor_y{};
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
		// #40: how far the open off hand has snapped onto the seated magazine (0..1, by distance in the
		// magazine zone): the drawn hand takes the game's magazine grip on it, so the grab does not jump.
		float magazine_snap{};
		// Zone centres and reaches (grab_debug): magazine body, charging handle, chest pouch.
		v3 zone_centre[3];
		float zone_reach[3]{};
		bool has_handle{};
		// Racking: the off hand holds the charging handle (handle_in_gun, Wep_Root axes when has_root)
		// and has pulled it this far back along the barrel (0..slide_travel).
		bool racking{};
		v3 handle_in_gun;
		float rack_pull{};
		// This update the pull reached rack_pull (once per stroke): the rack will count on release.
		// The rear-stop cue (sound and tick); the release plays the bolt going home.
		bool rack_at_rear{};
		magazine_draw draw{ magazine_draw::with_gun };
		bool in_fist{};       // in_hand and held: the drawn fist holds it (else at magazine_position, e.g. sliding home)
		v3 magazine_position; // for in_hand, falling and hidden
		q4 magazine_rotation;
		// A second magazine to draw (extra meshes, #36; the rig has one): the one still seated while the hand
		// holds a new one, the caught one in the fist (palmed, drawn untilted), or one falling.
		enum class extra_magazine { none, seated, in_fist, falling } extra{ extra_magazine::none };
		v3 extra_position; // falling: its top
		q4 extra_rotation;
		// AR palming (frame.palming): a new magazine brought to the magwell of a loaded gun turns aside in
		// the hand (ready); the button drops the old one into the same hand (caught); turning the hand so
		// the new one lines up with the magwell seats it, and the old one stays in the hand. The game has one
		// magazine mesh: the new magazine is drawn, the caught one is not.
		enum class palm_stage { none, ready, caught } palm{ palm_stage::none };
		bool palm_ready_now{}; // this update the hand became ready (a tick)
		float magazine_tilt{}; // radians the held magazine is turned aside (tilt_magazine)
		float palm_misalign_degrees{}; // caught: the new magazine's lean about the barrel from the catch pose
		float palm_off_degrees{};      // caught: its whole angle from the catch pose
		v3 palm_gap;                   // caught: its top less the magwell
	};

	struct hand_config
	{
		float reach_magazine{ 0.10f }; // off hand this close to the magwell grabs / catches; a held magazine this close seats
		float reach_slide{ 0.085f };
		float reach_chest{ 0.13f }; // the magazine pouch, low on the chest (body_rig)
		float rearm_distance{ 0.18f }; // a magazine taken from the gun must leave this far before it can go in
		float rack_pull{ 0.04f };      // pulled back this far along the barrel before letting go = rack
		float fall_seconds{ 0.6f };    // a dropped magazine falls this long, then disappears (no floor known, or flight off)
		float flick_seat_delay{ 0.25f }; // AK: after the old magazine is knocked out, the new one seats this much later
		float flick_speed{ 1.2f };     // AK quick reload: the old magazine is pushed out of the magwell this fast (m/s)
		// Where the magazine's top (the Wep_Mag origin) sits relative to the hand holding it, in the
		// controller's aim axes (x right, y up, -z forward): AR held from below, AK by the sides.
		v3 hold_ar{ 0.0f, 0.13f, -0.03f };
		v3 hold_ak{ 0.0f, 0.07f, -0.03f };
		// Charging handle in the gun frame (Wep_Root, +z along the barrel, metres): AR at the rear of the
		// receiver on top, AK on the receiver above the magazine (either side within the reach).
		v3 handle_ar{ 0.0f, 0.06f, 0.20f };
		v3 handle_ak{ 0.0f, 0.03f, 0.40f };
		float reach_handle{ 0.085f };
		// AK: the handle's grab zone sits this far from the knob (Wep_Root axes: +x is the gun's left, so
		// out on its right, where the AK's handle is, and a little low) and is larger, so an underhand rack
		// finds it; the drawn hand still pinches the knob.
		v3 handle_grab_ak{ -0.04f, -0.02f, 0.0f };
		float reach_handle_ak{ 0.11f };
		// The magazine is grabbed by its body, this far below where it seats (gun frame -y): on an AK the
		// charging handle sits only ~3 cm from the magwell, the hand on the magazine well below it.
		float magazine_grab_drop{ 0.08f };
		// Ahead of the magwell along the barrel is the handguard (the two-handed grip), not the magazine.
		float magazine_ahead_limit{ 0.03f };
		// #40: the hand is fully snapped onto the seated magazine this close to its grab point.
		float magazine_snap_full{ 0.05f };
		// How far the charging handle (and the drawn bolt) can come back.
		float slide_travel{ 0.08f };
		// A magazine pushed in slides from the hand into the magwell over this long.
		float seat_seconds{ 0.1f };
		// Palming: the new magazine turns aside this far (top towards the gun's left, about the barrel axis
		// through its body palm_pivot_drop below the top), easing over palm_tilt_seconds, leaving the
		// magwell to the old one. Ready ends once the hand takes the catch point (the untilted magazine's
		// top) farther than palm_leave from the magwell. Caught, the new one seats once its top is within
		// palm_seat_reach and it lines up within palm_align_degrees.
		float palm_tilt_degrees{ 40.0f };
		float palm_pivot_drop{ 0.07f };
		float palm_tilt_seconds{ 0.12f };
		float palm_leave{ 0.16f };
		float palm_seat_reach{ 0.08f };
		float palm_align_degrees{ 15.0f };
		float palm_max_degrees{ 45.0f }; // and its whole angle from the catch pose stays within this
	};

	// A held magazine (its top, the Wep_Mag origin, and rotation) turned by angle radians about the barrel
	// axis of the gun frame it would sit in, through its body pivot_drop below the top. seated: the
	// magazine's rotation relative to that frame (Wep_Root). Positive turns the top towards the gun's
	// left (+x), the off hand's outside.
	void tilt_magazine(v3& top, q4& rotation, q4 seated, float angle, float pivot_drop);
	// #38, the gun in the off hand: the free gun hand's grip at palm takes the gun back by its pistol grip
	// (true) rather than starting a reload step, when the palm is within regrip_reach of the grip and nearer
	// it than every reload zone of last (the previous update's zones).
	bool grip_before_reload(v3 palm, v3 grip, float regrip_reach, const hand_result& last);
	// Angle between two rotations, degrees.
	float angle_between(q4 a, q4 b);
	// Palming: how far a magazine held at in_gun (rotation relative to Wep_Root) still leans about the
	// barrel axis (+z) from target_in_gun, degrees; turns about other axes do not count.
	float palm_lean_degrees(q4 in_gun, q4 target_in_gun);

	class reload_hands
	{
	public:
		explicit reload_hands(const hand_config& config = {}) : config_(config) {}
		hand_result update(reload& gun, const hand_frame& frame);
		void reset();

	private:
		void emit(hand_result& result, reload_event event);
		void start_fall(v3 from, q4 rotation, v3 velocity, const hand_frame& frame);

		hand_config config_;
		bool last_grip_{};
		bool last_button_{};
		bool racking_{};
		bool rack_full_{};
		v3 rack_start_;
		bool insert_armed_{};
		q4 held_relative_; // the held magazine's rotation relative to the gun hand
		magazine_flight flight_;
		float seat_pause_{}; // AK two-step reload: seconds until the new magazine may seat after the flick
		// Smoothed velocities of the off hand's palm and the magwell (#47: a released magazine keeps them).
		bool have_last_{};
		v3 last_palm_;
		v3 last_magwell_;
		v3 palm_velocity_;
		v3 magwell_velocity_;
		bool seating_{};
		float seat_time_{};
		v3 seat_from_;
		q4 seat_rotation_;
		hand_result::palm_stage palm_{ hand_result::palm_stage::none };
		float tilt_{};          // radians, eased
		q4 catch_in_gun_;       // the hand's magazine (untilted) relative to the gun at the catch
		bool kept_old_{};       // palmed in: the hand holds the old magazine (not drawn: the mesh is in the gun)
		bool keep_snap_{};      // #40: a magazine just pulled out, the hand not yet out of the magazine zone
	};
}
