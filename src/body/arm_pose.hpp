#pragma once

#include <string>

#include "body/finger_pose.hpp"
#include "body/gun_recoil.hpp"
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
	// The knife's blade raised this many degrees towards the controller ray (its mesh points it down).
	void set_knife_pitch(float degrees);
	// Arm shape: forearm and hand offsets scaled by length (reach), clavicles moved outwards by width
	// metres per side (a wider torso). 1 and 0 = the rig as authored.
	void set_arm_shape(float length, float width);
	// The first-person legs collapsed onto the hips (they sit oddly under a tracked head).
	void set_legs_hidden(bool hidden);
	// The shoulders turn with the body the tracked head and hands imply (body/torso), not only with
	// the game body (which turns with the stick).
	void set_torso_follow(bool follow);
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
	};
	weapon_points latest_weapon_points();

	// Free hands (#35): the drawn gun lies at this OpenXR LOCAL point on its side, barrel along yaw
	// (atan2(-forward.x, -forward.z)), instead of in the gun hand; active = false puts it back.
	void set_dropped_gun(bool active, const float (&position)[3], float yaw);

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
	// Live setting extra_meshes: the magazine bone is tagged (scale stereo::extra_mesh::magazine_tag) so
	// its draw can be found.
	void set_extra_meshes(bool on);

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

	// Live setting finger_poses: the fingers follow the controller and what the hands hold
	// (body/finger_pose); off = as the game animated them.
	void set_finger_poses(bool enabled);
	// Live setting off_hand_pose: the off hand, when not on the gun, takes the gun hand's grip mirrored
	// (neutral: how a hand holds the controller) instead of the animated handguard hold turned by the
	// controller (animated). Needs a gun drawn once.
	void set_off_hand_neutral(bool neutral);
	// Live setting magazine_grip: how the off hand holds a magazine. "auto" = wrapped in the fist, cupped
	// on the palm (base plate in the palm, fingers closing on it by contact) once the palm faces up;
	// "wrap" / "cup" = always that grip.
	void set_magazine_grip(const std::string& grip);
	// Live setting rack_grip: "pinch" = the charging handle between the thumb and the side of the index
	// (the thumb closes on it by contact); "hook" = inside the curled index; "auto" = the pinch with the
	// palm down on the handle (overhand), the knob hooked in the index and middle with the palm up under
	// it (underhand), chosen as the hand takes the handle.
	void set_rack_grip(const std::string& grip);
	// What the off hand holds (magazine, charging handle, reaching for a grab); refresh every frame,
	// a request older than 250 ms counts as free. The two-handed hold and the gun hand are decided here.
	void set_off_hand_task(fingers::task task);
	// Racking: the drawn off hand on the charging handle (Wep_Root axes, metres), weight 0..1, and the
	// bolt / handle pulled this far back along the barrel. weight 0 = off.
	void set_handle_hold(float weight, const float (&handle_in_gun)[3], float pull);
	// Pipe "fingers": each hand's task, controller reading and shape.
	std::string hands_report();
	// Live setting recoil: how hard the drawn gun kicks per shot (body/gun_recoil; 0 = off, 1 = as tuned).
	void set_gun_recoil(float strength);
	// Shots the game fired this frame: the drawn gun kicks (the controller and the shot line do not).
	void kick_gun(int shots, const recoil_profile& gun = {});
	// Pipe "recoil": the current kick.
	std::string recoil_report();
}
