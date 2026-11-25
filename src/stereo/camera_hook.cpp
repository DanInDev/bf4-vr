#include "stereo/camera_hook.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <mutex>

#include <MinHook.h>

#include "common/config.hpp"
#include "control/control_pipe.hpp"
#include "physics/ray_cast.hpp"
#include "weapon/game_reload_sounds.hpp"
#include "weapon/own_crate.hpp"
#include "weapon/shot_origin.hpp"
#include "weapon/two_handed.hpp"
#include "weapon/weapon_swap.hpp"
#include "common/log.hpp"
#include "safety/mp_guard.hpp"
#include "input/vr_controls.hpp"
#include "stereo/eye_math.hpp"
#include "body/arm_pose.hpp"
#include "stereo/eye_pass.hpp"
#include "stereo/gun_pass_fov.hpp"
#include "stereo/gun_pass_fov_math.hpp"
#include "stereo/scope.hpp"
#include "input/physical_stance.hpp"
#include "stereo/scripted_view.hpp"
#include "stereo/steady_camera.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::stereo
{
	namespace
	{
		// BF4 179547 (x64) addresses (re/notes/camera.md); the hook is not installed on x86 builds.
		constexpr std::uint64_t game_renderer_global = 0x142672378;
		constexpr std::uint64_t view_params_copy = 0x14083a710;
		constexpr std::size_t render_view_offset = 0x60;

		// The copied view-params block holds three RenderViews (+0x0 world camera, +0x4E0 and +0x9C0
		// without visible effect), then the camera the first-person pass (weapon, arms) is drawn
		// with: rows right, up, forward, position in soldier-local space (position = eye above the
		// feet). Shifting it moves only the gun (re/notes/first_person.md).
		constexpr std::size_t second_view = 0x4e0;
		constexpr std::size_t first_person_transform = 0xed0;

		// RenderView layout: camera transform rows (right, up, forward, position) at +0x40,
		// FOV candidate at +0xB4, aspect at +0xC4, dirty flags at +0xE0
		// (set bits = cached matrices invalid, rebuilt by the renderer).
		constexpr std::size_t transform = 0x40;
		constexpr std::size_t fov_y = 0xb4;
		constexpr std::size_t aspect = 0xc4;
		constexpr std::size_t dirty_flags = 0xe0;

		// Prologue of view_params_copy: mov [rsp+8],rbx; push rdi; sub rsp,20h; mov rbx,rdx; mov rdi,rcx; call ...
		constexpr std::uint8_t expected_prologue[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x57, 0x48, 0x83, 0xec, 0x20, 0x48, 0x8b, 0xda, 0x48, 0x8b, 0xf9, 0xe8 };

		using view_params_copy_t = void*(__fastcall*)(void* destination, const void* source);
		view_params_copy_t original_copy{};

		// RenderView set-aspect: called each frame after the copy with the viewport's width/height;
		// stores +0xC4, recomputes the horizontal FOV (+0x250) and marks the matrices dirty.
		// Replacing the value renders an anamorphic image with the headset's per-eye aspect.
		constexpr std::uint64_t view_set_aspect = 0x140b89d60;
		// push rbx; sub rsp,30h; movaps [rsp+20h],xmm6; movaps xmm6,xmm1; mov rbx,rcx
		constexpr std::uint8_t set_aspect_prologue[] = { 0x40, 0x53, 0x48, 0x83, 0xec, 0x30, 0x0f, 0x29, 0x74, 0x24, 0x20, 0x0f, 0x28, 0xf1, 0x48, 0x8b, 0xd9 };
		using view_set_aspect_t = void(__fastcall*)(void* view, float aspect);
		view_set_aspect_t original_set_aspect{};
		// Aspect for the world view this frame (0 = the game's own), set by the copy hook.
		std::atomic<float> aspect_override{};
		std::atomic<float> research_aspect{};
		bool stereo_headset_aspect{};
		std::atomic<float> offset_metres{};
		std::atomic<DWORD> copy_thread{};
		std::atomic<std::uint64_t> copies{};
		// Research mode: odd copies +amplitude, even copies -amplitude (measures copy->present lag).
		std::atomic<float> alternate_amplitude{};
		std::atomic<float> fov_override{};
		// Research overrides per RenderView in the params block (0, 0x4E0, 0x9C0).
		constexpr std::size_t view_offsets[3] = { 0x0, second_view, 0x9c0 };
		// Research: a synthetic head yaw (degrees) applied like an eye pose when no headset drives
		// the camera, to check that the world and the gun turn together.
		std::atomic<float> fake_yaw_degrees{};
		// Apparent size of the gun and arms (1 = the game's). The foreground pass has its own fixed
		// FOV; multiplying the first-person camera's right and up rows by g scales the drawn gun by g
		// around the view centre (g = 1/0.7 made it visibly larger, re/notes/first_person.md).
		std::atomic<float> gun_scale{ 1.0f };
		// gun_life_size: the foreground pass draws at a fixed 45 degree vertical FOV with the world's aspect
		// (re/notes/first_person.md); scaling its rows by tan(22.5)/tan(fovY/2) draws it as the world would.
		std::atomic<bool> gun_life_size{ true };
		// gun_fov_life_size (#39): life size through the gun pass's own FOV (stereo/gun_pass_fov) instead of
		// scaling the first-person rows, which broke the sights' lens and reticle.
		std::atomic<bool> gun_fov_life_size{ true };
		constexpr float hip_foreground_half_fov = 0.39269908f; // the gun pass at the hip (45 degrees)
		std::atomic<float> applied_foreground_half_fov{ 0.39269908f }; // report

		// Scenes where the game moves the camera (stereo/scripted_view): shown on the theater screen
		// (cutscene_view = screen) or head-tracked as usual (look). Game thread.
		std::atomic<bool> cutscene_screen{ true };
		scripted_view scripted;
		LARGE_INTEGER scripted_last{};
		std::atomic<bool> scripted_now{};
		std::atomic<float> scripted_deviation{};
		std::atomic<float> scripted_health{};

		// Physical stance (#33, input/physical_stance): the real head's height asks for the game's
		// stance; the view then follows the real head only, the game's own eye drop added back.
		std::atomic<bool> physical_stance_on{};
		std::atomic<bool> physical_stance_reset{};
		input::physical_stance stance_body;
		LARGE_INTEGER stance_last{};
		float standing_eye{ 1.565f }; // the game's camera above the soldier's feet standing (live, AK-12 soldier)
		std::atomic<float> stance_drop{};
		std::atomic<float> stance_lift{};
		// #154 (owner 2026-11-24): the soldier's eye (1.565 m above the feet) sat lower than the squad's heads. The view,
		// the body rig and the shot origin all rise by this (setting extra_height), as they do for the physical stance.
		std::atomic<float> extra_eye_height{ 0.12f };
		std::atomic<int> stance_wanted{};

		// The game camera's height above the local soldier's feet: the first-person animatable component
		// (soldier +0x1E8) keeps the body's world transform at +0x90, translation = feet (+0xC0).
		bool eye_above_feet(const std::uint8_t* view, float& height)
		{
			std::uintptr_t soldier{}, component{};
			float feet[3]{};
			SIZE_T done = 0;
			const HANDLE self = GetCurrentProcess();
			if (!ReadProcessMemory(self, reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(0x14230f260ull)), &soldier, sizeof(soldier), &done) || !soldier
				|| !ReadProcessMemory(self, reinterpret_cast<LPCVOID>(soldier + 0x1e8), &component, sizeof(component), &done) || !component
				|| !ReadProcessMemory(self, reinterpret_cast<LPCVOID>(component + 0xc0), feet, sizeof(feet), &done))
			{
				return false;
			}
			height = reinterpret_cast<const float*>(view + transform)[13] - feet[1];
			return std::isfinite(height);
		}

		// Called before the eye pose goes onto the camera: returns how far to raise the eye (metres).
		float update_physical_stance(const std::uint8_t* view, float head_height)
		{
			LARGE_INTEGER counter{}, frequency{};
			QueryPerformanceCounter(&counter);
			QueryPerformanceFrequency(&frequency);
			const float seconds = stance_last.QuadPart
				? std::fmin(static_cast<float>(counter.QuadPart - stance_last.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
			stance_last = counter;
			if (physical_stance_reset.exchange(false))
			{
				stance_body.reset();
			}
			const input::physical_stance_result body = stance_body.update(head_height, seconds);
			if (body.changed)
			{
				input::request_stance(body.stance);
			}
			stance_drop = body.drop;
			stance_wanted = body.stance;
			float eye{};
			if (!eye_above_feet(view, eye))
			{
				stance_lift = 0.0f;
				return 0.0f;
			}
			if (input::game_stance() == 0 && eye > 1.4f && eye < 1.7f)
			{
				// Learns this soldier's standing eye; readings mid stand-up (stance already 0) are below the range.
				standing_eye += (eye - standing_eye) * std::fmin(1.0f, seconds * 2.0f);
			}
			const float lift = std::fmax(0.0f, std::fmin(1.5f, standing_eye - eye));
			stance_lift = lift;
			return lift;
		}

		bool update_scripted(const std::uint8_t* view)
		{
			LARGE_INTEGER counter{}, frequency{};
			QueryPerformanceCounter(&counter);
			QueryPerformanceFrequency(&frequency);
			const float seconds = scripted_last.QuadPart
				? std::fmin(static_cast<float>(counter.QuadPart - scripted_last.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
			scripted_last = counter;
			scripted_input input;
			float health{};
			input.soldier = input::soldier_health(health);
			input.dead = input.soldier && health <= 0.0f;
			float yaw{}, pitch{};
			input.aim_known = input::aim_angles(yaw, pitch);
			// The camera looks along -row 2 of its transform (live: row 2 (0, 0, -1) with the aimer at 0, 0).
			const auto* rows = reinterpret_cast<const float*>(view + transform);
			const float forward[3] = { -rows[8], -rows[9], -rows[10] };
			input.deviation_degrees = input.aim_known ? aim_deviation_degrees(forward, yaw, pitch) : NAN;
			scripted_deviation = input.deviation_degrees;
			scripted_health = input.soldier ? health : -1.0f;
			const bool active = scripted.update(input, seconds);
			scripted_now = active;
			return active;
		}
		std::atomic<float> applied_gun_scale{ 1.0f };
		std::atomic<float> ipd_scale{ 1.0f };
		std::atomic<float> motion_yaw_rate{};
		std::atomic<float> motion_speed{};
		// The turn aim_with_controller gave the first-person camera, for the arm IK (pose thread).
		// Valid briefly after each copy, so a frame without controller aim falls back to the game's.
		std::atomic<float> fp_turn_yaw{};
		std::atomic<bool> aim_follows_torso{ true }; // live setting aim_follows_torso
		std::atomic<ULONGLONG> fp_turn_until{};
		constexpr ULONGLONG fp_turn_hold_ms = 200;

		// steady_walk (stereo/steady_camera.hpp): the fast part of the first-person eye's sideways offset
		// (soldier space, +0xED0 row 3 x) comes off the world camera only. The gun pass stays as it is:
		// it is anchored to the same swaying camera joint as the arms, so the gun already holds still in
		// the view (monitor 2026-11-04: steadying the first-person camera too made the gun sway instead).
		void mark_dirty(std::uint8_t* view);
		std::atomic<bool> steady_walk_on{ true };
		sway_filter steady_sway;
		LARGE_INTEGER steady_last{};

		void steady_walk_sway(std::uint8_t* params, bool walking_frame)
		{
			LARGE_INTEGER counter{}, frequency{};
			QueryPerformanceCounter(&counter);
			QueryPerformanceFrequency(&frequency);
			const float seconds = steady_last.QuadPart
				? static_cast<float>(counter.QuadPart - steady_last.QuadPart) / static_cast<float>(frequency.QuadPart) : 0.0f;
			steady_last = counter;
			if (!steady_walk_on.load(std::memory_order_relaxed) || !walking_frame || seconds > 0.5f)
			{
				steady_sway.reset();
				return;
			}
			const auto* fp = reinterpret_cast<const float*>(params + first_person_transform);
			auto* world = reinterpret_cast<float*>(params + transform);
			const float sway = steady_sway.update(fp[12], seconds);
			// Soldier x in world axes: x . F^T . W (rows: F soldier-local camera, W world camera).
			for (int a = 0; a < 3; ++a)
			{
				world[12 + a] -= (fp[0] * world[a] + fp[4] * world[4 + a] + fp[8] * world[8 + a]) * sway;
			}
			mark_dirty(params);
		}

		// Game thread, once per head-tracked copy: the body camera (world rows before the eye pose).
		// #53: where the head put the view this frame (pipe lean). Head: eyes' midpoint in LOCAL (x right,
		// y up, z back of the body, since the body camera's rows are the LOCAL axes); body: the soldier's
		// camera position in the world before the head pose; view: the rendered eye's position after it.
		struct lean_sample
		{
			bool valid{};
			int eye{};
			float head[3]{};
			float body[3]{};
			float view[3]{};
		};
		std::mutex lean_lock;
		lean_sample lean_last;

		void track_body_motion(const float* world)
		{
			static LARGE_INTEGER last{};
			static float last_yaw{}, last_position[3]{};
			LARGE_INTEGER now{}, frequency{};
			QueryPerformanceCounter(&now);
			QueryPerformanceFrequency(&frequency);
			const float yaw = std::atan2(-world[8], -world[10]);
			const float seconds = last.QuadPart ? static_cast<float>(now.QuadPart - last.QuadPart) / static_cast<float>(frequency.QuadPart) : 0.0f;
			if (seconds > 1e-4f && seconds < 0.2f)
			{
				float turn = yaw - last_yaw;
				turn += turn > 3.14159265f ? -6.2831853f : turn < -3.14159265f ? 6.2831853f : 0.0f;
				const float dx = world[12] - last_position[0], dy = world[13] - last_position[1], dz = world[14] - last_position[2];
				const float speed = std::sqrt(dx * dx + dy * dy + dz * dz) / seconds;
				const float blend = std::min(seconds / 0.05f, 1.0f);
				motion_yaw_rate = motion_yaw_rate.load() + (turn / seconds - motion_yaw_rate.load()) * blend;
				// Longer for speed: pushing against a wall jitters back and forth at 10 m/s. A teleport
				// (respawn, checkpoint) is not motion.
				const float speed_blend = std::min(seconds / 0.15f, 1.0f);
				motion_speed = motion_speed.load() + ((speed < 40.0f ? speed : 0.0f) - motion_speed.load()) * speed_blend;
			}
			last = now;
			last_yaw = yaw;
			for (int axis = 0; axis < 3; ++axis)
			{
				last_position[axis] = world[12 + axis];
			}
		}
		// Fraction (tangent space) of the headset's reported FOV that is rendered, horizontal and
		// vertical (1 = all of it); config view_crop_h / view_crop_v.
		std::atomic<float> view_crop_h{ 1.0f };
		std::atomic<float> view_crop_v{ 1.0f };
		std::atomic<float> view_crop_bottom_rad{ 0.0f }; // view_crop_bottom: radians cut at the bottom, camera tilted up by half
		std::atomic<float> view_fov[3]{};
		std::atomic<float> view_shift[3]{};
		// Research: shift any transform (rows right, up, forward, position) inside the view-params
		// block, e.g. the camera copies after the three RenderViews.
		constexpr std::uint32_t max_transform_offset = 0x10C0;
		std::atomic<std::uint32_t> transform_shift_offset[4]{};
		std::atomic<float> transform_shift_metres[4]{};
		// Research: pin a float at an offset inside the view-params block every copy (0 offset = off),
		// marking view 0's matrices dirty so cached projections are rebuilt.
		std::atomic<std::uint32_t> pin_offset[4]{};
		std::atomic<float> pin_value[4]{};
		bool stereo_enabled{};
		// stereo_render = pair (default): both eyes from one game frame (stereo/eye_pass.hpp).
		std::atomic<bool> pair_render{};
		bool stereo_first_person{};

		// Live WorldRenderSettings, cached by the engine (lookup 0x140688C50([0x14261E930], its ClassInfo
		// 0x142C79010) at 0x140CCAF74 stores it here); +0x23C PostProcessAntialiasingMode
		// (re/notes/stereo-rendering.md, "Pair as the default").
		constexpr std::uint64_t world_render_settings_global = 0x1426724a0;
		constexpr std::size_t post_aa_mode = 0x23c;
		constexpr std::uint32_t aa_smaa_1x = 4;
		constexpr std::uint32_t aa_smaa_t2x = 5;
		bool smaa_downgraded{};

		// SMAA T2x blends the previous frame, which in pair mode is the other eye: run it as SMAA 1x
		// while pairing and give the game its T2x back afterwards.
		void guard_temporal_aa(bool pair)
		{
			std::uint64_t settings{};
			SIZE_T done = 0;
			std::uint32_t mode{};
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(world_render_settings_global), &settings, sizeof(settings), &done)
				|| !settings
				|| !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(settings + post_aa_mode), &mode, sizeof(mode), &done))
			{
				return;
			}
			const auto write = [&](std::uint32_t value) {
				WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(settings + post_aa_mode), &value, sizeof(value), nullptr);
			};
			if (pair && mode == aa_smaa_t2x)
			{
				write(aa_smaa_1x);
				smaa_downgraded = true;
				log::write("camera: pair rendering runs the game's SMAA T2x as SMAA 1x (no history across eyes)");
			}
			else if (!pair && smaa_downgraded)
			{
				if (mode == aa_smaa_1x)
				{
					write(aa_smaa_t2x);
				}
				smaa_downgraded = false;
			}
		}

		std::uint8_t* renderer_view_params()
		{
			std::uintptr_t renderer = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(game_renderer_global)), &renderer, sizeof(renderer), nullptr) || !renderer)
			{
				return nullptr;
			}
			std::uintptr_t params = 0;
			ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(renderer + render_view_offset), &params, sizeof(params), nullptr);
			return reinterpret_cast<std::uint8_t*>(params);
		}

		void mark_dirty(std::uint8_t* view)
		{
			*reinterpret_cast<std::uint32_t*>(view + dirty_flags) |= 0x7;
		}

		void shift_sideways(std::uint8_t* view, float metres)
		{
			const auto* right = reinterpret_cast<const float*>(view + transform);
			auto* position = reinterpret_cast<float*>(view + transform + 0x30);
			for (int axis = 0; axis < 3; ++axis)
			{
				position[axis] += right[axis] * metres;
			}
			mark_dirty(view);
		}

		// A z-mirrored conversion made yaw and pitch run backwards in the headset (2026-10-23
		// test), so the OpenXR pose is composed unchanged (eye_math::compose_pose).
		void compose_pose(float* camera, const pose& eye)
		{
			eye_math::compose_pose(camera, eye);
		}

		// The eye pose goes onto the world camera and, unless disabled, onto the first-person
		// camera, so the gun stays where the soldier aims while the head looks around.
		void apply_eye_pose(std::uint8_t* params, const pose& eye)
		{
			compose_pose(reinterpret_cast<float*>(params + transform), eye);
			mark_dirty(params);
			if (stereo_first_person)
			{
				compose_pose(reinterpret_cast<float*>(params + first_person_transform), eye);
			}
		}

		// Rows (right, up, back) of a level camera looking along camera yaw c (atan2(look.x, look.z));
		// looking along +z the right row is -x (RenderView data, re/notes/camera.md).
		void level_rows(float* rows, float c)
		{
			const float s = std::sin(c), k = std::cos(c);
			const float level[12] = { -k, 0, s, 0, 0, 1, 0, 0, -s, 0, -k, 0 };
			for (int i = 0; i < 12; ++i)
			{
				rows[i] = level[i];
			}
		}

		// Controller aim (#12): the world camera turns with the body yaw only (the head adds the rest),
		// the first-person camera (soldier space) is turned to match, and the aimer follows the right
		// controller's aim ray.
		// scene_aim = head: in a scripted scene the game holds the weapon itself (the Baku car's M320 lean-out:
		// its launcher is drawn fixed to the view, no gun is in the mod's hands), so the game aims along the
		// view's centre and its shot frame is left alone (weapon/shot_origin). Before, rounds followed the
		// controller's ray, 27-75 degrees off what the player lined up (log 2026-11-19, re/notes/aim.md).
		// scene_aim = game (default): the visible hit was not enough there. The launcher's round is spawned by the
		// game's in-process server from the game's own aim, which the scene resets as it fires: the round seen
		// hit, the helicopter took nothing (owner, 2026-11-19). So in a scripted scene the mod lets go: the game's
		// scene camera is shown (the head on top), its aim and shot frame are its own, and the right stick and the
		// left trigger (its aim down sights) reach it (input/vr_controls), as on a flat screen.
		std::atomic<scene_aim> scene_aim_mode{ scene_aim::game };
		// Held 1.5 s past the detector's end (camera against aim): it can let go for a moment mid-scene.
		std::atomic<ULONGLONG> scene_until{};
		std::atomic<bool> scene_stick_used{};          // #153: the right stick asked for the game's view in this scene
		constexpr int scene_stick_threshold = 16000;   // about half way
		bool scene_now()
		{
			return GetTickCount64() < scene_until.load(std::memory_order_relaxed);
		}
		std::atomic<bool> scene_aiming{}; // this frame aims along the view (read by weapon/shot_origin)
		// The view's centre in LOCAL while scene_aiming. The game resets its aimer when the scene's launcher fires
		// (log 2026-11-19: back to the car's heading at each trigger pull), so the round follows the shot frame,
		// which weapon/shot_origin points along this.
		std::atomic<float> scene_view_local[3]{ 0.0f, 0.0f, -1.0f };

		void aim_with_controller(std::uint8_t* params, const head_sample& head, bool scripted_frame)
		{
			auto* world = reinterpret_cast<float*>(params + transform);
			const float game_yaw = std::atan2(-world[8], -world[10]);
			if (input::vehicle_view(game_yaw))
			{
				// Driving: the view rides with the vehicle (its camera as the game gives it, pitch and roll
				// included, so the cockpit stays put around the head); nothing to aim.
				return;
			}
			const float body = input::body_camera_yaw(game_yaw);
			level_rows(world, body);
			if (stereo_first_person)
			{
				// The first-person rows are in soldier space; the world camera is the soldier's yaw on top of
				// them. Their own yaw (the camera joint's, from the soldier) is ~0 while the soldier faces where
				// it aims, but after a snap turn the soldier's body lags the aimer (60 degrees after two snaps,
				// monitor 2026-11-09): rendered at body - game_yaw alone, the gun pass turned by that lag
				// against the world and the arms (headset: only the starting orientation lined up, #37).
				auto* first_person = reinterpret_cast<float*>(params + first_person_transform);
				const float soldier_look = std::atan2(-first_person[8], -first_person[10]);
				level_rows(first_person, body - game_yaw + soldier_look);
				// arm_pose turns the camera joint (which carries soldier_look) by this.
				fp_turn_yaw = body - game_yaw;
				fp_turn_until = GetTickCount64() + fp_turn_hold_ms;
			}
			mark_dirty(params);
			const bool along_view = scripted_frame && head.valid && scene_aim_mode.load(std::memory_order_relaxed) == scene_aim::head;
			scene_aiming = along_view;
			if (along_view)
			{
				// The view's centre (both eyes' middle) in LOCAL, then in world axes.
				const float* h = eye_math::centre_pose(head).orientation;
				const float view[3] = { -2 * (h[0] * h[2] + h[1] * h[3]), -2 * (h[1] * h[2] - h[0] * h[3]), -(1 - 2 * (h[0] * h[0] + h[1] * h[1])) };
				for (int axis = 0; axis < 3; ++axis)
				{
					scene_view_local[axis] = view[axis];
				}
				float direction[3];
				for (int axis = 0; axis < 3; ++axis)
				{
					direction[axis] = view[0] * world[axis] + view[1] * world[4 + axis] + view[2] * world[8 + axis];
				}
				input::aim_along(direction);
				return;
			}
			const hand_sample hands = latest_hand_sample();
			if (!hands.valid[1])
			{
				return;
			}
			const float* q = hands.aim[1].orientation;
			// The shot direction in LOCAL space (the two-handed, converged barrel line, else the aim ray
			// -z), then in world axes (camera local axes = OpenXR axes).
			float local[3];
			float steer_yaw{};
			if (weapon::action_aim_direction(local))
			{
				// #70: the squad order, a melee strike or a C4 throw: the game's target along the head or the hand, also
				// while sprinting (it comes before the sprint steer).
			}
			else if (input::sprint_steer_yaw(steer_yaw))
			{
				// #52: sprinting or vaulting: the soldier faces the move direction (the gun is lowered in a
				// sprint). The world camera keeps the body yaw, so the view does not turn with it.
				const float turn = steer_yaw - body; // positive to the left
				local[0] = -std::sin(turn);
				local[1] = 0.0f;
				local[2] = -std::cos(turn);
			}
			else if (weapon::use_reach_direction(local))
			{
				// Reaching to use something: the game's use target is where the hand reaches.
			}
			else if (weapon::aim_stays_with_body())
			{
				// #37: shots take their frame from the gun (weapon/shot_origin), so the game's aim stays with
				// the body, level: the game no longer turns its first-person body with the gun (a gun hand swung
				// round the head spun the drawn body). Owner 2026-11-16: with the body that is the drawn torso, which
				// also turns when the player turns in the room (#13), not only the snap-turned game body; else the
				// knife, the C4, the squad order and the muzzle flash went along the old way after a turn in the room.
				float torso{};
				if (aim_follows_torso.load(std::memory_order_relaxed) && body::drawn_torso_yaw(torso))
				{
					local[0] = -std::sin(torso);
					local[1] = 0.0f;
					local[2] = -std::cos(torso);
				}
				else
				{
					local[0] = 0.0f;
					local[1] = 0.0f;
					local[2] = -1.0f;
				}
			}
			else if (!input::aim_direction(local))
			{
				local[0] = -2 * (q[0] * q[2] + q[1] * q[3]);
				local[1] = -2 * (q[1] * q[2] - q[0] * q[3]);
				local[2] = -(1 - 2 * (q[0] * q[0] + q[1] * q[1]));
			}
			float direction[3];
			for (int axis = 0; axis < 3; ++axis)
			{
				direction[axis] = local[0] * world[axis] + local[1] * world[4 + axis] + local[2] * world[8 + axis];
			}
			input::aim_along(direction);
		}

		// Menus and loading screens use a fixed camera: identity rotation at (0, 1, 0).
		// Stereo there makes the 2D menu unreadable, so those frames stay mono (theater screen).
		bool is_menu_camera(const std::uint8_t* view)
		{
			const auto* m = reinterpret_cast<const float*>(view + transform);
			const float expected[12] = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0 };
			for (int i = 0; i < 12; ++i)
			{
				if (std::fabs(m[i] - expected[i]) > 1e-4f)
				{
					return false;
				}
			}
			return std::fabs(m[12]) < 1e-4f && std::fabs(m[13] - 1.0f) < 1e-4f && std::fabs(m[14]) < 1e-4f;
		}

		void* __fastcall copy_detour(void* destination, const void* source)
		{
			void* const result = original_copy(destination, source);

			auto* const params = static_cast<std::uint8_t*>(destination);
			if (params != renderer_view_params())
			{
				return result;
			}
			copy_thread = GetCurrentThreadId();
			input::apply_view_turn();
			control::on_game_thread();
			physics::ray_cast_on_game_thread();
			weapon::game_reload_sounds_on_game_thread();
			weapon::own_crate_on_game_thread();
			const std::uint64_t index = ++copies;
			if (index % 64 == 0)
			{
				guard_temporal_aa(pair_render.load(std::memory_order_relaxed));
			}

			rendered_view tag;
			tag.copy_index = index;

			// Head-tracked stereo only inside a loaded campaign level; menus and loading screens stay
			// mono and appear on the theater screen, and so do scenes where the game moves the camera
			// (cutscenes, the death fall) with cutscene_view = screen.
			const bool menu_frame = is_menu_camera(params) || !safety::campaign_level_loaded();
			const bool scripted_frame = !menu_frame && update_scripted(params);
			if (menu_frame)
			{
				scripted_now = false; // the arms (body/arm_pose) read it; a menu is no scene
			}
			// A scene the player takes part in: scripted, with a weapon in the soldier's hands (the lean-out's
			// M320). Plain cutscenes keep the mod's level, head-tracked view.
			if (scripted_frame && input::current_client_weapon() != 0)
			{
				scene_until = GetTickCount64() + 1500;
			}
			const bool menu = menu_frame || (scripted_frame && cutscene_screen.load(std::memory_order_relaxed));
			const head_sample head = stereo_enabled && !menu ? latest_head_sample() : head_sample{};
			tag.mono = !stereo_enabled ? mono_reason::off : menu_frame ? mono_reason::menu : menu ? mono_reason::scene_screen
				: !head.valid ? mono_reason::no_head : mono_reason::none;
			float world_half_vertical = 0.0f; // set in head-tracked frames: the FOV the world is rendered with
			if (head.valid)
			{
				// Alternate-eye rendering: odd copies left eye, even copies right eye. Pair rendering:
				// every copy is the left eye; the eye pass renders the right eye from the same frame.
				const bool pair = pair_render.load(std::memory_order_relaxed);
				const int eye = pair ? 0 : static_cast<int>(index % 2 == 1 ? 0 : 1);
				// steady_walk: before anything turns the cameras, while both are still the game's.
				steady_walk_sway(params, !scripted_frame);

				// Render a symmetric frustum covering both eyes' FOVs. With the headset aspect the
				// 16:9 image is anamorphic and spends its pixels only on what the lens shows; with
				// the game's aspect it is wider than the lens.
				// The runtime's FOV can be wider than what the lens shows (Quest 3 via Steam Link:
				// headset test 2026-10-25). Cropping (in tangent space) spends the pixels on the
				// visible part; the compositor still gets exactly the FOV that was rendered.
				// view_crop_bottom: the bottom edge comes up and the camera tilts up by half, the top stays.
				const eye_math::tilted_fov tilted = eye_math::crop_bottom(eye_math::symmetric_fov(head.fovs, view_crop_h.load(std::memory_order_relaxed),
					view_crop_v.load(std::memory_order_relaxed), stereo_headset_aspect, *reinterpret_cast<const float*>(params + aspect)),
					view_crop_bottom_rad.load(std::memory_order_relaxed), stereo_headset_aspect);
				const eye_math::render_fov& rendered = tilted.fov;
				const float half_horizontal = rendered.half_horizontal;
				const float half_vertical = rendered.half_vertical;
				world_half_vertical = half_vertical;
				const float game_fov_y = *reinterpret_cast<const float*>(params + fov_y); // the game's, zoomed while aiming
				*reinterpret_cast<float*>(params + fov_y) = 2.0f * half_vertical;
				aspect_override = rendered.aspect;

				// ipd_scale (world scale): eyes moved apart around their midpoint.
				pose eye_pose = head.eyes[eye];
				const float scale = ipd_scale.load(std::memory_order_relaxed);
				if (scale != 1.0f)
				{
					const pose centre = eye_math::centre_pose(head);
					for (int axis = 0; axis < 3; ++axis)
					{
						eye_pose.position[axis] = centre.position[axis] + (eye_pose.position[axis] - centre.position[axis]) * scale;
					}
				}
				if (input::controller_aim_active() && !scene_game_control())
				{
					aim_with_controller(params, head, scene_now());
				}
				else
				{
					scene_aiming = false;
				}
				float lift = 0.0f;
				if (physical_stance_on.load(std::memory_order_relaxed) && !scripted_frame)
				{
					// The camera is level here (controller aim): its up is the world's.
					lift += update_physical_stance(params, eye_math::centre_pose(head).position[1]);
				}
				// #154: the extra height (in physical_stance_lift too, so the rig and the shot origin rise with it).
				lift += extra_eye_height.load(std::memory_order_relaxed);
				eye_pose.position[1] += lift;
				// The world rows here are the body camera (no head pose yet): head motion never counts.
				track_body_motion(reinterpret_cast<const float*>(params + transform));
				lean_sample lean;
				{
					const float* body_rows = reinterpret_cast<const float*>(params + transform);
					const pose centre = eye_math::centre_pose(head);
					for (int axis = 0; axis < 3; ++axis)
					{
						lean.head[axis] = centre.position[axis];
						lean.body[axis] = body_rows[12 + axis];
					}
				}
				// Only the rendered views turn: hands, gun and body are placed in tracking space, and the
				// compositor gets this same pose, so the picture stays where it belongs.
				if (tilted.tilt != 0.0f)
				{
					eye_pose = eye_math::pitched(eye_pose, tilted.tilt);
				}
				apply_eye_pose(params, eye_pose);
				{
					// The optic's glass measures the eye against the gun's points, which are in tracking space without the
					// lift (the rig rises with it, so their gap is the head's): the lifted eye sat 12 cm above every sight
					// line with extra_height and shut each scope's field stop (monitor 2026-11-24, #139).
					pose lens_pose = eye_pose;
					lens_pose.position[1] -= lift;
					set_lens_eye(lens_pose);
				}
				scope_on_view_copy(game_fov_y, eye_pose, head);
				{
					// sight_reticle: the sight line in this view's axes (x right, y up, -z ahead), for the game's reticle.
					float line[3]{};
					bool valid = weapon::sight_at_eye_direction(line);
					const float* q = eye_pose.orientation;
					// v = conj(q) * line: rotate by the inverse of the eye's orientation.
					const float ux = -q[0], uy = -q[1], uz = -q[2], w = q[3];
					const float cx = uy * line[2] - uz * line[1], cy = uz * line[0] - ux * line[2], cz = ux * line[1] - uy * line[0];
					const float vx = line[0] + 2.0f * (w * cx + uy * cz - uz * cy);
					const float vy = line[1] + 2.0f * (w * cy + uz * cx - ux * cz);
					const float vz = line[2] + 2.0f * (w * cz + ux * cy - uy * cx);
					valid = valid && vz < -0.5f; // within 60 degrees of the view's axis
					set_reticle_view(valid, valid ? vx / -vz : 0.0f, valid ? vy / -vz : 0.0f);
				}
				{
					// sight_lens: the gun's sight line and up in this view's axes, wherever the gun is (not only at an eye).
					const body::weapon_points points = body::latest_weapon_points();
					const bool fresh = points.valid && points.has_root && GetTickCount64() - points.at < 250;
					const auto rotate = [](const float* q, const float (&v)[3], float (&out)[3]) {
						const float cx = q[1] * v[2] - q[2] * v[1], cy = q[2] * v[0] - q[0] * v[2], cz = q[0] * v[1] - q[1] * v[0];
						out[0] = v[0] + 2.0f * (q[3] * cx + q[1] * cz - q[2] * cy);
						out[1] = v[1] + 2.0f * (q[3] * cy + q[2] * cx - q[0] * cz);
						out[2] = v[2] + 2.0f * (q[3] * cz + q[0] * cy - q[1] * cx);
					};
					const float* e = eye_pose.orientation;
					const float to_view[4]{ -e[0], -e[1], -e[2], e[3] };
					const float up_root[3]{ 0.0f, 1.0f, 0.0f };
					const float along_root[3]{ points.sight_direction[0], points.sight_direction[1], points.sight_direction[2] };
					float along_local[3]{}, up_local[3]{}, along_view[3]{}, up_view[3]{};
					rotate(points.root_rotation, along_root, along_local);
					rotate(points.root_rotation, up_root, up_local);
					rotate(to_view, along_local, along_view);
					rotate(to_view, up_local, up_view);
					set_lens_axes(fresh, along_view, up_view);
				}
				{
					const float* view_rows = reinterpret_cast<const float*>(params + transform);
					for (int axis = 0; axis < 3; ++axis)
					{
						lean.view[axis] = view_rows[12 + axis];
					}
					lean.eye = eye;
					lean.valid = true;
					const std::lock_guard guard(lean_lock);
					lean_last = lean;
				}

				tag.stereo = true;
				tag.eye = eye;
				tag.eye_pose = eye_pose;
				tag.rendered_fov = { -half_horizontal, half_horizontal, half_vertical, -half_vertical };
				if (pair)
				{
					const float ipd = eye_math::eye_distance(head) * scale;
					set_eye_pass_shift(ipd);
					tag.pair = true;
					tag.second_pose = eye_math::moved_along_right(eye_pose, ipd);
				}
			}
			else
			{
				steady_walk_sway(params, false); // no head tracking: nothing removed, the filter starts over
				aspect_override = research_aspect.load(std::memory_order_relaxed);
				const float override_fov = fov_override.load(std::memory_order_relaxed);
				if (override_fov > 0.0f)
				{
					*reinterpret_cast<float*>(params + fov_y) = override_fov;
					mark_dirty(params);
				}
				const float amplitude = alternate_amplitude.load(std::memory_order_relaxed);
				const float offset = amplitude != 0.0f ? (index % 2 ? amplitude : -amplitude) : offset_metres.load(std::memory_order_relaxed);
				if (offset != 0.0f)
				{
					shift_sideways(params, offset);
				}
				for (int view = 0; view < 3; ++view)
				{
					std::uint8_t* const render_view = params + view_offsets[view];
					if (const float radians = view_fov[view].load(std::memory_order_relaxed); radians > 0.0f)
					{
						*reinterpret_cast<float*>(render_view + fov_y) = radians;
						mark_dirty(render_view);
					}
					if (const float metres = view_shift[view].load(std::memory_order_relaxed); metres != 0.0f)
					{
						shift_sideways(render_view, metres);
					}
				}
				if (const float degrees = fake_yaw_degrees.load(std::memory_order_relaxed); degrees != 0.0f && !is_menu_camera(params))
				{
					const float half = degrees * 3.14159265f / 360.0f; // about +y, as a head turn
					pose turn;
					turn.orientation[1] = std::sin(half);
					turn.orientation[3] = std::cos(half);
					apply_eye_pose(params, turn);
				}
				for (int slot = 0; slot < 4; ++slot)
				{
					if (const std::uint32_t pinned = pin_offset[slot].load(std::memory_order_relaxed); pinned != 0)
					{
						*reinterpret_cast<float*>(params + pinned) = pin_value[slot].load(std::memory_order_relaxed);
						mark_dirty(params);
					}
				}
				for (int slot = 0; slot < 4; ++slot)
				{
					const float metres = transform_shift_metres[slot].load(std::memory_order_relaxed);
					if (metres != 0.0f)
					{
						auto* rows = reinterpret_cast<float*>(params + transform_shift_offset[slot].load(std::memory_order_relaxed));
						for (int axis = 0; axis < 3; ++axis)
						{
							rows[12 + axis] += rows[axis] * metres;
						}
					}
				}
			}

			float scale = gun_scale.load(std::memory_order_relaxed);
			const bool life_size = gun_life_size.load(std::memory_order_relaxed) && world_half_vertical > 0.01f && world_half_vertical < 1.5f;
			const float wanted_gun_fov = gun_pass_fov_math::life_size_half_fov(world_half_vertical, scale);
			if (life_size && gun_fov_life_size.load(std::memory_order_relaxed) && !is_menu_camera(params) && gun_pass_fov::apply(wanted_gun_fov))
			{
				// #39: the gun pass itself draws at the life-size FOV (hip and every zoom level), so the
				// first-person camera stays orthonormal and the sights' lens and reticle work.
				applied_foreground_half_fov = wanted_gun_fov;
				scale = 1.0f;
			}
			else
			{
				gun_pass_fov::restore();
				if (life_size)
				{
					// The camera squeeze: the gun pass's FOV as last drawn (it narrows while the game aims down
					// the sights), its rows scaled to the world's.
					const float drawn = stereo::foreground_half_fov(hip_foreground_half_fov);
					applied_foreground_half_fov = drawn;
					scale *= std::tan(drawn) / std::tan(world_half_vertical);
				}
			}
			applied_gun_scale = scale;
			if (scale > 0.0f && scale != 1.0f && !is_menu_camera(params))
			{
				auto* rows = reinterpret_cast<float*>(params + first_person_transform);
				for (int i = 0; i < 8; ++i) // right and up rows (4 floats each; w stays 0)
				{
					rows[i] *= scale;
				}
			}

			publish_rendered_view(tag);
			return result;
		}

		void __fastcall set_aspect_detour(void* view, float value)
		{
			if (const float wanted = aspect_override.load(std::memory_order_relaxed); wanted > 0.0f && view == renderer_view_params())
			{
				value = wanted;
			}
			original_set_aspect(view, value);
		}
	}

	namespace
	{
		bool hook_now()
		{
			auto* const target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(view_params_copy));
			MH_STATUS status = MH_Initialize();
			if (status != MH_OK && status != MH_ERROR_ALREADY_INITIALIZED)
			{
				return false;
			}
			status = MH_CreateHook(target, reinterpret_cast<void*>(&copy_detour), reinterpret_cast<void**>(&original_copy));
			if (status == MH_OK)
			{
				status = MH_EnableHook(target);
			}
			if (status != MH_OK)
			{
				log::write("camera: hooking %p failed: %s", target, MH_StatusToString(status));
				return false;
			}
			log::write("camera: hook installed on view-params copy %p (stereo=%d first_person=%d headset_aspect=%d pair=%d)", target,
				stereo_enabled, stereo_first_person, stereo_headset_aspect, pair_render.load());
			if (pair_render)
			{
				// Render twice per frame with eye-2 constants (flags 1 | 8); the shift follows the IPD.
				log::write("camera: pair rendering: %s", set_eye_pass(9, 0.064f).c_str());
			}

			// Optional second hook; without it stereo still works at the game's aspect.
			auto* const aspect_target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(view_set_aspect));
			std::uint8_t bytes[sizeof(set_aspect_prologue)]{};
			if (!ReadProcessMemory(GetCurrentProcess(), aspect_target, bytes, sizeof(bytes), nullptr)
				|| std::memcmp(bytes, set_aspect_prologue, sizeof(bytes)) != 0)
			{
				log::write("camera: set-aspect %p does not match build 179547; per-eye aspect unavailable", aspect_target);
				return true;
			}
			status = MH_CreateHook(aspect_target, reinterpret_cast<void*>(&set_aspect_detour), reinterpret_cast<void**>(&original_set_aspect));
			if (status == MH_OK)
			{
				status = MH_EnableHook(aspect_target);
			}
			log::write("camera: set-aspect hook at %p: %s", aspect_target, MH_StatusToString(status));
			return true;
		}

		// The DRM decrypts the game's code after our DLL loads, so wait until the expected
		// bytes appear; a different build never matches and is left alone.
		DWORD WINAPI wait_and_hook(LPVOID)
		{
			std::uint8_t bytes[sizeof(expected_prologue)]{};
			for (int attempt = 0; attempt < 600; ++attempt)
			{
				if (safety::mod_disabled())
				{
					return 0;
				}
				if (ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(static_cast<std::uintptr_t>(view_params_copy)), bytes, sizeof(bytes), nullptr)
					&& std::memcmp(bytes, expected_prologue, sizeof(bytes)) == 0)
				{
					hook_now();
					return 0;
				}
				Sleep(200);
			}
			log::write("camera: view-params copy never matched build 179547; hook not installed");
			return 0;
		}
	}

	bool install_camera_hook()
	{
#if !defined(_M_X64)
		log::write("camera: hook targets BF4 x64 only; not installed");
		return false;
#else
		stereo_enabled = config::get_bool("stereo", false);
		// View +0x4E0 turned out to be a copy of the world view with no visible effect (camera.md).
		stereo_first_person = config::get_bool("stereo_first_person", true);
		stereo_headset_aspect = config::get("stereo_aspect", "headset") != "game";
		pair_render = stereo_enabled && config::get("stereo_render", "pair") == "pair";
		set_gun_scale(static_cast<float>(std::atof(config::get("gun_scale", "1.0").c_str())));
		set_view_crop(static_cast<float>(std::atof(config::get("view_crop_h", "1.0").c_str())),
			static_cast<float>(std::atof(config::get("view_crop_v", "1.0").c_str())));
		const HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook, nullptr, 0, nullptr);
		if (!thread)
		{
			return false;
		}
		CloseHandle(thread);
		return true;
#endif
	}

	void set_eye_offset(float metres)
	{
		offset_metres = metres;
	}

	float eye_offset()
	{
		return offset_metres;
	}

	void set_alternating_offset(float amplitude)
	{
		alternate_amplitude = amplitude;
	}

	void set_fov_override(float radians)
	{
		fov_override = radians;
	}

	void set_view_override(int view, float fov_radians, float shift_metres)
	{
		if (view >= 0 && view < 3)
		{
			view_fov[view] = fov_radians;
			view_shift[view] = shift_metres;
		}
	}

	bool set_pin(int slot, std::uint32_t offset, float value)
	{
		if (slot < 0 || slot >= 4 || offset > max_transform_offset + 0x3C || offset % 4 != 0)
		{
			return false;
		}
		pin_offset[slot] = 0; // never pair a new offset with the old value
		pin_value[slot] = value;
		pin_offset[slot] = offset;
		return true;
	}

	void set_aim_follows_torso(bool on)
	{
		aim_follows_torso = on;
	}

	void set_gun_scale(float scale)
	{
		// Out-of-range or non-finite values would give the engine degenerate camera rows.
		gun_scale = std::isfinite(scale) && scale > 0.0f ? std::fmin(std::fmax(scale, 0.25f), 4.0f) : 1.0f;
	}

	void set_ipd_scale(float scale)
	{
		ipd_scale = std::isfinite(scale) && scale > 0.0f ? std::fmin(std::fmax(scale, 0.5f), 2.0f) : 1.0f;
	}

	void rendered_eye_position(const head_sample& head, int eye, float (&position)[3])
	{
		const float scale = ipd_scale.load(std::memory_order_relaxed);
		const pose centre = eye_math::centre_pose(head);
		for (int axis = 0; axis < 3; ++axis)
		{
			position[axis] = centre.position[axis] + (head.eyes[eye].position[axis] - centre.position[axis]) * scale;
		}
	}

	float current_gun_scale()
	{
		return gun_scale;
	}

	void set_gun_life_size(bool on)
	{
		gun_life_size = on;
	}

	void set_gun_fov_life_size(bool on)
	{
		gun_fov_life_size = on;
	}

	float applied_gun_size()
	{
		return applied_gun_scale.load(std::memory_order_relaxed);
	}

	float applied_gun_half_fov()
	{
		return applied_foreground_half_fov.load(std::memory_order_relaxed);
	}

	void set_view_crop(float horizontal, float vertical)
	{
		const auto clamp = [](float value) { return std::isfinite(value) && value > 0.0f ? std::fmin(std::fmax(value, 0.4f), 1.0f) : 1.0f; };
		view_crop_h = clamp(horizontal);
		view_crop_v = clamp(vertical);
	}

	void set_view_crop_bottom(float degrees)
	{
		const float clamped = std::isfinite(degrees) ? std::fmin(std::fmax(degrees, 0.0f), 30.0f) : 0.0f;
		view_crop_bottom_rad = clamped * 3.14159265f / 180.0f;
	}

	void set_steady_walk(bool enabled)
	{
		steady_walk_on = enabled;
	}

	void current_view_crop(float& horizontal, float& vertical)
	{
		horizontal = view_crop_h;
		vertical = view_crop_v;
	}

	void set_research_aspect(float value)
	{
		research_aspect = std::isfinite(value) && value > 0.0f ? std::fmin(std::fmax(value, 0.2f), 5.0f) : 0.0f;
	}

	void set_cutscene_screen(bool screen)
	{
		cutscene_screen = screen;
	}

	void set_physical_stance(bool enabled)
	{
		if (enabled && !physical_stance_on.load())
		{
			physical_stance_reset = true; // standing is measured afresh from now
		}
		physical_stance_on = enabled;
	}

	float physical_stance_lift()
	{
		return (physical_stance_on.load(std::memory_order_relaxed) ? stance_lift.load(std::memory_order_relaxed) : 0.0f)
			+ extra_eye_height.load(std::memory_order_relaxed);
	}

	void set_extra_height(float metres)
	{
		extra_eye_height = std::isfinite(metres) ? std::fmax(-0.3f, std::fmin(0.5f, metres)) : 0.0f;
	}

	std::string stance_report()
	{
		char text[192];
		std::snprintf(text, sizeof(text), "physical_stance=%d body=%d drop_m=%.2f game=%d eye_lift_m=%.2f standing_eye_m=%.3f", physical_stance_on.load() ? 1 : 0,
			stance_wanted.load(), stance_drop.load(), input::game_stance(), stance_lift.load(), standing_eye);
		return text;
	}

	bool scripted_scene()
	{
		return scripted_now.load(std::memory_order_relaxed);
	}

	void set_scene_aim(scene_aim mode)
	{
		scene_aim_mode = mode;
	}

	bool scene_game_control()
	{
		if (scene_aim_mode.load(std::memory_order_relaxed) != scene_aim::game || !scene_now())
		{
			scene_stick_used = false;
			return false;
		}
		// #153 (owner 2026-11-24): doors, the third level's walk and other scripted moves turned the view with the game's
		// camera. The game takes the view (and the aim) only once the right stick asks for it in this scene, as in the
		// Baku lean-out (#133); until then the view stays level on the body's yaw and only follows the walk.
		if (!scene_stick_used.load(std::memory_order_relaxed))
		{
			const input::gamepad_state pad = input::latest();
			if (std::abs(static_cast<int>(pad.right_x)) > scene_stick_threshold || std::abs(static_cast<int>(pad.right_y)) > scene_stick_threshold)
			{
				scene_stick_used = true;
			}
		}
		return scene_stick_used.load(std::memory_order_relaxed);
	}

	bool scene_aims_along_view()
	{
		return scene_aiming.load(std::memory_order_relaxed);
	}

	bool scene_view_direction(float (&local)[3])
	{
		for (int axis = 0; axis < 3; ++axis)
		{
			local[axis] = scene_view_local[axis].load(std::memory_order_relaxed);
		}
		return scene_aiming.load(std::memory_order_relaxed);
	}

	std::string cutscene_report()
	{
		char text[160];
		std::snprintf(text, sizeof(text), "cutscene_view=%s scripted=%d camera_vs_aim_deg=%.1f health=%.1f scene=%d game_view=%d", cutscene_screen.load() ? "screen" : "look",
			scripted_now.load() ? 1 : 0, scripted_deviation.load(), scripted_health.load(), scene_now() ? 1 : 0, scene_stick_used.load() ? 1 : 0);
		return text;
	}

	std::string set_stereo_render(const std::string& name)
	{
		if (name == "pair" || name == "aer")
		{
			const bool pair = name == "pair";
			// The eye pass must run before the camera hook tags copies as pairs, and stop after.
			if (pair)
			{
				const std::string result = set_eye_pass(9, 0.064f);
				if (result.rfind("error", 0) == 0)
				{
					return result;
				}
			}
			pair_render = pair && stereo_enabled;
			if (!pair)
			{
				set_eye_pass(0, 0.064f);
			}
		}
		return std::string("stereo_render=") + (pair_render.load() ? "pair" : "aer");
	}

	std::string lean_report()
	{
		lean_sample lean;
		{
			const std::lock_guard guard(lean_lock);
			lean = lean_last;
		}
		if (!lean.valid)
		{
			return "lean: no head-tracked frame yet";
		}
		const float forward = -lean.head[2], right = lean.head[0];
		const float from_origin = std::hypot(forward, right);
		const float dx = lean.view[0] - lean.body[0], dy = lean.view[1] - lean.body[1], dz = lean.view[2] - lean.body[2];
		char text[512];
		std::snprintf(text, sizeof(text),
			"lean: head from the session origin forward=%.3f right=%.3f up=%.3f (horizontal %.3f m)\n"
			"view (%s eye) from the soldier's camera: horizontal %.3f m, up %.3f m; soldier camera at %.2f %.2f %.2f%s",
			forward, right, lean.head[1], from_origin, lean.eye == 0 ? "left" : "right", std::hypot(dx, dz), dy, lean.body[0], lean.body[1], lean.body[2],
			from_origin > 0.5f ? "\nnote: the head is over 0.5 m from the session origin, so the view sits that far from the soldier (recenter the headset)" : "");
		return text;
	}

	void body_motion(float& yaw_rate, float& speed)
	{
		yaw_rate = motion_yaw_rate.load(std::memory_order_relaxed);
		speed = motion_speed.load(std::memory_order_relaxed);
	}

	first_person_turn first_person_view_turn()
	{
		first_person_turn turn;
		turn.active = GetTickCount64() < fp_turn_until.load(std::memory_order_relaxed);
		turn.yaw = fp_turn_yaw.load(std::memory_order_relaxed);
		return turn;
	}

	void set_fake_yaw(float degrees)
	{
		fake_yaw_degrees = degrees;
	}

	bool set_transform_shift(int slot, std::uint32_t offset, float metres)
	{
		if (slot < 0 || slot >= 4 || offset > max_transform_offset || offset % 16 != 0)
		{
			return false;
		}
		transform_shift_metres[slot] = 0.0f; // never pair a new offset with the old shift
		transform_shift_offset[slot] = offset;
		transform_shift_metres[slot] = metres;
		return true;
	}

	camera_stats stats()
	{
		return { copy_thread.load(), copies.load() };
	}
}
