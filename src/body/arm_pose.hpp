#pragma once

#include <string>

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
		bool has_optic{};
		float optic[3]{};      // Wep_Scope1 (sight, magnifier)
		bool has_rail{};
		float rail[3]{};       // Wep_Extra1 (forward rail: light, laser)
		bool hand_valid[2]{};  // arm solved this evaluation; 0 = left, 1 = right (the rig's arms)
		float hand[2][3]{};    // wrist goals (drawn hands)
		float hand_rotation[2][4]{};
	};
	weapon_points latest_weapon_points();

	// Where to draw the magazine: with the gun (detached = false) or at a pose in OpenXR LOCAL.
	struct magazine_pose
	{
		bool detached{};
		float position[3]{};
		float rotation[4]{ 0, 0, 0, 1 };
	};
	void set_magazine_pose(const magazine_pose& pose);

	struct arm_pose_stats
	{
		unsigned long long conversions{}; // pose conversions seen (all animated objects)
		unsigned long long matched{};     // of those, the local soldier's first-person rig
		unsigned long long solved{};      // of those, frames with at least one arm solved
	};
	arm_pose_stats stats();

	// Rig, target and forward-kinematics check, for the pipe's "arms" command.
	std::string report();
}
