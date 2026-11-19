#include "body/soldier_radius.hpp"

#include "common/controlled.hpp"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include <MinHook.h>

#include "common/log.hpp"
#include "safety/mp_guard.hpp"

namespace bfvr::body
{
	namespace
	{
		std::atomic<float> wanted_radius{};
	}

	void set_soldier_radius(float metres)
	{
		wanted_radius = metres;
	}

#if defined(_M_X64)
	namespace
	{
		// re/symbols/bf4-179547.md "Soldier collision": local soldier -> +0x30 SoldierEntityData ->
		// +0x118 CharacterPhysicsData -> +0x48 PhysicalRadius (read at spawn).
		constexpr std::size_t soldier_data = 0x30;
		constexpr std::size_t character_physics = 0x118;
		constexpr std::size_t physical_radius = 0x48;
		constexpr ULONGLONG interval_ms = 500;

		std::mutex lock;
		radius_keeper keeper;
		ULONGLONG next_check{};
		float last_seen{};

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T got = 0;
			return address >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &got) && got == sizeof(value);
		}

		bool write_float(std::uintptr_t address, float value)
		{
			MEMORY_BASIC_INFORMATION info{};
			auto* at = reinterpret_cast<std::uint8_t*>(address);
			if (!VirtualQuery(at, &info, sizeof(info)) || info.State != MEM_COMMIT || !(info.Protect & (PAGE_READWRITE | PAGE_WRITECOPY)) || (info.Protect & PAGE_GUARD)
				|| at + sizeof(value) > static_cast<std::uint8_t*>(info.BaseAddress) + info.RegionSize)
			{
				return false;
			}
			std::memcpy(at, &value, sizeof(value));
			return true;
		}

		// The keeper, given the asset at hand (lock held): writes the wanted radius or puts the game's back.
		void keep_radius(std::uintptr_t physics, const char* when)
		{
			float current{};
			if (!physics || !read(physics + physical_radius, current))
			{
				return;
			}
			last_seen = current;
			const radius_action action = keeper.update(physics, current, wanted_radius.load(std::memory_order_relaxed));
			if (!action.write)
			{
				return;
			}
			if (write_float(physics + physical_radius, action.value))
			{
				last_seen = action.value;
				log::write("soldier radius: %.2f -> %.2f m in %p (%s; the game's %.2f)", current, action.value, reinterpret_cast<void*>(physics), when, keeper.original());
			}
			else
			{
				log::write("soldier radius: %p is not writable, left at %.2f", reinterpret_cast<void*>(physics), current);
			}
		}

		// The spawn reads the radius in the pose shape builder (watched read, 2026-11-13: a checkpoint restart
		// read +0x48 here, called from 0x140B3D884 with r8 = [rbx+0xE8]): (out, poses, CharacterPhysicsData).
		// Writing the asset at its entry makes the setting apply from every spawn, a level's first included.
		constexpr std::uintptr_t pose_shape_builder = 0x140B412A0;
		// mov rax,rsp; push rbp; push rsi; push rdi; push r12..r15; sub rsp,0E0h
		constexpr std::uint8_t pose_shape_prologue[] = { 0x48, 0x8B, 0xC4, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x81, 0xEC, 0xE0, 0x00, 0x00, 0x00 };
		using pose_shape_builder_t = void*(__fastcall*)(void* out, void* poses, void* physics, void* extra);
		pose_shape_builder_t original_builder{};
		std::atomic<unsigned long long> spawn_writes{};

		// The asset's Name (Asset +0x10, a C string), or empty.
		std::string asset_name(std::uintptr_t asset)
		{
			std::uintptr_t text{};
			char buffer[96]{};
			if (!read(asset + 0x10, text) || !text)
			{
				return {};
			}
			SIZE_T got = 0;
			if (!ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(text), buffer, sizeof(buffer) - 1, &got))
			{
				return {};
			}
			buffer[std::min<SIZE_T>(got, sizeof(buffer) - 1)] = '\0';
			return buffer;
		}

		void* __fastcall pose_shape_detour(void* out, void* poses, void* physics, void* extra)
		{
			const auto asset = reinterpret_cast<std::uintptr_t>(physics);
			if (asset && is_player_physics(asset_name(asset)))
			{
				const std::lock_guard guard(lock);
				keep_radius(asset, "at spawn");
				++spawn_writes;
			}
			return original_builder(out, poses, physics, extra);
		}

		DWORD WINAPI wait_and_hook(LPVOID)
		{
			auto* const target = reinterpret_cast<void*>(pose_shape_builder);
			std::uint8_t bytes[sizeof(pose_shape_prologue)]{};
			for (int attempt = 0; attempt < 600; ++attempt)
			{
				if (safety::mod_disabled())
				{
					return 0;
				}
				if (ReadProcessMemory(GetCurrentProcess(), target, bytes, sizeof(bytes), nullptr) && std::memcmp(bytes, pose_shape_prologue, sizeof(bytes)) == 0)
				{
					MH_STATUS status = MH_Initialize();
					if (status == MH_OK || status == MH_ERROR_ALREADY_INITIALIZED)
					{
						status = MH_CreateHook(target, reinterpret_cast<void*>(&pose_shape_detour), reinterpret_cast<void**>(&original_builder));
						if (status == MH_OK)
						{
							status = MH_EnableHook(target);
						}
					}
					log::write("soldier radius: spawn hook at %p: %s", target, MH_StatusToString(status));
					return 0;
				}
				Sleep(200);
			}
			log::write("soldier radius: the pose shape builder never matched build 179547; the radius applies after a respawn only");
			return 0;
		}

		std::uintptr_t physics_data()
		{
			std::uintptr_t soldier{}, data{}, physics{};
			if (!(soldier = game::local_soldier()) != 0 || !soldier || !read(soldier + soldier_data, data) || !data || !read(data + character_physics, physics))
			{
				return 0;
			}
			return physics;
		}
	}

	void update_soldier_radius()
	{
		const ULONGLONG now = GetTickCount64();
		const std::lock_guard guard(lock);
		if (now < next_check)
		{
			return;
		}
		next_check = now + interval_ms;
		keep_radius(physics_data(), "live, applies from the next spawn");
	}

	bool install_soldier_radius_hook()
	{
		const HANDLE thread = CreateThread(nullptr, 0, &wait_and_hook, nullptr, 0, nullptr);
		if (!thread)
		{
			return false;
		}
		CloseHandle(thread);
		return true;
	}

	std::string soldier_radius_report()
	{
		const std::lock_guard guard(lock);
		char text[256];
		std::snprintf(text, sizeof(text), "soldier_radius wanted=%.2f (0 = the game's) asset=%p game=%.3f now=%.3f spawn_hook_calls=%llu (read at spawn: a change applies from the next spawn)",
			wanted_radius.load(), reinterpret_cast<void*>(keeper.asset()), keeper.original(), last_seen, spawn_writes.load());
		return text;
	}
#else
	void update_soldier_radius() {}
	bool install_soldier_radius_hook() { return false; }
	std::string soldier_radius_report() { return "soldier_radius: BF4 x64 only"; }
#endif
}
