#include "body/arm_pose.hpp"

#include "common/controlled.hpp"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <MinHook.h>

#include "body/arm_ik.hpp"
#include "body/finger_pose.hpp"
#include "body/grasp_contact.hpp"
#include "body/hand_rules.hpp"
#include "body/hand_collision.hpp"
#include "body/gun_recoil.hpp"
#include "body/pose_book.hpp"
#include "body/scene_blend.hpp"
#include "body/torso.hpp"
#include "input/vr_controls.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "input/fingers.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/camera_hook.hpp"
#include "stereo/extra_mesh.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::body
{
	namespace
	{
		// BF4 179547: local ClientSoldierEntity*; soldier+0x1E8 first-person ClientAntAnimatableComponent,
		// component+0x128 Animatable; Animatable+0x48 local pose (QuatTransform[bones]: translation+scale,
		// rotation), +0x230 SkeletonAsset (+0x10 name, +0x18 BoneNames, +0x28 Hierarchy Array<Int32>,
		// count at data-4). The object handed to the conversion holds the local-pose pointer at +0x128
		// and the model pose at +0x130 (re/notes/skeleton.md).
		constexpr std::uint64_t pose_conversion = 0x140716050;
		// push rbx; sub rsp,20h; xor r9d,r9d; mov rbx,rcx; cmp [rcx+16Ah],r9b
		constexpr std::uint8_t expected_prologue[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x20, 0x45, 0x33, 0xc9, 0x48, 0x8b, 0xd9, 0x44, 0x38, 0x89, 0x6a, 0x01 };
		constexpr std::uint64_t pose_object_local_pose = 0x128;
		constexpr std::uint64_t pose_object_model_pose = 0x130;
		constexpr int max_bones = 256;
		constexpr ULONGLONG rig_refresh_ms = 250;
		constexpr ULONGLONG check_interval_ms = 200;

		using pose_conversion_t = void(__fastcall*)(void* pose);
		pose_conversion_t original_conversion{};

		std::atomic<int> research_bone{ -1 };
		std::atomic<float> research_offset[3]{};
		std::atomic<bool> ik_enabled{};

		template <typename T>
		bool read(std::uint64_t address, T& value)
		{
			SIZE_T done = 0;
			return address != 0 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(address)), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		bool read_string(std::uint64_t pointer_address, char (&text)[48])
		{
			std::uint64_t string = 0;
			text[0] = 0;
			if (!read(pointer_address, string) || !read(string, text))
			{
				return false;
			}
			text[sizeof(text) - 1] = 0;
			return true;
		}

		struct rig_info
		{
			std::uint64_t soldier{};
			std::uint64_t local_pose{};
			std::uint64_t skeleton{};
			int bones{};
			int parents[max_bones]{};
			int camera{ -1 };
			int weapon{ -1 };      // Wep_Root: the gun's bones hang from it, not from the hand
			int magazine{ -1 };    // Wep_Mag (child of Wep_Root): the magazine mesh, physical reload (#14)
			int slide{ -1 };       // Wep_Slide: bolt / charging handle
			int optic{ -1 };       // Wep_Scope1: sight / magnifier (accessory toggle)
			int rail{ -1 };        // Wep_Extra1: forward rail accessory (light / laser)
			// #64, the M320 (re/notes/weapon.md "The M320's own reload"): the round in its barrel, the spent case, and
			// the barrel parts that turn with Wep_Mag.
			int launcher_round{ -1 };    // Wep_Mag_Ammo
			int launcher_case{ -1 };     // Wep_Belt2
			int launcher_parts[2]{ -1, -1 }; // Wep_Belt1, Wep_Extra4
			int belt[10]{ -1, -1, -1, -1, -1, -1, -1, -1, -1, -1 }; // Wep_Belt1..10: a belt-fed gun's belt (Belt1 under Wep_Root)
			int physic1{ -1 }; // Wep_Physic1: the revolver's cylinder
			int aim{ -1 };               // Wep_Aim: the game places the muzzle flash from it, unedited (#65)
			int ik_hand[2]{ -1, -1 }; // IK_Joint_LeftHand / IK_Joint_RightHand (under Wep_Root): where the gun wants the hands
			int legs[2]{ -1, -1 }; // LeftUpLeg, RightUpLeg: hidden in VR (they sit oddly under a real head)
			int ground{ -1 };      // AITrajectory: the soldier's ground point (free hands drop the gun onto it)
			// The body's branches: the children of the bone where the arms' chain meets the camera's
			// (SpineX and its siblings, not CameraBase). The game turns them with the aimer, i.e. the gun.
			static constexpr int max_trunk = 8;
			int trunk[max_trunk]{};
			bool trunk_pitches[max_trunk]{}; // SpineX: also pitched with the aim, about the camera joint
			int trunk_count{};
			// The bent finger joints per hand (body/finger_pose): index..pinky 1-3, Thumb2-3; <= 0 = none (0 is the root).
			int finger_bones[2][fingers::count][fingers::joints]{};
			int finger_tips[2][fingers::count]{}; // <Hand><Finger>4: the end of each finger (contact grips)
			int thumb_base[2]{};                  // <Hand>Thumb1: the thumb's swing across the palm (only the pinch sets it)
			ik::arm_bones arms[2]; // 0 = left, 1 = right
			char name[48]{};
		};

		// What one IK'd bone looked like from the animation, and what the solver last wrote. When
		// the animation has not rewritten the bone since, the conversion would otherwise feed the
		// solver its own output.
		struct bone_memory
		{
			ik::bone_transform animated;
			ik::bone_transform written;
			bool has_written{};
		};

		struct hand_target
		{
			bool active{};
			ik::vec3 position;
			ik::quat rotation;
		};

		struct arm_report
		{
			bool solved{};
			ik::vec3 shoulder, elbow, wrist, goal; // model space
			ik::vec3 goal_xr;                      // the wrist goal in OpenXR LOCAL (drawn hand)
			ik::vec3 palm_xr;                      // the controller's grip point, same space
			float reach{};                         // goal distance over the animated arm's length (> 1: out of reach)
			ik::quat aim_xr;                       // the aim rotation used, OpenXR LOCAL
			ik::vec3 wrist_in_camera;              // OpenXR camera axes
			ik::quat hand_in_aim;                  // the hand goal's rotation in the controller's aim axes
		};

		struct check_report
		{
			ULONGLONG at{};
			float fk_error{ -1 };           // max position error of our FK against the engine's model pose
			float fk_error_conjugate{ -1 }; // the same with the quaternions read as conjugates
			ik::bone_transform camera;
			ik::quat weapon_in_camera; // Wep_Root, OpenXR camera axes ((0,1,0,0) = pointing ahead, upright)
			ik::quat weapon_in_game;   // the same in the game's own camera frame (aimer yaw and pitch)
			ik::vec3 weapon_position;  // Wep_Root, OpenXR camera axes, from the camera
			ik::vec3 palette_camera;   // #41: the camera joint and Wep_Root in palette space (the extra_mesh copies' frame)
			ik::vec3 palette_weapon;
			ik::vec3 reference;        // local[0], the root
			arm_report arms[2];
		};

		// Guards everything below: the matching worker thread, the rig refresh and the pipe.
		std::mutex rig_lock;
		rig_info rig;
		std::atomic<std::uint64_t> rig_pose{}; // rig.local_pose, for the lock-free match
		std::atomic<ULONGLONG> next_refresh{};
		std::atomic<ULONGLONG> next_check{};
		// Bones the IK rewrites: both arms (upper, fore, roll, hand), the weapon root, the two shoulder
		// (clavicle) bones and the body's branches (rig_info::trunk).
		constexpr int max_edited = 11 + rig_info::max_trunk;
		int edited[max_edited]{};
		int edited_count{};
		bone_memory memory[max_edited];
		bool weapon_follows{ true };
		hand_target targets[2];
		// OpenXR camera axes -> CameraJoint axes (the joint's x points left, z forward).
		ik::quat camera_axes{ 0, 1, 0, 0 };
		ik::vec3 wrist_offset[2]; // wrist relative to the grip pose, grip axes
		std::atomic<float> shoulder_offset[3]{}; // live setting, head axes (x right, y up, z back), metres
		std::atomic<float> arm_length{ 1.0f };   // live setting: forearm and hand offsets scaled (reach)
		std::atomic<float> shoulder_width{};     // live setting: clavicles moved outwards, metres per side
		std::atomic<float> body_scale{ 1.0f };   // #46: the player's height against the reference (shoulder depth)
		std::atomic<float> shoulder_drop{ body::reference_shoulder_drop }; // #50: shoulder joints below the eyes, metres
		std::atomic<bool> body_shoulders{ true }; // live setting: shoulder joints at shoulder_anchor from the head
		std::atomic<float> shoulder_back{};       // live setting shoulder_back: metres added to the anchor's depth
		// Right shoulder joint from the head (between the eyes), OpenXR axes; the left one mirrored.
		constexpr ik::vec3 shoulder_anchor{ 0.18f, -0.22f, 0.04f };
		std::atomic<bool> legs_hidden{ true };   // live setting hide_legs
		// Live setting cutscene_hands: in a scene the game animates (stereo::scripted_scene) the arms, gun and
		// fingers stay as animated; the mod's pose eases out and back in (body/scene_blend). Under rig_lock.
		std::atomic<bool> cutscene_game_hands{ true };
		scene_blend scene_mix;
		LARGE_INTEGER scene_last{};
		ik::bone_transform scene_animated[max_bones];
		std::atomic<bool> scene_now{};
		std::atomic<float> scene_weight{ 1.0f };
		std::atomic<float> arm_stretch{ 0.25f };   // live setting: how much longer an arm still out of reach may get
		std::atomic<float> shoulder_reach{ 0.12f }; // live setting: how far a shoulder may move towards an out-of-reach hand
		std::mutex gun_hand_lock;
		bool gun_hand_override{};
		ik::quat gun_hand_rotation;
		std::atomic<bool> rendered_frame{ true }; // research A/B (pipe armframe): false = the game's camera frame
		std::atomic<bool> body_turn_on{ true };   // research A/B (pipe bodyturn): the body's branches turned back from the gun
		std::mutex dropped_lock;                   // free hands: the drawn gun lies on the floor
		bool gun_on_floor{};
		ik::vec3 floor_gun_position;               // OpenXR LOCAL
		float floor_gun_yaw{};
		bool floor_gun_hanging{}; // #38: on the sling (barrel down at the body), not lying on the floor
		std::atomic<bool> gun_on_ray{ true };     // live setting gun_pose (aim / animated)
		std::atomic<bool> gun_in_hand{ true };    // a primary or secondary gun is drawn (weapon_swap)
		std::atomic<float> support_weight{};       // two-handed hold, 0..1: the off hand is drawn on the gun
		std::atomic<float> recoil_strength{ 1.0f }; // live setting recoil
		std::atomic<float> one_hand_strength{ 1.0f }; // live setting one_hand_recoil (#60)
		std::mutex recoil_lock;
		gun_recoil recoil;
		recoil_offset last_kick; // for the pipe report
		recoil_profile last_profile;

		double steady_seconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::atomic<bool> torso_follow{ true }; // live setting torso_follow
		std::atomic<bool> lean_to_hands{ false }; // live setting torso_hands: lean towards two hands held together
		// Like a native VR game (#37): the torso turns only when the head passes the twist limit (or with the
		// stick turn); no slow settle onto the head, and no hand lean unless torso_hands.
		torso_config head_only_torso()
		{
			torso_config config;
			config.settle_seconds = 0.0f;
			return config;
		}
		torso_yaw torso_estimate{ head_only_torso() }; // pose-hook thread only
		double torso_time{};
		std::atomic<float> torso_report{};       // radians, for the pipe
		std::atomic<ULONGLONG> torso_reported_at{}; // the drawn torso's yaw is current (drawn_torso_yaw)
		constexpr float torso_spine_back = 0.10f; // the turning axis, behind the eyes (tracking z is back)

		// Torso yaw in tracking space (radians, left positive) from this frame's head and hands; 0 when off.
		float update_torso(bool head_valid, ik::vec3 head_position, ik::quat head_rotation, const stereo::hand_sample& hands)
		{
			if (!torso_follow.load(std::memory_order_relaxed) || !head_valid)
			{
				torso_estimate.reset();
				torso_time = 0.0;
				torso_report = 0.0f;
				return 0.0f;
			}
			torso_hands input;
			for (int hand = 0; hand < 2 && lean_to_hands.load(std::memory_order_relaxed); ++hand)
			{
				ik::vec3 offset;
				if (targets[hand].active)
				{
					offset = ik::rotate(head_rotation, targets[hand].position);
				}
				else if (hands.valid[hand])
				{
					offset = ik::vec3{ hands.grip[hand].position[0], hands.grip[hand].position[1], hands.grip[hand].position[2] } - head_position;
				}
				else
				{
					continue;
				}
				input.valid[hand] = true;
				input.offset[hand][0] = offset.x;
				input.offset[hand][1] = offset.y;
				input.offset[hand][2] = offset.z;
			}
			const double now = steady_seconds();
			const float seconds = torso_time > 0.0 ? static_cast<float>(std::fmin(now - torso_time, 0.1)) : 0.0f;
			torso_time = now;
			const float yaw = torso_estimate.update(yaw_of(head_rotation.x, head_rotation.y, head_rotation.z, head_rotation.w), input, seconds);
			torso_report = yaw;
			torso_reported_at = GetTickCount64();
			return yaw;
		}
		// Normalised linear blend on the shorter arc.
		ik::quat nlerp(ik::quat a, ik::quat b, float t)
		{
			const float s = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0 ? -1.0f : 1.0f;
			return ik::normalize(ik::quat{ a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t });
		}
		std::atomic<bool> knife_in_hand{};
		std::atomic<float> knife_pitch{ 2.5307274f }; // radians (145 degrees, the owner's hold, headset 2026-11-12), live setting knife_pitch
		std::atomic<float> knife_yaw{ -0.2617994f };  // radians (-15 degrees), live setting knife_yaw (headset 2026-11-10: the knife sat turned)
		std::atomic<float> knife_roll{ 2.9670597f };  // radians (170 degrees), live setting knife_roll
		ik::vec3 pole_direction[2]; // elbow hint from the shoulder, OpenXR camera axes
		check_report last_check;
		arm_report last_arms[2];
		// The animated hold, in Wep_Root axes (+z along the barrel, +y up), before the IK moves anything.
		struct gun_geometry
		{
			bool valid{};
			ik::bone_transform hand[2];    // LeftHand / RightHand bones
			ik::bone_transform ik_hand[2]; // IK_Joint_LeftHand / IK_Joint_RightHand
			bool has_ik[2]{};
			// The camera joint in Wep_Root axes, smoothed: the sight line (weapon_points::sight_*). At the
			// hip the rifle pose cants the gun (AK-12: the camera 1.4 degrees off its barrel); in the game's
			// own aim down sights the camera looks exactly through the sight, so that sample, once seen
			// for this gun, wins.
			bool has_sight{};
			ik::vec3 sight_eye;
			ik::vec3 sight_direction{ 0, 0, 1 };
			bool has_ads_sight{};
			ik::vec3 ads_eye;
			ik::vec3 ads_direction{ 0, 0, 1 };
			ik::vec3 sampled_gun;    // IK_Joint_RightHand in Wep_Root axes: another gun resets the samples
			// #62: Wep_Mag (the magazine's top in the magwell) in Wep_Root axes, taken while the magazine is in the gun.
			ik::vec3 magazine_top;
			bool has_magazine_top{};
			ik::vec3 support_reach; // #62: LeftHand at its furthest forward, Wep_Root axes (the handguard's end)
			bool has_support_reach{};
			double aiming_since{ -1.0 };
			double fired_at{ -100.0 };
		};
		constexpr double ads_settle_seconds = 0.5; // the game's zoom-in animation has finished
		// Animated camera-in-gun samples off the sight by more than this are another hold (a grenade, the
		// knife, a reload): the sight line keeps its last value.
		constexpr float sight_min_cos = 0.9962f;     // 5 degrees
		constexpr double sight_quiet_seconds = 0.4; // after the last shot
		// The ADS eye point from Wep_Scope1, Wep_Root axes (monitor 2026-11-04: AK-12 red dot (0.001, 0.035,
		// -0.159), SCAR-H holographic (0.001, 0.036, -0.141)).
		constexpr ik::vec3 default_eye_from_optic{ 0.0f, 0.035f, -0.15f };
		gun_geometry geometry;
		// Physical reload: where the gun's magazine and slide are (published each evaluation) and
		// where the magazine should be drawn (set by weapon/physical_reload).
		weapon_points points;
		magazine_pose magazine_request;
		bone_memory magazine_memory;
		// #64: the M320 by hand; Wep_Mag (the barrel) keeps magazine_memory, the other four bones these.
		launcher_pose launcher_request;
		bone_memory launcher_memory[4];
		belt_feed_pose belt_feed_request;
		bone_memory belt_feed_memory[12]; // Wep_Extra1, Wep_Scope1, Wep_Belt1..10
		bool belt_feed_drawn{};          // #95, for the arms report: the belt-fed edit ran, and the links' drawn scale
		float belt_link_scale{ 1.0f };
		bone_memory revolver_memory[8];   // Wep_Slide, Wep_Physic1, Wep_Belt1..6
		constexpr float launcher_open_radians = 15.0f * 3.14159265f / 180.0f;
		// #65: Wep_Aim in the game's world as the animation has it (before the mod's edit: where the game places the
		// muzzle flash) and as drawn, from the same conversion.
		ik::bone_transform muzzle_game{};
		ik::bone_transform root_game{};
		ik::bone_transform root_drawn{};
		ULONGLONG muzzle_at{};
		// Extra meshes (#36): the second magazine (stereo/extra_mesh), and the held magazine's pose before
		// the palming tilt (where a caught one sits).
		extra_magazine_pose extra_request;
		// Empty hands (#38): the off hand holds the gun by the handguard (weapon/free_hands); the gun hangs
		// from the drawn off hand, turning with its controller as it did at the hand-over.
		std::atomic<bool> gun_in_off_hand{};
		std::atomic<bool> off_by_grip{}; // #38: the off hand holds the pistol grip (mirrored gun-hand hold)
		std::atomic<bool> gun_hand_fist{}; // #38: the free gun hand holds a grenade
		std::atomic<bool> gun_on_floor_now{}; // mirror of gun_on_floor for the finger task (no lock)
		bool carried_by_off{};                 // pose-hook thread: last evaluation's carrier was the off hand
		ik::quat off_carry_in_aim{ 0, 0, 0, 1 }; // the drawn off hand relative to frame * its aim, at the hand-over
		ik::quat last_drawn_hand[2];           // pose-hook thread: each hand's drawn rotation last evaluation
		std::atomic<bool> extra_meshes_on{ true };
		// Held grenade (#36): the game's grenade is captured while it is the weapon, and drawn in the fist.
		std::atomic<bool> grenade_mesh_on{};
		std::atomic<bool> grenade_weapon_now{};
		std::atomic<bool> held_grenade_now{};
		std::atomic<int> held_grenade_hand{};
		std::atomic<bool> held_grenade_armed{};
		// #41: the grenade at its pouch (OpenXR LOCAL, the body's yaw), drawn while none is in hand.
		std::atomic<bool> pouch_grenade_shown{};
		std::atomic<float> pouch_grenade_at[3]{};
		std::atomic<float> pouch_grenade_yaw{};
		// #41: the knife. Out as the game's weapon it is captured once (hidden and tagged for that render);
		// otherwise the copy stands in its holster (OpenXR LOCAL, the body's yaw), blade down.
		std::atomic<bool> knife_weapon_now{};
		std::atomic<bool> knife_capture_ready{}; // #41: the knife out long enough for its capture
		std::atomic<bool> holster_knife_shown{};
		std::atomic<float> holster_knife_at[3]{};
		std::atomic<float> holster_knife_yaw{};
		bool knife_capturing{}; // rig_lock: this frame tags the knife for its capture
		// #58: the primary, captured once in hand, hangs at the sling while another weapon is out.
		std::atomic<bool> primary_capture_wanted{};
		std::atomic<bool> sling_primary_shown{};
		std::atomic<float> sling_primary_at[3]{};
		std::atomic<float> sling_primary_yaw{};
		// Wep_Root of a hand grenade in the fist's grasp frame (fingers::grasp: +y out past the index, +z out
		// through the curled fingers), from the game's own hold of the M67 (pipe extramesh game_hold_in_fist,
		// 2026-11-09: the right hand drawn by its controller, the game's fingers round the grenade).
		ik::bone_transform grenade_in_fist{ { 0.0061f, -0.0131f, -0.0020f }, 1.0f, { 0.0007f, 0.3770f, 0.0049f, 0.9262f } };
		ik::bone_transform game_grenade_in_fist; // rig_lock: last seen while the game's grenade was out
		bool has_game_grenade_in_fist{};
		// #41: the knife's model axes in the holster frame (degrees about x, y, z; pipe holsterknife). The holster places
		// the knife's own captured bone frame (extra_mesh::item_anchor), not its Wep_Root: the mesh hangs 0.3-0.6 m from
		// Wep_Root and moves against it with the animation (monitor 2026-11-16, extramesh from_root).
		std::atomic<float> holster_knife_turn[3]{ 90.0f, 0.0f, 0.0f }; // [0]: about the vertical blade, degrees (setting knife_holster_turn; 0 = edge ahead, 90 = edge left)
		// The gun hand's fist in the knife's Wep_Root frame at its capture (rig_lock), and from it the grip in the knife's
		// own frame (the anchor): the point the holster keeps at the grab spot whichever way the knife is turned.
		ik::vec3 knife_fist_in_root;
		bool has_knife_fist{};
		ik::vec3 knife_grip_in_anchor;
		bool has_knife_grip{};
		// Until measured in this run (a capture with the hand on it): the U_Knife's grip in its own frame (monitor 2026-11-16).
		// Two captures gave (0.017 0.157 0.124) and (-0.016 0.006 0.124): the second, on the knife's axis, is used.
		const ik::vec3 knife_grip_default{ -0.0161f, 0.0063f, 0.1241f };
		std::atomic<float> holster_knife_shift[3]{}; // pipe only (extramesh knifeturn x y z dx dy dz): looking at it away from the body
		ik::bone_transform fist_untilted;
		bool has_fist_untilted{};
		// #40, palming: the caught magazine beside the leaning new one (contact::beside_magazine), and the
		// fingers closed by contact on both bodies instead of the game's one-magazine grip (palm_contact).
		ik::bone_transform fist_caught;
		bool has_fist_caught{};
		bool palm_contact{};
		constexpr float palm_contact_tilt = 0.05f; // radians of lean before the palming grip takes over
		constexpr float caught_gap = 0.006f;
		// Racking (weapon/physical_reload): the off hand on the charging handle, the slide pulled back.
		struct handle_hold
		{
			float weight{};       // 0..1: how far the drawn hand is on the handle
			ik::vec3 handle;      // Wep_Root axes
			float pull{};         // metres back along the barrel; the slide bone moves with it
			bool underhand{};     // grabbed in the underhand zone (rack_grip fist)
			float side{};         // the handle's side of the gun: +1 left, -1 right, 0 on top
		};
		contact::rack_fist_offsets rack_fist; // under rig_lock (set_rack_fist_offsets)
		// The off hand on the handguard: locked where it grabbed (live settings support_lock, support_slide_back
		// and support_slide_forward; headset 2026-11-15: it slid along the gun with every move).
		std::atomic<bool> support_lock{ true };
		std::atomic<float> support_back{ 0.06f };
		std::atomic<float> support_forward{ 0.08f };
		contact::support_slide support_place; // under rig_lock
		// #91: a mounted foregrip (weapon/foregrip kind) holds the off hand on the game's grip spot (setting foregrip_hold).
		std::atomic<int> foregrip_kind{ 0 };
		std::atomic<bool> foregrip_hold_on{ true };
		bool support_on_grip{}; // under rig_lock (report)
		// #62: the free hand kept out of the gun (live setting hand_collision); under rig_lock.
		std::atomic<bool> hand_collision_on{ true };
		contact::hand_collider hand_colliders[2];
		bool hand_collision_touch[2]{};
		float hand_collision_depth[2]{};
		int hand_collision_boxes{};
		unsigned hand_collision_pushes{};
		contact::box hand_collision_box[contact::hand_collider::max_boxes]; // the last boxes, posed in the model
		ik::vec3 hand_collision_knuckle[2];
		bool hand_collision_checked[2]{}; // this evaluation ran the push for the hand
		float hand_collision_drawn[2]{};  // the drawn hand's depth in the gun after the solve (metres)
		ik::vec3 hand_collision_palm_in_gun[2]; // the palm sphere after the push, Wep_Root axes (pipe fingers)
		contact::box hand_collision_receiver;   // Wep_Root axes
		handle_hold handle_request;
		// #40: the open off hand snapping onto the seated magazine (set_magazine_snap). delta: the snapped
		// hand pose less the controller's (model space), kept from the last seated frame so the hand eases
		// back to the controller after the grab with the magazine in it.
		struct magazine_snap_hold
		{
			float weight{};
			bool ak{};
			bool has_delta{};
			ik::vec3 delta_position;
			ik::quat delta_rotation;
		};
		magazine_snap_hold snap_request;
		// Grip lock (set_magazine_lock): the off hand's goal from a magazine pose in OpenXR LOCAL.
		magazine_lock_pose lock_request;
		// Racking locked (grip_lock, headset 2026-11-14: the hand could turn all the way round the knob and into
		// the gun): the hand's rotation relative to the gun is taken as the stroke begins (overhand or underhand,
		// as grabbed) and held until the handle is let go.
		std::atomic<bool> rack_lock_on{ true };
		std::atomic<float> wall_push{};          // #63: metres back along the barrel
		std::atomic<bool> wall_push_target{};    // a gun on the gun hand's ray this frame
		bool has_held_root{};                    // #63: the gun where the controller holds it (model space)
		ik::bone_transform held_root;
		bool rack_locked{};
		ik::quat rack_hand_in_gun;
		// Live setting off_hand_pose: the free off hand's wrist from the mirrored gun grip (true) or the
		// animated handguard hold turned by the controller (false).
		std::atomic<bool> off_hand_neutral{};
		// Live setting gun_hand_pose (#68): the gun hand without a gun (the off hand carries it) or with a gadget
		// takes the gun grip as you hold the controller (true) or the game's animated hold turned by it (false).
		std::atomic<bool> gun_hand_neutral{ true };
		std::atomic<bool> hand_item_in_gun_hand{}; // the hand grenade or the C4 detonator is drawn (set_gun_hand_item)
		// Contact grips (body/grasp_contact). All but the setting under rig_lock.
		std::atomic<int> magazine_grip{}; // live setting: 0 auto (palm up cups), 1 wrap, 2 cup
		bool cupped{};
		contact::cup_placement cup;        // this frame's cupped magazine, model space
		float cup_palm_up{};
		float cup_gap_mm[fingers::count]{}; // each finger's nearest point to the magazine after closing
		float cup_weight{};                 // the drawn magazine's blend from the fist (0) onto the palm (1)
		// Live setting: 0 pinch (thumb and index side), 1 hook (the index's curl), 2 auto (the pinch overhand,
		// the index and middle hooked underhand, by the palm as the hand takes the handle).
		// 3 fist (owner, 2026-11-15): the knob in a closed fist, overhand or underhand by the zone the stroke
		// was grabbed in (weapon/reload_hands), the hand locked on it.
		std::atomic<int> rack_grip{ 3 };
		bool fisting{};           // this frame: the racking hand holds the knob in its fist
		// #87: the drawn fist's grasp centre against the knob (Wep_Root axes, metres), after the arm's solve.
		ik::vec3 rack_grip_error;
		bool rack_grip_measured{};
		ik::vec3 fist_in_hand;    // the fist's grasp centre, hand-bone axes, from the last shaping
		bool has_fist{};
		bool pinching{};
		float pinch_gap_mm{};
		bool rack_stroke{};       // the handle hold of this stroke has been decided
		bool rack_underhand{};    // rack_grip auto: this (or the last) stroke is underhand
		float rack_palm_up{};     // palm normal . the gun's up as the stroke began
		bool hooking_under{};     // this frame: the underhand hook
		ik::vec3 under_hook_in_hand; // the inside of the curled index and middle, hand-bone axes, from the last shaping
		bool has_under_hook{};
		// The game's own off-hand grips, captured from its empty reloads with arm IK off (2026-11-03,
		// re/notes/skeleton.md "Authored grips"): the left hand's local rotations of Thumb1-3 and
		// <Finger>1-3, and for magazines the Wep_Mag bone relative to the LeftHand bone.
		struct authored_grip
		{
			ik::quat joints[fingers::count][3];
			ik::vec3 magazine;          // position in hand-bone axes
			ik::quat magazine_rotation; // rotation relative to the hand bone
		};
		// The AK-12's rack, as the slide moved (thumb and index steady within 2 degrees through the pull).
		constexpr authored_grip pinch_grip{ {
			{ { 0.5502f, 0.2659f, -0.0928f, 0.7861f }, { 0.2034f, 0.0140f, 0.4286f, 0.8802f }, { 0.0f, 0.0f, -0.2339f, 0.9723f } },
			{ { -0.1182f, -0.1661f, 0.0578f, 0.9773f }, { 0.0f, 0.0f, 0.1663f, 0.9861f }, { 0.0f, 0.0f, -0.0006f, 1.0f } },
			{ { -0.1585f, -0.1258f, 0.1576f, 0.9665f }, { 0.0f, 0.0f, 0.2175f, 0.9760f }, { 0.0f, 0.0f, 0.0385f, 0.9993f } },
			{ { -0.0905f, -0.0033f, 0.1269f, 0.9878f }, { 0.0f, 0.0f, 0.3840f, 0.9233f }, { 0.0f, 0.0f, 0.1027f, 0.9947f } },
			{ { -0.0673f, 0.1470f, 0.2627f, 0.9512f }, { 0.0f, 0.0f, 0.2281f, 0.9736f }, { 0.0f, 0.0f, 0.0389f, 0.9992f } },
		}, {}, {} };
		// The AK-12's new magazine on its way to the gun (the old one out held the same within 3 mm).
		constexpr authored_grip magazine_grip_ak{ {
			{ { 0.5565f, 0.4990f, -0.1566f, 0.6456f }, { 0.1068f, 0.2057f, 0.3889f, 0.8916f }, { 0.0f, 0.0f, 0.3482f, 0.9374f } },
			{ { -0.1851f, -0.2113f, 0.2261f, 0.9327f }, { 0.0f, 0.0f, 0.5463f, 0.8376f }, { 0.0f, 0.0f, 0.4007f, 0.9162f } },
			{ { -0.2661f, -0.1956f, 0.5247f, 0.7846f }, { 0.0f, 0.0f, 0.7072f, 0.7070f }, { 0.0f, 0.0f, 0.5949f, 0.8038f } },
			{ { -0.1903f, -0.0947f, 0.6089f, 0.7642f }, { 0.0f, 0.0f, 0.3603f, 0.9329f }, { 0.0f, 0.0f, 0.4423f, 0.8969f } },
			{ { -0.1934f, -0.0117f, 0.4644f, 0.8641f }, { 0.0f, 0.0f, 0.4077f, 0.9131f }, { 0.0f, 0.0f, 0.3861f, 0.9225f } },
		}, { 0.1146f, 0.0441f, -0.1175f }, { 0.2538f, -0.2256f, 0.6042f, -0.7208f } };
		// The SCAR-H's new magazine (AR pattern).
		constexpr authored_grip magazine_grip_ar{ {
			{ { 0.5623f, 0.3768f, 0.0936f, 0.7301f }, { 0.1298f, -0.1785f, -0.1483f, 0.9640f }, { 0.0f, 0.0f, -0.3126f, 0.9499f } },
			{ { 0.1277f, -0.0618f, -0.0526f, 0.9885f }, { 0.0f, 0.0f, 0.2630f, 0.9648f }, { 0.0f, 0.0f, 0.8033f, 0.5955f } },
			{ { 0.0729f, -0.0969f, 0.2824f, 0.9516f }, { 0.0f, 0.0f, 0.4992f, 0.8665f }, { 0.0f, 0.0f, 0.7576f, 0.6528f } },
			{ { 0.0985f, -0.0403f, 0.2998f, 0.9480f }, { 0.0f, 0.0f, 0.6421f, 0.7666f }, { 0.0f, 0.0f, 0.7272f, 0.6864f } },
			{ { 0.0088f, -0.1836f, 0.0891f, 0.9789f }, { 0.0f, 0.0f, 0.7955f, 0.6060f }, { 0.0f, 0.0f, 0.7965f, 0.6046f } },
		}, { 0.1558f, 0.0372f, -0.1136f }, { -0.0958f, 0.4847f, -0.7140f, 0.4961f } };
		const authored_grip* authored{};      // the grip the off hand takes this frame, if any
		int authored_hand{};                  // the hand it is applied to (1: the free gun hand racking, mirrored, #38)
		int authored_hand_shown{};
		const authored_grip* authored_shown{}; // the one being eased (kept while easing out)
		float authored_weight{};
		bone_memory thumb_base_memory[2];    // each hand's Thumb1 while an authored grip sets it
		ik::vec3 pinch_in_hand;             // where the knob belongs, hand-bone axes, from the last shaping
		bool has_pinch{};
		constexpr float knob_half = 0.008f; // a charging handle's knob, half its size
		float last_finger_seconds{};        // frame time of the last finger update
		constexpr float hand_half_thickness = 0.02f; // hand bone axis to the palm's skin
		constexpr float finger_radius = 0.009f;
		const contact::magazine_size magazine_body{}; // a rifle magazine (AK-12 / STANAG sized; not measured per gun yet)
		ik::quat gun_hand_in_aim;      // the gun hand's rotation in its aim axes, last gun drawn
		// The gun hand's wrist in the gun's frame (Wep_Root), last gun held: a slung copy hangs by its pistol
		// grip at the sling point, where the hand takes it (headset 2026-11-14: drawn 21 cm below its zone).
		ik::vec3 sling_grip_in_gun;
		bool has_sling_grip{};
		bool has_gun_hand_in_aim{};
		// Racking: the inside of the hooked index (centroid of Index1-3), hand-bone axes, from the last shaping.
		ik::vec3 hook_in_hand;
		bool has_hook{};
		int hook_hand{}; // the hand the hook points were measured on (the racking hand, #38)
		// A held magazine's top sits magazine_request.hold_drop (else held_magazine_drop) above the fist's grasp centre
		// (it is held by its body, #90).
		constexpr float held_magazine_drop = 0.07f;
		// #90: the drawn top against the fist's grasp centre (fist axes), for pipe fingers.
		ik::vec3 held_top_in_fist;
		bool held_top_measured{};
		constexpr float held_shell_drop = 0.01f; // a shell in the fingers
		// #94 (mark 10): a shell lies along the gun's barrel when seated, so drawn "as seated" in the fist it stuck out
		// through the curled fingers (the fist frame's +z). Turned -90 degrees about the fist's x it lies across the
		// knuckles (+y, little finger to index), as a fist holds a cylinder; its nose out at the index side.
		const ik::quat shell_across_fist{ -0.70710678f, 0.0f, 0.0f, 0.70710678f };
		std::atomic<bool> shell_across{ true }; // live setting shell_across; 0 = as seated, the old pose
		bone_memory slide_memory;
		// #94: a pump gun's fore-end at rest (set_pump_rest); the rest and the report under rig_lock.
		std::atomic<bool> pump_rest_on{ false };
		std::atomic<unsigned> pump_rest_key{ 0 };
		// #99: the game's two hands' separation along the barrel as this gun's hold: the value the game keeps steady (within
		// 1 cm) for half a second (draws, switches and reload poses move the hands; the hold is what stays). Under rig_lock.
		unsigned along_key{};
		float along_hold{ -1.0f };  // the gun's hold: the separation last held steady (-1: not settled yet)
		float along_now{};
		float along_candidate{ -1.0f };
		float along_still{};
		ULONGLONG along_last_ms{};
		ik::vec3 support_drawn_in_gun{}, support_game_in_gun{}; // the drawn off wrist and the game's own, gun axes (report)
		bool support_drawn{};
		contact::slide_rest pump_rest;
		bool pump_rest_held{};
		float pump_animated_z{}, pump_drawn_z{}, pump_hidden_mm{};
		float pump_animated_turn{}, pump_drawn_turn{}; // #96: degrees from the rest pose (the game's, as drawn)
		ik::quat pump_animated_rotation{};
		unsigned pump_hidden_frames{}, last_pump_key{};
		std::atomic<bool> bolt_pose_on{ false };
		std::atomic<float> bolt_pose_turn{ 0.0f };
		std::atomic<float> bolt_pose_pull{ 0.0f };
		ULONGLONG pump_last_ms{};
		// Fingers: what each hand is doing and its eased shape (body/finger_pose).
		std::atomic<bool> finger_poses{ true };     // live setting finger_poses
		// Pose editor: a hand held in one grip (-1 off), and each hand's shown grip and eased tweak.
		std::atomic<int> preview_hand{ -1 };
		std::atomic<int> preview_grip{ static_cast<int>(poses::grip::none) };
		poses::grip shown_grip[2]{ poses::grip::none, poses::grip::none };
		poses::tweak shown_tweak[2];
		std::atomic<int> off_task{ static_cast<int>(fingers::task::free) };
		std::atomic<ULONGLONG> off_task_at{};       // a request older than task_max_age_ms counts as free
		constexpr ULONGLONG task_max_age_ms = 250;
		bone_memory finger_memory[2][fingers::count][fingers::joints];
		fingers::shape_smoother finger_shapes[2];
		fingers::task last_task[2]{ fingers::task::free, fingers::task::item };
		fingers::finger_input last_input[2];
		float last_index_bend[2][fingers::joints]{}; // radians, for the report
		LARGE_INTEGER last_finger_update{};

		std::atomic<std::uint64_t> conversions{};
		std::atomic<std::uint64_t> matched{};
		std::atomic<std::uint64_t> solved{};
		// Translation last written by the research edit; if the animation has not rewritten the bone
		// since (evaluation skipped while conversion runs), the offset is not added again.
		float last_written[3]{};

		int find_bone(const char (*names)[48], int count, const char* wanted)
		{
			for (int bone = 0; bone < count; ++bone)
			{
				if (std::strcmp(names[bone], wanted) == 0)
				{
					return bone;
				}
			}
			return -1;
		}

		void forget_written()
		{
			for (bone_memory& bone : memory)
			{
				bone.has_written = false;
			}
			for (auto& hand : finger_memory)
			{
				for (auto& finger : hand)
				{
					for (bone_memory& joint : finger)
					{
						joint.has_written = false;
					}
				}
			}
			slide_memory.has_written = false;
		}

		void forget_rig()
		{
			rig = {};
			rig_pose = 0;
			forget_written();
		}

		// Called with rig_lock held. Names and hierarchy are read once per skeleton.
		void refresh_rig()
		{
			std::uint64_t soldier = 0, component = 0, animatable = 0, local_pose = 0, skeleton = 0;
			if (!(soldier = game::local_soldier()) != 0 || !read(soldier + 0x1e8, component) || !read(component + 0x128, animatable)
				|| !read(animatable + 0x48, local_pose) || local_pose == 0 || !read(animatable + 0x230, skeleton) || skeleton == 0)
			{
				forget_rig();
				return;
			}
			if (skeleton == rig.skeleton && rig.camera >= 0)
			{
				if (local_pose != rig.local_pose)
				{
					forget_written();
					rig.local_pose = local_pose;
				}
				rig.soldier = soldier;
				rig_pose = local_pose;
				return;
			}

			forget_rig();
			std::uint64_t names = 0, hierarchy = 0;
			std::uint32_t bones = 0, hierarchy_count = 0;
			if (!read(skeleton + 0x18, names) || names < 4 || !read(names - 4, bones) || bones == 0 || bones > max_bones
				|| !read(skeleton + 0x28, hierarchy) || hierarchy < 4 || !read(hierarchy - 4, hierarchy_count) || hierarchy_count != bones)
			{
				// #86: some level loads leave no skeleton here (seen right after a fresh build). Once per object: what the
				// chain holds instead, to compare with a good load (Animatable vtable 141CD6F40, skeleton vtable 141D026A0).
				static std::uint64_t logged = 0;
				if (skeleton != logged)
				{
					logged = skeleton;
					std::uint64_t animatable_vtable = 0, skeleton_first = 0, component_vtable = 0;
					read(animatable, animatable_vtable);
					read(skeleton, skeleton_first);
					read(component, component_vtable);
					log::write("arms: no first-person skeleton (#86): component vtable %llx, animatable %llx vtable %llx, +0x230 %llx begins %llx",
						static_cast<unsigned long long>(component_vtable), static_cast<unsigned long long>(animatable),
						static_cast<unsigned long long>(animatable_vtable), static_cast<unsigned long long>(skeleton),
						static_cast<unsigned long long>(skeleton_first));
				}
				return;
			}
			rig_info fresh;
			fresh.soldier = soldier;
			fresh.skeleton = skeleton;
			fresh.local_pose = local_pose;
			fresh.bones = static_cast<int>(bones);
			SIZE_T done = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(hierarchy)), fresh.parents, bones * sizeof(int), &done)
				|| done != bones * sizeof(int))
			{
				return;
			}
			static char bone_names[max_bones][48];
			for (int bone = 0; bone < fresh.bones; ++bone)
			{
				if (fresh.parents[bone] < -1 || fresh.parents[bone] >= fresh.bones || fresh.parents[bone] == bone)
				{
					return;
				}
				read_string(names + bone * sizeof(std::uint64_t), bone_names[bone]);
			}
			read_string(skeleton + 0x10, fresh.name);
			fresh.camera = find_bone(bone_names, fresh.bones, "CameraJoint");
			fresh.weapon = find_bone(bone_names, fresh.bones, "Wep_Root");
			fresh.magazine = find_bone(bone_names, fresh.bones, "Wep_Mag");
			fresh.slide = find_bone(bone_names, fresh.bones, "Wep_Slide");
			fresh.optic = find_bone(bone_names, fresh.bones, "Wep_Scope1");
			fresh.rail = find_bone(bone_names, fresh.bones, "Wep_Extra1");
			fresh.launcher_round = find_bone(bone_names, fresh.bones, "Wep_Mag_Ammo");
			fresh.launcher_case = find_bone(bone_names, fresh.bones, "Wep_Belt2");
			fresh.launcher_parts[0] = find_bone(bone_names, fresh.bones, "Wep_Belt1");
			fresh.physic1 = find_bone(bone_names, fresh.bones, "Wep_Physic1");
			for (int link = 0; link < 10; ++link)
			{
				char name[16];
				std::snprintf(name, sizeof(name), "Wep_Belt%d", link + 1);
				fresh.belt[link] = find_bone(bone_names, fresh.bones, name);
			}
			fresh.launcher_parts[1] = find_bone(bone_names, fresh.bones, "Wep_Extra4");
			fresh.aim = find_bone(bone_names, fresh.bones, "Wep_Aim");
			fresh.ik_hand[0] = find_bone(bone_names, fresh.bones, "IK_Joint_LeftHand");
			fresh.ik_hand[1] = find_bone(bone_names, fresh.bones, "IK_Joint_RightHand");
			fresh.legs[0] = find_bone(bone_names, fresh.bones, "LeftUpLeg");
			fresh.legs[1] = find_bone(bone_names, fresh.bones, "RightUpLeg");
			fresh.ground = find_bone(bone_names, fresh.bones, "AITrajectory");
			if (fresh.magazine >= 0 && fresh.parents[fresh.magazine] != fresh.weapon)
			{
				fresh.magazine = -1;
			}
			if (fresh.slide >= 0 && fresh.parents[fresh.slide] != fresh.weapon)
			{
				fresh.slide = -1;
			}
			{
				const char* const sides[2]{ "LeftHand", "RightHand" };
				const char* const finger_names[fingers::count]{ "Thumb", "Index", "Middle", "Ring", "Pinky" };
				for (int hand = 0; hand < 2; ++hand)
				{
					for (int finger = 0; finger < fingers::count; ++finger)
					{
						for (int joint = 0; joint < fingers::joints; ++joint)
						{
							// The thumb bends at Thumb2 and Thumb3; the other fingers at 1-3.
							const int number = finger == fingers::thumb ? joint + 2 : joint + 1;
							char wanted[48];
							std::snprintf(wanted, sizeof(wanted), "%s%s%d", sides[hand], finger_names[finger], number);
							const bool used = finger != fingers::thumb || joint < 2;
							fresh.finger_bones[hand][finger][joint] = used ? find_bone(bone_names, fresh.bones, wanted) : -1;
						}
						char tip[48];
						std::snprintf(tip, sizeof(tip), "%s%s4", sides[hand], finger_names[finger]);
						fresh.finger_tips[hand][finger] = find_bone(bone_names, fresh.bones, tip);
						if (finger == fingers::thumb)
						{
							char base[48];
							std::snprintf(base, sizeof(base), "%sThumb1", sides[hand]);
							fresh.thumb_base[hand] = find_bone(bone_names, fresh.bones, base);
						}
					}
				}
			}
			fresh.arms[0] = { find_bone(bone_names, fresh.bones, "LeftArm"), find_bone(bone_names, fresh.bones, "LeftForeArm"),
				find_bone(bone_names, fresh.bones, "LeftHand"), find_bone(bone_names, fresh.bones, "LeftForeArmRoll") };
			fresh.arms[1] = { find_bone(bone_names, fresh.bones, "RightArm"), find_bone(bone_names, fresh.bones, "RightForeArm"),
				find_bone(bone_names, fresh.bones, "RightHand"), find_bone(bone_names, fresh.bones, "RightForeArmRoll") };
			// The roll bone must sit between the forearm and the hand, the weapon root off the arm.
			for (ik::arm_bones& arm : fresh.arms)
			{
				if (arm.roll >= 0 && (fresh.parents[arm.hand] != arm.roll || fresh.parents[arm.roll] != arm.fore))
				{
					arm.roll = -1;
				}
			}
			const bool complete = fresh.camera >= 0 && fresh.arms[0].upper >= 0 && fresh.arms[0].fore >= 0 && fresh.arms[0].hand >= 0
				&& fresh.arms[1].upper >= 0 && fresh.arms[1].fore >= 0 && fresh.arms[1].hand >= 0;
			if (complete)
			{
				// Where the left arm's chain meets the camera's (AITrajectory in the BF4 rig): its children
				// other than the camera's own branch carry the body (re/notes/skeleton.md).
				bool camera_line[max_bones]{};
				for (int bone = fresh.camera; bone >= 0; bone = fresh.parents[bone])
				{
					camera_line[bone] = true;
				}
				int meet = fresh.arms[0].upper;
				while (meet >= 0 && !camera_line[meet])
				{
					meet = fresh.parents[meet];
				}
				// Only the body: the arms' branch (SpineX: spine, shoulders, gun root) and Hips (chest, legs).
				// Trajectory, Connect and TempFillJointRoot are the game's own bookkeeping: turning them
				// moved the drawn gun out of view (monitor, 2026-11-05).
				int arms_branch = fresh.arms[0].upper;
				while (arms_branch >= 0 && fresh.parents[arms_branch] != meet)
				{
					arms_branch = fresh.parents[arms_branch];
				}
				const int hips = find_bone(bone_names, fresh.bones, "Hips");
				for (const int bone : { arms_branch, hips })
				{
					if (meet >= 0 && bone >= 0 && fresh.parents[bone] == meet && !camera_line[bone] && fresh.trunk_count < rig_info::max_trunk)
					{
						fresh.trunk_pitches[fresh.trunk_count] = bone == arms_branch;
						fresh.trunk[fresh.trunk_count++] = bone;
					}
				}
				log::write("arms: body branches from bone %d: %d (first %d)", meet, fresh.trunk_count, fresh.trunk_count ? fresh.trunk[0] : -1);
			}
			log::write("arms: skeleton %s, %d bones, camera %d, weapon %d, left %d/%d/%d/%d, right %d/%d/%d/%d%s", fresh.name, fresh.bones,
				fresh.camera, fresh.weapon, fresh.arms[0].upper, fresh.arms[0].fore, fresh.arms[0].roll, fresh.arms[0].hand, fresh.arms[1].upper,
				fresh.arms[1].fore, fresh.arms[1].roll, fresh.arms[1].hand, complete ? "" : " (incomplete: arms left alone)");
			if (complete)
			{
				rig = fresh;
				rig_pose = local_pose;
				magazine_memory = {};
				slide_memory = {};
				points = {};
				edited_count = 0;
				for (const ik::arm_bones& arm : rig.arms)
				{
					for (const int bone : { arm.upper, arm.fore, arm.roll, arm.hand })
					{
						if (bone >= 0)
						{
							edited[edited_count++] = bone;
						}
					}
				}
				if (rig.weapon >= 0)
				{
					edited[edited_count++] = rig.weapon;
				}
				for (const ik::arm_bones& arm : rig.arms)
				{
					if (arm.upper >= 0 && rig.parents[arm.upper] >= 0 && rig.parents[arm.upper] != rig.camera)
					{
						edited[edited_count++] = rig.parents[arm.upper];
					}
				}
				for (int i = 0; i < rig.trunk_count; ++i)
				{
					edited[edited_count++] = rig.trunk[i];
				}
			}
		}

		void apply_research_offset(ik::bone_transform* local)
		{
			const int bone = research_bone.load(std::memory_order_relaxed);
			if (bone < 0 || bone >= rig.bones)
			{
				return;
			}
			float* const translation = &local[bone].position.x;
			if (std::memcmp(translation, last_written, sizeof(last_written)) != 0)
			{
				for (int axis = 0; axis < 3; ++axis)
				{
					translation[axis] += research_offset[axis].load(std::memory_order_relaxed);
					last_written[axis] = translation[axis];
				}
			}
		}

		// The first-person camera in rig space, OpenXR axes: at the CameraJoint, turned by the aim
		// pitch the joint does not carry (soldier +0x4DC, radians, up positive; aim.md). Under
		// controller aim the camera hook renders the first-person view level at the body's yaw
		// instead (the joint follows the aimer, i.e. the gun): the hands belong in that frame.
		ik::quat camera_frame(const ik::bone_transform& camera_joint, bool rendered = true)
		{
			if (const stereo::first_person_turn turn = stereo::first_person_view_turn(); rendered && turn.active && rendered_frame.load(std::memory_order_relaxed))
			{
				return camera_joint.rotation * ik::quat{ 0, std::sin(0.5f * turn.yaw), 0, std::cos(0.5f * turn.yaw) } * camera_axes;
			}
			float pitch = 0.0f;
			if (!read(rig.soldier + 0x4dc, pitch) || !std::isfinite(pitch))
			{
				pitch = 0.0f;
			}
			return camera_joint.rotation * camera_axes * ik::quat{ std::sin(0.5f * pitch), 0, 0, std::cos(0.5f * pitch) };
		}

		// The tracking origin (OpenXR LOCAL) in rig space: the camera joint, raised by what physical
		// stance adds back to the eye (the game's own crouch drop). Without it a real crouch drew the
		// hands, gun and body that drop too low again (monitor 2026-11-05: the gun left the view).
		ik::vec3 tracking_origin(const ik::bone_transform& camera, const ik::quat& frame)
		{
			return camera.position + ik::rotate(frame, ik::vec3{ 0.0f, stereo::physical_stance_lift(), 0.0f });
		}

		// #40: the off hand's goal on the seated magazine: the game's magazine grip inverted (the hand pose that
		// holds the magazine where it sits), blended in by the snap weight. After the grab (the magazine
		// detached) the offset from the last seated frame fades with the weight, the magazine in the hand.
		void apply_magazine_snap(ik::bone_transform* local, ik::arm_goal& goal)
		{
			const float weight = snap_request.weight;
			if (weight <= 0.0f)
			{
				snap_request.has_delta = false;
				return;
			}
			if (!magazine_request.detached && rig.magazine >= 0)
			{
				const authored_grip& grip = snap_request.ak ? magazine_grip_ak : magazine_grip_ar;
				const ik::bone_transform seated = ik::model_of(local, rig.parents, rig.magazine);
				const ik::quat hand = ik::normalize(seated.rotation * ik::conjugate(grip.magazine_rotation));
				const ik::vec3 wrist = seated.position - ik::rotate(hand, grip.magazine);
				snap_request.delta_position = wrist - goal.wrist;
				snap_request.delta_rotation = ik::normalize(hand * ik::conjugate(goal.hand));
				snap_request.has_delta = true;
			}
			if (snap_request.has_delta)
			{
				goal.wrist = goal.wrist + snap_request.delta_position * weight;
				goal.hand = nlerp(goal.hand, ik::normalize(snap_request.delta_rotation * goal.hand), weight);
			}
		}

		// #62: the drawn hand (palm and knuckles, from its own bones) pushed out of the receiver and handguard, which are
		// built from the gun's bones; the whole hand moves, its turn stays. push false: only the side the hand is on is
		// followed (the hand is held by something else this frame).
		void apply_hand_collision(const ik::bone_transform* local, int hand, ik::arm_goal& goal, bool push)
		{
			const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
			const contact::gun_points gun_at{ geometry.magazine_top, geometry.hand[1].position,
				geometry.has_support_reach ? geometry.support_reach : geometry.hand[0].position };
			contact::box boxes[contact::hand_collider::max_boxes];
			const int count = contact::gun_boxes(gun_at, contact::gun_shape{}, boxes);
			hand_collision_receiver = boxes[0];
			for (int b = 0; b < count; ++b)
			{
				boxes[b].centre = gun.position + ik::rotate(gun.rotation, boxes[b].centre);
				boxes[b].frame = ik::normalize(gun.rotation * boxes[b].frame);
			}
			hand_collision_boxes = count;
			// The middle finger's knuckle in the hand bone's axes (the metacarpals are not bent by the mod).
			const int knuckle_bone = rig.finger_bones[hand][fingers::middle][0];
			ik::vec3 knuckle_in_hand{ 0.0f, 0.0f, 0.0f };
			if (knuckle_bone > 0)
			{
				const ik::bone_transform hand_bone = ik::model_of(local, rig.parents, rig.arms[hand].hand);
				knuckle_in_hand = ik::rotate(ik::conjugate(hand_bone.rotation), ik::model_of(local, rig.parents, knuckle_bone).position - hand_bone.position);
			}
			if (ik::length(knuckle_in_hand) < 0.03f)
			{
				hand_colliders[hand].reset();
				return;
			}
			const contact::sphere spheres[2]{
				{ goal.wrist + ik::rotate(goal.hand, knuckle_in_hand * 0.55f), 0.035f },
				{ goal.wrist + ik::rotate(goal.hand, knuckle_in_hand), 0.025f },
			};
			const ik::vec3 resolved = hand_colliders[hand].resolve(boxes, count, spheres, 2);
			const ik::vec3 shift = push ? resolved : ik::vec3{};
			hand_collision_touch[hand] = push && hand_colliders[hand].touching();
			hand_collision_depth[hand] = push ? hand_colliders[hand].depth() : 0.0f;
			if (hand_collision_touch[hand])
			{
				++hand_collision_pushes;
				goal.wrist = goal.wrist + shift;
			}
			for (int b = 0; b < count; ++b)
			{
				hand_collision_box[b] = boxes[b];
			}
			hand_collision_palm_in_gun[hand] = ik::rotate(ik::conjugate(gun.rotation), spheres[0].centre + shift - gun.position);
			hand_collision_knuckle[hand] = knuckle_in_hand;
			hand_collision_checked[hand] = push;
		}

		// #62, pipe `fingers`: how deep the hand as drawn (after the arm's solve) still is in the gun, metres.
		float drawn_hand_depth(const ik::bone_transform* local, int hand)
		{
			const ik::bone_transform drawn = ik::model_of(local, rig.parents, rig.arms[hand].hand);
			const contact::sphere spheres[2]{
				{ drawn.position + ik::rotate(drawn.rotation, hand_collision_knuckle[hand] * 0.55f), 0.035f },
				{ drawn.position + ik::rotate(drawn.rotation, hand_collision_knuckle[hand]), 0.025f },
			};
			float deepest = 0.0f;
			for (int b = 0; b < hand_collision_boxes; ++b)
			{
				for (const contact::sphere& s : spheres)
				{
					deepest = std::fmax(deepest, s.radius - contact::signed_distance(hand_collision_box[b], s.centre));
				}
			}
			return deepest;
		}

		// Grip lock: the off hand's goal where the game's magazine grip holds the magazine at the locked pose
		// (on the magwell's rail, sliding home, or seated), blended by the lock's weight.
		void apply_magazine_lock(const ik::bone_transform* local, ik::arm_goal& goal)
		{
			const float weight = lock_request.weight;
			if (weight <= 0.0f || rig.camera < 0)
			{
				return;
			}
			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const float* p = lock_request.position;
			const float* q = lock_request.rotation;
			const ik::vec3 top = tracking_origin(camera, frame) + ik::rotate(frame, ik::vec3{ p[0], p[1], p[2] });
			const ik::quat rotation = frame * ik::normalize(ik::quat{ q[0], q[1], q[2], q[3] });
			const authored_grip& grip = lock_request.ak ? magazine_grip_ak : magazine_grip_ar;
			const ik::quat hand = ik::normalize(rotation * ik::conjugate(grip.magazine_rotation));
			const ik::vec3 wrist = top - ik::rotate(hand, grip.magazine);
			goal.wrist = goal.wrist + (wrist - goal.wrist) * weight;
			goal.hand = nlerp(goal.hand, hand, weight);
		}

		// Called with rig_lock held, before the conversion: the arms reach for the controllers.
		void edit_arms(ik::bone_transform* local)
		{
			for (int i = 0; i < edited_count; ++i)
			{
				bone_memory& bone = memory[i];
				ik::bone_transform& current = local[edited[i]];
				if (bone.has_written && std::memcmp(&current, &bone.written, sizeof(current)) == 0)
				{
					current = bone.animated;
				}
				else
				{
					bone.animated = current;
				}
				bone.has_written = false;
			}
			last_arms[0].solved = last_arms[1].solved = false;
			geometry.valid = rig.weapon >= 0;
			if (geometry.valid)
			{
				const ik::bone_transform to_gun = ik::inverse(ik::model_of(local, rig.parents, rig.weapon));
				for (int hand = 0; hand < 2; ++hand)
				{
					geometry.hand[hand] = ik::compose(to_gun, ik::model_of(local, rig.parents, rig.arms[hand].hand));
					if (hand == 1 && gun_in_hand.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed)
						&& !sling_primary_shown.load(std::memory_order_relaxed)) // frozen at the primary's while its copy hangs
					{
						sling_grip_in_gun = geometry.hand[1].position;
						has_sling_grip = true;
					}
					geometry.has_ik[hand] = rig.ik_hand[hand] >= 0;
					if (hand == 1)
					{
						along_now = geometry.hand[0].position.z - geometry.hand[1].position.z;
						const unsigned key = pump_rest_key.load(std::memory_order_relaxed);
						if (key != along_key)
						{
							along_key = key;
							along_hold = along_candidate = -1.0f;
							along_still = 0.0f;
						}
						const ULONGLONG now_ms = GetTickCount64();
						const float dt = along_last_ms && now_ms >= along_last_ms ? std::fmin(0.1f, static_cast<float>(now_ms - along_last_ms) * 0.001f) : 0.0f;
						along_last_ms = now_ms;
						if (std::isfinite(along_now))
						{
							if (along_candidate >= -0.5f && std::fabs(along_now - along_candidate) <= 0.01f)
							{
								along_still += dt;
							}
							else
							{
								along_candidate = along_now;
								along_still = 0.0f;
							}
							if (along_still >= 0.5f)
							{
								along_hold = along_candidate;
							}
						}
					}
					if (geometry.has_ik[hand])
					{
						geometry.ik_hand[hand] = ik::compose(to_gun, ik::model_of(local, rig.parents, rig.ik_hand[hand]));
					}
				}
				if (rig.magazine >= 0 && !magazine_request.detached)
				{
					// The highest sample per gun: a seated magazine is as high as it goes, and the game's own reload animation
					// (reload = button) moves it down and out (monitor 2026-11-16: a 2 cm window crept along with it).
					const ik::vec3 top = ik::compose(to_gun, ik::model_of(local, rig.parents, rig.magazine)).position;
					if (!geometry.has_magazine_top || top.y > geometry.magazine_top.y)
					{
						geometry.magazine_top = top;
						geometry.has_magazine_top = true;
					}
				}
				// The support hand's furthest-forward sample per gun: animations take it back towards the magazine.
				if (!geometry.has_support_reach || geometry.hand[0].position.z > geometry.support_reach.z)
				{
					geometry.support_reach = geometry.hand[0].position;
					geometry.has_support_reach = true;
				}
				if (rig.camera >= 0)
				{
					// The joint's z looks forward. Samples are taken only while steady: never while firing or
					// just after (the recoil and the game's aim pitch move the camera joint against the gun:
					// +-13 degrees seen), and only within 5 degrees of the barrel (no sight is further off).
					const ik::bone_transform eye = ik::compose(to_gun, ik::model_of(local, rig.parents, rig.camera));
					const ik::vec3 forward = ik::normalize(ik::rotate(eye.rotation, ik::vec3{ 0, 0, 1 }));
					// The gun in hand, by where its grip sits: a swap forgets both samples.
					const ik::vec3 gun_grip = geometry.has_ik[1] ? geometry.ik_hand[1].position : geometry.hand[1].position;
					if (ik::length(gun_grip - geometry.sampled_gun) > 0.01f)
					{
						geometry.has_sight = false;
						geometry.has_ads_sight = false;
						geometry.has_magazine_top = false; // another gun: its magwell and handguard are sampled afresh
						geometry.has_support_reach = false;
						geometry.sampled_gun = gun_grip;
					}
					const double now = steady_seconds();
					if (input::latest().right_trigger > 30)
					{
						geometry.fired_at = now;
					}
					if (!input::game_aiming())
					{
						geometry.aiming_since = -1.0;
					}
					else if (geometry.aiming_since < 0.0)
					{
						geometry.aiming_since = now;
					}
					if (now - geometry.fired_at > sight_quiet_seconds && forward.z > sight_min_cos)
					{
						const float keep = geometry.has_sight ? 0.9f : 0.0f;
						geometry.sight_direction = ik::normalize(geometry.sight_direction * keep + forward * (1.0f - keep));
						geometry.sight_eye = geometry.sight_eye * keep + eye.position * (1.0f - keep);
						geometry.has_sight = true;
						// Only a camera that really sits behind the optic is the game's ADS (not the hip pose while
						// the game ignores the aim button).
						const bool on_optic = rig.optic < 0
							|| ik::ads_eye_behind_optic(eye.position - ik::compose(to_gun, ik::model_of(local, rig.parents, rig.optic)).position);
						if (geometry.aiming_since >= 0.0 && now - geometry.aiming_since > ads_settle_seconds && on_optic)
						{
							const float ads_keep = geometry.has_ads_sight ? 0.9f : 0.0f;
							geometry.ads_direction = ik::normalize(geometry.ads_direction * ads_keep + forward * (1.0f - ads_keep));
							geometry.ads_eye = geometry.ads_eye * ads_keep + eye.position * (1.0f - ads_keep);
							geometry.has_ads_sight = true;
						}
					}
				}
			}
			if (!ik_enabled.load(std::memory_order_relaxed))
			{
				return;
			}

			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const ik::vec3 origin = tracking_origin(camera, frame);
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			// Synthetic targets are placed relative to the view: the head (between the eyes) when tracked.
			ik::vec3 head_position;
			ik::quat head_rotation;
			bool head_valid = false;
			if (const stereo::head_sample head = stereo::latest_head_sample(); head.valid)
			{
				const float* left = head.eyes[0].position;
				const float* right = head.eyes[1].position;
				const float* q = head.eyes[0].orientation;
				head_position = { 0.5f * (left[0] + right[0]), 0.5f * (left[1] + right[1]), 0.5f * (left[2] + right[2]) };
				head_rotation = { q[0], q[1], q[2], q[3] };
				head_valid = true;
			}
			// The neck sits behind the eyes along the way the head faces, not along tracking's back: the
			// body is laid out from a head looking ahead with the neck there (body_head), and the torso
			// turns about the neck. About a fixed point behind the starting direction instead, a player
			// turned 180 degrees in the room had the neck and shoulders 20 cm in front of the face, and
			// the body swung round them like a turret while turning (headset 2026-11-05).
			ik::vec3 neck = head_position;
			if (head_valid)
			{
				float behind[3];
				neck_offset(yaw_of(head_rotation.x, head_rotation.y, head_rotation.z, head_rotation.w), torso_spine_back, behind);
				neck = head_position + ik::vec3{ behind[0], behind[1], behind[2] };
			}
			const ik::vec3 body_head = neck - ik::vec3{ 0.0f, 0.0f, torso_spine_back };
			// Torso (#13): the body turns about the spine (behind the head) to face where the tracked
			// head and hands say it faces; the game body only turns with the stick.
			const float torso_yaw_now = update_torso(head_valid, neck, head_rotation, hands);
			const float torso = torso_follow.load(std::memory_order_relaxed) && head_valid ? angle_between(torso_yaw_now, 0.0f) : 0.0f;
			const ik::vec3 torso_axis = ik::rotate(frame, ik::vec3{ 0.0f, 1.0f, 0.0f });
			const ik::vec3 spine = origin + ik::rotate(frame, neck);
			const float torso_sin = std::sin(0.5f * torso);
			const ik::quat torso_turn{ torso_axis.x * torso_sin, torso_axis.y * torso_sin, torso_axis.z * torso_sin, std::cos(0.5f * torso) };
			bool any = false;
			bool shaped = false; // written bones must be remembered even when no arm solves
			{
				// Under controller aim the game turns its whole first-person body (chest, clavicles, gun
				// root) with the aimer, i.e. with the gun: a wrist turn or the knife pointed across swung
				// the chest round (headset 2026-11-04). The rendered frame faces the body's yaw (frame turns
				// the joint by turn.yaw), so the body's branches turn by the same angle about the camera
				// joint first, then by the torso about the spine. The camera's own branch stays.
				const stereo::first_person_turn view_turn = stereo::first_person_view_turn();
				const bool level_view = view_turn.active && rendered_frame.load(std::memory_order_relaxed);
				const float back = level_view ? view_turn.yaw : 0.0f;
				const ik::quat about_joint{ 0, std::sin(0.5f * back), 0, std::cos(0.5f * back) };
				const ik::quat to_body = ik::normalize(camera.rotation * about_joint * ik::conjugate(camera.rotation));
				// SpineX (neck, clavicles, gun root) also pitches with the aim about the camera joint's x:
				// a gun pointed 40 degrees up swung the neck 13 cm forward and down, 40 down lifted it 11 cm
				// (monitor 2026-11-05), so a wrist tilt still rocked the upper body. The rendered view is
				// level, so that pitch is taken out too (the joint itself carries none, see camera_frame).
				float pitch = 0.0f;
				if (!level_view || !read(rig.soldier + 0x4dc, pitch) || !std::isfinite(pitch))
				{
					pitch = 0.0f;
				}
				const ik::quat unpitch = ik::normalize(camera.rotation * ik::quat{ std::sin(0.5f * pitch), 0, 0, std::cos(0.5f * pitch) } * ik::conjugate(camera.rotation));
				const ik::quat to_level_body = ik::normalize(to_body * unpitch);
				// The body also goes where the head is: the camera joint is the tracking origin, so a player
				// leaning or crouching in the room moved only the shoulders (body_shoulders follow the head),
				// and the chest stayed behind (headset 2026-11-04: "the chest does not move with the arms").
				const ik::vec3 lift = origin - camera.position;
				const ik::vec3 with_head = lift + (head_valid ? ik::rotate(frame, body_head) : ik::vec3{});
				if ((back != 0.0f || pitch != 0.0f || torso != 0.0f || head_valid || ik::length(lift) > 0.0f) && body_turn_on.load(std::memory_order_relaxed))
				{
					for (int i = 0; i < rig.trunk_count; ++i)
					{
						const ik::quat undo = rig.trunk_pitches[i] ? to_level_body : to_body;
						ik::bone_transform branch = ik::model_of(local, rig.parents, rig.trunk[i]);
						const ik::vec3 at_body = camera.position + ik::rotate(undo, branch.position - camera.position) + with_head;
						branch.position = spine + ik::rotate(torso_turn, at_body - spine);
						branch.rotation = ik::normalize(torso_turn * undo * branch.rotation);
						ik::set_model_transform(local, rig.parents, rig.trunk[i], branch);
						shaped = true;
					}
				}
			}
			bool gun_placed = false;
			// The hand carrying the gun first: the other is drawn on the gun (handguard, handle) where it now is.
			const int carrier = gun_in_off_hand.load(std::memory_order_relaxed) ? 0 : 1;
			if (carrier == 0 && !carried_by_off)
			{
				// The hand-over: keep the off hand's hold on the handguard as drawn, relative to its controller.
				const float* q0 = hands.aim[0].orientation;
				off_carry_in_aim = ik::normalize(ik::conjugate(frame * ik::normalize(ik::quat{ q0[0], q0[1], q0[2], q0[3] })) * last_drawn_hand[0]);
			}
			carried_by_off = carrier == 0;
			for (const int hand : { carrier, 1 - carrier })
			{
				ik::vec3 wrist;
				ik::vec3 palm;
				ik::quat aim;
				if (targets[hand].active)
				{
					wrist = head_position + ik::rotate(head_rotation, targets[hand].position);
					palm = wrist;
					aim = head_rotation * targets[hand].rotation;
				}
				else if (hands.valid[hand])
				{
					const stereo::pose& grip = hands.grip[hand];
					const ik::quat grip_rotation{ grip.orientation[0], grip.orientation[1], grip.orientation[2], grip.orientation[3] };
					wrist = ik::vec3{ grip.position[0], grip.position[1], grip.position[2] } + ik::rotate(grip_rotation, wrist_offset[hand]);
					// shoulder_offset: the shoulders sit fixed under the camera, so moving them by an
					// offset (head axes) equals moving the hands by minus that offset.
					const ik::vec3 shoulders{ shoulder_offset[0].load(), shoulder_offset[1].load(), shoulder_offset[2].load() };
					wrist = wrist - ik::rotate(head_rotation, shoulders);
					palm = ik::vec3{ grip.position[0], grip.position[1], grip.position[2] } - ik::rotate(head_rotation, shoulders);
					const float* q = hands.aim[hand].orientation;
					aim = { q[0], q[1], q[2], q[3] };
					if (hand == (off_by_grip.load(std::memory_order_relaxed) ? 0 : 1)) // the hand on the pistol grip (#38)
					{
						const std::lock_guard held(gun_hand_lock);
						if (gun_hand_override)
						{
							aim = gun_hand_rotation;
						}
					}
				}
				else
				{
					continue;
				}

				const ik::arm_bones& bones = rig.arms[hand];
				// The controller turns the animated hand: pointing straight ahead keeps the authored grip.
				const ik::bone_transform hand_model = ik::model_of(local, rig.parents, bones.hand);
				const ik::quat hand_animated = hand_model.rotation;
				// The gun keeps its animated placement relative to the right hand.
				const bool carry_weapon = hand == carrier && weapon_follows && rig.weapon >= 0;
				const bool gun_hand_carries = carry_weapon && hand == 1; // the gun-hand-only holds below (aim ray, knife, recoil)
				const ik::bone_transform weapon_in_hand = carry_weapon
					? ik::compose(ik::inverse(hand_model), ik::model_of(local, rig.parents, rig.weapon)) : ik::bone_transform{};
				// Arm shape (live settings): wider shoulders and longer arms, set before the solve, which
				// takes its bone lengths from the pose. The weapon keeps its authored grip (measured above).
				const float width = shoulder_width.load(std::memory_order_relaxed);
				if (body_shoulders.load(std::memory_order_relaxed) && head_valid && bones.upper >= 0 && rig.parents[bones.upper] >= 0)
				{
					// body_shoulders: the rifle animation holds the left shoulder joint on the chest's centre
					// line and the right one 14 cm forward (monitor, 2026-11-04); a real body has them either
					// side of the neck, below and a little behind the eyes. The clavicle carries the upper arm
					// there (the torso turn below then turns both about the spine).
					const int clavicle = rig.parents[bones.upper];
					const ik::vec3 at = ik::model_of(local, rig.parents, bones.upper).position;
					// #46, #50: the anchor sits shoulder_drop below the eyes, its depth scales with the player's
					// height (its width comes from shoulder_width).
					const float scale = body_scale.load(std::memory_order_relaxed);
					const ik::vec3 anchor{ (hand == 0 ? -1.0f : 1.0f) * (shoulder_anchor.x + width), -shoulder_drop.load(std::memory_order_relaxed), shoulder_anchor.z * scale + shoulder_back.load(std::memory_order_relaxed) };
					const ik::vec3 wanted = spine + ik::rotate(torso_turn, origin + ik::rotate(frame, body_head + anchor) - spine);
					ik::bone_transform moved = ik::model_of(local, rig.parents, clavicle);
					moved.position = moved.position + (wanted - at);
					ik::set_model_transform(local, rig.parents, clavicle, moved);
					shaped = true;
				}
				else if (width != 0.0f && bones.upper >= 0 && rig.parents[bones.upper] >= 0)
				{
					const int clavicle = rig.parents[bones.upper];
					ik::bone_transform moved = ik::model_of(local, rig.parents, clavicle);
					moved.position = moved.position + ik::rotate(torso_turn * frame, ik::vec3{ hand == 0 ? -width : width, 0.0f, 0.0f });
					ik::set_model_transform(local, rig.parents, clavicle, moved);
					shaped = true;
				}
				const float length = arm_length.load(std::memory_order_relaxed);
				if (length != 1.0f)
				{
					for (const int bone : { bones.fore, bones.roll, bones.hand })
					{
						if (bone >= 0)
						{
							local[bone].position = local[bone].position * length;
						}
					}
					shaped = true;
				}
				ik::arm_goal goal;
				goal.wrist = origin + ik::rotate(frame, wrist);
				goal.hand = frame * ik::normalize(aim) * ik::conjugate(frame) * hand_animated;
				if (gun_hand_carries && gun_in_hand.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed))
				{
					// The gun hand's authored grip with the barrel along the aim ray: how a hand holds a
					// pistol grip, i.e. the controller. Mirrored, it is the free off hand's wrist (off_hand_pose).
					gun_hand_in_aim = ik::normalize(ik::quat{ 0, 1, 0, 0 } * ik::conjugate(weapon_in_hand.rotation));
					has_gun_hand_in_aim = true;
				}
				if (hand == 0 && carrier == 1 && off_hand_neutral.load(std::memory_order_relaxed) && has_gun_hand_in_aim)
				{
					goal.hand = frame * ik::normalize(aim) * fingers::mirrored_hand(gun_hand_in_aim);
				}
				// #68 (owner 2026-11-15): the gun hand turned itself round once the gun went to the off hand (the game's
				// animated grip, turned by the controller), and a grenade or the C4 detonator in it sat oddly. Like the
				// free off hand, it now holds as you hold the controller: the gun grip on the aim ray. Items keep their
				// own place in the hand (the knife has its own frame below).
				// Only the hand grenade and the C4 detonator (set_gun_hand_item): a launcher in a gadget slot (M320) is
				// gun-like and already follows the aim with its own hold (re/notes/weapon.md "Gadgets from the stash").
				if (hand_rules::gun_hand_controller_grip(hand, carrier, gun_hand_carries, hand_item_in_gun_hand.load(std::memory_order_relaxed), gun_hand_neutral.load(std::memory_order_relaxed), has_gun_hand_in_aim))
				{
					goal.hand = frame * ik::normalize(aim) * gun_hand_in_aim;
				}
				const bool off_grip_carries = carry_weapon && hand == 0 && off_by_grip.load(std::memory_order_relaxed);
				if (carry_weapon && hand == 0)
				{
					// #38: the off hand carries the gun by the handguard, held as it was at the hand-over; by the
					// pistol grip it holds it as the gun hand does, mirrored, barrel along its pointing ray.
					goal.hand = off_grip_carries && has_gun_hand_in_aim ? ik::normalize(frame * ik::normalize(aim) * fingers::mirrored_hand(gun_hand_in_aim))
						: ik::normalize(frame * ik::normalize(aim) * off_carry_in_aim);
				}
				if (gun_hand_carries && gun_in_hand.load(std::memory_order_relaxed) && gun_on_ray.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed))
				{
					// gun_pose = aim: Wep_Root lies on the aim ray, upright ((0,1,0,0) in camera axes points
					// its +z barrel along -z), instead of the hip animation (its own angle, sway and bob)
					// turned by the controller. The hand keeps the gun's authored grip. Guns only: a grenade or the
					// C4 detonator is held by the controller grip above (gun_hand_pose), the knife below.
					goal.hand = frame * ik::normalize(aim) * ik::quat{ 0, 1, 0, 0 } * ik::conjugate(weapon_in_hand.rotation);
				}
				if (gun_hand_carries && knife_in_hand.load(std::memory_order_relaxed))
				{
					// The knife's Wep_Root points its blade about 30 degrees off -z towards +y, edge on +y
					// (monitor, 2026-10-27): 180 degrees about z from the camera axes, then knife_pitch up,
					// puts the blade along the controller ray, edge down. The hand follows the knife's grip.
					// knife_yaw turns it about the controller's up (+ = to the right), knife_roll about the ray
					// (+ = clockwise as seen from behind), both in the controller's axes before the pitch.
					const float half = 0.5f * knife_pitch.load(std::memory_order_relaxed);
					const float half_yaw = -0.5f * knife_yaw.load(std::memory_order_relaxed);
					const float half_roll = -0.5f * knife_roll.load(std::memory_order_relaxed);
					const ik::quat level{ std::sin(half), 0, 0, std::cos(half) };
					const ik::quat turn{ 0, std::sin(half_yaw), 0, std::cos(half_yaw) };
					const ik::quat roll{ 0, 0, std::sin(half_roll), std::cos(half_roll) };
					goal.hand = frame * ik::normalize(aim) * turn * roll * level * ik::quat{ 0, 0, 1, 0 } * ik::conjugate(weapon_in_hand.rotation);
				}
				if (gun_hand_carries && gun_in_hand.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed))
				{
					recoil_offset kick;
					{
						const std::lock_guard guard(recoil_lock);
						kick = recoil.at(steady_seconds());
						last_kick = kick;
					}
					if (kick.degrees > 0.0f || kick.back > 0.0f || kick.yaw != 0.0f)
					{
						// Recoil: the muzzle climbs about the wrist (the controller's right axis), wanders
						// sideways about its up axis, and the gun comes back along its barrel; a two-handed
						// off hand follows the gun below.
						const ik::quat held = frame * ik::normalize(aim);
						const ik::vec3 right = ik::rotate(held, ik::vec3{ 1.0f, 0.0f, 0.0f });
						const ik::vec3 up = ik::rotate(held, ik::vec3{ 0.0f, 1.0f, 0.0f });
						const ik::vec3 forward = ik::rotate(held, ik::vec3{ 0.0f, 0.0f, -1.0f });
						const auto turn = [](const ik::vec3& axis, float degrees) {
							const float half = 0.5f * degrees * 0.017453293f;
							const float s = std::sin(half);
							return ik::quat{ axis.x * s, axis.y * s, axis.z * s, std::cos(half) };
						};
						// Positive yaw turns the muzzle right: about -up for a -z forward.
						goal.hand = ik::normalize(turn(up, -kick.yaw) * turn(right, kick.degrees) * goal.hand);
						goal.wrist = goal.wrist - forward * kick.back;
					}
					const bool on_ray = gun_on_ray.load(std::memory_order_relaxed) && !gun_on_floor_now.load(std::memory_order_relaxed); // not dropped or slung
					wall_push_target = on_ray;
					// The real gun for the wall ray: as the hand goal holds it, before the push and the arm's reach.
					held_root.position = goal.wrist + ik::rotate(goal.hand, weapon_in_hand.position);
					held_root.rotation = ik::normalize(goal.hand * weapon_in_hand.rotation);
					has_held_root = on_ray;
					const float push = wall_push.load(std::memory_order_relaxed);
					if (on_ray && push > 0.0f)
					{
						// #63: the drawn gun (and the hand on it) pulled back along the barrel off the wall; the
						// support hand follows the gun below.
						const ik::vec3 barrel = ik::rotate(frame * ik::normalize(aim), ik::vec3{ 0.0f, 0.0f, -1.0f });
						goal.wrist = goal.wrist - barrel * push;
					}
				}
				else if (gun_hand_carries)
				{
					wall_push_target = false;
					has_held_root = false;
				}
				const float support = support_weight.load(std::memory_order_relaxed);
				if (hand != carrier && !(support > 0.0f))
				{
					support_place.update(false, 0.0f, 0.0f, 0.0f, 0.0f, true); // the hold ended: the next grab takes its place afresh
					support_drawn = false;
					support_drawn_in_gun = support_game_in_gun = {}; // no hold: nothing to report
				}
				if (hand != carrier && (carrier == 1 || off_by_grip.load(std::memory_order_relaxed)) && gun_placed && geometry.valid && support > 0.0f)
				{
					// Two-handed: the off hand is drawn where the gun's own animation holds it, slid along
					// the barrel to where the controller is (within the handguard), blended by the hold. The gun
					// hand on the handguard (#38: the left hand on the grip) holds that spot mirrored across the gun.
					const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
					ik::bone_transform held_at = geometry.hand[0];
					if (hand == 1)
					{
						held_at.position.x = -held_at.position.x;
						held_at.rotation = fingers::mirrored_hand(held_at.rotation);
					}
					// Locked where the hold took it (support_lock), within the range (support_slide_back/forward).
					const float along = ik::rotate(ik::conjugate(gun.rotation), goal.wrist - gun.position).z;
					// #99: a pistol's cupping hand stays where the game's own left hand wraps it (no slide along a pistol).
					const bool cupping = along_hold >= 0.0f && along_hold < pistol_hold_metres;
					// #91: on a foregrip the game's hand sits on one fixed point of the grip; sliding takes it off.
					support_on_grip = foregrip_hold_on.load(std::memory_order_relaxed) && foregrip_kind.load(std::memory_order_relaxed) != 0;
					const ik::vec3 on_gun{ held_at.position.x, held_at.position.y, cupping || support_on_grip ? held_at.position.z
						: support_place.update(true, along, held_at.position.z, support_back.load(std::memory_order_relaxed),
							support_forward.load(std::memory_order_relaxed), support_lock.load(std::memory_order_relaxed)) };
					support_game_in_gun = held_at.position;
					support_drawn = support >= 0.99f;
					const ik::vec3 snapped = gun.position + ik::rotate(gun.rotation, on_gun);
					goal.wrist = goal.wrist + (snapped - goal.wrist) * support;
					goal.hand = nlerp(goal.hand, gun.rotation * held_at.rotation, support);
				}
				if (hand != carrier && gun_placed && handle_request.weight > 0.0f)
				{
					// Racking: the handle stays between the thumb and the index's side (pinch), or inside the
					// hooked index (hook), else under the palm, moved back by the pull, wherever the gun goes;
					// the hand keeps the controller's rotation; either hand (the gun hand racks while the off hand
					// carries, #38).
					const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
					const ik::vec3 on_handle = gun.position + ik::rotate(gun.rotation, handle_request.handle - ik::vec3{ 0.0f, 0.0f, handle_request.pull });
					const bool fist_hold = rack_grip.load(std::memory_order_relaxed) == 3;
					// The fist is always locked on the knob: the turn it took the handle with, held to the release.
					if (rack_lock_on.load(std::memory_order_relaxed) || fist_hold)
					{
						if (!rack_locked)
						{
							rack_hand_in_gun = ik::normalize(ik::conjugate(gun.rotation) * goal.hand);
							rack_locked = true;
						}
						goal.hand = nlerp(goal.hand, ik::normalize(gun.rotation * rack_hand_in_gun), handle_request.weight);
					}
					const bool own = hand == hook_hand; // the hook and pinch points were measured on this hand
					const bool pinch = own && pinching && has_pinch;
					// The fist holds the knob at its inner edge, out on the handle's side: its grasp centre is moved
					// off the knob so it stays outside the receiver (headset 2026-11-15, SCAR-H).
					const ik::vec3 fist_at = on_handle + ik::rotate(gun.rotation, contact::rack_fist_offset(handle_request.underhand, handle_request.side, rack_fist));
					const ik::vec3 hooked = own && fist_hold && has_fist ? fist_at - ik::rotate(goal.hand, fist_in_hand)
						: pinch ? on_handle - ik::rotate(goal.hand, pinch_in_hand)
						: own && hooking_under && has_under_hook ? on_handle - ik::rotate(goal.hand, under_hook_in_hand)
						: own && has_hook ? on_handle - ik::rotate(goal.hand, hook_in_hand)
						: goal.wrist + (on_handle - (origin + ik::rotate(frame, palm)));
					goal.wrist = goal.wrist + (hooked - goal.wrist) * handle_request.weight;
				}
				if (hand != carrier && !(gun_placed && handle_request.weight > 0.0f))
				{
					rack_locked = false; // the handle let go: the next stroke takes the hand's rotation again
				}
				if (hand == 0 && carrier == 1 && gun_placed)
				{
					apply_magazine_snap(local, goal);
					apply_magazine_lock(local, goal);
				}
				// #62: the free hand stays out of the gun; not while it holds the handguard or the charging handle.
				// Nor while a magazine is placed at the magwell (the snap and the grip lock); meanwhile the side the hand is
				// on is still followed, so a hand that leaves such a hold and slaps through the gun still meets it.
				if (hand != carrier && gun_placed && geometry.valid && geometry.has_magazine_top && hand_collision_on.load(std::memory_order_relaxed))
				{
					const bool held_elsewhere = support > 0.0f || handle_request.weight > 0.0f || snap_request.weight > 0.0f || lock_request.weight > 0.0f;
					apply_hand_collision(local, hand, goal, !held_elsewhere);
				}
				else
				{
					hand_colliders[hand].reset();
					hand_collision_touch[hand] = false;
					hand_collision_depth[hand] = 0.0f;
					hand_collision_checked[hand] = false;
				}
				// The elbow hints turn with the torso; the second (elbow out and back) takes over where
				// the hand lies along the first, the belt and hip for the default hint.
				const ik::quat body_frame = torso_turn * frame;
				const ik::vec3 shoulder_at = ik::model_of(local, rig.parents, bones.upper).position;
				goal.pole = shoulder_at + ik::rotate(body_frame, pole_direction[hand]);
				goal.pole_fallback = shoulder_at + ik::rotate(body_frame, ik::vec3{ hand == 0 ? -0.6f : 0.6f, 0.0f, 1.0f });
				goal.has_pole_fallback = true;
				goal.max_stretch = arm_stretch.load(std::memory_order_relaxed);
				goal.max_protraction = shoulder_reach.load(std::memory_order_relaxed);
				last_arms[hand].hand_in_aim = ik::normalize(ik::conjugate(frame * ik::normalize(aim)) * goal.hand);
				const float reach = ik::reach_ratio(local, rig.parents, bones, goal.wrist);
				if (ik::solve_arm(local, rig.parents, bones, goal))
				{
					any = true;
					last_arms[hand].solved = true;
					last_drawn_hand[hand] = ik::model_of(local, rig.parents, bones.hand).rotation;
					if (hand != carrier && support_drawn && rig.weapon >= 0)
					{
						// #99: where the solved off wrist landed on the gun (the hand bone, as geometry.hand is), for the report.
						const ik::bone_transform gun_now = ik::model_of(local, rig.parents, rig.weapon);
						support_drawn_in_gun = ik::rotate(ik::conjugate(gun_now.rotation), ik::model_of(local, rig.parents, bones.hand).position - gun_now.position);
					}
					// Cleared on every solve of the hand that racks when no fist holds the knob, so a reading is always current.
					if (hand != carrier && !(gun_placed && handle_request.weight > 0.0f && fisting && has_fist))
					{
						rack_grip_measured = false;
						rack_grip_error = {};
					}
					if (hand != carrier && gun_placed && handle_request.weight > 0.0f && fisting && has_fist)
					{
						const ik::bone_transform drawn = ik::model_of(local, rig.parents, bones.hand);
						const ik::bone_transform gun_at = ik::model_of(local, rig.parents, rig.weapon);
						const ik::vec3 grip = drawn.position + ik::rotate(drawn.rotation, fist_in_hand);
						const ik::vec3 knob = handle_request.handle - ik::vec3{ 0.0f, 0.0f, handle_request.pull };
						rack_grip_error = ik::rotate(ik::conjugate(gun_at.rotation), grip - gun_at.position) - knob;
						rack_grip_measured = true;
					}
					hand_collision_drawn[hand] = hand_collision_checked[hand] ? drawn_hand_depth(local, hand) : 0.0f;
					last_arms[hand].reach = reach;
					last_arms[hand].goal = goal.wrist;
					last_arms[hand].goal_xr = wrist;
					last_arms[hand].palm_xr = palm;
					last_arms[hand].aim_xr = ik::normalize(aim);
					if (carry_weapon)
					{
						bool on_floor = false;
						ik::vec3 floor_position;
						float floor_yaw{};
						bool hanging{};
						{
							const std::lock_guard dropped(dropped_lock);
							on_floor = gun_on_floor && gun_in_hand.load(std::memory_order_relaxed);
							floor_position = floor_gun_position;
							floor_yaw = floor_gun_yaw;
							hanging = floor_gun_hanging;
						}
						if (on_floor)
						{
							// Free hands: the gun lies on its side on the floor, barrel along the yaw it fell
							// with ((0,1,0,0) points Wep_Root's +z barrel along -z, a quarter turn about the barrel
							// lays it on its side); the empty hand stays on the controller.
							const ik::quat yaw_turn{ 0, std::sin(0.5f * floor_yaw), 0, std::cos(0.5f * floor_yaw) };
							const ik::quat on_side{ 0, 0, 0.70710678f, 0.70710678f };
							ik::bone_transform lying = ik::model_of(local, rig.parents, rig.weapon);
							lying.position = origin + ik::rotate(frame, floor_position);
							lying.rotation = ik::normalize(frame * yaw_turn * ik::quat{ 0, 1, 0, 0 } * on_side);
							if (hanging)
							{
								// On the sling: upright, barrel 75 degrees down, facing where the body faces, the
								// pistol grip at the sling point.
								const ik::quat down{ -0.60876143f, 0.0f, 0.0f, 0.79335334f };
								lying.rotation = ik::normalize(frame * yaw_turn * down * ik::quat{ 0, 1, 0, 0 });
								if (geometry.valid)
								{
									lying.position = lying.position - ik::rotate(lying.rotation, geometry.hand[1].position);
								}
							}
							ik::set_model_transform(local, rig.parents, rig.weapon, lying);
						}
						else
						{
							if (off_grip_carries && geometry.valid)
							{
								// The pistol grip in the left hand: the gun upright along the left aim ray, placed so
								// that the right hand's spot on the grip, mirrored across the gun, is the left hand.
								ik::bone_transform held = ik::model_of(local, rig.parents, rig.weapon);
								held.rotation = ik::normalize(frame * ik::normalize(aim) * ik::quat{ 0, 1, 0, 0 });
								const ik::vec3 grip = geometry.hand[1].position;
								held.position = ik::model_of(local, rig.parents, bones.hand).position - ik::rotate(held.rotation, ik::vec3{ -grip.x, grip.y, grip.z });
								ik::set_model_transform(local, rig.parents, rig.weapon, held);
							}
							else
							{
								ik::set_model_transform(local, rig.parents, rig.weapon,
									ik::compose(ik::model_of(local, rig.parents, bones.hand), weapon_in_hand));
							}
							gun_placed = true;
						}
					}
				}
			}
			if (any || shaped)
			{
				++solved;
				// Bones of an arm left alone are rewritten unchanged; that is harmless.
				for (int i = 0; i < edited_count; ++i)
				{
					memory[i].written = local[edited[i]];
					memory[i].has_written = true;
				}
			}
		}

		// Called with rig_lock held: draws the magazine at magazine_request's position (OpenXR LOCAL).
		void place_requested_magazine(ik::bone_transform* local)
		{
			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const float* p = magazine_request.position;
			const float* q = magazine_request.rotation;
			ik::bone_transform placed = ik::model_of(local, rig.parents, rig.magazine);
			placed.position = tracking_origin(camera, frame) + ik::rotate(frame, ik::vec3{ p[0], p[1], p[2] });
			placed.rotation = frame * ik::normalize(ik::quat{ q[0], q[1], q[2], q[3] });
			ik::set_model_transform(local, rig.parents, rig.magazine, placed);
			magazine_memory.written = local[rig.magazine];
			magazine_memory.has_written = true;
		}

		// Called with rig_lock held, after edit_weapon: a belt-fed gun by hand. The lid (Wep_Extra1, its local position the
		// hinge) turned up about the gun's x axis as the game's own reload does it (q about +x), the optic on it (Wep_Scope1,
		// also under Wep_Root) turned about the same hinge; the belt laid as animated, hanging out of the box (its end at the
		// box mouth, Belt5's rest), or its end in the hand. Publishes the hinge, the lid's grab point, the tray and the belt end.
		// The top-break revolver (belt_feed_request.revolver): the frame (Wep_Slide) turned about its own origin, the hinge,
		// with the cylinder, the cartridges and the seated speedloader; publishes the hinge and a grab point on the barrel.
		void edit_revolver(ik::bone_transform* local)
		{
			const int bones[8] = { rig.slide, rig.physic1, rig.belt[0], rig.belt[1], rig.belt[2], rig.belt[3], rig.belt[4], rig.belt[5] };
			for (int i = 0; i < 8; ++i)
			{
				if (bones[i] < 0)
				{
					continue;
				}
				ik::bone_transform& current = local[bones[i]];
				if (revolver_memory[i].has_written && std::memcmp(&current, &revolver_memory[i].written, sizeof(current)) == 0)
				{
					current = revolver_memory[i].animated;
				}
				else
				{
					revolver_memory[i].animated = current;
				}
				revolver_memory[i].has_written = false;
			}
			if (!belt_feed_request.active || !belt_feed_request.revolver || rig.weapon < 0 || rig.slide < 0 || rig.parents[rig.slide] != rig.weapon)
			{
				return;
			}
			const float angle = std::fmin(std::fmax(belt_feed_request.lid, 0.0f), 1.0f) * belt_feed_request.lid_degrees * 3.14159265f / 180.0f;
			const ik::quat turn{ std::sin(0.5f * angle), 0.0f, 0.0f, std::cos(0.5f * angle) };
			const ik::vec3 hinge = local[rig.slide].position;
			local[rig.slide].rotation = ik::normalize(turn * local[rig.slide].rotation);
			const auto swing = [&](int bone)
			{
				if (bone >= 0 && rig.parents[bone] == rig.weapon)
				{
					local[bone].position = hinge + ik::rotate(turn, local[bone].position - hinge);
					local[bone].rotation = ik::normalize(turn * local[bone].rotation);
				}
			};
			swing(rig.physic1);
			swing(rig.belt[0]);
			if (rig.magazine >= 0 && !magazine_request.detached)
			{
				swing(rig.magazine);
				magazine_memory.written = local[rig.magazine]; // edit_weapon restores the animated one next frame
				magazine_memory.has_written = true;
			}
			if (!belt_feed_request.rounds)
			{
				for (int link = 0; link < 6; ++link)
				{
					if (rig.belt[link] >= 0)
					{
						local[rig.belt[link]].scale = 0.001f;
					}
				}
			}
			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const ik::quat to_xr = ik::conjugate(frame);
			const ik::vec3 origin = tracking_origin(camera, frame);
			const ik::bone_transform slide = ik::model_of(local, rig.parents, rig.slide);
			const ik::quat root = ik::model_of(local, rig.parents, rig.weapon).rotation;
			// 11 cm along the barrel from the hinge, turned with the frame: where the off hand takes it.
			const ik::vec3 grab = slide.position + ik::rotate(root * turn, ik::vec3{ 0.0f, 0.0f, 0.11f });
			// The speedloader's seat: the turned cylinder's rear face (Wep_Physic1 sits mid-cylinder, its face 4.7 cm behind,
			// where the game seats Wep_Mag), as the magwell for the reload.
			ik::vec3 seat = slide.position;
			if (rig.physic1 >= 0)
			{
				seat = ik::model_of(local, rig.parents, rig.physic1).position + ik::rotate(root * turn, ik::vec3{ 0.0f, 0.0f, -0.047f });
			}
			const ik::vec3 values[3] = { ik::rotate(to_xr, slide.position - origin), ik::rotate(to_xr, grab - origin), ik::rotate(to_xr, seat - origin) };
			float* const outs[3] = { points.lid_hinge, points.lid_grab, points.magazine };
			for (int i = 0; i < 3; ++i)
			{
				outs[i][0] = values[i].x;
				outs[i][1] = values[i].y;
				outs[i][2] = values[i].z;
			}
			points.has_belt_feed = true;
			for (int i = 0; i < 8; ++i)
			{
				if (bones[i] >= 0)
				{
					revolver_memory[i].written = local[bones[i]];
					revolver_memory[i].has_written = true;
				}
			}
		}

		void edit_belt_feed(ik::bone_transform* local)
		{
			const int bones[12] = { rig.rail, rig.optic, rig.belt[0], rig.belt[1], rig.belt[2], rig.belt[3], rig.belt[4], rig.belt[5],
				rig.belt[6], rig.belt[7], rig.belt[8], rig.belt[9] };
			for (int i = 0; i < 12; ++i)
			{
				if (bones[i] < 0)
				{
					continue;
				}
				ik::bone_transform& current = local[bones[i]];
				if (belt_feed_memory[i].has_written && std::memcmp(&current, &belt_feed_memory[i].written, sizeof(current)) == 0)
				{
					current = belt_feed_memory[i].animated;
				}
				else
				{
					belt_feed_memory[i].animated = current;
				}
				belt_feed_memory[i].has_written = false;
			}
			points.has_belt_feed = false;
			belt_feed_drawn = false;
			belt_link_scale = 1.0f;
			if (!belt_feed_request.active || belt_feed_request.revolver || rig.weapon < 0 || rig.rail < 0 || rig.optic < 0 || rig.belt[0] < 0 || rig.belt[4] < 0
				|| rig.parents[rig.rail] != rig.weapon || rig.parents[rig.optic] != rig.weapon || rig.parents[rig.belt[0]] != rig.weapon)
			{
				return;
			}
			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const ik::quat to_xr = ik::conjugate(frame);
			const ik::vec3 origin = tracking_origin(camera, frame);
			const auto to_local = [&](const ik::vec3& model) { return ik::rotate(to_xr, model - origin); };
			// The tray and the belt end at rest, before the belt is moved.
			const ik::vec3 tray = ik::model_of(local, rig.parents, rig.belt[0]).position;
			const ik::vec3 belt_end = ik::model_of(local, rig.parents, rig.belt[4]).position;

			const float angle = std::fmin(std::fmax(belt_feed_request.lid, 0.0f), 1.0f) * belt_feed_request.lid_degrees * 3.14159265f / 180.0f;
			const ik::quat turn{ std::sin(0.5f * angle), 0.0f, 0.0f, std::cos(0.5f * angle) };
			const ik::vec3 hinge = local[rig.rail].position; // in Wep_Root's frame, as the optic's
			local[rig.rail].rotation = ik::normalize(turn * local[rig.rail].rotation);
			local[rig.optic].position = hinge + ik::rotate(turn, local[rig.optic].position - hinge);
			local[rig.optic].rotation = ik::normalize(turn * local[rig.optic].rotation);

			if (belt_feed_request.belt != belt_feed_pose::belt_place::laid)
			{
				ik::bone_transform placed = ik::model_of(local, rig.parents, rig.belt[0]);
				if (belt_feed_request.belt == belt_feed_pose::belt_place::hanging)
				{
					placed.position = belt_end; // the end out of the box mouth, the chain on from there
				}
				else
				{
					const float (&p)[3] = belt_feed_request.belt_position;
					const float (&q)[4] = belt_feed_request.belt_rotation;
					placed.position = origin + ik::rotate(frame, ik::vec3{ p[0], p[1], p[2] });
					placed.rotation = frame * ik::normalize(ik::quat{ q[0], q[1], q[2], q[3] });
				}
				ik::set_model_transform(local, rig.parents, rig.belt[0], placed);
			}

			if (!belt_feed_request.rounds) // #95: an empty belt (the lid up on an empty box): no rounds at the tray or out of the box
			{
				for (const int link : rig.belt)
				{
					if (link >= 0)
					{
						local[link].scale = 0.001f;
					}
				}
			}
			belt_feed_drawn = true;
			belt_link_scale = local[rig.belt[0]].scale;
			const ik::vec3 hinge_model = ik::model_of(local, rig.parents, rig.rail).position;
			const ik::vec3 grab_model = ik::model_of(local, rig.parents, rig.optic).position;
			const ik::vec3 values[4] = { to_local(hinge_model), to_local(grab_model), to_local(tray), to_local(belt_end) };
			float* const outs[4] = { points.lid_hinge, points.lid_grab, points.feed_tray, points.belt_end };
			for (int i = 0; i < 4; ++i)
			{
				outs[i][0] = values[i].x;
				outs[i][1] = values[i].y;
				outs[i][2] = values[i].z;
			}
			points.has_belt_feed = true;
			for (int i = 0; i < 12; ++i)
			{
				if (bones[i] >= 0)
				{
					belt_feed_memory[i].written = local[bones[i]];
					belt_feed_memory[i].has_written = true;
				}
			}
		}

		// Called with rig_lock held, after edit_weapon: the M320 by hand (#64). Turns the barrel open about its front
		// pivot (the back end to the gun's left, as the game's own reload does it), places the round and the spent case,
		// and publishes the barrel's mouth and left side for weapon/physical_reload.
		void edit_launcher(ik::bone_transform* local)
		{
			const int bones[4] = { rig.launcher_round, rig.launcher_case, rig.launcher_parts[0], rig.launcher_parts[1] };
			for (int i = 0; i < 4; ++i)
			{
				if (bones[i] < 0)
				{
					continue;
				}
				ik::bone_transform& current = local[bones[i]];
				if (launcher_memory[i].has_written && std::memcmp(&current, &launcher_memory[i].written, sizeof(current)) == 0)
				{
					current = launcher_memory[i].animated;
				}
				else
				{
					launcher_memory[i].animated = current;
				}
				launcher_memory[i].has_written = false;
			}
			points.has_launcher = false;
			if (!launcher_request.active || rig.magazine < 0 || rig.weapon < 0 || rig.launcher_round < 0 || rig.launcher_case < 0)
			{
				return;
			}
			const float angle = std::fmin(std::fmax(launcher_request.open, 0.0f), 1.0f) * launcher_open_radians;
			// About the parent's up axis: the game's reload turns Wep_Mag by (0, -0.131, 0, 0.991) fully open.
			const ik::quat turn{ 0.0f, -std::sin(0.5f * angle), 0.0f, std::cos(0.5f * angle) };
			local[rig.magazine].rotation = ik::normalize(turn * local[rig.magazine].rotation);
			magazine_memory.written = local[rig.magazine];
			magazine_memory.has_written = true;
			for (int part = 0; part < 2; ++part)
			{
				if (rig.launcher_parts[part] >= 0)
				{
					local[rig.launcher_parts[part]].rotation = ik::normalize(turn * local[rig.launcher_parts[part]].rotation);
				}
			}

			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const ik::quat to_xr = ik::conjugate(frame);
			const ik::vec3 origin = tracking_origin(camera, frame);
			// The mouth: where the round sits in the (turned) barrel; the side: 5 cm to the gun's left of it, 10 cm on.
			const ik::vec3 mouth = ik::model_of(local, rig.parents, rig.launcher_round).position;
			const ik::quat root = ik::model_of(local, rig.parents, rig.weapon).rotation;
			const ik::vec3 side = mouth + ik::rotate(root, ik::vec3{ 0.05f, 0.0f, 0.10f });
			const ik::vec3 mouth_xr = ik::rotate(to_xr, mouth - origin);
			const ik::vec3 side_xr = ik::rotate(to_xr, side - origin);
			points.has_launcher = true;
			points.barrel_mouth[0] = mouth_xr.x;
			points.barrel_mouth[1] = mouth_xr.y;
			points.barrel_mouth[2] = mouth_xr.z;
			points.barrel_side[0] = side_xr.x;
			points.barrel_side[1] = side_xr.y;
			points.barrel_side[2] = side_xr.z;

			const auto place = [&](int bone, launcher_pose::place where, const float (&p)[3], const float (&q)[4])
			{
				if (where == launcher_pose::place::hidden)
				{
					local[bone].scale = 0.001f;
				}
				else if (where == launcher_pose::place::at_pose)
				{
					ik::bone_transform placed = ik::model_of(local, rig.parents, bone);
					placed.position = origin + ik::rotate(frame, ik::vec3{ p[0], p[1], p[2] });
					placed.rotation = frame * ik::normalize(ik::quat{ q[0], q[1], q[2], q[3] });
					ik::set_model_transform(local, rig.parents, bone, placed);
				}
			};
			place(rig.launcher_round, launcher_request.round, launcher_request.round_position, launcher_request.round_rotation);
			place(rig.launcher_case, launcher_request.spent_case, launcher_request.case_position, launcher_request.case_rotation);
			for (int i = 0; i < 4; ++i)
			{
				if (bones[i] >= 0)
				{
					launcher_memory[i].written = local[bones[i]];
					launcher_memory[i].has_written = true;
				}
			}
		}

		// Called with rig_lock held, after edit_arms: publishes the magazine and slide in OpenXR LOCAL
		// and draws the magazine where physical_reload wants it (in the hand, falling, hidden).
		void edit_weapon(ik::bone_transform* local)
		{
			if (rig.magazine < 0 || rig.weapon < 0)
			{
				points.valid = false;
				return;
			}
			ik::bone_transform& current = local[rig.magazine];
			if (magazine_memory.has_written && std::memcmp(&current, &magazine_memory.written, sizeof(current)) == 0)
			{
				current = magazine_memory.animated;
			}
			else
			{
				magazine_memory.animated = current;
			}
			magazine_memory.has_written = false;
			if (rig.slide >= 0)
			{
				ik::bone_transform& slide = local[rig.slide];
				if (slide_memory.has_written && std::memcmp(&slide, &slide_memory.written, sizeof(slide)) == 0)
				{
					slide = slide_memory.animated;
				}
				else
				{
					slide_memory.animated = slide;
				}
				slide_memory.has_written = false;
				pump_animated_z = slide.position.z;
				pump_animated_rotation = slide.rotation;
				const unsigned key = pump_rest_key.load(std::memory_order_relaxed);
				if (pump_rest_on.load(std::memory_order_relaxed) && key != 0)
				{
					if (!pump_rest_held || key != last_pump_key)
					{
						pump_hidden_frames = 0;
						pump_hidden_mm = 0.0f;
					}
					const ULONGLONG now = GetTickCount64();
					const float seconds = pump_rest_held && now >= pump_last_ms ? static_cast<float>(now - pump_last_ms) * 0.001f : 0.0f;
					pump_last_ms = now;
					pump_rest.hold(key, std::fmin(seconds, 0.1f), slide);
					last_pump_key = key;
					const float hidden_mm = (slide.position.z - pump_animated_z) * 1000.0f;
					if (hidden_mm > 5.0f)
					{
						++pump_hidden_frames;
						pump_hidden_mm = std::fmax(pump_hidden_mm, hidden_mm);
					}
					pump_rest_held = true;
				}
				else
				{
					pump_rest.reset();
					pump_rest_held = false;
				}
				if (bolt_pose_on.load(std::memory_order_relaxed))
				{
					const float turn = bolt_pose_turn.load(std::memory_order_relaxed);
					const ik::quat up{ 0.0f, 0.0f, -std::sin(0.5f * turn), std::cos(0.5f * turn) }; // about -z through the bone's origin
					slide.rotation = ik::normalize(up * slide.rotation);
					slide.position.z -= bolt_pose_pull.load(std::memory_order_relaxed);
					slide_memory.written = slide; // the game's pose comes back next frame, the turn is never applied twice
					slide_memory.has_written = true;
				}
				pump_drawn_z = slide.position.z;
				if (pump_rest.valid())
				{
					const auto turned = [&](const ik::quat& q)
					{
						const ik::quat& r = pump_rest.rest().rotation;
						return 2.0f * std::acos(std::fmin(1.0f, std::fabs(q.x * r.x + q.y * r.y + q.z * r.z + q.w * r.w))) * 57.29578f;
					};
					pump_animated_turn = turned(pump_animated_rotation);
					pump_drawn_turn = turned(slide.rotation);
				}
			}

			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const ik::quat to_xr = ik::conjugate(frame);
			const ik::vec3 origin = tracking_origin(camera, frame);
			const ik::bone_transform magazine = ik::model_of(local, rig.parents, rig.magazine);
			const ik::vec3 magazine_xr = ik::rotate(to_xr, magazine.position - origin);
			const ik::quat magazine_rotation_xr = to_xr * magazine.rotation;
			points.valid = true;
			points.at = GetTickCount64();
			points.world_origin[0] = origin.x;
			points.world_origin[1] = origin.y;
			points.world_origin[2] = origin.z;
			points.world_to_local[0] = to_xr.x;
			points.world_to_local[1] = to_xr.y;
			points.world_to_local[2] = to_xr.z;
			points.world_to_local[3] = to_xr.w;
			points.has_floor = rig.ground >= 0;
			if (points.has_floor)
			{
				points.floor_y = ik::rotate(to_xr, ik::model_of(local, rig.parents, rig.ground).position - origin).y;
			}
			points.magazine[0] = magazine_xr.x;
			points.magazine[1] = magazine_xr.y;
			points.magazine[2] = magazine_xr.z;
			points.magazine_rotation[0] = magazine_rotation_xr.x;
			points.magazine_rotation[1] = magazine_rotation_xr.y;
			points.magazine_rotation[2] = magazine_rotation_xr.z;
			points.magazine_rotation[3] = magazine_rotation_xr.w;
			points.has_slide = rig.slide >= 0;
			if (points.has_slide)
			{
				const ik::vec3 slide = ik::rotate(to_xr, ik::model_of(local, rig.parents, rig.slide).position - origin);
				points.slide[0] = slide.x;
				points.slide[1] = slide.y;
				points.slide[2] = slide.z;
			}
			const auto publish = [&](int bone, bool& has, float (&out)[3])
			{
				has = bone >= 0;
				if (has)
				{
					const ik::vec3 p = ik::rotate(to_xr, ik::model_of(local, rig.parents, bone).position - origin);
					out[0] = p.x;
					out[1] = p.y;
					out[2] = p.z;
				}
			};
			publish(rig.optic, points.has_optic, points.optic);
			publish(rig.weapon, points.has_root, points.root);
			points.has_held_root = has_held_root;
			if (has_held_root)
			{
				const ik::vec3 at = ik::rotate(to_xr, held_root.position - origin);
				const ik::quat turned = to_xr * held_root.rotation;
				points.held_root[0] = at.x;
				points.held_root[1] = at.y;
				points.held_root[2] = at.z;
				points.held_root_rotation[0] = turned.x;
				points.held_root_rotation[1] = turned.y;
				points.held_root_rotation[2] = turned.z;
				points.held_root_rotation[3] = turned.w;
			}
			if (points.has_root)
			{
				const ik::quat root_xr = to_xr * ik::model_of(local, rig.parents, rig.weapon).rotation;
				points.root_rotation[0] = root_xr.x;
				points.root_rotation[1] = root_xr.y;
				points.root_rotation[2] = root_xr.z;
				points.root_rotation[3] = root_xr.w;
				// Guns only: every first-person rig has a Wep_Scope1 (grenades, the knife, the C4, whose
				// left trigger throws it).
				points.has_sight = gun_in_hand.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed)
					&& (geometry.has_ads_sight || rig.optic >= 0 || geometry.has_sight);
				if (points.has_sight)
				{
					const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
					// Until the game's own ADS was seen for this gun: the barrel line from where both
					// measured guns have their eye point from the optic bone (the hip pose's camera is
					// 7.6 cm off to the side, and moves with the aim pitch).
					ik::vec3 sight_eye = geometry.ads_eye;
					if (!geometry.has_ads_sight)
					{
						sight_eye = rig.optic >= 0
							? ik::rotate(ik::conjugate(gun.rotation), ik::model_of(local, rig.parents, rig.optic).position - gun.position) + default_eye_from_optic
							: geometry.sight_eye;
					}
					const ik::vec3 sight_direction = geometry.has_ads_sight ? geometry.ads_direction : ik::vec3{ 0, 0, 1 };
					const ik::vec3 eye = ik::rotate(to_xr, gun.position + ik::rotate(gun.rotation, sight_eye) - origin);
					points.sight_eye[0] = eye.x;
					points.sight_eye[1] = eye.y;
					points.sight_eye[2] = eye.z;
					points.sight_direction[0] = sight_direction.x;
					points.sight_direction[1] = sight_direction.y;
					points.sight_direction[2] = sight_direction.z;
					points.sight_from_ads = geometry.has_ads_sight;
				}
			}
			publish(rig.rail, points.has_rail, points.rail);
			points.has_support = geometry.valid;
			if (points.has_support)
			{
				const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
				const ik::vec3 support = ik::rotate(to_xr, gun.position + ik::rotate(gun.rotation, geometry.hand[0].position) - origin);
				points.support[0] = support.x;
				points.support[1] = support.y;
				points.support[2] = support.z;
				// Where the gun's animation holds the gun hand (the pistol grip): the gun hand takes it back there.
				const ik::vec3 grip = ik::rotate(to_xr, gun.position + ik::rotate(gun.rotation, geometry.hand[1].position) - origin);
				points.grip[0] = grip.x;
				points.grip[1] = grip.y;
				points.grip[2] = grip.z;
			}
			for (int hand = 0; hand < 2; ++hand)
			{
				const arm_report& arm = last_arms[hand];
				points.hand_valid[hand] = arm.solved;
				points.hand[hand][0] = arm.goal_xr.x;
				points.hand[hand][1] = arm.goal_xr.y;
				points.hand[hand][2] = arm.goal_xr.z;
				points.palm[hand][0] = arm.palm_xr.x;
				points.palm[hand][1] = arm.palm_xr.y;
				points.palm[hand][2] = arm.palm_xr.z;
				points.hand_rotation[hand][0] = arm.aim_xr.x;
				points.hand_rotation[hand][1] = arm.aim_xr.y;
				points.hand_rotation[hand][2] = arm.aim_xr.z;
				points.hand_rotation[hand][3] = arm.aim_xr.w;
			}

			// Racking: the bolt / charging handle comes back with the hand (Wep_Slide hangs from Wep_Root,
			// so its local -z is back along the barrel). Published above at rest, where the grab is.
			if (rig.slide >= 0 && handle_request.pull > 0.0f)
			{
				local[rig.slide].position.z -= handle_request.pull * handle_request.weight;
				slide_memory.written = local[rig.slide];
				slide_memory.has_written = true;
				pump_drawn_z = local[rig.slide].position.z;
			}

			if (magazine_request.detached && !magazine_request.in_hand)
			{
				place_requested_magazine(local);
			}
			// in_hand: placed after the fingers (attach_held_magazine)
		}

		// What a hand is doing, for its finger shape. An arm the IK left alone keeps the animation's hold.
		fingers::task task_of(int hand, ULONGLONG now)
		{
			if (!last_arms[hand].solved)
			{
				return fingers::task::item;
			}
			const bool off_carries = gun_in_off_hand.load(std::memory_order_relaxed);
			if (hand == 1 && off_carries && support_weight.load(std::memory_order_relaxed) >= 0.5f)
			{
				return fingers::task::support; // #38: on the handguard while the left hand holds the grip
			}
			if (hand == 1 && off_carries)
			{
				// #38: the free gun hand reloads: racking (choose_rack_grip picks pinch, hook or underhand), a
				// magazine in its fist, reaching, otherwise open.
				const auto task = static_cast<fingers::task>(off_task.load(std::memory_order_relaxed));
				const bool fresh = now - off_task_at.load(std::memory_order_relaxed) < task_max_age_ms;
				return fresh && (task == fingers::task::handle || task == fingers::task::pinch || task == fingers::task::underhand) ? fingers::task::handle
					: fresh && (task == fingers::task::magazine || task == fingers::task::cup) ? fingers::task::magazine
					: fresh && task == fingers::task::reach ? fingers::task::reach : fingers::task::free;
			}
			if (hand == 1 && gun_hand_fist.load(std::memory_order_relaxed))
			{
				return fingers::task::magazine; // a grenade in the free gun hand (#38): the fist
			}
			if (hand == 1 && gun_on_floor_now.load(std::memory_order_relaxed))
			{
				return fingers::task::free; // the gun on the floor or the sling: the hand is empty
			}
			if (hand == 1)
			{
				return gun_in_hand.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed) ? fingers::task::gun_grip
					: fingers::task::item;
			}
			if (off_carries)
			{
				return off_by_grip.load(std::memory_order_relaxed) ? fingers::task::gun_grip : fingers::task::support;
			}
			if (support_weight.load(std::memory_order_relaxed) >= 0.5f)
			{
				return fingers::task::support;
			}
			return now - off_task_at.load(std::memory_order_relaxed) < task_max_age_ms ? static_cast<fingers::task>(off_task.load(std::memory_order_relaxed))
				: fingers::task::free;
		}

		// The off hand's magazine standing on its palm: hand bone +y is the palm, the knuckles (Finger1)
		// give the finger direction. Also how far the palm faces up (palm normal . OpenXR up).
		contact::cup_placement place_cup(const ik::bone_transform* local)
		{
			const int hand_bone = rig.arms[0].hand;
			const auto& bones = rig.finger_bones[0];
			if (hand_bone < 0 || rig.camera < 0 || bones[fingers::index][0] <= 0 || bones[fingers::pinky][0] <= 0)
			{
				return {};
			}
			const ik::bone_transform hand = ik::model_of(local, rig.parents, hand_bone);
			ik::vec3 knuckles;
			int counted = 0;
			for (const int finger : { fingers::index, fingers::middle, fingers::ring, fingers::pinky })
			{
				if (bones[finger][0] > 0)
				{
					knuckles = knuckles + ik::model_of(local, rig.parents, bones[finger][0]).position;
					++counted;
				}
			}
			knuckles = knuckles * (1.0f / static_cast<float>(counted));
			const ik::vec3 normal = ik::rotate(hand.rotation, ik::vec3{ 0.0f, 1.0f, 0.0f });
			const ik::vec3 up = ik::rotate(camera_frame(ik::model_of(local, rig.parents, rig.camera)), ik::vec3{ 0.0f, 1.0f, 0.0f });
			cup_palm_up = ik::dot(normal, up);
			// The middle of the palm: a little over halfway from the wrist to the knuckles.
			const ik::vec3 palm = hand.position + (knuckles - hand.position) * 0.55f;
			return contact::cup_magazine(palm, normal, knuckles - hand.position, magazine_body, hand_half_thickness);
		}

		// Closes one off-hand finger from `from` until a joint or its tip touches the shape (curl up to 1);
		// gap_mm: its nearest point's distance to the surface then, less a finger's radius.
		float close_on(ik::bone_transform* local, int finger, const contact::box& shape, float from, float fallback, float& gap_mm)
		{
			const int (&joints)[fingers::joints] = rig.finger_bones[0][finger];
			const int tip = rig.finger_tips[0][finger];
			if (tip <= 0 || joints[0] <= 0)
			{
				gap_mm = 0.0f;
				return fallback;
			}
			ik::quat animated[fingers::joints];
			for (int joint = 0; joint < fingers::joints; ++joint)
			{
				animated[joint] = joints[joint] > 0 ? local[joints[joint]].rotation : ik::quat{};
			}
			fingers::hand_shape probe;
			probe.animated[finger] = 0.0f;
			float nearest = 0.0f;
			const auto touching = [&](float curl) {
				probe.curl[finger] = curl;
				for (int joint = 0; joint < fingers::joints; ++joint)
				{
					if (joints[joint] > 0)
					{
						local[joints[joint]].rotation = fingers::shaped(animated[joint], static_cast<fingers::finger>(finger), joint, probe);
					}
				}
				nearest = 1e9f;
				for (const int point : { joints[1], joints[2], tip })
				{
					if (point > 0)
					{
						nearest = std::fmin(nearest, contact::signed_distance(shape, ik::model_of(local, rig.parents, point).position));
					}
				}
				return nearest <= finger_radius;
			};
			const float curl = contact::close_until(touching, from, 1.0f);
			touching(curl); // for the gap report
			gap_mm = (nearest - finger_radius) * 1000.0f;
			for (int joint = 0; joint < fingers::joints; ++joint)
			{
				if (joints[joint] > 0)
				{
					local[joints[joint]].rotation = animated[joint];
				}
			}
			return curl;
		}

		// The off hand's grip on a held magazine: wrapped the way the game's own reload holds it (an
		// authored grip per variant) or cupped (contact), by the setting or the palm facing up. Rewrites
		// the task and its target shape for a cup.
		// #40, palming: the game's grip holds one upright magazine; leaning aside (and with the caught one
		// beside it) its fingers floated off or went through. The magazine stays where that grip puts it
		// (attach_held_magazine), the fingers close by contact on the bodies in the hand.
		bool close_on_palmed(ik::bone_transform* local, fingers::task& task, fingers::hand_shape& target)
		{
			if (rig.arms[0].hand < 0 || rig.magazine < 0 || rig.weapon < 0)
			{
				return false;
			}
			const authored_grip& grip = magazine_request.ak ? magazine_grip_ak : magazine_grip_ar;
			const ik::bone_transform hand = ik::model_of(local, rig.parents, rig.arms[0].hand);
			const ik::quat seated = ik::conjugate(ik::model_of(local, rig.parents, rig.weapon).rotation) * ik::model_of(local, rig.parents, rig.magazine).rotation;
			ik::vec3 top = hand.position + ik::rotate(hand.rotation, grip.magazine);
			ik::quat rotation = ik::normalize(hand.rotation * grip.magazine_rotation);
			contact::tilt_magazine(top, rotation, seated, magazine_request.tilt, magazine_request.tilt_pivot);
			const contact::box held = contact::magazine_box(top, rotation, seated, magazine_body);
			const bool has_caught = extra_request.where == extra_magazine_pose::kind::in_fist && extra_meshes_on.load(std::memory_order_relaxed);
			contact::box caught{};
			if (has_caught)
			{
				ik::vec3 caught_top = top;
				contact::beside_magazine(caught_top, rotation, seated, magazine_body, caught_gap);
				caught = contact::magazine_box(caught_top, rotation, seated, magazine_body);
			}
			task = fingers::task::cup;
			target = fingers::shape_for(task, {});
			bool touched[fingers::count]{};
			for (int finger = 0; finger < fingers::count; ++finger)
			{
				const float open = target.curl[finger];
				float gap = 0.0f;
				float curl = close_on(local, finger, held, 0.0f, open, gap);
				if (has_caught)
				{
					float caught_gap_mm = 0.0f;
					const float on_caught = close_on(local, finger, caught, 0.0f, open, caught_gap_mm);
					if (on_caught < curl)
					{
						curl = on_caught;
						gap = caught_gap_mm;
					}
				}
				target.curl[finger] = curl;
				cup_gap_mm[finger] = gap;
				touched[finger] = gap <= 0.5f;
			}
			contact::couple_free_fingers(target.curl, touched);
			return true;
		}

		void choose_magazine_grip(ik::bone_transform* local, fingers::task& task, fingers::hand_shape& target)
		{
			const bool holding = task == fingers::task::magazine && magazine_request.detached && magazine_request.in_hand;
			cup = holding ? place_cup(local) : contact::cup_placement{};
			const int mode = magazine_grip.load(std::memory_order_relaxed);
			cupped = cup.valid && (mode == 2 || (mode == 0 && contact::cup_grip(cupped, cup_palm_up)));
			palm_contact = false;
			if (!cupped)
			{
				if (holding && rig.thumb_base[0] > 0 && magazine_request.tilt > palm_contact_tilt && close_on_palmed(local, task, target))
				{
					palm_contact = true;
				}
				else if (holding && rig.thumb_base[0] > 0)
				{
					authored = magazine_request.ak ? &magazine_grip_ak : &magazine_grip_ar;
					authored_hand = 0;
				}
				else if (!magazine_request.detached && snap_request.weight >= 0.5f && rig.thumb_base[0] > 0)
				{
					// #40: snapping onto the seated magazine, the fingers already take its grip.
					authored = snap_request.ak ? &magazine_grip_ak : &magazine_grip_ar;
					authored_hand = 0;
				}
				else if (lock_request.weight >= 0.5f && rig.thumb_base[0] > 0)
				{
					// Grip lock: on the rail, sliding home or held on the seated magazine, the game's grip.
					authored = lock_request.ak ? &magazine_grip_ak : &magazine_grip_ar;
					authored_hand = 0;
				}
				return;
			}
			task = fingers::task::cup;
			target = fingers::shape_for(task, {});
			bool touched[fingers::count]{};
			for (int finger = 0; finger < fingers::count; ++finger)
			{
				target.curl[finger] = close_on(local, finger, cup.body, 0.0f, target.curl[finger], cup_gap_mm[finger]);
				touched[finger] = cup_gap_mm[finger] <= 0.5f;
			}
			contact::couple_free_fingers(target.curl, touched);
		}

		// The knob of the charging handle where the racking hand holds it (moved back by the pull), model space.
		contact::box handle_knob(const ik::bone_transform* local)
		{
			const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
			const ik::vec3 on_handle = gun.position + ik::rotate(gun.rotation, handle_request.handle - ik::vec3{ 0.0f, 0.0f, handle_request.pull });
			return { on_handle, gun.rotation, { knob_half, knob_half, knob_half } };
		}

		// The hand that racks: the off hand, or the free gun hand while the off hand carries the gun (#38).
		int racking_hand()
		{
			return gun_in_off_hand.load(std::memory_order_relaxed) ? 1 : 0;
		}

		// The racking hand's palm normal (hand bone +y, both hands) against the gun's up (Wep_Root +y): 1 = palm up.
		float palm_up_on_gun(const ik::bone_transform* local, int hand)
		{
			if (rig.arms[hand].hand < 0 || rig.weapon < 0)
			{
				return std::nanf("");
			}
			const ik::vec3 palm = ik::rotate(ik::model_of(local, rig.parents, rig.arms[hand].hand).rotation, ik::vec3{ 0.0f, 1.0f, 0.0f });
			const ik::vec3 up = ik::rotate(ik::model_of(local, rig.parents, rig.weapon).rotation, ik::vec3{ 0.0f, 1.0f, 0.0f });
			return ik::dot(palm, up);
		}

		// Racking: the handle task becomes the game's own pinch (pinch_grip, eased in after the shaping)
		// for rack_grip = pinch and auto overhand; the curled thumb alone cannot reach the knob, the pinch
		// needs Thumb1's swing across the palm, which only the authored pose has. Auto underhand: the knob
		// hooked in the index and middle. Auto decides as the stroke starts and keeps it to the release.
		void choose_rack_grip(const ik::bone_transform* local, int hand, fingers::task& task, fingers::hand_shape& target)
		{
			const int mode = rack_grip.load(std::memory_order_relaxed);
			const bool on_handle = task == fingers::task::handle && rig.weapon >= 0 && handle_request.weight > 0.0f;
			if (!on_handle)
			{
				rack_stroke = false;
			}
			else if (!rack_stroke)
			{
				rack_stroke = true;
				rack_palm_up = palm_up_on_gun(local, hand);
				rack_underhand = contact::underhand_rack(rack_underhand, rack_palm_up);
			}
			fisting = on_handle && mode == 3;
			if (fisting)
			{
				hooking_under = false;
				pinching = false;
				task = handle_request.underhand ? fingers::task::fist_under : fingers::task::fist_over;
				target = fingers::shape_for(task, {});
				return;
			}
			hooking_under = on_handle && mode == 2 && rack_underhand;
			// The pinch is the left hand's authored pose, mirrored onto the right hand when it racks (#38).
			pinching = on_handle && (mode == 0 || (mode == 2 && !rack_underhand)) && rig.thumb_base[hand] > 0;
			if (hooking_under)
			{
				task = fingers::task::underhand;
				target = fingers::shape_for(task, {});
			}
			else if (pinching)
			{
				task = fingers::task::pinch;
				target = fingers::shape_for(task, {});
				authored = &pinch_grip;
				authored_hand = hand;
			}
		}

		// A hand's joint for authored_grip::joints[finger][joint]: Thumb1-3, else <Finger>1-3.
		int authored_bone(int hand, int finger, int joint)
		{
			if (finger == fingers::thumb)
			{
				return joint == 0 ? rig.thumb_base[hand] : rig.finger_bones[hand][fingers::thumb][joint - 1];
			}
			return rig.finger_bones[hand][finger][joint];
		}

		// Eases a hand's fingers into this frame's authored grip (over ~50 ms) and out of it. The grips are the
		// left hand's; on the right hand each local rotation is mirrored (the bone axes mirror in z,
		// fingers::mirrored_hand).
		void apply_authored(ik::bone_transform* local, float seconds)
		{
			if (authored && (authored != authored_shown || authored_hand != authored_hand_shown))
			{
				authored_weight = authored_shown ? 0.0f : authored_weight; // a different grip starts from the shaped fingers
				authored_shown = authored;
				authored_hand_shown = authored_hand;
			}
			authored_weight += ((authored ? 1.0f : 0.0f) - authored_weight) * (1.0f - std::exp(-seconds / 0.05f));
			if (!authored_shown || authored_weight < 0.001f)
			{
				authored_shown = authored;
				authored_hand_shown = authored_hand;
				return;
			}
			const int hand = authored_hand_shown;
			for (int finger = 0; finger < fingers::count; ++finger)
			{
				for (int joint = 0; joint < 3; ++joint)
				{
					const int bone = authored_bone(hand, finger, joint);
					if (bone <= 0)
					{
						continue;
					}
					const ik::quat& grip = authored_shown->joints[finger][joint];
					const ik::quat target = hand == 0 ? grip : ik::quat{ -grip.x, -grip.y, grip.z, grip.w };
					local[bone].rotation = nlerp(local[bone].rotation, target, authored_weight);
					bone_memory& remembered = finger == fingers::thumb && joint == 0 ? thumb_base_memory[hand]
						: finger_memory[hand][finger][finger == fingers::thumb ? joint - 1 : joint];
					remembered.written = local[bone];
					remembered.has_written = true;
				}
			}
		}

		// Pose editor: the hand held in the previewed grip, as the real one would shape it (contact grips
		// without their closing).
		void preview_into(int hand, fingers::task& task, fingers::hand_shape& target)
		{
			const poses::grip which = static_cast<poses::grip>(preview_grip.load(std::memory_order_relaxed));
			if (which == poses::grip::none)
			{
				return;
			}
			if (authored && authored_hand == hand)
			{
				authored = nullptr;
			}
			switch (which)
			{
			case poses::grip::relaxed: task = fingers::task::free; break;
			case poses::grip::reach: task = fingers::task::reach; break;
			case poses::grip::fist: task = fingers::task::magazine; break;
			case poses::grip::cup:
			case poses::grip::palm: task = fingers::task::cup; break;
			case poses::grip::hook: task = fingers::task::handle; break;
			case poses::grip::rack_over: task = fingers::task::fist_over; break;
			case poses::grip::rack_under: task = fingers::task::fist_under; break;
			case poses::grip::underhand: task = fingers::task::underhand; break;
			case poses::grip::pinch:
			case poses::grip::magazine_ak:
			case poses::grip::magazine_ar:
				task = which == poses::grip::pinch ? fingers::task::pinch : fingers::task::magazine;
				if (rig.thumb_base[hand] > 0)
				{
					authored = which == poses::grip::pinch ? &pinch_grip : which == poses::grip::magazine_ak ? &magazine_grip_ak : &magazine_grip_ar;
					authored_hand = hand;
				}
				break;
			default: return;
			}
			target = fingers::shape_for(task, last_input[hand]);
		}

		// The grip a hand shows now, for its tweak: the game's captured grip while it is eased in, else the task's.
		poses::grip grip_shown(int hand)
		{
			if (hand == preview_hand.load(std::memory_order_relaxed) && preview_grip.load(std::memory_order_relaxed) != static_cast<int>(poses::grip::none))
			{
				return static_cast<poses::grip>(preview_grip.load(std::memory_order_relaxed));
			}
			if (authored_shown && authored_hand_shown == hand && authored_weight >= 0.5f)
			{
				return authored_shown == &pinch_grip ? poses::grip::pinch : authored_shown == &magazine_grip_ak ? poses::grip::magazine_ak : poses::grip::magazine_ar;
			}
			switch (last_task[hand])
			{
			case fingers::task::free: return poses::grip::relaxed;
			case fingers::task::reach: return poses::grip::reach;
			case fingers::task::magazine: return poses::grip::fist;
			case fingers::task::cup: return hand == 0 && palm_contact ? poses::grip::palm : poses::grip::cup;
			case fingers::task::handle: return poses::grip::hook;
			case fingers::task::pinch: return poses::grip::pinch;
			case fingers::task::underhand: return poses::grip::underhand;
			case fingers::task::fist_over: return poses::grip::rack_over;
			case fingers::task::fist_under: return poses::grip::rack_under;
			default: return poses::grip::none; // the gun's own grips and items: as the game animates them
			}
		}

		// The pose editor's tweak for each hand's grip, on top of everything drawn so far, eased between grips.
		void apply_tweaks(ik::bone_transform* local, float seconds)
		{
			const poses::table book = pose_book::current();
			for (int hand = 0; hand < 2; ++hand)
			{
				shown_grip[hand] = grip_shown(hand);
				poses::ease(shown_tweak[hand], shown_grip[hand] == poses::grip::none ? poses::tweak{} : book[static_cast<int>(shown_grip[hand])], seconds);
				const poses::tweak& value = shown_tweak[hand];
				if (value.empty())
				{
					continue;
				}
				const bool right = hand == 1;
				for (int finger = 0; finger < fingers::count; ++finger)
				{
					for (int joint = 0; joint < fingers::joints; ++joint)
					{
						const int bone = rig.finger_bones[hand][finger][joint];
						if (bone <= 0)
						{
							continue;
						}
						local[bone].rotation = poses::tweaked(local[bone].rotation, static_cast<fingers::finger>(finger), joint, value, right);
						finger_memory[hand][finger][joint].written = local[bone];
						finger_memory[hand][finger][joint].has_written = true;
					}
				}
				const int base = rig.thumb_base[hand];
				if (base > 0 && (value.thumb_across != 0.0f || value.thumb_out != 0.0f))
				{
					local[base].rotation = poses::tweaked_thumb_base(local[base].rotation, value, right);
					thumb_base_memory[hand].written = local[base];
					thumb_base_memory[hand].has_written = true;
				}
			}
		}

		// Called with rig_lock held, after edit_arms: shapes the fingers (finger_poses), eased between tasks.
		void edit_fingers(ik::bone_transform* local)
		{
			// As for the arms: a joint the animation did not rewrite since our last write gets its animated value back.
			for (int hand = 0; hand < 2; ++hand)
			{
				for (int finger = 0; finger < fingers::count; ++finger)
				{
					for (int joint = 0; joint < fingers::joints; ++joint)
					{
						const int bone = rig.finger_bones[hand][finger][joint];
						if (bone <= 0)
						{
							continue;
						}
						bone_memory& remembered = finger_memory[hand][finger][joint];
						if (remembered.has_written && std::memcmp(&local[bone], &remembered.written, sizeof(local[bone])) == 0)
						{
							local[bone] = remembered.animated;
						}
						else
						{
							remembered.animated = local[bone];
						}
						remembered.has_written = false;
					}
				}
			}
			for (int hand = 0; hand < 2; ++hand)
			{
				const int base = rig.thumb_base[hand];
				if (base <= 0)
				{
					continue;
				}
				bone_memory& remembered = thumb_base_memory[hand];
				if (remembered.has_written && std::memcmp(&local[base], &remembered.written, sizeof(local[base])) == 0)
				{
					local[base] = remembered.animated;
				}
				else
				{
					remembered.animated = local[base];
				}
				remembered.has_written = false;
			}
			if (!finger_poses.load(std::memory_order_relaxed))
			{
				has_hook = false; // racking falls back to the palm
				has_fist = false;
				fisting = false;
				has_pinch = false;
				pinching = false;
				has_under_hook = false;
				hooking_under = false;
				rack_stroke = false;
				cupped = false;
				authored = authored_shown = nullptr;
				authored_weight = 0.0f;
				return;
			}
			LARGE_INTEGER counter{}, frequency{};
			QueryPerformanceCounter(&counter);
			QueryPerformanceFrequency(&frequency);
			const float seconds = last_finger_update.QuadPart
				? std::fmin(static_cast<float>(counter.QuadPart - last_finger_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
			last_finger_update = counter;
			last_finger_seconds = seconds;
			input::latest_fingers(last_input);
			const ULONGLONG now = GetTickCount64();
			for (int hand = 0; hand < 2; ++hand)
			{
				fingers::task task = task_of(hand, now);
				fingers::hand_shape target = fingers::shape_for(task, last_input[hand]);
				const int rack = racking_hand();
				if (hand == 0)
				{
					authored = nullptr;
					if (rack == 0)
					{
						choose_magazine_grip(local, task, target);
					}
					else
					{
						cup = {}; // the free gun hand holds a magazine in its fist (#38), never cupped
						cupped = false;
						palm_contact = false;
					}
				}
				if (hand == rack)
				{
					choose_rack_grip(local, hand, task, target);
				}
				if (hand == preview_hand.load(std::memory_order_relaxed))
				{
					preview_into(hand, task, target);
				}
				last_task[hand] = task;
				const fingers::hand_shape& shape = finger_shapes[hand].update(target, seconds);
				for (int finger = 0; finger < fingers::count; ++finger)
				{
					for (int joint = 0; joint < fingers::joints; ++joint)
					{
						const int bone = rig.finger_bones[hand][finger][joint];
						if (bone <= 0)
						{
							continue;
						}
						local[bone].rotation = fingers::shaped(local[bone].rotation, static_cast<fingers::finger>(finger), joint, shape);
						if (finger == fingers::index)
						{
							last_index_bend[hand][joint] = fingers::bend_of(local[bone].rotation);
						}
						finger_memory[hand][finger][joint].written = local[bone];
						finger_memory[hand][finger][joint].has_written = true;
					}
				}
			}
			apply_authored(local, seconds);
			apply_tweaks(local, seconds);
			// The inside of the racking hand's index curl, for the next racking hold.
			const int rack = racking_hand();
			hook_hand = rack;
			const int (&index_joints)[fingers::joints] = rig.finger_bones[rack][fingers::index];
			has_hook = rig.arms[rack].hand >= 0 && index_joints[0] > 0 && index_joints[1] > 0 && index_joints[2] > 0;
			if (has_hook)
			{
				const ik::bone_transform hand = ik::model_of(local, rig.parents, rig.arms[rack].hand);
				const ik::vec3 curl = (ik::model_of(local, rig.parents, index_joints[0]).position + ik::model_of(local, rig.parents, index_joints[1]).position
					+ ik::model_of(local, rig.parents, index_joints[2]).position) * (1.0f / 3.0f);
				hook_in_hand = ik::rotate(ik::conjugate(hand.rotation), curl - hand.position);
				// Underhand: inside both hooked fingers (Index1-3 and Middle1-3).
				const int (&middle_joints)[fingers::joints] = rig.finger_bones[rack][fingers::middle];
				has_under_hook = middle_joints[0] > 0 && middle_joints[1] > 0 && middle_joints[2] > 0;
				if (has_under_hook)
				{
					const ik::vec3 middle = (ik::model_of(local, rig.parents, middle_joints[0]).position
						+ ik::model_of(local, rig.parents, middle_joints[1]).position + ik::model_of(local, rig.parents, middle_joints[2]).position) * (1.0f / 3.0f);
					under_hook_in_hand = ik::rotate(ik::conjugate(hand.rotation), (curl + middle) * 0.5f - hand.position);
				}
				// The fist: the knob at its grasp centre (between the curled middle finger and the thumb).
				const int (&fist_middle)[fingers::joints] = rig.finger_bones[rack][fingers::middle];
				const int fist_pinky = rig.finger_bones[rack][fingers::pinky][0];
				const int fist_thumb = rig.finger_bones[rack][fingers::thumb][1];
				has_fist = false;
				if (fist_middle[0] > 0 && fist_middle[2] > 0 && fist_pinky > 0 && fist_thumb > 0)
				{
					const auto at = [&](int bone) { return ik::model_of(local, rig.parents, bone).position; };
					const fingers::grasp grasp = fingers::grasp_of(at(index_joints[0]), at(fist_pinky), at(fist_middle[0]), at(fist_middle[2]), at(fist_thumb));
					if (grasp.valid)
					{
						fist_in_hand = ik::rotate(ik::conjugate(hand.rotation), grasp.centre - hand.position);
						has_fist = true;
					}
				}
				// The pinch: the knob against the side of the index's middle segment, towards the thumb tip.
				const int thumb_tip = rig.finger_tips[rack][fingers::thumb];
				has_pinch = thumb_tip > 0;
				if (has_pinch)
				{
					const ik::vec3 index_side = (ik::model_of(local, rig.parents, index_joints[1]).position + ik::model_of(local, rig.parents, index_joints[2]).position) * 0.5f;
					const ik::vec3 thumb = ik::model_of(local, rig.parents, thumb_tip).position;
					const ik::vec3 knob = contact::pinch_point(index_side, thumb, knob_half, finger_radius);
					// Room left for the knob between the thumb and the index (negative: they press it).
					pinch_gap_mm = (ik::length(thumb - index_side) - 2.0f * (knob_half + finger_radius)) * 1000.0f;
					pinch_in_hand = ik::rotate(ik::conjugate(hand.rotation), knob - hand.position);
				}
			}
			else
			{
				has_pinch = false;
				has_under_hook = false;
				has_fist = false;
			}
		}

		// Called with rig_lock held, after edit_fingers: a held magazine sits in the drawn off hand's fist
		// as it sits in the gun, with the fist's grasp frame in the gun's place, and is published for the
		// reload (what you see is what goes into the magwell).
		void attach_held_magazine(ik::bone_transform* local)
		{
			points.has_held = false;
			held_top_measured = false;
			held_top_in_fist = {};
			has_fist_untilted = false;
			has_fist_caught = false;
			if (!magazine_request.detached || !magazine_request.in_hand || rig.magazine < 0 || rig.weapon < 0)
			{
				return;
			}
			// The hand that reloads: the off hand, or the free gun hand while the off hand carries the gun (#38;
			// the grasp frame comes out mirrored there, and a magazine is symmetric across the gun).
			const int holder = racking_hand();
			const auto (&bones)[fingers::count][fingers::joints] = rig.finger_bones[holder];
			const int needed[] = { bones[fingers::index][0], bones[fingers::pinky][0], bones[fingers::middle][0], bones[fingers::middle][2], bones[fingers::thumb][1] };
			bool complete = last_arms[holder].solved;
			for (const int bone : needed)
			{
				complete = complete && bone > 0;
			}
			const auto at = [&](int bone) { return ik::model_of(local, rig.parents, bone).position; };
			const bool palm_valid = holder == 0 && cup.valid && last_arms[0].solved;
			const fingers::grasp fist = complete ? fingers::grasp_of(at(needed[0]), at(needed[1]), at(needed[2]), at(needed[3]), at(needed[4])) : fingers::grasp{};
			if (!fist.valid && !palm_valid)
			{
				place_requested_magazine(local); // no drawn fist: where the reload put it from the controller
				return;
			}
			// Between the grips the magazine slides from the fist onto the palm and back (as the fingers ease).
			const float step = 1.0f - std::exp(-last_finger_seconds / 0.03f);
			cup_weight += ((cupped && palm_valid ? 1.0f : 0.0f) - cup_weight) * step;
			const float on_palm = !fist.valid ? 1.0f : !palm_valid ? 0.0f : cup_weight;
			// The magazine relative to the gun as seated (local is the animation's here), placed in the
			// fist's frame, or standing on the palm with its base plate on it (cup).
			const ik::quat seated = ik::conjugate(ik::model_of(local, rig.parents, rig.weapon).rotation) * ik::model_of(local, rig.parents, rig.magazine).rotation;
			ik::bone_transform placed = ik::model_of(local, rig.parents, rig.magazine);
			const ik::quat fist_frame = fist.valid ? fist.frame : cup.frame;
			ik::quat fist_rotation = ik::normalize(magazine_request.shell && shell_across.load() ? fist_frame * shell_across_fist * seated : fist_frame * seated);
			const float drop = magazine_request.shell ? held_shell_drop : contact::held_fist_drop(magazine_request.hold_drop, magazine_request.ak);
			ik::vec3 fist_top = fist.valid ? fist.centre + ik::rotate(fist.frame, ik::vec3{ 0.0f, drop, 0.0f }) : cup.top;
			// Palming (#40): the fingers close by contact (close_on_palmed) on the magazine where the game's grip
			// holds it, so it stays there.
			const authored_grip* by_grip = palm_contact ? (magazine_request.ak ? &magazine_grip_ak : &magazine_grip_ar) : authored_shown;
			const float grip_weight = palm_contact ? 1.0f : authored_weight;
			if (!magazine_request.shell && (by_grip == &magazine_grip_ak || by_grip == &magazine_grip_ar) && grip_weight > 0.0f && (palm_contact || authored_hand_shown == 0) && holder == 0
				&& rig.arms[0].hand >= 0)
			{
				// Where the game's own reload holds it, relative to the hand, eased in with the fingers.
				const ik::bone_transform hand = ik::model_of(local, rig.parents, rig.arms[0].hand);
				const ik::vec3 held = hand.position + ik::rotate(hand.rotation, by_grip->magazine);
				fist_rotation = nlerp(fist_rotation, hand.rotation * by_grip->magazine_rotation, grip_weight);
				fist_top = fist_top + (held - fist_top) * grip_weight;
				// #90: the grip was taken from one gun (AR: the SCAR-H, grab depth 5.3 cm; AK: the AK-12, 4.3 cm); this gun's
				// magazine sits as deep in the hand as the game's own hand takes it (hold_drop): along the magazine's own up.
				fist_top = fist_top + ik::rotate(fist_rotation, ik::vec3{ 0.0f, contact::authored_grip_shift(magazine_request.hold_drop, by_grip == &magazine_grip_ak) * grip_weight, 0.0f });
			}
			const ik::quat palm_rotation = palm_valid ? ik::normalize(cup.frame * seated) : fist_rotation;
			const ik::vec3 palm_top = palm_valid ? cup.top : fist_top;
			placed.rotation = nlerp(fist_rotation, palm_rotation, on_palm);
			placed.position = fist_top + (palm_top - fist_top) * on_palm;
			fist_untilted = placed;
			has_fist_untilted = true;
			// Palming: turned aside in the hand, leaving the magwell to the old magazine.
			contact::tilt_magazine(placed.position, placed.rotation, seated, magazine_request.tilt, magazine_request.tilt_pivot);
			held_top_measured = fist.valid; // #90: the drawn top (after the palming turn) against the fist's grasp centre
			held_top_in_fist = fist.valid ? ik::rotate(ik::conjugate(fist.frame), placed.position - fist.centre) : ik::vec3{};
			// The caught one (extra meshes) beside it, parallel, not crossing it (#40; untilted at the fist it did).
			fist_caught = placed;
			contact::beside_magazine(fist_caught.position, fist_caught.rotation, seated, magazine_body, caught_gap);
			has_fist_caught = magazine_request.tilt > palm_contact_tilt;
			ik::bone_transform& current = local[rig.magazine];
			ik::set_model_transform(local, rig.parents, rig.magazine, placed);
			magazine_memory.written = current;
			magazine_memory.has_written = true;

			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat to_xr = ik::conjugate(camera_frame(camera));
			const ik::vec3 top = ik::rotate(to_xr, placed.position - tracking_origin(camera, camera_frame(camera)));
			const ik::quat rotation = to_xr * placed.rotation;
			points.has_held = true;
			points.held_tilt = magazine_request.tilt;
			points.held_magazine[0] = top.x;
			points.held_magazine[1] = top.y;
			points.held_magazine[2] = top.z;
			points.held_rotation[0] = rotation.x;
			points.held_rotation[1] = rotation.y;
			points.held_rotation[2] = rotation.z;
			points.held_rotation[3] = rotation.w;
		}

		// The drawn off-hand fist's magazine pose (as seated in the gun, in the fist's grasp frame), for a
		// caught magazine while the rig's one is back in the gun.
		bool fist_magazine(const ik::bone_transform* local, ik::bone_transform& out)
		{
			const int holder = racking_hand();
			const auto (&bones)[fingers::count][fingers::joints] = rig.finger_bones[holder];
			const int needed[] = { bones[fingers::index][0], bones[fingers::pinky][0], bones[fingers::middle][0], bones[fingers::middle][2], bones[fingers::thumb][1] };
			for (const int bone : needed)
			{
				if (bone <= 0)
				{
					return false;
				}
			}
			if (!last_arms[holder].solved)
			{
				return false;
			}
			const auto at = [&](int bone) { return ik::model_of(local, rig.parents, bone).position; };
			const fingers::grasp fist = fingers::grasp_of(at(needed[0]), at(needed[1]), at(needed[2]), at(needed[3]), at(needed[4]));
			if (!fist.valid)
			{
				return false;
			}
			const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
			const ik::bone_transform seated = ik::compose(ik::model_of(local, rig.parents, rig.parents[rig.magazine]), magazine_memory.animated);
			out = seated;
			out.rotation = ik::normalize(magazine_request.shell && shell_across.load() ? fist.frame * shell_across_fist * ik::conjugate(gun.rotation) * seated.rotation
				: fist.frame * ik::conjugate(gun.rotation) * seated.rotation);
			out.position = fist.centre + ik::rotate(fist.frame, ik::vec3{ 0.0f, magazine_request.shell ? held_shell_drop : contact::held_fist_drop(magazine_request.hold_drop, magazine_request.ak), 0.0f });
			return true;
		}

		// 3x4 rows (rotation | translation), as the bone palette stores them; the scale is left out.
		void palette_rows(const ik::bone_transform& t, float (&rows)[12])
		{
			const ik::quat& q = t.rotation;
			const float m[12] = {
				1 - 2 * (q.y * q.y + q.z * q.z), 2 * (q.x * q.y - q.z * q.w), 2 * (q.x * q.z + q.y * q.w), t.position.x,
				2 * (q.x * q.y + q.z * q.w), 1 - 2 * (q.x * q.x + q.z * q.z), 2 * (q.y * q.z - q.x * q.w), t.position.y,
				2 * (q.x * q.z - q.y * q.w), 2 * (q.y * q.z + q.x * q.w), 1 - 2 * (q.x * q.x + q.y * q.y), t.position.z,
			};
			std::memcpy(rows, m, sizeof(rows));
		}

		// Called with rig_lock held, last before the conversion (extra meshes, #36): the magazine bone gets
		// its tag scale (set, never multiplied), and the second magazine's transform relative to the drawn
		// one goes to stereo/extra_mesh, in the bone palette's space (the model pose: the root left out).
		bool primary_capturing_now();

		void publish_extra_magazine(ik::bone_transform* local)
		{
			// The held grenade is drawn after the magazine's draws, so the tag stays on for it too.
			const bool tag = (extra_meshes_on.load(std::memory_order_relaxed) || grenade_mesh_on.load(std::memory_order_relaxed)) && rig.magazine >= 0 && rig.weapon >= 0
				&& rig.parents[rig.magazine] >= 0;
			if (!tag)
			{
				stereo::extra_mesh::set_magazine_copy(false, nullptr);
				return;
			}
			ik::bone_transform& magazine = local[rig.magazine];
			const bool remembered = magazine_memory.has_written && std::memcmp(&magazine, &magazine_memory.written, sizeof(magazine)) == 0;
			// While the held grenade is captured (publish_extra_grenade), the gun's magazine marks the gun instead.
			// While the knife is out, nothing is marked: the knife's own mesh carries the Wep_Mag bone too (its
			// captures all came out "the gun among them", monitor 2026-11-11); a gun still drawn is caught by
			// the list of magazine meshes seen before.
			const bool capturing = grenade_mesh_on.load(std::memory_order_relaxed) && grenade_weapon_now.load(std::memory_order_relaxed);
			// #58: while the primary itself is captured its magazine is part of the item, unmarked.
			magazine.scale = capturing ? stereo::extra_mesh::capture_magazine_scale
				: knife_weapon_now.load(std::memory_order_relaxed) || primary_capturing_now() ? 1.0f : stereo::extra_mesh::magazine_tag;
			if (remembered)
			{
				magazine_memory.written = magazine;
			}
			if (!extra_meshes_on.load(std::memory_order_relaxed))
			{
				stereo::extra_mesh::set_magazine_copy(false, nullptr);
				return;
			}

			ik::bone_transform extra;
			bool placed = false;
			switch (extra_request.where)
			{
			case extra_magazine_pose::kind::seated:
				extra = ik::compose(ik::model_of(local, rig.parents, rig.parents[rig.magazine]), magazine_memory.animated);
				placed = true;
				break;
			case extra_magazine_pose::kind::in_fist:
				if (has_fist_caught)
				{
					extra = fist_caught;
					placed = true;
				}
				else if (has_fist_untilted)
				{
					extra = fist_untilted;
					placed = true;
				}
				else
				{
					placed = fist_magazine(local, extra);
				}
				break;
			case extra_magazine_pose::kind::at_pose:
			{
				const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
				const ik::quat frame = camera_frame(camera);
				const float* p = extra_request.position;
				const float* q = extra_request.rotation;
				extra.position = tracking_origin(camera, frame) + ik::rotate(frame, ik::vec3{ p[0], p[1], p[2] });
				extra.rotation = frame * ik::normalize(ik::quat{ q[0], q[1], q[2], q[3] });
				placed = true;
				break;
			}
			default:
				break;
			}
			if (!placed)
			{
				stereo::extra_mesh::set_magazine_copy(false, nullptr);
				return;
			}
			// Palette space: the root (Reference, the soldier in the world) left out.
			const ik::bone_transform to_soldier = ik::inverse(local[0]);
			ik::bone_transform drawn = ik::compose(to_soldier, ik::model_of(local, rig.parents, rig.magazine));
			extra = ik::compose(to_soldier, extra);
			drawn.scale = 1.0f;
			extra.scale = 1.0f;
			const ik::bone_transform d = ik::compose(extra, ik::inverse(drawn));
			float matrix[12];
			palette_rows(d, matrix);
			stereo::extra_mesh::set_magazine_copy(true, matrix);
		}

		// The drawn fist of one hand (0 = left, 1 = right) as a transform: fingers::grasp's centre and frame.
		bool fist_transform(const ik::bone_transform* local, int hand, ik::bone_transform& out)
		{
			const auto (&bones)[fingers::count][fingers::joints] = rig.finger_bones[hand];
			const int needed[] = { bones[fingers::index][0], bones[fingers::pinky][0], bones[fingers::middle][0], bones[fingers::middle][2], bones[fingers::thumb][1] };
			for (const int bone : needed)
			{
				if (bone <= 0)
				{
					return false;
				}
			}
			const auto at = [&](int bone) { return ik::model_of(local, rig.parents, bone).position; };
			const fingers::grasp fist = fingers::grasp_of(at(needed[0]), at(needed[1]), at(needed[2]), at(needed[3]), at(needed[4]));
			out = {};
			out.position = fist.centre;
			out.rotation = fist.frame;
			return fist.valid;
		}

		// Called with rig_lock held, after publish_extra_magazine (held grenade, #36): while the game's
		// grenade is its weapon, Wep_Root goes to the capture pose (hidden, tagged); while physical_grenade
		// holds one, its Wep_Root in the holding fist goes to stereo/extra_mesh, palette space.
		void publish_extra_grenade(ik::bone_transform* local)
		{
			namespace em = stereo::extra_mesh;
			const bool on = grenade_mesh_on.load(std::memory_order_relaxed) && rig.weapon >= 0 && rig.parents[rig.weapon] >= 0 && rig.bones > 0;
			const bool game_grenade = on && grenade_weapon_now.load(std::memory_order_relaxed);
			if (game_grenade)
			{
				ik::bone_transform fist;
				if (last_arms[1].solved && fist_transform(local, 1, fist))
				{
					game_grenade_in_fist = ik::compose(ik::inverse(fist), ik::model_of(local, rig.parents, rig.weapon));
					has_game_grenade_in_fist = true;
				}
				ik::bone_transform hidden;
				hidden.position = { em::grenade_capture_position[0], em::grenade_capture_position[1], em::grenade_capture_position[2] };
				hidden.scale = em::grenade_tag;
				ik::set_model_transform(local, rig.parents, rig.weapon, ik::compose(local[0], hidden));
				em::set_grenade_capture(true);
			}
			const int hand = held_grenade_hand.load(std::memory_order_relaxed) == 1 ? 1 : 0;
			ik::bone_transform fist;
			if (on && !game_grenade && held_grenade_now.load(std::memory_order_relaxed) && last_arms[hand].solved && fist_transform(local, hand, fist))
			{
				ik::bone_transform root = ik::compose(ik::inverse(local[0]), ik::compose(fist, grenade_in_fist));
				root.scale = 1.0f;
				float matrix[12];
				palette_rows(root, matrix);
				em::set_grenade_copy(true, matrix, held_grenade_armed.load(std::memory_order_relaxed));
				return;
			}
			// #41: not in hand, a grenade stands at the pouch (instead of the grab ring), upright, facing ahead
			// with the body: the fist's frame there is the camera's turned to look forward (+y up, +z ahead).
			if (on && !game_grenade && pouch_grenade_shown.load(std::memory_order_relaxed) && rig.camera >= 0)
			{
				const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
				const ik::quat frame = camera_frame(camera);
				ik::bone_transform pouch;
				pouch.position = tracking_origin(camera, frame)
					+ ik::rotate(frame, ik::vec3{ pouch_grenade_at[0].load(), pouch_grenade_at[1].load(), pouch_grenade_at[2].load() });
				const float half = 0.5f * pouch_grenade_yaw.load(std::memory_order_relaxed);
				pouch.rotation = ik::normalize(frame * ik::quat{ 0, std::sin(half), 0, std::cos(half) } * ik::quat{ 0, 1, 0, 0 });
				ik::bone_transform root = ik::compose(ik::inverse(local[0]), ik::compose(pouch, grenade_in_fist));
				root.scale = 1.0f;
				float matrix[12];
				palette_rows(root, matrix);
				em::set_grenade_copy(true, matrix, false);
				return;
			}
			em::set_grenade_copy(false, nullptr, false);
		}

		// Called with rig_lock held, after publish_extra_grenade (#41): the first time the game's knife is out,
		// its Wep_Root goes to the capture pose for the render that records it (hidden for that moment); while
		// it is not out, the captured knife stands in its holster, handle up, blade down, edge ahead.
		void publish_holster_knife(ik::bone_transform* local)
		{
			namespace em = stereo::extra_mesh;
			const bool on = grenade_mesh_on.load(std::memory_order_relaxed) && rig.weapon >= 0 && rig.parents[rig.weapon] >= 0 && rig.bones > 0;
			const bool knife_out = on && knife_weapon_now.load(std::memory_order_relaxed);
			// #41: only once the knife has been out a moment (the switch animation over): before, the gun can
			// still be drawn under the hidden root and taken for the knife.
			knife_capturing = knife_out && knife_capture_ready.load(std::memory_order_relaxed) && !grenade_weapon_now.load(std::memory_order_relaxed)
				&& !em::item_captured(em::captured::knife);
			ik::bone_transform fist;
			if (knife_capturing && last_arms[1].solved && fist_transform(local, 1, fist))
			{
				knife_fist_in_root = ik::compose(ik::inverse(ik::model_of(local, rig.parents, rig.weapon)), fist).position;
				has_knife_fist = true;
			}
			if (knife_capturing)
			{
				ik::bone_transform hidden;
				hidden.position = { em::grenade_capture_position[0], em::grenade_capture_position[1], em::grenade_capture_position[2] };
				hidden.scale = em::grenade_tag;
				ik::set_model_transform(local, rig.parents, rig.weapon, ik::compose(local[0], hidden));
				em::set_item_capture(em::captured::knife, true);
			}
			float anchor_rows[12];
			if (on && !knife_out && holster_knife_shown.load(std::memory_order_relaxed) && rig.camera >= 0 && em::item_anchor(em::captured::knife, anchor_rows))
			{
				const float* m = anchor_rows;
				ik::bone_transform anchor;
				anchor.rotation = ik::normalize(fingers::from_basis(ik::normalize(ik::vec3{ m[0], m[4], m[8] }), ik::normalize(ik::vec3{ m[1], m[5], m[9] }),
					ik::normalize(ik::vec3{ m[2], m[6], m[10] })));
				anchor.position = { m[3], m[7], m[11] };
				if (has_knife_fist)
				{
					// The grip is a point of the knife's model, the same in every capture: measured once is enough.
					ik::bone_transform fist_point;
					fist_point.position = knife_fist_in_root;
					knife_grip_in_anchor = ik::compose(ik::inverse(anchor), fist_point).position;
					has_knife_grip = true;
					has_knife_fist = false;
				}
				const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
				const ik::quat frame = camera_frame(camera);
				ik::bone_transform sheath;
				sheath.position = tracking_origin(camera, frame)
					+ ik::rotate(frame, ik::vec3{ holster_knife_at[0].load() + holster_knife_shift[0].load(), holster_knife_at[1].load() + holster_knife_shift[1].load(),
						holster_knife_at[2].load() + holster_knife_shift[2].load() });
				// Held as in the hand, with the controller pointing straight down: in the hand the knife's Wep_Root is
				// frame * aim * hold (edit_arms: the blade along the controller ray, edge toward its down), so the
				// controller's axes in the knife's own frame are conj(anchor) * conj(hold). At the holster the ray
				// points down and the controller's down (the edge) to the body's left (owner 2026-11-16: blade down,
				// handle up, edge left), then spun about the blade by knife_holster_turn.
				const float yaw_half = 0.5f * holster_knife_yaw.load(std::memory_order_relaxed);
				const ik::quat body_yaw{ 0, std::sin(yaw_half), 0, std::cos(yaw_half) };
				const float pitch_half = 0.5f * knife_pitch.load(std::memory_order_relaxed);
				const float turn_half = -0.5f * knife_yaw.load(std::memory_order_relaxed);
				const float roll_half = -0.5f * knife_roll.load(std::memory_order_relaxed);
				const ik::quat hold = ik::quat{ 0, std::sin(turn_half), 0, std::cos(turn_half) } * ik::quat{ 0, 0, std::sin(roll_half), std::cos(roll_half) }
					* ik::quat{ std::sin(pitch_half), 0, 0, std::cos(pitch_half) } * ik::quat{ 0, 0, 1, 0 };
				// Controller axes to the holster's: +z (behind the ray) up, +y (the controller's up) to the right.
				const ik::quat ray_down = fingers::from_basis(ik::vec3{ 0, 0, 1 }, ik::vec3{ 1, 0, 0 }, ik::vec3{ 0, 1, 0 });
				// In the hand the blade lies off the ray (the knife_pitch/yaw/roll tuning): with the controller turned
				// half round about the ray and tilted 35 degrees about the body's left-right axis the blade hangs
				// straight down, handle up (monitor 2026-11-16, front and side views). [0] (setting
				// knife_holster_turn) then turns it about that vertical blade; [1] and [2] add tilt about the
				// left-right and forward axes (pipe only, for tuning).
				constexpr float half_radian = 0.5f * 3.14159265f / 180.0f;
				const float about_half = half_radian * holster_knife_turn[0].load(std::memory_order_relaxed);
				const float tilt_x_half = half_radian * (35.0f + holster_knife_turn[1].load(std::memory_order_relaxed));
				const float tilt_z_half = half_radian * holster_knife_turn[2].load(std::memory_order_relaxed);
				const ik::quat about_blade{ 0, std::sin(about_half), 0, std::cos(about_half) };
				const ik::quat tilt{ ik::quat{ std::sin(tilt_x_half), 0, 0, std::cos(tilt_x_half) } * ik::quat{ 0, 0, std::sin(tilt_z_half), std::cos(tilt_z_half) } };
				const ik::quat half_round{ 0, 0, 1, 0 };
				sheath.rotation = ik::normalize(frame * body_yaw * about_blade * tilt * ray_down * half_round * hold * anchor.rotation);
				// The grip, not the knife's own origin, at the grab spot.
				sheath.position = sheath.position - ik::rotate(sheath.rotation, has_knife_grip ? knife_grip_in_anchor : knife_grip_default);
				ik::bone_transform root = ik::compose(ik::compose(ik::inverse(local[0]), sheath), ik::inverse(anchor));
				root.scale = 1.0f;
				float matrix[12];
				palette_rows(root, matrix);
				em::set_item_copy(em::captured::knife, true, matrix);
				return;
			}
			em::set_item_copy(em::captured::knife, false, nullptr);
		}

		// #58: this frame hides and tags the primary for its capture (weapon/sling_primary decides when; never
		// while a grenade or the knife is being captured, never once a capture exists).
		bool primary_capturing_now()
		{
			namespace em = stereo::extra_mesh;
			return grenade_mesh_on.load(std::memory_order_relaxed) && rig.weapon >= 0 && rig.parents[rig.weapon] >= 0 && rig.bones > 0
				&& primary_capture_wanted.load(std::memory_order_relaxed) && !grenade_weapon_now.load(std::memory_order_relaxed)
				&& !knife_weapon_now.load(std::memory_order_relaxed) && !em::item_captured(em::captured::primary);
		}

		// Called with rig_lock held, after publish_holster_knife (#58): the primary's capture (Wep_Root under
		// the hidden root for that render, its magazine untagged by publish_extra_magazine), and while
		// another weapon is out its copy hanging at the sling as the let-go gun does (edit_arms): upright,
		// barrel 75 degrees down, facing where the body faces.
		void publish_sling_primary(ik::bone_transform* local)
		{
			namespace em = stereo::extra_mesh;
			const bool on = grenade_mesh_on.load(std::memory_order_relaxed) && rig.weapon >= 0 && rig.parents[rig.weapon] >= 0 && rig.bones > 0;
			if (primary_capturing_now())
			{
				ik::bone_transform hidden;
				hidden.position = { em::grenade_capture_position[0], em::grenade_capture_position[1], em::grenade_capture_position[2] };
				hidden.scale = em::grenade_tag;
				ik::set_model_transform(local, rig.parents, rig.weapon, ik::compose(local[0], hidden));
				em::set_item_capture(em::captured::primary, true);
			}
			if (on && sling_primary_shown.load(std::memory_order_relaxed) && rig.camera >= 0)
			{
				const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
				const ik::quat frame = camera_frame(camera);
				ik::bone_transform sling;
				sling.position = tracking_origin(camera, frame)
					+ ik::rotate(frame, ik::vec3{ sling_primary_at[0].load(), sling_primary_at[1].load(), sling_primary_at[2].load() });
				const float half = 0.5f * sling_primary_yaw.load(std::memory_order_relaxed);
				const ik::quat yaw_turn{ 0, std::sin(half), 0, std::cos(half) };
				const ik::quat down{ -0.60876143f, 0.0f, 0.0f, 0.79335334f };
				sling.rotation = ik::normalize(frame * yaw_turn * down * ik::quat{ 0, 1, 0, 0 });
				if (has_sling_grip)
				{
					sling.position = sling.position - ik::rotate(sling.rotation, sling_grip_in_gun);
				}
				ik::bone_transform root = ik::compose(ik::inverse(local[0]), sling);
				root.scale = 1.0f;
				float matrix[12];
				palette_rows(root, matrix);
				em::set_item_copy(em::captured::primary, true, matrix);
				return;
			}
			em::set_item_copy(em::captured::primary, false, nullptr);
		}

		// Called with rig_lock held, after the conversion: our forward kinematics against the
		// engine's model pose, and the numbers for the report.
		void check_pose(const ik::bone_transform* local, std::uint64_t pose_object)
		{
			std::uint64_t model_pose = 0;
			if (!read(pose_object + pose_object_model_pose, model_pose) || model_pose == 0)
			{
				return;
			}
			const auto* model = reinterpret_cast<const ik::bone_transform*>(static_cast<std::uintptr_t>(model_pose));
			// The root carries the world transform; the engine's model pose leaves it out.
			static ik::bone_transform rooted[max_bones], conjugated[max_bones];
			for (int bone = 0; bone < rig.bones; ++bone)
			{
				rooted[bone] = local[bone];
				conjugated[bone] = local[bone];
				conjugated[bone].rotation = ik::conjugate(local[bone].rotation);
				if (rig.parents[bone] < 0)
				{
					rooted[bone] = conjugated[bone] = {};
				}
			}
			float error = 0, error_conjugate = 0;
			const int checked[] = { rig.camera, rig.arms[0].upper, rig.arms[0].fore, rig.arms[0].hand, rig.arms[1].upper, rig.arms[1].fore, rig.arms[1].hand };
			for (const int bone : checked)
			{
				error = std::fmax(error, ik::length(ik::model_of(rooted, rig.parents, bone).position - model[bone].position));
				error_conjugate = std::fmax(error_conjugate, ik::length(ik::model_of(conjugated, rig.parents, bone).position - model[bone].position));
			}
			check_report report;
			report.at = GetTickCount64();
			report.fk_error = error;
			report.fk_error_conjugate = error_conjugate;
			report.camera = ik::model_of(local, rig.parents, rig.camera);
			report.palette_camera = ik::compose(ik::inverse(local[0]), report.camera).position;
			report.reference = local[0].position;
			const ik::quat to_camera = ik::conjugate(camera_frame(report.camera));
			if (rig.weapon >= 0)
			{
				const ik::bone_transform weapon = ik::model_of(local, rig.parents, rig.weapon);
				report.weapon_in_camera = to_camera * weapon.rotation;
				report.weapon_in_game = ik::conjugate(camera_frame(report.camera, false)) * weapon.rotation;
				report.weapon_position = ik::rotate(to_camera, weapon.position - report.camera.position);
				report.palette_weapon = ik::compose(ik::inverse(local[0]), weapon).position;
			}
			for (int hand = 0; hand < 2; ++hand)
			{
				arm_report& arm = report.arms[hand];
				arm = last_arms[hand];
				arm.shoulder = ik::model_of(local, rig.parents, rig.arms[hand].upper).position;
				arm.elbow = ik::model_of(local, rig.parents, rig.arms[hand].fore).position;
				arm.wrist = ik::model_of(local, rig.parents, rig.arms[hand].hand).position;
				arm.wrist_in_camera = ik::rotate(to_camera, arm.wrist - report.camera.position);
			}
			last_check = report;
		}

		void __fastcall conversion_detour(void* pose)
		{
			++conversions;
			const auto object = reinterpret_cast<std::uint64_t>(pose);
			const ULONGLONG now = GetTickCount64();
			if (now >= next_refresh.load(std::memory_order_relaxed) && rig_lock.try_lock())
			{
				next_refresh = now + rig_refresh_ms;
				refresh_rig();
				rig_lock.unlock();
			}

			const std::uint64_t local_pose = *reinterpret_cast<const std::uint64_t*>(static_cast<std::uintptr_t>(object + pose_object_local_pose));
			const bool ours = local_pose != 0 && local_pose == rig_pose.load(std::memory_order_relaxed);
			auto* const local = reinterpret_cast<ik::bone_transform*>(static_cast<std::uintptr_t>(local_pose));
			if (ours)
			{
				++matched;
				const std::lock_guard guard(rig_lock);
				if (local_pose == rig.local_pose)
				{
					apply_research_offset(local);
					if (legs_hidden.load(std::memory_order_relaxed))
					{
						// Scale collapses the legs onto the hips (children inherit it); set, never multiplied,
						// so a pose the animation skipped stays the same.
						for (const int leg : rig.legs)
						{
							if (leg >= 0)
							{
								local[leg].scale = 0.001f;
							}
						}
					}
					// A cutscene: the game's own arms (edit_arms would turn its animated body back to the
					// rendered frame and pull the arms to the controllers: hands reversed and twisted).
					LARGE_INTEGER counter{}, frequency{};
					QueryPerformanceCounter(&counter);
					QueryPerformanceFrequency(&frequency);
					const float scene_seconds = scene_last.QuadPart
						? std::fmin(static_cast<float>(counter.QuadPart - scene_last.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
					scene_last = counter;
					const bool scene = cutscene_game_hands.load(std::memory_order_relaxed) && stereo::scripted_scene();
					const float weight = scene_mix.update(scene, scene_seconds);
					scene_now = scene;
					scene_weight = weight;
					if (weight > 0.0f)
					{
						const int count = rig.bones < max_bones ? rig.bones : max_bones;
						if (weight < 1.0f)
						{
							std::memcpy(scene_animated, local, sizeof(ik::bone_transform) * static_cast<std::size_t>(count));
						}
						const bool has_aim = rig.aim >= 0 && rig.weapon >= 0;
						if (has_aim)
						{
							muzzle_game = ik::model_of(local, rig.parents, rig.aim);
							root_game = ik::model_of(local, rig.parents, rig.weapon);
						}
						edit_arms(local);
						edit_weapon(local);
						edit_launcher(local);
						edit_belt_feed(local);
						edit_revolver(local);
						if (has_aim)
						{
							root_drawn = ik::model_of(local, rig.parents, rig.weapon);
							muzzle_at = GetTickCount64();
						}
						edit_fingers(local);
						attach_held_magazine(local);
						if (weight < 1.0f)
						{
							blend_pose(local, scene_animated, count, weight);
						}
					}
					publish_extra_magazine(local);
					publish_extra_grenade(local);
					publish_holster_knife(local);
					publish_sling_primary(local);
				}
			}
			original_conversion(pose);
			if (ours && now >= next_check.load(std::memory_order_relaxed))
			{
				next_check = now + check_interval_ms;
				const std::lock_guard guard(rig_lock);
				if (local_pose == rig.local_pose)
				{
					check_pose(local, object);
				}
			}
		}

		ik::vec3 config_vector(const char* key, const char* fallback)
		{
			ik::vec3 value;
			if (sscanf_s(config::get(key, fallback).c_str(), "%f %f %f", &value.x, &value.y, &value.z) != 3)
			{
				sscanf_s(fallback, "%f %f %f", &value.x, &value.y, &value.z);
			}
			return value;
		}

		DWORD WINAPI wait_and_hook(LPVOID)
		{
			auto* const target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(pose_conversion));
			std::uint8_t bytes[sizeof(expected_prologue)]{};
			for (int attempt = 0; attempt < 600; ++attempt)
			{
				if (safety::mod_disabled())
				{
					return 0;
				}
				if (ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr) && std::memcmp(bytes, expected_prologue, sizeof(bytes)) == 0)
				{
					MH_STATUS status = MH_Initialize();
					if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
					{
						status = MH_CreateHook(target, reinterpret_cast<void*>(&conversion_detour), reinterpret_cast<void**>(&original_conversion));
						if (status == MH_OK)
						{
							status = MH_EnableHook(target);
						}
					}
					log::write("arms: pose-conversion hook at %p: %s (arm_ik=%d)", target, MH_StatusToString(status), ik_enabled.load() ? 1 : 0);
					return 0;
				}
				Sleep(200);
			}
			log::write("arms: pose conversion never matched build 179547; hook not installed");
			return 0;
		}
	}

	bool install_arm_pose_hook()
	{
#if !defined(_M_X64)
		log::write("arms: hook targets BF4 x64 only; not installed");
		return false;
#else
		{
			const std::lock_guard guard(rig_lock);
			// Grip axes: -z along the handle, so the wrist sits behind the palm (+z). Mirrored in x.
			wrist_offset[1] = config_vector("arm_ik_wrist", "0 0 0.06");
			wrist_offset[0] = { -wrist_offset[1].x, wrist_offset[1].y, wrist_offset[1].z };
			// Elbows down, outwards and slightly back (OpenXR: +x right, +y up, +z back).
			pole_direction[1] = config_vector("arm_ik_pole", "0.5 -1 0.3");
			pole_direction[0] = { -pole_direction[1].x, pole_direction[1].y, pole_direction[1].z };
		}
		ik_enabled = config::get_bool("arm_ik", true);
		weapon_follows = config::get_bool("arm_ik_weapon", true);
		const HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook, nullptr, 0, nullptr);
		if (!thread)
		{
			return false;
		}
		CloseHandle(thread);
		return true;
#endif
	}

	void set_arm_ik(bool enabled)
	{
		ik_enabled = enabled;
	}

	void set_gun_hand_rotation(bool active, const float (&rotation)[4])
	{
		const std::lock_guard guard(gun_hand_lock);
		gun_hand_override = active;
		gun_hand_rotation = { rotation[0], rotation[1], rotation[2], rotation[3] };
	}

	void set_knife_in_hand(bool knife)
	{
		knife_in_hand = knife;
	}

	void set_gun_hand_item(bool item)
	{
		hand_item_in_gun_hand = item;
	}

	void set_knife_pitch(float degrees)
	{
		knife_pitch = degrees * 3.14159265f / 180.0f;
	}

	void set_knife_turn(float yaw_degrees, float roll_degrees)
	{
		knife_yaw = yaw_degrees * 3.14159265f / 180.0f;
		knife_roll = roll_degrees * 3.14159265f / 180.0f;
	}

	void set_shoulder_reach(float metres)
	{
		shoulder_reach = std::fmax(0.0f, metres);
	}

	void set_arm_stretch(float fraction)
	{
		arm_stretch = std::fmax(0.0f, fraction);
	}

	bool arm_ik_enabled()
	{
		return ik_enabled;
	}

	void set_legs_hidden(bool hidden)
	{
		legs_hidden = hidden;
	}

	void set_torso_follow(bool follow)
	{
		torso_follow = follow;
	}

	bool drawn_torso_yaw(float& yaw)
	{
		yaw = torso_report.load(std::memory_order_relaxed);
		return torso_follow.load(std::memory_order_relaxed) && GetTickCount64() - torso_reported_at.load(std::memory_order_relaxed) < 250;
	}

	void set_torso_hands(bool lean)
	{
		lean_to_hands = lean;
	}

	void set_body_shoulders(bool on)
	{
		body_shoulders = on;
	}

	void set_arm_shape(float length, float width)
	{
		arm_length = length > 0.5f && length < 2.0f ? length : 1.0f;
		shoulder_width = width;
	}

	void set_shoulder_back(float metres)
	{
		shoulder_back = std::isfinite(metres) ? std::fmax(-0.15f, std::fmin(0.15f, metres)) : 0.0f;
	}

	void set_body_size(const body_size& size)
	{
		body_scale = std::isfinite(size.scale) && size.scale > 0.5f && size.scale < 1.5f ? size.scale : 1.0f;
		shoulder_drop = std::isfinite(size.shoulder_drop) && size.shoulder_drop > 0.0f && size.shoulder_drop < 0.5f ? size.shoulder_drop : body::reference_shoulder_drop;
	}

	void set_shoulder_offset(float x, float y, float z)
	{
		shoulder_offset[0] = x;
		shoulder_offset[1] = y;
		shoulder_offset[2] = z;
	}

	void set_hand_target(int hand, bool active, const float position[3], float yaw_degrees, float pitch_degrees)
	{
		if (hand < 0 || hand > 1)
		{
			return;
		}
		constexpr float half_radians = 3.14159265f / 360.0f;
		const ik::quat yaw{ 0, std::sin(yaw_degrees * half_radians), 0, std::cos(yaw_degrees * half_radians) };
		const ik::quat pitch{ std::sin(pitch_degrees * half_radians), 0, 0, std::cos(pitch_degrees * half_radians) };
		const std::lock_guard guard(rig_lock);
		targets[hand].active = active;
		targets[hand].position = { position[0], position[1], position[2] };
		targets[hand].rotation = yaw * pitch;
	}

	void set_camera_axes(const float rotation[4])
	{
		const std::lock_guard guard(rig_lock);
		camera_axes = ik::normalize(ik::quat{ rotation[0], rotation[1], rotation[2], rotation[3] });
	}

	weapon_points latest_weapon_points()
	{
		const std::lock_guard guard(rig_lock);
		return points;
	}

	bool muzzle_frames(muzzle_frame& aim, muzzle_frame& game, muzzle_frame& drawn)
	{
		const std::lock_guard guard(rig_lock);
		if (!muzzle_at || GetTickCount64() - muzzle_at > 250)
		{
			return false;
		}
		const auto copy = [](const ik::bone_transform& from, muzzle_frame& to)
		{
			to.position[0] = from.position.x;
			to.position[1] = from.position.y;
			to.position[2] = from.position.z;
			to.rotation[0] = from.rotation.x;
			to.rotation[1] = from.rotation.y;
			to.rotation[2] = from.rotation.z;
			to.rotation[3] = from.rotation.w;
		};
		copy(muzzle_game, aim);
		copy(root_game, game);
		copy(root_drawn, drawn);
		return true;
	}

	void set_launcher_pose(const launcher_pose& pose)
	{
		const std::lock_guard guard(rig_lock);
		launcher_request = pose;
	}

	void set_belt_feed_pose(const belt_feed_pose& pose)
	{
		const std::lock_guard guard(rig_lock);
		belt_feed_request = pose;
	}

	void set_magazine_pose(const magazine_pose& pose)
	{
		const std::lock_guard guard(rig_lock);
		magazine_request = pose;
	}

	void set_extra_magazine(const extra_magazine_pose& pose)
	{
		const std::lock_guard guard(rig_lock);
		extra_request = pose;
	}

	void set_extra_meshes(bool on)
	{
		extra_meshes_on = on;
	}

	void set_gun_in_off_hand(bool held, bool by_grip)
	{
		gun_in_off_hand = held;
		off_by_grip = held && by_grip;
	}

	void set_gun_hand_fist(bool fist)
	{
		gun_hand_fist = fist;
	}

	void set_grenade_mesh(bool on)
	{
		grenade_mesh_on = on;
	}

	void set_grenade_weapon(bool current)
	{
		grenade_weapon_now = current;
	}

	void set_knife_weapon(bool out, bool capture_ready)
	{
		knife_weapon_now = out;
		knife_capture_ready = out && capture_ready;
	}

	void set_primary_capture(bool wanted)
	{
		primary_capture_wanted = wanted;
	}

	void set_sling_primary(bool shown, const float (&position)[3], float yaw)
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			sling_primary_at[axis] = position[axis];
		}
		sling_primary_yaw = yaw;
		sling_primary_shown = shown;
	}

	void set_holster_knife(bool shown, const float (&position)[3], float yaw)
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			holster_knife_at[axis] = position[axis];
		}
		holster_knife_yaw = yaw;
		holster_knife_shown = shown;
	}

	void set_holster_knife_shift(float dx, float dy, float dz)
	{
		holster_knife_shift[0] = dx;
		holster_knife_shift[1] = dy;
		holster_knife_shift[2] = dz;
	}

	void set_holster_knife_turn(float x_degrees, float y_degrees, float z_degrees)
	{
		holster_knife_turn[0] = x_degrees;
		holster_knife_turn[1] = y_degrees;
		holster_knife_turn[2] = z_degrees;
	}

	void set_pouch_grenade(bool shown, const float (&position)[3], float yaw)
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			pouch_grenade_at[axis] = position[axis];
		}
		pouch_grenade_yaw = yaw;
		pouch_grenade_shown = shown;
	}

	void set_held_grenade(bool holding, int hand, bool armed)
	{
		held_grenade_hand = hand;
		held_grenade_armed = armed;
		held_grenade_now = holding;
	}

	std::string grenade_hold_report()
	{
		const std::lock_guard guard(rig_lock);
		const auto line = [](const char* name, const ik::bone_transform& t) {
			char text[192];
			std::snprintf(text, sizeof(text), "%s pos=(%.4f %.4f %.4f) rot=(%.4f %.4f %.4f %.4f)", name, t.position.x, t.position.y, t.position.z, t.rotation.x, t.rotation.y,
				t.rotation.z, t.rotation.w);
			return std::string(text);
		};
		std::string text = "grenade_mesh=" + std::to_string(grenade_mesh_on.load() ? 1 : 0) + " game_grenade=" + std::to_string(grenade_weapon_now.load() ? 1 : 0)
			+ " held=" + std::to_string(held_grenade_now.load() ? 1 : 0) + " hand=" + std::to_string(held_grenade_hand.load()) + " armed="
			+ std::to_string(held_grenade_armed.load() ? 1 : 0) + "\n" + line("hold_in_fist", grenade_in_fist);
		if (has_game_grenade_in_fist)
		{
			text += "\n" + line("game_hold_in_fist", game_grenade_in_fist);
		}
		char grip[96];
		std::snprintf(grip, sizeof(grip), "\nknife_grip_in_anchor=(%.4f %.4f %.4f) measured=%d", knife_grip_in_anchor.x, knife_grip_in_anchor.y, knife_grip_in_anchor.z,
			has_knife_grip ? 1 : 0);
		text += grip;
		text += "\nholster_knife_turn=(" + std::to_string(holster_knife_turn[0].load()) + " " + std::to_string(holster_knife_turn[1].load()) + " "
			+ std::to_string(holster_knife_turn[2].load()) + ")";
		return text;
	}

	arm_pose_stats stats()
	{
		return { conversions.load(), matched.load(), solved.load() };
	}

	void set_bone_offset(int bone, float dx, float dy, float dz)
	{
		research_bone = -1; // never pair a new bone with the old offset
		research_offset[0] = dx;
		research_offset[1] = dy;
		research_offset[2] = dz;
		research_bone = (bone >= 0 && bone < max_bones) ? bone : -1;
	}

	void set_rendered_frame(bool rendered)
	{
		rendered_frame = rendered;
	}

	void set_body_turn(bool on)
	{
		body_turn_on = on;
	}

	void set_dropped_gun(bool active, const float (&position)[3], float yaw, bool hanging)
	{
		const std::lock_guard dropped(dropped_lock);
		gun_on_floor = active;
		gun_on_floor_now = active;
		floor_gun_hanging = hanging;
		floor_gun_position = { position[0], position[1], position[2] };
		floor_gun_yaw = yaw;
	}

	bool body_turn()
	{
		return body_turn_on.load();
	}

	void set_gun_on_ray(bool on_ray)
	{
		gun_on_ray = on_ray;
	}

	void set_gun_recoil(float strength)
	{
		recoil_strength = std::fmax(0.0f, strength);
		if (!(strength > 0.0f))
		{
			const std::lock_guard guard(recoil_lock);
			recoil.reset();
		}
	}

	void set_one_hand_recoil(float strength)
	{
		one_hand_strength = std::isfinite(strength) ? std::fmax(0.0f, std::fmin(1.0f, strength)) : 1.0f;
	}

	void kick_gun(int shots, const recoil_profile& gun)
	{
		const float strength = recoil_strength.load(std::memory_order_relaxed);
		const std::lock_guard guard(recoil_lock);
		recoil.shot(steady_seconds(), shots, strength, support_weight.load(std::memory_order_relaxed) >= 0.5f, gun,
			one_hand_strength.load(std::memory_order_relaxed));
		last_profile = gun;
	}

	std::string recoil_report()
	{
		char text[384];
		const std::lock_guard guard(recoil_lock);
		std::snprintf(text, sizeof(text),
			"recoil strength=%.2f drawn_kick_deg=%.2f drawn_back_m=%.4f drawn_yaw_deg=%.2f now_deg=%.2f gun vertical=%.2f first_shot=%.2f yaw=%.2f..%.2f"
			" one_hand=%.2f pistol=%d two_handed=%d",
			recoil_strength.load(), last_kick.degrees, last_kick.back, last_kick.yaw, recoil.at(steady_seconds()).degrees, last_profile.vertical,
			last_profile.first_shot, last_profile.yaw_min, last_profile.yaw_max, one_hand_strength.load(), last_profile.pistol ? 1 : 0,
			support_weight.load() >= 0.5f ? 1 : 0);
		return text;
	}

	void set_gun_in_hand(bool gun)
	{
		gun_in_hand = gun;
	}

	void set_support_hand(float weight)
	{
		support_weight = std::fmax(0.0f, std::fmin(1.0f, weight));
	}

	void set_cutscene_hands(bool game)
	{
		cutscene_game_hands = game;
	}

	std::string cutscene_hands_report()
	{
		char text[128];
		std::snprintf(text, sizeof(text), "cutscene_hands=%s arms_scene=%d mod_pose_weight=%.2f", cutscene_game_hands.load() ? "game" : "controllers",
			scene_now.load() ? 1 : 0, scene_weight.load());
		return text;
	}

	void set_finger_poses(bool enabled)
	{
		finger_poses = enabled;
	}

	void set_magazine_grip(const std::string& grip)
	{
		magazine_grip = grip == "wrap" ? 1 : grip == "cup" ? 2 : 0;
	}

	void set_rack_grip(const std::string& grip)
	{
		rack_grip = grip == "pinch" ? 0 : grip == "hook" ? 1 : grip == "auto" ? 2 : 3;
	}

	void set_off_hand_neutral(bool neutral)
	{
		off_hand_neutral = neutral;
	}

	void set_gun_hand_neutral(bool neutral)
	{
		gun_hand_neutral = neutral;
	}

	void set_off_hand_task(fingers::task task)
	{
		off_task = static_cast<int>(task);
		off_task_at = GetTickCount64();
	}

	void set_hand_collision(bool on)
	{
		hand_collision_on = on;
	}

	void set_support_slide(bool lock, float back, float forward)
	{
		const auto metres = [](float v) { return std::isfinite(v) ? std::fmax(0.0f, std::fmin(0.3f, v)) : 0.0f; };
		support_lock = lock;
		support_back = metres(back);
		support_forward = metres(forward);
	}

	void set_foregrip(int kind)
	{
		foregrip_kind = kind >= 0 && kind <= 3 ? kind : 0;
	}

	void set_foregrip_hold(bool on)
	{
		foregrip_hold_on = on;
	}

	void set_rack_fist_offsets(float over_out, float over_up, float under_out, float under_down)
	{
		const auto metres = [](float v) { return std::isfinite(v) ? std::fmax(-0.15f, std::fmin(0.15f, v)) : 0.0f; };
		const std::lock_guard guard(rig_lock);
		rack_fist = { metres(over_out), metres(over_up), metres(under_out), metres(under_down) };
	}

	void set_handle_hold(float weight, const float (&handle_in_gun)[3], float pull, bool underhand, float side)
	{
		const std::lock_guard guard(rig_lock);
		handle_request.underhand = underhand;
		handle_request.side = std::isfinite(side) ? side : 0.0f;
		handle_request.weight = std::fmax(0.0f, std::fmin(1.0f, weight));
		handle_request.handle = { handle_in_gun[0], handle_in_gun[1], handle_in_gun[2] };
		handle_request.pull = std::fmax(0.0f, pull);
	}

	void set_shell_across(bool on)
	{
		shell_across.store(on, std::memory_order_relaxed);
	}

	void set_bolt_pose(bool on, float turn, float pull)
	{
		bolt_pose_turn.store(std::isfinite(turn) ? turn : 0.0f, std::memory_order_relaxed);
		bolt_pose_pull.store(std::isfinite(pull) ? std::fmax(0.0f, std::fmin(0.2f, pull)) : 0.0f, std::memory_order_relaxed);
		bolt_pose_on.store(on, std::memory_order_relaxed);
	}

	void set_pump_rest(bool on, unsigned gun_key)
	{
		pump_rest_key.store(gun_key, std::memory_order_relaxed);
		pump_rest_on.store(on && gun_key != 0, std::memory_order_relaxed);
	}

	void set_pose_preview(int hand, poses::grip which)
	{
		preview_grip = static_cast<int>(which);
		preview_hand = (hand == 0 || hand == 1) && which != poses::grip::none ? hand : -1;
	}

	std::string pose_grips_report()
	{
		std::lock_guard lock(rig_lock);
		const int hand = preview_hand.load();
		std::string text = "preview=" + std::string(hand == 0 ? "left " : hand == 1 ? "right " : "off")
			+ (hand >= 0 ? std::string(poses::name_of(static_cast<poses::grip>(preview_grip.load()))) : std::string());
		for (int h = 0; h < 2; ++h)
		{
			const poses::tweak& t = shown_tweak[h];
			char line[192];
			std::snprintf(line, sizeof(line), "\n%s hand: grip=%s tweak bend thumb..pinky=%.0f %.0f %.0f %.0f %.0f across=%.0f out=%.0f", h == 0 ? "left" : "right",
				std::string(poses::name_of(shown_grip[h])).c_str(), t.fingers[0].bend, t.fingers[1].bend, t.fingers[2].bend, t.fingers[3].bend, t.fingers[4].bend,
				t.thumb_across, t.thumb_out);
			text += line;
		}
		return text;
	}

	void set_wall_push(float metres)
	{
		wall_push = std::isfinite(metres) ? std::fmax(0.0f, std::fmin(0.5f, metres)) : 0.0f;
	}

	bool wall_push_applies()
	{
		return wall_push_target.load(std::memory_order_relaxed);
	}

	void set_rack_lock(bool on)
	{
		rack_lock_on = on;
	}

	void set_magazine_lock(const magazine_lock_pose& pose)
	{
		const std::lock_guard guard(rig_lock);
		lock_request = pose;
		lock_request.weight = std::isfinite(pose.weight) ? std::fmax(0.0f, std::fmin(1.0f, pose.weight)) : 0.0f;
	}

	void set_magazine_snap(float weight, bool ak)
	{
		const std::lock_guard guard(rig_lock);
		snap_request.weight = std::isfinite(weight) ? std::fmax(0.0f, std::fmin(1.0f, weight)) : 0.0f;
		snap_request.ak = ak;
	}

	std::string hands_report()
	{
		const std::lock_guard guard(rig_lock);
		const auto task_name = [](fingers::task task)
		{
			switch (task)
			{
			case fingers::task::free: return "free";
			case fingers::task::item: return "item";
			case fingers::task::gun_grip: return "gun_grip";
			case fingers::task::support: return "support";
			case fingers::task::magazine: return "magazine";
			case fingers::task::cup: return "cup";
			case fingers::task::handle: return "handle";
			case fingers::task::pinch: return "pinch";
			case fingers::task::underhand: return "underhand";
			case fingers::task::reach: return "reach";
			case fingers::task::fist_over: return "fist_over";
			case fingers::task::fist_under: return "fist_under";
			}
			return "?";
		};
		int found = 0;
		for (const auto& hand : rig.finger_bones)
		{
			for (const auto& finger : hand)
			{
				for (const int bone : finger)
				{
					found += bone > 0 ? 1 : 0;
				}
			}
		}
		char text[1536];
		int length = std::snprintf(text, sizeof(text), "finger_poses=%d joints=%d handle_hold=%.2f pull=%.3f handle=(%.3f,%.3f,%.3f)",
			finger_poses.load() ? 1 : 0, found, handle_request.weight, handle_request.pull, handle_request.handle.x, handle_request.handle.y,
			handle_request.handle.z);
		for (int hand = 0; hand < 2 && length > 0 && length < static_cast<int>(sizeof(text)); ++hand)
		{
			const fingers::finger_input& in = last_input[hand];
			const fingers::hand_shape& s = finger_shapes[hand].current();
			length += std::snprintf(text + length, sizeof(text) - length,
				"\n%s task=%s input valid=%d squeeze=%.2f trigger=%.2f touch_sensors=%d trigger_touch=%d thumb_touch=%d"
				" curl thumb..pinky=%.2f %.2f %.2f %.2f %.2f animated=%.2f %.2f %.2f %.2f %.2f index_bend_deg=%.0f %.0f %.0f",
				hand == 0 ? "off" : "gun", task_name(last_task[hand]), in.valid ? 1 : 0, in.squeeze, in.trigger, in.touch_sensors ? 1 : 0,
				in.trigger_touch ? 1 : 0, in.thumb_touch ? 1 : 0, s.curl[0], s.curl[1], s.curl[2], s.curl[3], s.curl[4], s.animated[0], s.animated[1], s.animated[2],
				s.animated[3], s.animated[4], last_index_bend[hand][0] * 57.29578f, last_index_bend[hand][1] * 57.29578f,
				last_index_bend[hand][2] * 57.29578f);
		}
		if (length > 0 && length < static_cast<int>(sizeof(text)))
		{
			static const char* const grips[] = { "auto", "wrap", "cup" };
			std::snprintf(text + length, sizeof(text) - length,
				"\nmagazine_grip=%s cupped=%d palm_up=%.2f on_palm=%.2f gap_mm thumb..pinky=%.0f %.0f %.0f %.0f %.0f"
				"\nrack_grip=%s pinching=%d underhand=%d stroke_palm_up=%.2f thumb_gap_mm=%.0f pinch_in_hand=(%.3f,%.3f,%.3f)"
				"\nauthored=%s weight=%.2f palm_contact=%d caught_beside=%d rack_fist_grip_minus_knob=(%.3f,%.3f,%.3f) %.3f measured=%d"
				" held_top_above_fist=(%.3f,%.3f,%.3f) hold_drop=%.3f held=%d", grips[magazine_grip.load() % 3],
				cupped ? 1 : 0, cup_palm_up, cup_weight, cup_gap_mm[0], cup_gap_mm[1], cup_gap_mm[2], cup_gap_mm[3], cup_gap_mm[4],
				rack_grip.load() == 0 ? "pinch" : rack_grip.load() == 1 ? "hook" : rack_grip.load() == 2 ? "auto" : fisting ? (handle_request.underhand ? "fist(under)" : "fist(over)") : "fist", pinching ? 1 : 0, hooking_under ? 1 : 0, rack_palm_up, pinch_gap_mm, pinch_in_hand.x, pinch_in_hand.y, pinch_in_hand.z,
				authored_shown == &pinch_grip ? "pinch" : authored_shown == &magazine_grip_ak ? "magazine_ak"
					: authored_shown == &magazine_grip_ar ? "magazine_ar" : "none",
				authored_weight, palm_contact ? 1 : 0, has_fist_caught ? 1 : 0, rack_grip_error.x, rack_grip_error.y, rack_grip_error.z,
				std::sqrt(rack_grip_error.x * rack_grip_error.x + rack_grip_error.y * rack_grip_error.y + rack_grip_error.z * rack_grip_error.z),
				rack_grip_measured ? 1 : 0, held_top_in_fist.x, held_top_in_fist.y, held_top_in_fist.z,
				magazine_request.hold_drop, held_top_measured ? 1 : 0);
		}
		if (length > 0 && length < static_cast<int>(sizeof(text)))
		{
			length += static_cast<int>(std::strlen(text + length));
		}
		if (length > 0 && length < static_cast<int>(sizeof(text)))
		{
			std::snprintf(text + length, sizeof(text) - length,
				"\nhand_collision=%d boxes=%d magazine_top=%d (%.3f,%.3f,%.3f) pushes=%u"
				" receiver=(%.3f,%.3f,%.3f)+-(%.3f,%.3f,%.3f) palm_in_gun off=(%.3f,%.3f,%.3f) gun=(%.3f,%.3f,%.3f)"
				"\n off checked=%d touch=%d depth_mm=%.0f drawn_mm=%.0f; gun checked=%d touch=%d depth_mm=%.0f drawn_mm=%.0f",
				hand_collision_on.load() ? 1 : 0, hand_collision_boxes, geometry.has_magazine_top ? 1 : 0, geometry.magazine_top.x,
				geometry.magazine_top.y, geometry.magazine_top.z, hand_collision_pushes, hand_collision_receiver.centre.x,
				hand_collision_receiver.centre.y, hand_collision_receiver.centre.z, hand_collision_receiver.half.x, hand_collision_receiver.half.y,
				hand_collision_receiver.half.z, hand_collision_palm_in_gun[0].x, hand_collision_palm_in_gun[0].y, hand_collision_palm_in_gun[0].z,
				hand_collision_palm_in_gun[1].x, hand_collision_palm_in_gun[1].y, hand_collision_palm_in_gun[1].z,
				hand_collision_checked[0] ? 1 : 0, hand_collision_touch[0] ? 1 : 0, hand_collision_depth[0] * 1000.0f, hand_collision_drawn[0] * 1000.0f,
				hand_collision_checked[1] ? 1 : 0, hand_collision_touch[1] ? 1 : 0, hand_collision_depth[1] * 1000.0f, hand_collision_drawn[1] * 1000.0f);
		}
		return text;
	}

	bool support_offset(float (&offset)[2])
	{
		const std::lock_guard guard(rig_lock);
		if (!geometry.valid)
		{
			return false;
		}
		// Gun axes (+x left, +y up, +z barrel) to aim axes (x right, y up, -z barrel).
		const ik::vec3 between = geometry.hand[0].position - geometry.hand[1].position;
		offset[0] = -between.x;
		offset[1] = between.y;
		return true;
	}

	bool support_along(float& along)
	{
		const std::lock_guard guard(rig_lock);
		if (!geometry.valid)
		{
			return false;
		}
		along = along_hold; // the separation the game last held steady for this gun (draws and reload poses move the hands)
		return along >= 0.0f && std::isfinite(along);
	}

	void wrist_of(int hand, const stereo::pose& grip, float (&wrist)[3])
	{
		ik::vec3 offset;
		{
			const std::lock_guard guard(rig_lock);
			offset = wrist_offset[hand == 0 ? 0 : 1];
		}
		const ik::vec3 at = ik::vec3{ grip.position[0], grip.position[1], grip.position[2] }
			+ ik::rotate(ik::quat{ grip.orientation[0], grip.orientation[1], grip.orientation[2], grip.orientation[3] }, offset);
		wrist[0] = at.x;
		wrist[1] = at.y;
		wrist[2] = at.z;
	}

	std::string gun_report()
	{
		const std::lock_guard guard(rig_lock);
		const stereo::hand_sample hands = stereo::latest_hand_sample();
		const stereo::first_person_turn turn = stereo::first_person_view_turn();
		constexpr float degrees = 57.29578f;
		// Barrel of a rotation: Wep_Root +z for the gun (camera axes via (0,1,0,0)), -z for an aim pose.
		const auto yaw_pitch = [](ik::vec3 d, char (&out)[48]) -> const char*
		{
			d = ik::normalize(d);
			std::snprintf(out, sizeof(out), "yaw=%.1f pitch=%.1f", std::atan2(-d.x, -d.z) * degrees, std::asin(std::fmax(-1.0f, std::fmin(1.0f, d.y))) * degrees);
			return out;
		};
		const auto between = [](ik::vec3 a, ik::vec3 b)
		{
			return std::acos(std::fmax(-1.0f, std::fmin(1.0f, ik::dot(ik::normalize(a), ik::normalize(b))))) * degrees;
		};
		const check_report& r = last_check;
		const ik::vec3 barrel = ik::rotate(r.weapon_in_camera, { 0, 0, 1 });
		const ik::vec3 barrel_game = ik::rotate(r.weapon_in_game, { 0, 0, 1 });
		ik::vec3 ray{};
		if (hands.valid[1])
		{
			const float* q = hands.aim[1].orientation;
			ray = ik::rotate(ik::quat{ q[0], q[1], q[2], q[3] }, { 0, 0, -1 });
		}
		ik::quat used;
		{
			const std::lock_guard held(gun_hand_lock);
			used = gun_hand_override ? gun_hand_rotation : ik::quat{ 0, 0, 0, 0 };
		}
		const bool overridden = used.w != 0 || used.x != 0 || used.y != 0 || used.z != 0;
		const ik::vec3 drawn_aim = overridden ? ik::rotate(used, { 0, 0, -1 }) : ray;
		char a[48], b[48], c[48], d[48];
		char text[1536];
		int length = std::snprintf(text, sizeof(text),
			"frame=%s turn=%d(%.1f) check_age_ms=%llu\n"
			"barrel_view %s\nbarrel_game %s\naim_ray     %s\naim_used    %s (two-hand override=%d)\n"
			"angle barrel_view-aim_used=%.2f barrel_game-aim_used=%.2f barrel_view-aim_ray=%.2f\n"
			"wep_root_in_camera=(%.3f,%.3f,%.3f) controller_grip=(%.3f,%.3f,%.3f)\n",
			rendered_frame.load() ? "rendered" : "game", turn.active ? 1 : 0, turn.yaw * degrees, r.at ? GetTickCount64() - r.at : 0ull,
			yaw_pitch(barrel, a), yaw_pitch(barrel_game, b), yaw_pitch(ray, c), yaw_pitch(drawn_aim, d), overridden ? 1 : 0,
			between(barrel, drawn_aim), between(barrel_game, drawn_aim), between(barrel, ray),
			r.weapon_position.x, r.weapon_position.y, r.weapon_position.z, hands.grip[1].position[0], hands.grip[1].position[1], hands.grip[1].position[2]);
		if (geometry.valid)
		{
			for (int hand = 0; hand < 2 && length > 0 && length < static_cast<int>(sizeof(text)); ++hand)
			{
				const ik::vec3 h = geometry.hand[hand].position;
				const ik::vec3 j = geometry.ik_hand[hand].position;
				length += std::snprintf(text + length, sizeof(text) - length, "%s hand_in_gun=(%.3f,%.3f,%.3f) ik_joint=%s(%.3f,%.3f,%.3f) apart=%.3f\n",
					hand == 0 ? "left " : "right", h.x, h.y, h.z, geometry.has_ik[hand] ? "" : "none", j.x, j.y, j.z,
					geometry.has_ik[hand] ? ik::length(h - j) : -1.0f);
			}
		}
		return text;
	}

	std::string report()
	{
		const std::lock_guard guard(rig_lock);
		const hand_target hand_targets[2] = { targets[0], targets[1] };
		const stereo::hand_sample hands = stereo::latest_hand_sample();
		char text[1536];
		const auto v = [](const ik::vec3& p, char (&out)[48]) -> const char*
		{
			std::snprintf(out, sizeof(out), "(%.3f,%.3f,%.3f)", p.x, p.y, p.z);
			return out;
		};
		char a[48], b[48], c[48], d[48], e[48];
		const check_report& r = last_check;
		const ik::quat q = r.camera.rotation;
		int length = std::snprintf(text, sizeof(text),
			"arm_ik=%d skeleton=%s bones=%d conversions=%llu matched=%llu solved=%llu\n"
			"controllers left=%d right=%d targets left=%d right=%d\n"
			"fk_error=%.5f fk_error_conjugate=%.5f check_age_ms=%llu\n"
			"camera_joint pos=%s rot=(%.4f,%.4f,%.4f,%.4f) right=%s up=%s back=%s scale=%.3f\n"
			"knife=%d wep_root_in_camera=(%.3f,%.3f,%.3f,%.3f) torso_follow=%d torso_deg=%.1f\n"
			"palette camera=(%.3f,%.3f,%.3f) wep_root=(%.3f,%.3f,%.3f) reference=(%.3f,%.3f,%.3f)\n",
			ik_enabled.load() ? 1 : 0, rig.camera >= 0 ? rig.name : "-", rig.bones, conversions.load(), matched.load(), solved.load(),
			hands.valid[0] ? 1 : 0, hands.valid[1] ? 1 : 0, hand_targets[0].active ? 1 : 0, hand_targets[1].active ? 1 : 0,
			r.fk_error, r.fk_error_conjugate, r.at ? GetTickCount64() - r.at : 0ull,
			v(r.camera.position, a), q.x, q.y, q.z, q.w, v(ik::rotate(q, { 1, 0, 0 }), b), v(ik::rotate(q, { 0, 1, 0 }), c),
			v(ik::rotate(q, { 0, 0, 1 }), d), r.camera.scale, knife_in_hand.load() ? 1 : 0, r.weapon_in_camera.x, r.weapon_in_camera.y,
			r.weapon_in_camera.z, r.weapon_in_camera.w, torso_follow.load() ? 1 : 0, torso_report.load() * 57.29578f,
			r.palette_camera.x, r.palette_camera.y, r.palette_camera.z, r.palette_weapon.x, r.palette_weapon.y, r.palette_weapon.z,
			r.reference.x, r.reference.y, r.reference.z);
		if (length > 0 && length < static_cast<int>(sizeof(text)))
		{
			length += std::snprintf(text + length, sizeof(text) - length,
				"support along_now=%.3f along_hold=%.3f cupping=%d foregrip=%d on_grip=%d held=%d solved_wrist_in_gun=(%.3f,%.3f,%.3f) game_hand_in_gun=(%.3f,%.3f,%.3f)\n",
				along_now, along_hold, along_hold >= 0.0f && along_hold < pistol_hold_metres ? 1 : 0, foregrip_kind.load(), support_on_grip ? 1 : 0,
				support_drawn ? 1 : 0, support_drawn_in_gun.x,
				support_drawn_in_gun.y, support_drawn_in_gun.z, support_game_in_gun.x, support_game_in_gun.y, support_game_in_gun.z);
		}
		if (length > 0 && length < static_cast<int>(sizeof(text)))
		{
			length += std::snprintf(text + length, sizeof(text) - length,
				"pump_rest=%d held=%d slide_animated_z=%.3f drawn_z=%.3f rest_z=%.3f hidden_frames=%u hidden_max_mm=%.0f animated_turn_deg=%.1f drawn_turn_deg=%.1f\n",
				pump_rest_on.load() ? 1 : 0, pump_rest_held ? 1 : 0, pump_animated_z, pump_drawn_z, pump_rest.valid() ? pump_rest.rest_z() : 0.0f,
				pump_hidden_frames, pump_hidden_mm, pump_animated_turn, pump_drawn_turn);
			length += std::snprintf(text + length, sizeof(text) - length, "belt_feed drawn=%d rounds=%d lid=%.2f belt_scale=%.3f\n",
				belt_feed_drawn ? 1 : 0, belt_feed_request.rounds ? 1 : 0, belt_feed_request.lid, belt_link_scale);
		}
		for (int hand = 0; hand < 2 && length > 0 && length < static_cast<int>(sizeof(text)); ++hand)
		{
			const arm_report& arm = r.arms[hand];
			length += std::snprintf(text + length, sizeof(text) - length, "%s solved=%d reach=%.2f shoulder=%s elbow=%s wrist=%s goal=%s wrist_in_camera=%s\n",
				hand == 0 ? "left " : "right", arm.solved ? 1 : 0, arm.reach, v(arm.shoulder, a), v(arm.elbow, b), v(arm.wrist, c), v(arm.goal, d),
				v(arm.wrist_in_camera, e));
			const ik::quat h = arm.hand_in_aim;
			length += std::snprintf(text + length, sizeof(text) - length, "  hand_in_aim=(%.3f,%.3f,%.3f,%.3f)\n", h.x, h.y, h.z, h.w);
		}
		return text;
	}
}
