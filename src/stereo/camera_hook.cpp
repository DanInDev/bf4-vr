#include "stereo/camera_hook.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <cstring>

#include <MinHook.h>

#include "common/config.hpp"
#include "common/log.hpp"
#include "safety/mp_guard.hpp"
#include "input/vr_controls.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/eye_pass.hpp"
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
		// Fraction (tangent space) of the headset's reported FOV that is rendered, horizontal and
		// vertical (1 = all of it); config view_crop_h / view_crop_v.
		std::atomic<float> view_crop_h{ 1.0f };
		std::atomic<float> view_crop_v{ 1.0f };
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
		// stereo_render = pair (research): both eyes from one game frame (stereo/eye_pass.hpp).
		std::atomic<bool> pair_render{};
		bool stereo_first_person{};

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
			const std::uint64_t index = ++copies;

			rendered_view tag;
			tag.copy_index = index;

			// Head-tracked stereo only inside a loaded campaign level; menus and loading screens stay
			// mono and appear on the theater screen.
			const bool menu = is_menu_camera(params) || !safety::campaign_level_loaded();
			const head_sample head = stereo_enabled && !menu ? latest_head_sample() : head_sample{};
			if (head.valid)
			{
				// Alternate-eye rendering: odd copies left eye, even copies right eye. Pair rendering:
				// every copy is the left eye; the eye pass renders the right eye from the same frame.
				const bool pair = pair_render.load(std::memory_order_relaxed);
				const int eye = pair ? 0 : static_cast<int>(index % 2 == 1 ? 0 : 1);

				// Render a symmetric frustum covering both eyes' FOVs. With the headset aspect the
				// 16:9 image is anamorphic and spends its pixels only on what the lens shows; with
				// the game's aspect it is wider than the lens.
				// The runtime's FOV can be wider than what the lens shows (Quest 3 via Steam Link:
				// headset test 2026-10-25). Cropping (in tangent space) spends the pixels on the
				// visible part; the compositor still gets exactly the FOV that was rendered.
				const eye_math::render_fov rendered = eye_math::symmetric_fov(head.fovs, view_crop_h.load(std::memory_order_relaxed),
					view_crop_v.load(std::memory_order_relaxed), stereo_headset_aspect, *reinterpret_cast<const float*>(params + aspect));
				const float half_horizontal = rendered.half_horizontal;
				const float half_vertical = rendered.half_vertical;
				*reinterpret_cast<float*>(params + fov_y) = 2.0f * half_vertical;
				aspect_override = rendered.aspect;

				apply_eye_pose(params, head.eyes[eye]);

				tag.stereo = true;
				tag.eye = eye;
				tag.eye_pose = head.eyes[eye];
				tag.rendered_fov = { -half_horizontal, half_horizontal, half_vertical, -half_vertical };
				if (pair)
				{
					const float ipd = eye_math::eye_distance(head);
					set_eye_pass_shift(ipd);
					tag.pair = true;
					tag.second_pose = eye_math::moved_along_right(head.eyes[0], ipd);
				}
			}
			else
			{
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

			if (const float scale = gun_scale.load(std::memory_order_relaxed); scale > 0.0f && scale != 1.0f && !is_menu_camera(params))
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
		pair_render = stereo_enabled && config::get("stereo_render", "aer") == "pair";
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

	void set_gun_scale(float scale)
	{
		// Out-of-range or non-finite values would give the engine degenerate camera rows.
		gun_scale = std::isfinite(scale) && scale > 0.0f ? std::fmin(std::fmax(scale, 0.25f), 4.0f) : 1.0f;
	}

	float current_gun_scale()
	{
		return gun_scale;
	}

	void set_view_crop(float horizontal, float vertical)
	{
		const auto clamp = [](float value) { return std::isfinite(value) && value > 0.0f ? std::fmin(std::fmax(value, 0.4f), 1.0f) : 1.0f; };
		view_crop_h = clamp(horizontal);
		view_crop_v = clamp(vertical);
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
