#include "weapon/hit_feedback.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "common/log.hpp"
#include "input/haptics.hpp"

namespace bfvr::weapon
{
	namespace
	{
		constexpr std::uintptr_t local_soldier_global = static_cast<std::uintptr_t>(0x14230f260ull);
		constexpr std::uintptr_t soldier_health_component = 0x140; // BFClientSoldierHealthComponent*
		constexpr std::uintptr_t component_health = 0x20;          // health, then max health

		std::uintptr_t seen_soldier{};
		float seen_health{ -1.0f };
		unsigned long long hits{};
		float last_damage{};

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}
	}

	void update_hit_feedback()
	{
#if defined(_M_X64)
		std::uintptr_t soldier{}, component{};
		float health[2]{}; // health, max
		if (!read(local_soldier_global, soldier) || !soldier || !read(soldier + soldier_health_component, component)
			|| !read(component + component_health, health) || !std::isfinite(health[0]) || !(health[1] > 0.0f) || health[1] > 10000.0f)
		{
			seen_soldier = 0;
			return;
		}
		// A new soldier (respawn, section change) starts fresh; so does a heal.
		if (soldier == seen_soldier && seen_health > 0.0f && health[0] < seen_health - 0.5f)
		{
			last_damage = seen_health - health[0];
			++hits;
			input::hit(input::hit_pulse(last_damage, health[1]));
		}
		seen_soldier = soldier;
		seen_health = health[0];
#endif
	}

	std::string hit_feedback_report()
	{
		char text[96];
		std::snprintf(text, sizeof(text), "health=%.1f hits=%llu last_damage=%.1f", seen_health, hits, last_damage);
		return text;
	}
}
