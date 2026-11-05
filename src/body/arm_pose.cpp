#include "body/arm_pose.hpp"

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
#include "body/gun_recoil.hpp"
#include "body/torso.hpp"
#include "input/vr_controls.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "input/fingers.hpp"
#include "safety/mp_guard.hpp"
#include "stereo/camera_hook.hpp"
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
		constexpr std::uint64_t local_soldier_global = 0x14230f260;
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
		std::atomic<bool> body_shoulders{ true }; // live setting: shoulder joints at shoulder_anchor from the head
		// Right shoulder joint from the head (between the eyes), OpenXR axes; the left one mirrored.
		constexpr ik::vec3 shoulder_anchor{ 0.18f, -0.22f, 0.04f };
		std::atomic<bool> legs_hidden{ true };   // live setting hide_legs
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
		std::atomic<bool> gun_on_ray{ true };     // live setting gun_pose (aim / animated)
		std::atomic<bool> gun_in_hand{ true };    // a primary or secondary gun is drawn (weapon_swap)
		std::atomic<float> support_weight{};       // two-handed hold, 0..1: the off hand is drawn on the gun
		std::atomic<float> recoil_strength{ 1.0f }; // live setting recoil
		std::mutex recoil_lock;
		gun_recoil recoil;
		recoil_offset last_kick; // for the pipe report
		recoil_profile last_profile;

		double steady_seconds()
		{
			return std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
		}

		std::atomic<bool> torso_follow{ true }; // live setting torso_follow
		torso_yaw torso_estimate;               // pose-hook thread only
		double torso_time{};
		std::atomic<float> torso_report{};       // radians, for the pipe
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
			for (int hand = 0; hand < 2; ++hand)
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
			return yaw;
		}
		// How far the drawn off hand may slide from where the animation holds the handguard, metres.
		constexpr float support_slide_back = 0.10f;
		constexpr float support_slide_forward = 0.20f;

		// Normalised linear blend on the shorter arc.
		ik::quat nlerp(ik::quat a, ik::quat b, float t)
		{
			const float s = a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0 ? -1.0f : 1.0f;
			return ik::normalize(ik::quat{ a.x + (s * b.x - a.x) * t, a.y + (s * b.y - a.y) * t, a.z + (s * b.z - a.z) * t, a.w + (s * b.w - a.w) * t });
		}
		std::atomic<bool> knife_in_hand{};
		std::atomic<float> knife_pitch{ 0.5235988f }; // radians, live setting knife_pitch
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
		// Racking (weapon/physical_reload): the off hand on the charging handle, the slide pulled back.
		struct handle_hold
		{
			float weight{};       // 0..1: how far the drawn hand is on the handle
			ik::vec3 handle;      // Wep_Root axes
			float pull{};         // metres back along the barrel; the slide bone moves with it
		};
		handle_hold handle_request;
		// Live setting off_hand_pose: the free off hand's wrist from the mirrored gun grip (true) or the
		// animated handguard hold turned by the controller (false).
		std::atomic<bool> off_hand_neutral{};
		// Contact grips (body/grasp_contact). All but the setting under rig_lock.
		std::atomic<int> magazine_grip{}; // live setting: 0 auto (palm up cups), 1 wrap, 2 cup
		bool cupped{};
		contact::cup_placement cup;        // this frame's cupped magazine, model space
		float cup_palm_up{};
		float cup_gap_mm[fingers::count]{}; // each finger's nearest point to the magazine after closing
		float cup_weight{};                 // the drawn magazine's blend from the fist (0) onto the palm (1)
		// Live setting: 0 pinch (thumb and index side), 1 hook (the index's curl), 2 auto (the pinch overhand,
		// the index and middle hooked underhand, by the palm as the hand takes the handle).
		std::atomic<int> rack_grip{ 2 };
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
		const authored_grip* authored_shown{}; // the one being eased (kept while easing out)
		float authored_weight{};
		bone_memory thumb_base_memory;       // the off hand's Thumb1 while an authored grip sets it
		ik::vec3 pinch_in_hand;             // where the knob belongs, hand-bone axes, from the last shaping
		bool has_pinch{};
		constexpr float knob_half = 0.008f; // a charging handle's knob, half its size
		float last_finger_seconds{};        // frame time of the last finger update
		constexpr float hand_half_thickness = 0.02f; // hand bone axis to the palm's skin
		constexpr float finger_radius = 0.009f;
		const contact::magazine_size magazine_body{}; // a rifle magazine (AK-12 / STANAG sized; not measured per gun yet)
		ik::quat gun_hand_in_aim;      // the gun hand's rotation in its aim axes, last gun drawn
		bool has_gun_hand_in_aim{};
		// Racking: the inside of the hooked index (centroid of Index1-3), hand-bone axes, from the last shaping.
		ik::vec3 hook_in_hand;
		bool has_hook{};
		// A held magazine's top sits this far above the fist's grasp centre (it is held by its body).
		constexpr float held_magazine_drop = 0.07f;
		bone_memory slide_memory;
		// Fingers: what each hand is doing and its eased shape (body/finger_pose).
		std::atomic<bool> finger_poses{ true };     // live setting finger_poses
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
			if (!read(local_soldier_global, soldier) || !read(soldier + 0x1e8, component) || !read(component + 0x128, animatable)
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
					geometry.has_ik[hand] = rig.ik_hand[hand] >= 0;
					if (geometry.has_ik[hand])
					{
						geometry.ik_hand[hand] = ik::compose(to_gun, ik::model_of(local, rig.parents, rig.ik_hand[hand]));
					}
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
						if (geometry.aiming_since >= 0.0 && now - geometry.aiming_since > ads_settle_seconds)
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
			// The gun hand first: a two-handed off hand is drawn on the gun where it now is.
			for (const int hand : { 1, 0 })
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
					if (hand == 1)
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
				const bool carry_weapon = hand == 1 && weapon_follows && rig.weapon >= 0;
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
					const ik::vec3 anchor{ (hand == 0 ? -1.0f : 1.0f) * (shoulder_anchor.x + width), shoulder_anchor.y, shoulder_anchor.z };
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
				if (carry_weapon && gun_in_hand.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed))
				{
					// The gun hand's authored grip with the barrel along the aim ray: how a hand holds a
					// pistol grip, i.e. the controller. Mirrored, it is the free off hand's wrist (off_hand_pose).
					gun_hand_in_aim = ik::normalize(ik::quat{ 0, 1, 0, 0 } * ik::conjugate(weapon_in_hand.rotation));
					has_gun_hand_in_aim = true;
				}
				if (hand == 0 && off_hand_neutral.load(std::memory_order_relaxed) && has_gun_hand_in_aim)
				{
					goal.hand = frame * ik::normalize(aim) * fingers::mirrored_hand(gun_hand_in_aim);
				}
				if (carry_weapon && gun_in_hand.load(std::memory_order_relaxed) && gun_on_ray.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed))
				{
					// gun_pose = aim: Wep_Root lies on the aim ray, upright ((0,1,0,0) in camera axes points
					// its +z barrel along -z), instead of the hip animation (its own angle, sway and bob)
					// turned by the controller. The hand keeps the gun's authored grip. Guns only: a
					// grenade or the C4 detonator keeps its own hold.
					goal.hand = frame * ik::normalize(aim) * ik::quat{ 0, 1, 0, 0 } * ik::conjugate(weapon_in_hand.rotation);
				}
				if (carry_weapon && knife_in_hand.load(std::memory_order_relaxed))
				{
					// The knife's Wep_Root points its blade about 30 degrees off -z towards +y, edge on +y
					// (monitor, 2026-10-27): 180 degrees about z from the camera axes, then knife_pitch up,
					// puts the blade along the controller ray, edge down. The hand follows the knife's grip.
					const float half = 0.5f * knife_pitch.load(std::memory_order_relaxed);
					const ik::quat level{ std::sin(half), 0, 0, std::cos(half) };
					goal.hand = frame * ik::normalize(aim) * level * ik::quat{ 0, 0, 1, 0 } * ik::conjugate(weapon_in_hand.rotation);
				}
				if (carry_weapon && gun_in_hand.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed))
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
				}
				const float support = support_weight.load(std::memory_order_relaxed);
				if (hand == 0 && gun_placed && geometry.valid && support > 0.0f)
				{
					// Two-handed: the off hand is drawn where the gun's own animation holds it, slid along
					// the barrel to where the controller is (within the handguard), blended by the hold.
					const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
					const ik::bone_transform& held_at = geometry.hand[0];
					const float along = ik::rotate(ik::conjugate(gun.rotation), goal.wrist - gun.position).z;
					const ik::vec3 on_gun{ held_at.position.x, held_at.position.y,
						std::fmax(held_at.position.z - support_slide_back, std::fmin(held_at.position.z + support_slide_forward, along)) };
					const ik::vec3 snapped = gun.position + ik::rotate(gun.rotation, on_gun);
					goal.wrist = goal.wrist + (snapped - goal.wrist) * support;
					goal.hand = nlerp(goal.hand, gun.rotation * held_at.rotation, support);
				}
				if (hand == 0 && gun_placed && handle_request.weight > 0.0f)
				{
					// Racking: the handle stays between the thumb and the index's side (pinch), or inside the
					// hooked index (hook), else under the palm, moved back by the pull, wherever the gun goes;
					// the hand keeps the controller's rotation.
					const ik::bone_transform gun = ik::model_of(local, rig.parents, rig.weapon);
					const ik::vec3 on_handle = gun.position + ik::rotate(gun.rotation, handle_request.handle - ik::vec3{ 0.0f, 0.0f, handle_request.pull });
					const bool pinch = pinching && has_pinch;
					const ik::vec3 hooked = pinch ? on_handle - ik::rotate(goal.hand, pinch_in_hand)
						: hooking_under && has_under_hook ? on_handle - ik::rotate(goal.hand, under_hook_in_hand)
						: has_hook ? on_handle - ik::rotate(goal.hand, hook_in_hand)
						: goal.wrist + (on_handle - (origin + ik::rotate(frame, palm)));
					goal.wrist = goal.wrist + (hooked - goal.wrist) * handle_request.weight;
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
				const float reach = ik::reach_ratio(local, rig.parents, bones, goal.wrist);
				if (ik::solve_arm(local, rig.parents, bones, goal))
				{
					any = true;
					last_arms[hand].solved = true;
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
						{
							const std::lock_guard dropped(dropped_lock);
							on_floor = gun_on_floor && gun_in_hand.load(std::memory_order_relaxed);
							floor_position = floor_gun_position;
							floor_yaw = floor_gun_yaw;
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
							ik::set_model_transform(local, rig.parents, rig.weapon, lying);
						}
						else
						{
							ik::set_model_transform(local, rig.parents, rig.weapon,
								ik::compose(ik::model_of(local, rig.parents, bones.hand), weapon_in_hand));
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
			if (hand == 1)
			{
				return gun_in_hand.load(std::memory_order_relaxed) && !knife_in_hand.load(std::memory_order_relaxed) ? fingers::task::gun_grip
					: fingers::task::item;
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
		void choose_magazine_grip(ik::bone_transform* local, fingers::task& task, fingers::hand_shape& target)
		{
			const bool holding = task == fingers::task::magazine && magazine_request.detached && magazine_request.in_hand;
			cup = holding ? place_cup(local) : contact::cup_placement{};
			const int mode = magazine_grip.load(std::memory_order_relaxed);
			cupped = cup.valid && (mode == 2 || (mode == 0 && contact::cup_grip(cupped, cup_palm_up)));
			if (!cupped)
			{
				if (holding && rig.thumb_base[0] > 0)
				{
					authored = magazine_request.ak ? &magazine_grip_ak : &magazine_grip_ar;
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

		// The off hand's palm normal (hand bone +y) against the gun's up (Wep_Root +y): 1 = palm up.
		float palm_up_on_gun(const ik::bone_transform* local)
		{
			if (rig.arms[0].hand < 0 || rig.weapon < 0)
			{
				return std::nanf("");
			}
			const ik::vec3 palm = ik::rotate(ik::model_of(local, rig.parents, rig.arms[0].hand).rotation, ik::vec3{ 0.0f, 1.0f, 0.0f });
			const ik::vec3 up = ik::rotate(ik::model_of(local, rig.parents, rig.weapon).rotation, ik::vec3{ 0.0f, 1.0f, 0.0f });
			return ik::dot(palm, up);
		}

		// Racking: the handle task becomes the game's own pinch (pinch_grip, eased in after the shaping)
		// for rack_grip = pinch and auto overhand; the curled thumb alone cannot reach the knob, the pinch
		// needs Thumb1's swing across the palm, which only the authored pose has. Auto underhand: the knob
		// hooked in the index and middle. Auto decides as the stroke starts and keeps it to the release.
		void choose_rack_grip(const ik::bone_transform* local, fingers::task& task, fingers::hand_shape& target)
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
				rack_palm_up = palm_up_on_gun(local);
				rack_underhand = contact::underhand_rack(rack_underhand, rack_palm_up);
			}
			hooking_under = on_handle && mode == 2 && rack_underhand;
			pinching = on_handle && (mode == 0 || (mode == 2 && !rack_underhand)) && rig.thumb_base[0] > 0;
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
			}
		}

		// The off hand's joint for authored_grip::joints[finger][joint]: Thumb1-3, else <Finger>1-3.
		int authored_bone(int finger, int joint)
		{
			if (finger == fingers::thumb)
			{
				return joint == 0 ? rig.thumb_base[0] : rig.finger_bones[0][fingers::thumb][joint - 1];
			}
			return rig.finger_bones[0][finger][joint];
		}

		// Eases the off hand's fingers into this frame's authored grip (over ~50 ms) and out of it.
		void apply_authored(ik::bone_transform* local, float seconds)
		{
			if (authored && authored != authored_shown)
			{
				authored_weight = authored_shown ? 0.0f : authored_weight; // a different grip starts from the shaped fingers
				authored_shown = authored;
			}
			authored_weight += ((authored ? 1.0f : 0.0f) - authored_weight) * (1.0f - std::exp(-seconds / 0.05f));
			if (!authored_shown || authored_weight < 0.001f)
			{
				authored_shown = authored;
				return;
			}
			for (int finger = 0; finger < fingers::count; ++finger)
			{
				for (int joint = 0; joint < 3; ++joint)
				{
					const int bone = authored_bone(finger, joint);
					if (bone <= 0)
					{
						continue;
					}
					local[bone].rotation = nlerp(local[bone].rotation, authored_shown->joints[finger][joint], authored_weight);
					bone_memory& remembered = finger == fingers::thumb && joint == 0 ? thumb_base_memory
						: finger_memory[0][finger][finger == fingers::thumb ? joint - 1 : joint];
					remembered.written = local[bone];
					remembered.has_written = true;
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
			if (const int base = rig.thumb_base[0]; base > 0)
			{
				if (thumb_base_memory.has_written && std::memcmp(&local[base], &thumb_base_memory.written, sizeof(local[base])) == 0)
				{
					local[base] = thumb_base_memory.animated;
				}
				else
				{
					thumb_base_memory.animated = local[base];
				}
				thumb_base_memory.has_written = false;
			}
			if (!finger_poses.load(std::memory_order_relaxed))
			{
				has_hook = false; // racking falls back to the palm
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
				if (hand == 0)
				{
					authored = nullptr;
					choose_magazine_grip(local, task, target);
					choose_rack_grip(local, task, target);
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
			// The inside of the off hand's index curl, for the next racking hold.
			const int (&index_joints)[fingers::joints] = rig.finger_bones[0][fingers::index];
			has_hook = rig.arms[0].hand >= 0 && index_joints[0] > 0 && index_joints[1] > 0 && index_joints[2] > 0;
			if (has_hook)
			{
				const ik::bone_transform hand = ik::model_of(local, rig.parents, rig.arms[0].hand);
				const ik::vec3 curl = (ik::model_of(local, rig.parents, index_joints[0]).position + ik::model_of(local, rig.parents, index_joints[1]).position
					+ ik::model_of(local, rig.parents, index_joints[2]).position) * (1.0f / 3.0f);
				hook_in_hand = ik::rotate(ik::conjugate(hand.rotation), curl - hand.position);
				// Underhand: inside both hooked fingers (Index1-3 and Middle1-3).
				const int (&middle_joints)[fingers::joints] = rig.finger_bones[0][fingers::middle];
				has_under_hook = middle_joints[0] > 0 && middle_joints[1] > 0 && middle_joints[2] > 0;
				if (has_under_hook)
				{
					const ik::vec3 middle = (ik::model_of(local, rig.parents, middle_joints[0]).position
						+ ik::model_of(local, rig.parents, middle_joints[1]).position + ik::model_of(local, rig.parents, middle_joints[2]).position) * (1.0f / 3.0f);
					under_hook_in_hand = ik::rotate(ik::conjugate(hand.rotation), (curl + middle) * 0.5f - hand.position);
				}
				// The pinch: the knob against the side of the index's middle segment, towards the thumb tip.
				const int thumb_tip = rig.finger_tips[0][fingers::thumb];
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
			}
		}

		// Called with rig_lock held, after edit_fingers: a held magazine sits in the drawn off hand's fist
		// as it sits in the gun, with the fist's grasp frame in the gun's place, and is published for the
		// reload (what you see is what goes into the magwell).
		void attach_held_magazine(ik::bone_transform* local)
		{
			points.has_held = false;
			if (!magazine_request.detached || !magazine_request.in_hand || rig.magazine < 0 || rig.weapon < 0)
			{
				return;
			}
			const auto (&bones)[fingers::count][fingers::joints] = rig.finger_bones[0];
			const int needed[] = { bones[fingers::index][0], bones[fingers::pinky][0], bones[fingers::middle][0], bones[fingers::middle][2], bones[fingers::thumb][1] };
			bool complete = last_arms[0].solved;
			for (const int bone : needed)
			{
				complete = complete && bone > 0;
			}
			const auto at = [&](int bone) { return ik::model_of(local, rig.parents, bone).position; };
			const bool palm_valid = cup.valid && last_arms[0].solved;
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
			ik::quat fist_rotation = ik::normalize(fist_frame * seated);
			ik::vec3 fist_top = fist.valid ? fist.centre + ik::rotate(fist.frame, ik::vec3{ 0.0f, held_magazine_drop, 0.0f }) : cup.top;
			if ((authored_shown == &magazine_grip_ak || authored_shown == &magazine_grip_ar) && authored_weight > 0.0f && rig.arms[0].hand >= 0)
			{
				// Where the game's own reload holds it, relative to the hand, eased in with the fingers.
				const ik::bone_transform hand = ik::model_of(local, rig.parents, rig.arms[0].hand);
				const ik::vec3 held = hand.position + ik::rotate(hand.rotation, authored_shown->magazine);
				fist_rotation = nlerp(fist_rotation, hand.rotation * authored_shown->magazine_rotation, authored_weight);
				fist_top = fist_top + (held - fist_top) * authored_weight;
			}
			const ik::quat palm_rotation = palm_valid ? ik::normalize(cup.frame * seated) : fist_rotation;
			const ik::vec3 palm_top = palm_valid ? cup.top : fist_top;
			placed.rotation = nlerp(fist_rotation, palm_rotation, on_palm);
			placed.position = fist_top + (palm_top - fist_top) * on_palm;
			// Palming: turned aside in the hand, leaving the magwell to the old magazine.
			contact::tilt_magazine(placed.position, placed.rotation, seated, magazine_request.tilt, magazine_request.tilt_pivot);
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
			const ik::quat to_camera = ik::conjugate(camera_frame(report.camera));
			if (rig.weapon >= 0)
			{
				const ik::bone_transform weapon = ik::model_of(local, rig.parents, rig.weapon);
				report.weapon_in_camera = to_camera * weapon.rotation;
				report.weapon_in_game = ik::conjugate(camera_frame(report.camera, false)) * weapon.rotation;
				report.weapon_position = ik::rotate(to_camera, weapon.position - report.camera.position);
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
					edit_arms(local);
					edit_weapon(local);
					edit_fingers(local);
					attach_held_magazine(local);
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

	void set_knife_pitch(float degrees)
	{
		knife_pitch = degrees * 3.14159265f / 180.0f;
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

	void set_body_shoulders(bool on)
	{
		body_shoulders = on;
	}

	void set_arm_shape(float length, float width)
	{
		arm_length = length > 0.5f && length < 2.0f ? length : 1.0f;
		shoulder_width = width;
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

	void set_magazine_pose(const magazine_pose& pose)
	{
		const std::lock_guard guard(rig_lock);
		magazine_request = pose;
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

	void set_dropped_gun(bool active, const float (&position)[3], float yaw)
	{
		const std::lock_guard dropped(dropped_lock);
		gun_on_floor = active;
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

	void kick_gun(int shots, const recoil_profile& gun)
	{
		const float strength = recoil_strength.load(std::memory_order_relaxed);
		const std::lock_guard guard(recoil_lock);
		recoil.shot(steady_seconds(), shots, strength, support_weight.load(std::memory_order_relaxed) >= 0.5f, gun);
		last_profile = gun;
	}

	std::string recoil_report()
	{
		char text[256];
		const std::lock_guard guard(recoil_lock);
		std::snprintf(text, sizeof(text),
			"recoil strength=%.2f drawn_kick_deg=%.2f drawn_back_m=%.4f drawn_yaw_deg=%.2f now_deg=%.2f gun vertical=%.2f first_shot=%.2f yaw=%.2f..%.2f",
			recoil_strength.load(), last_kick.degrees, last_kick.back, last_kick.yaw, recoil.at(steady_seconds()).degrees, last_profile.vertical,
			last_profile.first_shot, last_profile.yaw_min, last_profile.yaw_max);
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
		rack_grip = grip == "pinch" ? 0 : grip == "hook" ? 1 : 2;
	}

	void set_off_hand_neutral(bool neutral)
	{
		off_hand_neutral = neutral;
	}

	void set_off_hand_task(fingers::task task)
	{
		off_task = static_cast<int>(task);
		off_task_at = GetTickCount64();
	}

	void set_handle_hold(float weight, const float (&handle_in_gun)[3], float pull)
	{
		const std::lock_guard guard(rig_lock);
		handle_request.weight = std::fmax(0.0f, std::fmin(1.0f, weight));
		handle_request.handle = { handle_in_gun[0], handle_in_gun[1], handle_in_gun[2] };
		handle_request.pull = std::fmax(0.0f, pull);
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
				"\nauthored=%s weight=%.2f", grips[magazine_grip.load() % 3],
				cupped ? 1 : 0, cup_palm_up, cup_weight, cup_gap_mm[0], cup_gap_mm[1], cup_gap_mm[2], cup_gap_mm[3], cup_gap_mm[4],
				rack_grip.load() == 0 ? "pinch" : rack_grip.load() == 1 ? "hook" : "auto", pinching ? 1 : 0, hooking_under ? 1 : 0, rack_palm_up, pinch_gap_mm, pinch_in_hand.x, pinch_in_hand.y, pinch_in_hand.z,
				authored_shown == &pinch_grip ? "pinch" : authored_shown == &magazine_grip_ak ? "magazine_ak"
					: authored_shown == &magazine_grip_ar ? "magazine_ar" : "none",
				authored_weight);
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
			"knife=%d wep_root_in_camera=(%.3f,%.3f,%.3f,%.3f) torso_follow=%d torso_deg=%.1f\n",
			ik_enabled.load() ? 1 : 0, rig.camera >= 0 ? rig.name : "-", rig.bones, conversions.load(), matched.load(), solved.load(),
			hands.valid[0] ? 1 : 0, hands.valid[1] ? 1 : 0, hand_targets[0].active ? 1 : 0, hand_targets[1].active ? 1 : 0,
			r.fk_error, r.fk_error_conjugate, r.at ? GetTickCount64() - r.at : 0ull,
			v(r.camera.position, a), q.x, q.y, q.z, q.w, v(ik::rotate(q, { 1, 0, 0 }), b), v(ik::rotate(q, { 0, 1, 0 }), c),
			v(ik::rotate(q, { 0, 0, 1 }), d), r.camera.scale, knife_in_hand.load() ? 1 : 0, r.weapon_in_camera.x, r.weapon_in_camera.y,
			r.weapon_in_camera.z, r.weapon_in_camera.w, torso_follow.load() ? 1 : 0, torso_report.load() * 57.29578f);
		for (int hand = 0; hand < 2 && length > 0 && length < static_cast<int>(sizeof(text)); ++hand)
		{
			const arm_report& arm = r.arms[hand];
			length += std::snprintf(text + length, sizeof(text) - length, "%s solved=%d reach=%.2f shoulder=%s elbow=%s wrist=%s goal=%s wrist_in_camera=%s\n",
				hand == 0 ? "left " : "right", arm.solved ? 1 : 0, arm.reach, v(arm.shoulder, a), v(arm.elbow, b), v(arm.wrist, c), v(arm.goal, d),
				v(arm.wrist_in_camera, e));
		}
		return text;
	}
}
