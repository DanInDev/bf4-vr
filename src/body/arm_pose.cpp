#include "body/arm_pose.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>

#include <MinHook.h>

#include "body/arm_ik.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "safety/mp_guard.hpp"
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
			int legs[2]{ -1, -1 }; // LeftUpLeg, RightUpLeg: hidden in VR (they sit oddly under a real head)
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
			ik::quat aim_xr;                       // the aim rotation used, OpenXR LOCAL
			ik::vec3 wrist_in_camera;              // OpenXR camera axes
		};

		struct check_report
		{
			ULONGLONG at{};
			float fk_error{ -1 };           // max position error of our FK against the engine's model pose
			float fk_error_conjugate{ -1 }; // the same with the quaternions read as conjugates
			ik::bone_transform camera;
			arm_report arms[2];
		};

		// Guards everything below: the matching worker thread, the rig refresh and the pipe.
		std::mutex rig_lock;
		rig_info rig;
		std::atomic<std::uint64_t> rig_pose{}; // rig.local_pose, for the lock-free match
		std::atomic<ULONGLONG> next_refresh{};
		std::atomic<ULONGLONG> next_check{};
		// Bones the IK rewrites: both arms (upper, fore, roll, hand) and the weapon root.
		constexpr int max_edited = 11; // + the two shoulder (clavicle) bones for shoulder_width
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
		std::atomic<bool> legs_hidden{ true };   // live setting hide_legs
		ik::vec3 pole_direction[2]; // elbow hint from the shoulder, OpenXR camera axes
		check_report last_check;
		arm_report last_arms[2];
		// Physical reload: where the gun's magazine and slide are (published each evaluation) and
		// where the magazine should be drawn (set by weapon/physical_reload).
		weapon_points points;
		magazine_pose magazine_request;
		bone_memory magazine_memory;

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
			fresh.legs[0] = find_bone(bone_names, fresh.bones, "LeftUpLeg");
			fresh.legs[1] = find_bone(bone_names, fresh.bones, "RightUpLeg");
			if (fresh.magazine >= 0 && fresh.parents[fresh.magazine] != fresh.weapon)
			{
				fresh.magazine = -1;
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
			log::write("arms: skeleton %s, %d bones, camera %d, weapon %d, left %d/%d/%d/%d, right %d/%d/%d/%d%s", fresh.name, fresh.bones,
				fresh.camera, fresh.weapon, fresh.arms[0].upper, fresh.arms[0].fore, fresh.arms[0].roll, fresh.arms[0].hand, fresh.arms[1].upper,
				fresh.arms[1].fore, fresh.arms[1].roll, fresh.arms[1].hand, complete ? "" : " (incomplete: arms left alone)");
			if (complete)
			{
				rig = fresh;
				rig_pose = local_pose;
				magazine_memory = {};
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
		// pitch the joint does not carry (soldier +0x4DC, radians, up positive; aim.md).
		ik::quat camera_frame(const ik::bone_transform& camera_joint)
		{
			float pitch = 0.0f;
			if (!read(rig.soldier + 0x4dc, pitch) || !std::isfinite(pitch))
			{
				pitch = 0.0f;
			}
			return camera_joint.rotation * camera_axes * ik::quat{ std::sin(0.5f * pitch), 0, 0, std::cos(0.5f * pitch) };
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
			if (!ik_enabled.load(std::memory_order_relaxed))
			{
				return;
			}

			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const stereo::hand_sample hands = stereo::latest_hand_sample();
			// Synthetic targets are placed relative to the view: the head (between the eyes) when tracked.
			ik::vec3 head_position;
			ik::quat head_rotation;
			if (const stereo::head_sample head = stereo::latest_head_sample(); head.valid)
			{
				const float* left = head.eyes[0].position;
				const float* right = head.eyes[1].position;
				const float* q = head.eyes[0].orientation;
				head_position = { 0.5f * (left[0] + right[0]), 0.5f * (left[1] + right[1]), 0.5f * (left[2] + right[2]) };
				head_rotation = { q[0], q[1], q[2], q[3] };
			}
			bool any = false;
			bool shaped = false; // written bones must be remembered even when no arm solves
			for (int hand = 0; hand < 2; ++hand)
			{
				ik::vec3 wrist;
				ik::quat aim;
				if (targets[hand].active)
				{
					wrist = head_position + ik::rotate(head_rotation, targets[hand].position);
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
					const float* q = hands.aim[hand].orientation;
					aim = { q[0], q[1], q[2], q[3] };
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
				if (width != 0.0f && bones.upper >= 0 && rig.parents[bones.upper] >= 0)
				{
					const int clavicle = rig.parents[bones.upper];
					ik::bone_transform moved = ik::model_of(local, rig.parents, clavicle);
					moved.position = moved.position + ik::rotate(frame, ik::vec3{ hand == 0 ? -width : width, 0.0f, 0.0f });
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
				goal.wrist = camera.position + ik::rotate(frame, wrist);
				goal.hand = frame * ik::normalize(aim) * ik::conjugate(frame) * hand_animated;
				goal.pole = ik::model_of(local, rig.parents, bones.upper).position + ik::rotate(frame, pole_direction[hand]);
				if (ik::solve_arm(local, rig.parents, bones, goal))
				{
					any = true;
					last_arms[hand].solved = true;
					last_arms[hand].goal = goal.wrist;
					last_arms[hand].goal_xr = wrist;
					last_arms[hand].aim_xr = ik::normalize(aim);
					if (carry_weapon)
					{
						ik::set_model_transform(local, rig.parents, rig.weapon,
							ik::compose(ik::model_of(local, rig.parents, bones.hand), weapon_in_hand));
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

			const ik::bone_transform camera = ik::model_of(local, rig.parents, rig.camera);
			const ik::quat frame = camera_frame(camera);
			const ik::quat to_xr = ik::conjugate(frame);
			const ik::bone_transform magazine = ik::model_of(local, rig.parents, rig.magazine);
			const ik::vec3 magazine_xr = ik::rotate(to_xr, magazine.position - camera.position);
			const ik::quat magazine_rotation_xr = to_xr * magazine.rotation;
			points.valid = true;
			points.at = GetTickCount64();
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
				const ik::vec3 slide = ik::rotate(to_xr, ik::model_of(local, rig.parents, rig.slide).position - camera.position);
				points.slide[0] = slide.x;
				points.slide[1] = slide.y;
				points.slide[2] = slide.z;
			}
			const auto publish = [&](int bone, bool& has, float (&out)[3])
			{
				has = bone >= 0;
				if (has)
				{
					const ik::vec3 p = ik::rotate(to_xr, ik::model_of(local, rig.parents, bone).position - camera.position);
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
			}
			publish(rig.rail, points.has_rail, points.rail);
			for (int hand = 0; hand < 2; ++hand)
			{
				const arm_report& arm = last_arms[hand];
				points.hand_valid[hand] = arm.solved;
				points.hand[hand][0] = arm.goal_xr.x;
				points.hand[hand][1] = arm.goal_xr.y;
				points.hand[hand][2] = arm.goal_xr.z;
				points.hand_rotation[hand][0] = arm.aim_xr.x;
				points.hand_rotation[hand][1] = arm.aim_xr.y;
				points.hand_rotation[hand][2] = arm.aim_xr.z;
				points.hand_rotation[hand][3] = arm.aim_xr.w;
			}

			if (!magazine_request.detached)
			{
				return;
			}
			const float* p = magazine_request.position;
			const float* q = magazine_request.rotation;
			ik::bone_transform placed = magazine;
			placed.position = camera.position + ik::rotate(frame, ik::vec3{ p[0], p[1], p[2] });
			placed.rotation = frame * ik::normalize(ik::quat{ q[0], q[1], q[2], q[3] });
			ik::set_model_transform(local, rig.parents, rig.magazine, placed);
			magazine_memory.written = current;
			magazine_memory.has_written = true;
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

	bool arm_ik_enabled()
	{
		return ik_enabled;
	}

	void set_legs_hidden(bool hidden)
	{
		legs_hidden = hidden;
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
			"camera_joint pos=%s rot=(%.4f,%.4f,%.4f,%.4f) right=%s up=%s back=%s scale=%.3f\n",
			ik_enabled.load() ? 1 : 0, rig.camera >= 0 ? rig.name : "-", rig.bones, conversions.load(), matched.load(), solved.load(),
			hands.valid[0] ? 1 : 0, hands.valid[1] ? 1 : 0, hand_targets[0].active ? 1 : 0, hand_targets[1].active ? 1 : 0,
			r.fk_error, r.fk_error_conjugate, r.at ? GetTickCount64() - r.at : 0ull,
			v(r.camera.position, a), q.x, q.y, q.z, q.w, v(ik::rotate(q, { 1, 0, 0 }), b), v(ik::rotate(q, { 0, 1, 0 }), c),
			v(ik::rotate(q, { 0, 0, 1 }), d), r.camera.scale);
		for (int hand = 0; hand < 2 && length > 0 && length < static_cast<int>(sizeof(text)); ++hand)
		{
			const arm_report& arm = r.arms[hand];
			length += std::snprintf(text + length, sizeof(text) - length, "%s solved=%d shoulder=%s elbow=%s wrist=%s goal=%s wrist_in_camera=%s\n",
				hand == 0 ? "left " : "right", arm.solved ? 1 : 0, v(arm.shoulder, a), v(arm.elbow, b), v(arm.wrist, c), v(arm.goal, d),
				v(arm.wrist_in_camera, e));
		}
		return text;
	}
}
