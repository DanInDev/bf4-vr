#include "weapon/hit_feedback.hpp"

#include <windows.h>

#include <cmath>
#include <cstdint>
#include <cstdio>

#include "body/arm_pose.hpp"
#include "common/log.hpp"
#include "common/settings_store.hpp"
#include "hooks/game_window.hpp"
#include "input/gamepad.hpp"
#include "input/haptics.hpp"
#include "ui/body_calibration_flow.hpp"
#include "weapon/death_watch.hpp"
#include "weapon/physical_grenade.hpp"
#include "weapon/physical_reload.hpp"
#include "weapon/weapon_swap.hpp"

namespace bfvr::weapon
{
	namespace
	{
		constexpr std::uintptr_t local_soldier_global = static_cast<std::uintptr_t>(0x14230f260ull);
		constexpr std::uintptr_t soldier_health_component = 0x140; // BFClientSoldierHealthComponent*
		constexpr std::uintptr_t component_health = 0x20;          // health, then max health

		death_watch life;
		ULONGLONG respawn_log_at{};

		// #73: what the mod holds at a death and after the respawn, for the next time the rig breaks.
		void log_state(const char* when)
		{
			const input::gamepad_state pad = input::latest();
			std::string arms = body::report();
			arms = arms.substr(0, arms.find('\n'));
			std::string calibration = ui::body_calibration_command("");
			calibration = calibration.substr(0, calibration.find('\n'));
			log::write("death watch: %s; game pad buttons=%04X lt=%u rt=%u left=(%d,%d); %s; grenade_in_hand=%d gun_dropped=%d gun_in_off_hand=%d "
				"reload_claims_hand=%d pointer_active=%d; %s; %s", when, pad.buttons, pad.left_trigger, pad.right_trigger, pad.left_x, pad.left_y,
				input::focus_release_report().c_str(), grenade_in_hand() ? 1 : 0, gun_dropped() ? 1 : 0, gun_in_off_hand() ? 1 : 0,
				reload_claims_free_hand() ? 1 : 0, hooks::game_pointer_active() ? 1 : 0, calibration.c_str(), arms.c_str());
		}

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
		const bool known = read(local_soldier_global, soldier) && soldier && read(soldier + soldier_health_component, component)
			&& read(component + component_health, health) && std::isfinite(health[0]) && health[1] > 0.0f && health[1] <= 10000.0f;
		const ULONGLONG now = GetTickCount64();
		switch (life.update(known, health[0]))
		{
		case life_event::died:
			if (settings::global().boolean("death_release", true))
			{
				input::close_held_inputs();
			}
			log_state("died");
			break;
		case life_event::respawned:
			respawn_log_at = now + 3000;
			break;
		case life_event::none:
			break;
		}
		if (respawn_log_at && now >= respawn_log_at)
		{
			respawn_log_at = 0;
			log_state("3 s after the respawn");
		}
		if (!known)
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
		char text[160];
		std::snprintf(text, sizeof(text), "health=%.1f hits=%llu last_damage=%.1f deaths=%u dead=%d", seen_health, hits, last_damage, life.deaths(),
			life.dead() ? 1 : 0);
		return text;
	}
}
