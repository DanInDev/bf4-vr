#pragma once

#include "physics/loose_body.hpp"
#include "weapon/magazine_flight.hpp"
#include "weapon/reload.hpp"
#include "weapon/vec.hpp"

// Physical reload gestures (issue #14): turns the off hand, its grip, the reload button and the
// gun's magazine and slide positions into reload events, and says where to draw the magazine.
// Engine-free, all positions in one space (OpenXR LOCAL, metres). Tested by tests/reload_test.cpp.
namespace bfvr::weapon
{
	// The charging handle's two grab zones, per gun where they differ from hand_config's rack_* fields (#69;
	// weapon/profiles rack_zones_for starts from those, so the defaults live in one place).
	struct rack_zone_shape
	{
		v3 over_offset;
		float over_reach{};
		v3 under_offset;
		v3 under_radii;
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
		// #57: the magazine zone is a capsule from the pouch down to the off-side hip.
		bool has_chest_hip{};
		v3 chest_hip;
		float chest_reach{};  // 0: hand_config::reach_chest
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
		// Where this gun's magazine is grabbed, from the seated Wep_Mag in Wep_Root axes (weapon/profiles
		// magazine_grab_for); otherwise magazine_grab_drop straight below it.
		bool has_magazine_grab{};
		v3 magazine_grab;
		// #69: this gun's own handle zones (weapon/profiles rack_zones_for), else hand_config's.
		bool has_rack_shape{};
		rack_zone_shape rack_shape;
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
		// Grip lock (setting grip_lock): a magazine near the magwell rides a rail along the magwell's axis
		// (in and out), drawn upright, and the drawn hand is locked onto it with the game's grip until it
		// seats or leaves; after seating the hand stays on it while the grip is held. Needs has_root.
		bool grip_lock{};
		// #61: with the game's world and its ground, a discarded magazine is a rigid body on the level's real
		// ground (physics/loose_body), lying 30 s and going only unseen; floor_y is then its fallback under
		// ground not known yet. local = rotate(world_to_local, world - world_origin). Without them: the flight
		// above.
		bool has_world{};
		v3 world_origin;
		q4 world_to_local;
		physics::ground_source* ground{};
		bool has_view{};      // the head (LOCAL): a lying magazine never goes while looked at
		v3 view_position;
		v3 view_forward;
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
		bool has_chest_hip{}; // #57: the pouch zone is a capsule down to this point (its reach as zone_reach[2])
		v3 chest_hip;
		bool has_handle{};
		// The underhand handle zone (grab_debug: an ellipsoid in the gun frame), when the gun frame is known.
		bool has_under_zone{};
		v3 under_centre;
		v3 under_radii;
		// Racking: the off hand holds the charging handle (handle_in_gun, Wep_Root axes when has_root)
		// and has pulled it this far back along the barrel (0..slide_travel).
		bool racking{};
		bool rack_underhand{}; // grabbed in the underhand zone: the fist holds the knob palm up from below
		float rack_side{};     // the handle's side of the gun (rack_side): +1 left, -1 right, 0 on top
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
		bool palm_locked{};    // owner 2026-11-16: the palming lock holds the off hand on the gun
		float magazine_tilt{}; // radians the held magazine is turned aside (tilt_magazine)
		float palm_misalign_degrees{}; // caught: the new magazine's lean about the barrel from the catch pose
		float palm_off_degrees{};      // caught: its whole angle from the catch pose
		v3 palm_gap;                   // caught: its top less the magwell
		// Grip lock: the drawn off hand holds the magazine at lock_position / lock_rotation (its top and
		// rotation, as magazine_position) with the game's magazine grip, at this weight (0 or 1).
		float hand_lock{};
		v3 lock_position;
		q4 lock_rotation;
		bool guided{};      // the held magazine rides the magwell's rail
		float rail_depth{}; // guided: how far below the magwell its top is (metres along the rail)
		// The held magazine against the rail (for the report): its top's depth below the magwell, its distance
		// off the rail and its angle from how it sits.
		float rail_probe_depth{};
		float rail_probe_off{};
		float rail_probe_degrees{};
		int rail_probe_flags{}; // 1 grip, 2 armed, 4 well free, 8 aligned, 16 guided this update, 32 left this update
		bool stow_zone{};     // a held magazine can be put back: the hand is in the (larger) stow zone
		float stow_reach{};   // that zone's reach around the pouch-to-hip line
		bool loose{};         // a discarded magazine is in the world (flying or lying)
		bool loose_resting{}; // #61: lying on the ground
	};

	struct hand_config
	{
		float reach_magazine{ 0.10f }; // off hand this close to the magwell grabs / catches; a held magazine this close seats
		float reach_slide{ 0.085f };
		float reach_chest{ 0.13f }; // the magazine pouch, low on the chest (body_rig)
		// Putting a held magazine back: the pouch-to-hip zone this many times larger (headset 2026-11-14: hard to
		// find; a stow marker shows it while a magazine is in the hand).
		float stow_reach_scale{ 1.6f };
		float rearm_distance{ 0.18f }; // a magazine taken from the gun must leave this far before it can go in
		float rack_pull{ 0.04f };      // pulled back this far along the barrel before letting go = rack
		float fall_seconds{ 0.6f };    // a dropped magazine falls this long, then disappears (no floor known, or flight off)
		float flick_seat_delay{ 0.25f }; // AK: after the old magazine is knocked out, the new one seats this much later
		// AK quick reload, two motions (owner 2026-11-15: it flicked the old one away and the new one was in at once):
		// after the flick the new magazine seats only once it has left the magwell this far and is pushed in again.
		float flick_rearm{ 0.05f };
		float flick_seat_reach{ 0.03f }; // ... and is pushed back up to within this below the magwell (or past it)
		float flick_speed{ 1.2f };     // AK quick reload: the old magazine is pushed out of the magwell this fast (m/s)
		float eject_speed{ 1.0f };     // a released magazine not caught leaves the magwell this fast (m/s), plus the gun's motion (1.5 felt too fast)
		// Where the magazine's top (the Wep_Mag origin) sits relative to the hand holding it, in the
		// controller's aim axes (x right, y up, -z forward): AR held from below, AK by the sides.
		v3 hold_ar{ 0.0f, 0.13f, -0.03f };
		v3 hold_ak{ 0.0f, 0.07f, -0.03f };
		// Charging handle in the gun frame (Wep_Root, +z along the barrel, metres): AR at the rear of the
		// receiver on top, AK on the receiver above the magazine (either side within the reach).
		v3 handle_ar{ 0.0f, 0.06f, 0.20f };
		v3 handle_ak{ 0.0f, 0.03f, 0.40f };
		float reach_handle{ 0.085f };
		// The charging handle's two grab zones (owner, 2026-11-15), Wep_Root axes from the knob with x
		// towards the handle's side of the gun (rack_side): overhand at the handle's level, out on its side
		// and a little above (a sphere); underhand a larger region below it (an ellipsoid, radii out, down
		// and along the barrel; short along the barrel so the handguard ahead stays the two-handed grab's).
		v3 rack_over_offset{ 0.03f, 0.03f, 0.0f };
		float rack_over_reach{ 0.09f };
		v3 rack_under_offset{ 0.02f, -0.09f, 0.0f };
		v3 rack_under_radii{ 0.10f, 0.12f, 0.06f };
		// In both zones the hand counts as underhand this far below the knob's height.
		float rack_under_below{ 0.01f };
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
		// Owner 2026-11-16 (with grip_lock): once palming is ready, the off hand is held where it was on the gun; the
		// controller this far from that place lets go.
		float palm_lock_leave{ 0.12f };
		float palm_seat_reach{ 0.08f };
		float palm_align_degrees{ 15.0f };
		float palm_max_degrees{ 45.0f }; // and its whole angle from the catch pose stays within this
		// Grip lock: the rail runs lock_rail below the magwell along the gun's down axis. A held magazine
		// whose top comes within lock_catch of it, turned within lock_align_degrees of the seated
		// magazine, locks on; pushed up to within lock_seat_depth of the magwell it seats; more than
		// lock_leave off the rail, past its end or with the grip open it is free again.
		float lock_rail{ 0.10f };
		float lock_catch{ 0.10f };  // monitor 2026-11-14: the fist's top lands 2-11 cm off the axis for one hand pose
		float lock_align_degrees{ 50.0f }; // a forward-pointing fist holds it ~30-47 degrees off
		float lock_seat_depth{ 0.03f };
		float lock_leave{ 0.16f };
		// #61: the magazine as a box (Wep_Root axes, from its top down): half width, length and depth.
		v3 magazine_half{ 0.015f, 0.095f, 0.04f };
		float seen_degrees{ 55.0f }; // within this of the view's centre counts as seen
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
	// Which charging-handle zone a palm is in (hand_config's rack zones around the knob, in the gun frame
	// given by root_rotation; side +1: the handle is on the gun's left (+x), -1 its right, 0 on top).
	enum class rack_zone { none, over, under };
	struct rack_zone_test
	{
		rack_zone zone{ rack_zone::none };
		float over_distance{ 1e9f }; // palm to the overhand zone's centre, metres
		float under_scaled{ 1e9f };  // the palm in the underhand ellipsoid's radii (< 1 inside)
		v3 over_centre;
		v3 under_centre;
	};
	rack_zone_test rack_zone_of(v3 palm, v3 knob, q4 root_rotation, float side, const hand_config& config);
	// The gun wants charging: nothing chambered, or the bolt held open. Only then is the large underhand handle
	// zone there (the overhand one always is).
	bool needs_rack(const gun_state& state);
	// The handle's side from its knob in the gun frame (+x left); AK-pattern guns default to the right.
	float rack_side(v3 handle_in_gun, bool ak);
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
		bool rack_underhand_{};
		bool rack_full_{};
		v3 rack_start_;
		bool insert_armed_{};
		bool flick_rearm_{}; // after an AK flick: armed again at flick_rearm from the magwell (not rearm_distance)
		bool flick_second_{}; // re-armed after a flick: it seats only within flick_seat_reach (the second push)
		q4 held_relative_; // the held magazine's rotation relative to the gun hand
		magazine_flight flight_;
		physics::loose_body loose_; // #61: on the real ground (world)
		bool loose_on_{};
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
		bool guided_{};         // grip lock: the held magazine rides the rail
		bool pulling_out_{};    // grip lock: guided out of the gun after a grab (not armed to seat)
		bool lock_hold_{};      // grip lock: seated by the locked hand, the grip still held on it
		// Palming lock: the off hand where it was on the gun when palming got ready (gun frame).
		bool palm_lock_on_{};
		v3 palm_lock_hand_;
		v3 palm_lock_palm_;
		q4 palm_lock_rotation_;
		// Grip lock: where the magazine sat against the off hand when it locked (the hand's aim axes). Locked, the
		// fist no longer draws it, and the controller's fixed guess can be far off it (headset 2026-11-14: off the
		// rail, unlocked, locked again: a flicker that never seated).
		bool has_guide_hold_{};
		v3 guide_offset_;
		q4 guide_rotation_;
	};
}
