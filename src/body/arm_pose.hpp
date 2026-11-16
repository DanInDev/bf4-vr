#pragma once

#include <string>

#include "body/body_size.hpp"
#include "body/finger_pose.hpp"
#include "body/gun_recoil.hpp"
#include "body/pose_tweaks.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::body
{
	// Hooks the per-object pose conversion (local pose -> model pose and skinning data, BF4 build
	// 179547, re/notes/skeleton.md) so the local soldier's first-person arm bones can be edited after
	// the animation and before skinning: arm IK to the VR controllers (config arm_ik, default on)
	// and research edits. Waits for the DRM to decrypt the code; x64 only.
	bool install_arm_pose_hook();

	// Arms follow the controllers (or synthetic targets) while enabled.
	void set_arm_ik(bool enabled);
	// Moves the first-person shoulders relative to the head (head axes: x right, y up, z back; metres).
	void set_shoulder_offset(float x, float y, float z);
	// Out of reach, an arm may lengthen by up to this fraction to keep the hand on the controller
	// (the game's arms can be shorter than the player's, #31). 0 = the rig's length.
	void set_arm_stretch(float fraction);
	// Out of reach, a shoulder first moves up to this far (metres) towards its hand; stretching
	// covers the rest.
	void set_shoulder_reach(float metres);
	// Two-handed weapon (#29): the gun hand takes this rotation (OpenXR LOCAL, x y z w) instead of
	// its controller's aim rotation while active.
	void set_gun_hand_rotation(bool active, const float (&rotation)[4]);
	// The knife is in the gun hand: its animated hand is not a gun grip, so the knife itself is turned
	// to the controller (blade along the aim, edge down) and the hand holds it as animated.
	void set_knife_in_hand(bool knife);
	// #68: the hand grenade or the C4 detonator is drawn in the gun hand; with gun_hand_pose neutral it is held by
	// the controller grip. Other gadgets (launchers) keep their own hold.
	void set_gun_hand_item(bool item);
	// The knife's blade raised this many degrees towards the controller ray (its mesh points it down).
	void set_knife_pitch(float degrees);
	// knife_yaw / knife_roll: further turns of the held knife about the controller's up and its ray, degrees.
	void set_knife_turn(float yaw_degrees, float roll_degrees);
	// Arm shape: forearm and hand offsets scaled by length (reach), clavicles moved outwards by width
	// metres per side (a wider torso). 1 and 0 = the rig as authored.
	void set_arm_shape(float length, float width);
	// #46, #50: the player's body: the shoulders' height below the eyes (shoulder_drop) and their depth (scale).
	void set_body_size(const body_size& size);
	// Live setting shoulder_back: the drawn shoulders moved back (+) or forward (-), metres.
	void set_shoulder_back(float metres);
	// The first-person legs collapsed onto the hips (they sit oddly under a tracked head).
	void set_legs_hidden(bool hidden);
	// The shoulders turn with the body the tracked head and hands imply (body/torso), not only with
	// the game body (which turns with the stick).
	void set_torso_follow(bool follow);
	// The drawn torso's yaw from the torso follow: radians, left positive, in tracking space (0 = the way the
	// snap-turned game body faces); false while torso_follow is off or it was not updated in the last 250 ms.
	bool drawn_torso_yaw(float& yaw);
	// Live setting torso_hands: the torso also leans towards two hands held together in front (off: head only).
	void set_torso_hands(bool lean);
	// The shoulder joints where a body has them (either side of the neck, below the eyes), not where
	// the game's rifle animation holds them (the left one on the chest's centre line).
	void set_body_shoulders(bool on);
	bool arm_ik_enabled();

	// Synthetic hand target overriding the controller, for tests on the monitor. Position in metres
	// and yaw/pitch in degrees relative to the view (the tracked head, else the unturned
	// first-person camera), OpenXR axes (x right, y up, -z forward). hand: 0 = left, 1 = right.
	void set_hand_target(int hand, bool active, const float position[3], float yaw_degrees, float pitch_degrees);

	// Research: rotation (x, y, z, w) from OpenXR camera axes to the CameraJoint bone's axes.
	void set_camera_axes(const float rotation[4]);

	// Research: every evaluation, add (dx, dy, dz) metres to the local translation of one bone of the
	// first-person skeleton (index < 0 = off).
	void set_bone_offset(int bone, float dx, float dy, float dz);

	// Physical reload (#14): the gun's magazine and slide and the drawn hands, OpenXR LOCAL, from the
	// last first-person pose evaluation (after the arm IK moved the gun).
	struct weapon_points
	{
		bool valid{};          // the rig has a Wep_Mag under Wep_Root
		unsigned long long at{}; // GetTickCount64 of the evaluation
		float magazine[3]{};   // Wep_Mag where the gun holds it
		float magazine_rotation[4]{ 0, 0, 0, 1 };
		bool has_slide{};
		float slide[3]{};      // Wep_Slide (bolt / charging handle)
		bool has_root{};
		float root[3]{};       // Wep_Root: the gun's frame (+z along the barrel, +y up)
		float root_rotation[4]{ 0, 0, 0, 1 };
		// #63: Wep_Root where the controller holds the gun (before the arm's reach limit and the wall push), the
		// real gun the wall ray follows.
		bool has_held_root{};
		float held_root[3]{};
		float held_root_rotation[4]{ 0, 0, 0, 1 };
		bool has_optic{};
		float optic[3]{};      // Wep_Scope1 (sight, magnifier)
		// The sight line: the game's animation keeps the camera joint on the gun's sight, so the camera
		// in Wep_Root axes is where the eye sits behind the sight and where the sight looks (smoothed
		// over the animation's sway). sight_eye in OpenXR LOCAL, sight_direction in Wep_Root axes.
		bool has_sight{};
		float sight_eye[3]{};
		float sight_direction[3]{ 0, 0, 1 };
		bool sight_from_ads{}; // measured in the game's aim down sights (else the hip pose's estimate)
		bool has_rail{};
		float rail[3]{};       // Wep_Extra1 (forward rail: light, laser)
		bool has_support{};
		float support[3]{};    // where the gun's animation holds the off hand (handguard)
		float grip[3]{};       // and the gun hand (pistol grip), both while has_support
		bool hand_valid[2]{};  // arm solved this evaluation; 0 = left, 1 = right (the rig's arms)
		float hand[2][3]{};    // wrist goals (drawn hands)
		float palm[2][3]{};    // the controllers' grip points (palms) in the same space: where grabs are measured
		float hand_rotation[2][4]{};
		// A magazine held in the off hand (magazine_pose in_hand): its top (Wep_Mag) and rotation where
		// the drawn fist holds it, OpenXR LOCAL.
		bool has_held{};
		float held_magazine[3]{};
		float held_rotation[4]{ 0, 0, 0, 1 };
		float held_tilt{}; // radians it is drawn turned aside (magazine_pose::tilt)
		// The soldier's ground (the rig's AITrajectory bone) as a height in OpenXR LOCAL.
		bool has_floor{};
		float floor_y{};
		// The game's world (the rig's model space) to OpenXR LOCAL, as every point above: local =
		// rotate(world_to_local, world - world_origin) (#54: pickups lie in the world).
		float world_origin[3]{};
		float world_to_local[4]{ 0, 0, 0, 1 };
		// #64 (launcher_pose active): the barrel's back end where a round goes in (the round's place in the barrel as
		// drawn) and a point on the barrel's left side, where a push shuts it; OpenXR LOCAL.
		bool has_launcher{};
		float barrel_mouth[3]{};
		float barrel_side[3]{};
	};
	weapon_points latest_weapon_points();

	// Free hands (#35): the drawn gun lies at this OpenXR LOCAL point on its side, barrel along yaw
	// (atan2(-forward.x, -forward.z)), instead of in the gun hand; active = false puts it back.
	// hanging (#38): on the sling at position (the body point), barrel down, facing yaw; else lying on the floor.
	void set_dropped_gun(bool active, const float (&position)[3], float yaw, bool hanging = false);

	// Where to draw the magazine: with the gun (detached = false), in the drawn off hand's fist
	// (in_hand), or at a pose in OpenXR LOCAL.
	struct magazine_pose
	{
		bool detached{};
		bool in_hand{};
		bool ak{}; // an AK-pattern magazine (held the AK way), else AR (weapon/profiles)
		float position[3]{};
		float rotation[4]{ 0, 0, 0, 1 };
		// in_hand, palming: the fist's magazine turned aside by tilt radians (contact::tilt_magazine,
		// about its body tilt_pivot metres below its top).
		float tilt{};
		float tilt_pivot{ 0.07f };
	};
	void set_magazine_pose(const magazine_pose& pose);

	// #64: the M320, a break-open launcher reloaded by hand (weapon/physical_reload). Its barrel (Wep_Mag, pivot at its
	// front) turned open by `open` (0 shut .. 1 the game's own 15 degrees, the back end to the gun's left) with the barrel
	// parts Wep_Belt1 and Wep_Extra4; the round (Wep_Mag_Ammo) and the spent case (Wep_Belt2) in the barrel as animated,
	// hidden, or at a pose in OpenXR LOCAL (in the hand, falling). re/notes/weapon.md "The M320's own reload".
	struct launcher_pose
	{
		bool active{};
		float open{};
		enum class place { in_barrel, hidden, at_pose };
		place round{ place::in_barrel };
		place spent_case{ place::hidden };
		float round_position[3]{};
		float round_rotation[4]{ 0, 0, 0, 1 };
		float case_position[3]{};
		float case_rotation[4]{ 0, 0, 0, 1 };
	};
	void set_launcher_pose(const launcher_pose& pose);

	// Extra meshes (#36): a second magazine, drawn by stereo::extra_mesh as a copy of the game's magazine
	// draw: where the gun holds it (seated), in the drawn off-hand fist untilted (the caught one while
	// palming), or at a pose in OpenXR LOCAL (falling).
	struct extra_magazine_pose
	{
		enum class kind { none, seated, in_fist, at_pose } where{ kind::none };
		float position[3]{};
		float rotation[4]{ 0, 0, 0, 1 };
	};
	void set_extra_magazine(const extra_magazine_pose& pose);
	// Second magazine (always on since #48, was extra_meshes): the magazine bone is tagged (scale stereo::extra_mesh::magazine_tag) so
	// its draw can be found.
	void set_extra_meshes(bool on);
	// Empty hands (#38, weapon/free_hands): the off hand holds the gun by the handguard; the gun hand is free.
	// by_grip: by the pistol grip (picked up with the off hand), the hold mirrored from the gun hand's.
	void set_gun_in_off_hand(bool held, bool by_grip = false);
	// #38: the free gun hand holds a grenade (its fingers make a fist).
	void set_gun_hand_fist(bool fist);
	// Held grenade drawn as the game's own (#36, stereo/extra_mesh). On with physical
	// grenades: while weapon/weapon_swap has the game's grenade selected for a capture (set_grenade_weapon) it
	// is hidden at a tagged pose and captured; while physical_grenade holds one (hand 0 = off, 1 = gun hand)
	// the capture is drawn in that fist, its pin left out once armed.
	void set_grenade_mesh(bool on);
	void set_grenade_weapon(bool current);
	void set_held_grenade(bool holding, int hand, bool armed);
	// #41: a grenade drawn at its pouch (OpenXR LOCAL, facing yaw = atan2(-forward.x, -forward.z)) while none
	// is held, in place of the grab ring.
	void set_pouch_grenade(bool shown, const float (&position)[3], float yaw);
	// #41: the game's knife is the weapon (captured once, hidden for that render), and the knife drawn at its
	// holster (OpenXR LOCAL, the body's yaw) while it is not.
	// capture_ready: out long enough (#41) that the gun is no longer drawn; only then is it recorded.
	void set_knife_weapon(bool out, bool capture_ready);
	void set_holster_knife(bool shown, const float (&position)[3], float yaw);
	// #58: the primary in hand is hidden and tagged for its capture this frame; and its copy hangs at the
	// sling (OpenXR LOCAL, the body's yaw) while another weapon is out.
	void set_primary_capture(bool wanted);
	void set_sling_primary(bool shown, const float (&position)[3], float yaw);
	// Pipe extramesh: the hold used, and the game's own hold as last seen (Wep_Root in the right fist's frame).
	std::string grenade_hold_report();

	struct arm_pose_stats
	{
		unsigned long long conversions{}; // pose conversions seen (all animated objects)
		unsigned long long matched{};     // of those, the local soldier's first-person rig
		unsigned long long solved{};      // of those, frames with at least one arm solved
	};
	arm_pose_stats stats();

	// Rig, target and forward-kinematics check, for the pipe's "arms" command.
	std::string report();
	// Pipe "gunlook": the drawn barrel (rendered and game frame), the controller's aim ray and the
	// override the two-handed grip sets, their angles, and the gun's hand joints vs the hands.
	std::string gun_report();
	// Pipe "armframe": the hands in the rendered first-person frame (true, default) or the game's.
	void set_rendered_frame(bool rendered);
	// Pipe "bodyturn": the body's branches turned back from the aimer (the gun) to the rendered frame
	// and the torso (true, default), or left as the game turned them (research A/B).
	void set_body_turn(bool on);
	bool body_turn();
	// gun_pose = aim (true): the gun's barrel lies exactly on the controller's aim ray; animated
	// (false): the game's hip pose turned by the controller (its idle angle and sway stay).
	void set_gun_on_ray(bool on_ray);
	// A primary or secondary gun is in hand (gun_pose applies to guns only, not gadgets or grenades).
	void set_gun_in_hand(bool gun);
	// Two-handed hold (0..1): the off hand is drawn on the handguard, blended by the weight.
	void set_support_hand(float weight);
	// Where the gun's animation holds the off hand relative to the gun hand, across the barrel, in
	// gun-hand aim axes (x right, y up), metres; false without a gun in the rig.
	bool support_offset(float (&offset)[2]);
	// The drawn wrist for a controller grip pose (0 = left rig arm, 1 = right), OpenXR LOCAL.
	void wrist_of(int hand, const stereo::pose& grip, float (&wrist)[3]);

	// Finger poses (always on since #48, was finger_poses): the fingers follow the controller and what the hands hold
	// (body/finger_pose); off = as the game animated them.
	void set_finger_poses(bool enabled);
	// Live setting cutscene_hands: in cutscenes (the game moving the camera itself) the arms, gun and fingers
	// stay as the game animates them (true) or follow the controllers as in play (false).
	void set_cutscene_hands(bool game);
	// Pipe "cutscene": the setting, whether the arms see a scene now and how much of the mod's pose is drawn.
	std::string cutscene_hands_report();
	// Live setting off_hand_pose: the off hand, when not on the gun, takes the gun hand's grip mirrored
	// (neutral: how a hand holds the controller) instead of the animated handguard hold turned by the
	// controller (animated). Needs a gun drawn once.
	void set_off_hand_neutral(bool neutral);
	// Live setting gun_hand_pose (#68): the gun hand while the off hand carries the gun, or holding a grenade or a
	// gadget, takes the gun grip as you hold the controller (neutral) or the game's animated hold (animated).
	void set_gun_hand_neutral(bool neutral);
	// Live setting magazine_grip: how the off hand holds a magazine. "auto" = wrapped in the fist, cupped
	// on the palm (base plate in the palm, fingers closing on it by contact) once the palm faces up;
	// "wrap" / "cup" = always that grip.
	void set_magazine_grip(const std::string& grip);
	// Live setting rack_grip: "fist" (default) = the knob in a closed fist, overhand or underhand by the zone
	// the stroke was grabbed in, the hand locked on it; "pinch" = between the thumb and the side of the index
	// (the thumb closes on it by contact); "hook" = inside the curled index; "auto" = the pinch with the
	// palm down on the handle (overhand), the knob hooked in the index and middle with the palm up under
	// it (underhand), chosen as the hand takes the handle.
	void set_rack_grip(const std::string& grip);
	// What the off hand holds (magazine, charging handle, reaching for a grab); refresh every frame,
	// a request older than 250 ms counts as free. The two-handed hold and the gun hand are decided here.
	void set_off_hand_task(fingers::task task);
	// Racking: the drawn off hand on the charging handle (Wep_Root axes, metres), weight 0..1, and the
	// bolt / handle pulled this far back along the barrel. weight 0 = off.
	// underhand: the stroke was grabbed in the handle's underhand zone (rack_grip fist: palm up from below).
	// side: the handle's side of the gun (+1 left, -1 right, 0 on top): the fist sits out on that side.
	void set_handle_hold(float weight, const float (&handle_in_gun)[3], float pull, bool underhand = false, float side = 0.0f);
	// Live settings rack_over_out / rack_over_up / rack_under_out / rack_under_down (metres): where the rack fist
	// sits against the knob (body/grasp_contact rack_fist_offset).
	void set_rack_fist_offsets(float over_out, float over_up, float under_out, float under_down);
	// Live settings support_lock / support_slide_back / support_slide_forward: the off hand on the handguard stays
	// where it grabbed (lock) and within back..forward metres of the gun's own support spot (body/grasp_contact).
	void set_support_slide(bool lock, float back, float forward);
	// #62, live setting hand_collision: the free hand is drawn on the gun's surface instead of inside its receiver and
	// handguard (body/hand_collision); pipe `fingers` shows the contact.
	void set_hand_collision(bool on);
	// #40: the open off hand snapping onto the seated magazine (weight 0..1, eased by the caller): the drawn
	// hand takes the game's magazine grip on it (AK or AR variant) with those fingers, and after the grab
	// eases back to the controller with the magazine in it. weight 0 = off.
	void set_magazine_snap(float weight, bool ak);
	// Grip lock (weapon/physical_reload): the drawn off hand holds the magazine at this pose (its top,
	// Wep_Mag, and rotation, OpenXR LOCAL) with the game's magazine grip, blended by weight.
	struct magazine_lock_pose
	{
		float weight{};
		bool ak{};
		float position[3]{};
		float rotation[4]{ 0, 0, 0, 1 };
	};
	void set_magazine_lock(const magazine_lock_pose& pose);
	// Setting grip_lock, racking: the hand's rotation on the charging handle held relative to the gun from
	// the stroke's start (no turning round the knob).
	void set_rack_lock(bool on);
	// #63: the drawn gun pulled back along its barrel by this much (metres; weapon/wall_stop), and whether a
	// gun is drawn in the gun hand on its ray now (else nothing to push).
	void set_wall_push(float metres);
	// Pose editor (the panel's Skeleton page): hand 0 left, 1 right, -1 off holds that grip whatever the hand
	// is doing, so its tweaks (body/pose_book) can be shaped with the other hand on the panel. Contact grips
	// (cup, palm) show their open shape there: nothing is in the hand to close on.
	void set_pose_preview(int hand, poses::grip which);
	// Pipe "poses": which grip each hand shows now (its tweak is applied) and the preview.
	std::string pose_grips_report();
	bool wall_push_applies();
	// Pipe "fingers": each hand's task, controller reading and shape.
	std::string hands_report();
	// Live setting recoil: how hard the drawn gun kicks per shot (body/gun_recoil; 0 = off, 1 = as tuned).
	void set_gun_recoil(float strength);
	// #60, setting one_hand_recoil: the one-hand penalty for long guns (0..1).
	void set_one_hand_recoil(float strength);
	// Shots the game fired this frame: the drawn gun kicks (the controller and the shot line do not).
	void kick_gun(int shots, const recoil_profile& gun = {});
	// Pipe "recoil": the current kick.
	std::string recoil_report();
}
