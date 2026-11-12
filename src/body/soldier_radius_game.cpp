#include "body/soldier_radius.hpp"

#include <windows.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "common/log.hpp"

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
		constexpr std::uintptr_t local_soldier_global = 0x14230F260;
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

		std::uintptr_t physics_data()
		{
			std::uintptr_t soldier{}, data{}, physics{};
			if (!read(local_soldier_global, soldier) || !soldier || !read(soldier + soldier_data, data) || !data || !read(data + character_physics, physics))
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
		const std::uintptr_t physics = physics_data();
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
			log::write("soldier radius: %.2f -> %.2f m in %p (the game's %.2f; applies from the next spawn: respawn or checkpoint restart)", current, action.value,
				reinterpret_cast<void*>(physics), keeper.original());
		}
		else
		{
			log::write("soldier radius: %p is not writable, left at %.2f", reinterpret_cast<void*>(physics), current);
		}
	}

	std::string soldier_radius_report()
	{
		const std::lock_guard guard(lock);
		char text[256];
		std::snprintf(text, sizeof(text), "soldier_radius wanted=%.2f (0 = the game's) asset=%p game=%.3f now=%.3f (read at spawn: a change applies after a respawn or checkpoint restart)",
			wanted_radius.load(), reinterpret_cast<void*>(keeper.asset()), keeper.original(), last_seen);
		return text;
	}
#else
	void update_soldier_radius() {}
	std::string soldier_radius_report() { return "soldier_radius: BF4 x64 only"; }
#endif
}
