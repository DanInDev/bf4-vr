#include "stereo/world_pass.hpp"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>

#include <MinHook.h>

#include "common/config.hpp"
#include "common/log.hpp"

namespace bfvr::stereo
{
	namespace
	{
		constexpr std::uint64_t world_module_render = 0x1408bec40;
		// mov [rsp+8],rbx; mov [rsp+10h],rsi; push rdi; sub rsp,40h; cmp byte ptr [rcx+39h],0
		constexpr std::uint8_t world_module_prologue[] = { 0x48, 0x89, 0x5c, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57,
			0x48, 0x83, 0xec, 0x40, 0x80, 0x79, 0x39, 0x00 };
		constexpr std::uint64_t game_renderer_global = 0x142672378;
		constexpr std::size_t module_views = 0x550;
		constexpr std::size_t renderer_views = 0x60;
		constexpr std::size_t transform = 0x40;
		constexpr std::size_t dirty_flags = 0xe0;

		// bool-less: void setup_views(GameRenderer*, unsigned count, const ViewParams* source), game thread.
		constexpr std::uint64_t setup_views_address = 0x140848b00;
		// mov r11,rsp; push rbp; push rsi; push r15; sub rsp,630h
		constexpr std::uint8_t setup_views_prologue[] = { 0x4c, 0x8b, 0xdc, 0x55, 0x56, 0x41, 0x57, 0x48, 0x81, 0xec, 0x30, 0x06, 0x00, 0x00 };
		constexpr std::size_t view_block = 0x1020;
		constexpr std::size_t viewport_index = 0xec0;
		using setup_views_t = void(__fastcall*)(void* renderer, unsigned count, const std::uint8_t* source);
		setup_views_t original_setup_views{};
		std::atomic<int> extra_views{};
		std::atomic<float> extra_shift{};
		std::atomic<int> extra_viewport{};
		std::atomic<unsigned long long> setup_calls{};
		std::atomic<unsigned> last_count{};
		alignas(64) std::uint8_t views[2 * view_block];

		void __fastcall setup_views_detour(void* renderer, unsigned count, const std::uint8_t* source)
		{
			++setup_calls;
			last_count = count;
			if (extra_views.load() == 0 || count != 1 || !source)
			{
				original_setup_views(renderer, count, source);
				return;
			}
			std::memcpy(views, source, view_block);
			std::memcpy(views + view_block, source, view_block);
			std::uint8_t* const second = views + view_block;
			const auto* right = reinterpret_cast<const float*>(second + transform);
			auto* position = reinterpret_cast<float*>(second + transform + 0x30);
			for (int axis = 0; axis < 3; ++axis)
			{
				position[axis] += right[axis] * extra_shift.load();
			}
			*reinterpret_cast<std::uint32_t*>(second + dirty_flags) |= 0x7;
			*reinterpret_cast<std::uint32_t*>(second + viewport_index) = static_cast<std::uint32_t>(extra_viewport.load());
			original_setup_views(renderer, 2, views);
		}

		using module_render_t = void(__fastcall*)(void* module, void* argument, void* context);
		module_render_t original_render{};

		std::atomic<int> passes{ 1 };
		std::atomic<float> shift{};
		std::atomic<int> shift_target{};
		std::atomic<unsigned long long> calls{};

		std::uint8_t* view_for(void* module)
		{
			if (shift_target.load() == 0)
			{
				return static_cast<std::uint8_t*>(module) + module_views;
			}
			auto* const renderer = *reinterpret_cast<std::uint8_t**>(static_cast<std::uintptr_t>(game_renderer_global));
			return renderer ? *reinterpret_cast<std::uint8_t**>(renderer + renderer_views) : nullptr;
		}

		void __fastcall render_detour(void* module, void* argument, void* context)
		{
			++calls;
			if (passes.load() < 2)
			{
				original_render(module, argument, context);
				return;
			}
			original_render(module, argument, context);

			std::uint8_t* const view = view_for(module);
			const float metres = shift.load();
			float saved[3]{};
			if (view && metres != 0.0f)
			{
				const auto* right = reinterpret_cast<const float*>(view + transform);
				auto* position = reinterpret_cast<float*>(view + transform + 0x30);
				std::memcpy(saved, position, sizeof(saved));
				for (int axis = 0; axis < 3; ++axis)
				{
					position[axis] += right[axis] * metres;
				}
				*reinterpret_cast<std::uint32_t*>(view + dirty_flags) |= 0x7;
			}
			original_render(module, argument, context);
			if (view && metres != 0.0f)
			{
				std::memcpy(view + transform + 0x30, saved, sizeof(saved));
				*reinterpret_cast<std::uint32_t*>(view + dirty_flags) |= 0x7;
			}
		}
	}

	void install_world_pass_hook()
	{
#if defined(_M_X64)
		auto* const target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(world_module_render));
		std::uint8_t bytes[sizeof(world_module_prologue)]{};
		if (!ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr)
			|| std::memcmp(bytes, world_module_prologue, sizeof(bytes)) != 0)
		{
			log::write("world pass: %p does not match build 179547; not hooked", target);
			return;
		}
		MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void*>(&render_detour), reinterpret_cast<void**>(&original_render));
		if (status == MH_OK)
		{
			status = MH_EnableHook(target);
		}
		log::write("world pass: hook on %p: %s", target, MH_StatusToString(status));
#endif
	}

	namespace
	{
		std::atomic<bool> views_hooked{};

		bool hook_view_setup()
		{
#if defined(_M_X64)
			if (views_hooked.load())
			{
				return true;
			}
			auto* const target = reinterpret_cast<void*>(static_cast<std::uintptr_t>(setup_views_address));
			std::uint8_t bytes[sizeof(setup_views_prologue)]{};
			if (!ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr)
				|| std::memcmp(bytes, setup_views_prologue, sizeof(bytes)) != 0)
			{
				return false;
			}
			MH_STATUS status = MH_CreateHook(target, reinterpret_cast<void*>(&setup_views_detour), reinterpret_cast<void**>(&original_setup_views));
			if (status == MH_OK)
			{
				status = MH_EnableHook(target);
			}
			log::write("views: hook on %p: %s (extra view %d)", target, MH_StatusToString(status), extra_views.load());
			views_hooked = status == MH_OK;
			return views_hooked.load();
#else
			return false;
#endif
		}

		// The DRM decrypts the code after the DLL loads; hook before the first frame so the view
		// arrays are allocated for two views once (changing the count reallocates them under the
		// render thread: 2026-10-25 hang).
		DWORD WINAPI wait_and_hook_views(LPVOID)
		{
			for (int attempt = 0; attempt < 600 && !hook_view_setup(); ++attempt)
			{
				Sleep(100);
			}
			return 0;
		}
	}

	void install_view_experiment()
	{
		const int view_count = std::atoi(config::get("research_views", "1").c_str());
		if (view_count < 2)
		{
			return;
		}
		extra_views = 1;
		extra_shift = static_cast<float>(std::atof(config::get("research_view_shift", "0.0").c_str()));
		extra_viewport = std::atoi(config::get("research_view_viewport", "0").c_str());
		if (const HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook_views, nullptr, 0, nullptr))
		{
			CloseHandle(thread);
		}
	}

	std::string set_view_count(int count, float shift_m, int viewport)
	{
		if (!hook_view_setup())
		{
			return "error: view setup does not match build 179547";
		}
		if (count > 0)
		{
			extra_views = count >= 2 ? 1 : 0;
			extra_shift = shift_m;
			extra_viewport = viewport;
		}
		return "viewcount extra=" + std::to_string(extra_views.load()) + " shift=" + std::to_string(extra_shift.load()) + " viewport="
			+ std::to_string(extra_viewport.load()) + " setup_calls=" + std::to_string(setup_calls.load()) + " game_count="
			+ std::to_string(last_count.load());
	}

	std::string set_world_pass(int new_passes, float shift_m, int target)
	{
		static bool installed = false;
		if (!installed)
		{
			installed = true;
			install_world_pass_hook();
		}
		passes = new_passes < 1 ? 1 : (new_passes > 2 ? 2 : new_passes);
		shift = shift_m;
		shift_target = target ? 1 : 0;
		return "worldpass passes=" + std::to_string(passes.load()) + " shift=" + std::to_string(shift.load()) + " target="
			+ std::to_string(shift_target.load()) + " calls=" + std::to_string(calls.load());
	}
}
