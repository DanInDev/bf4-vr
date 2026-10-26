#include "weapon/physical_grenade.hpp"

#include <windows.h>
#include <Xinput.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <string>

#include "body/arm_pose.hpp"
#include "common/config.hpp"
#include "common/log.hpp"
#include "stereo/tracking.hpp"
#include "weapon/grenade_hands.hpp"
#include "weapon/reload_sounds.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// BF4 179547 (re/notes/weapon.md "Grenades"). Client weapon slots as for the reload.
		constexpr std::uintptr_t local_soldier_global = static_cast<std::uintptr_t>(0x14230f260ull); // x64 only
		constexpr std::uintptr_t server_soldier_global = static_cast<std::uintptr_t>(0x142311db8ull);
		constexpr std::uintptr_t soldier_weapons = 0x570;
		constexpr std::uintptr_t weapons_begin = 0x890;
		constexpr std::uintptr_t weapons_end = 0x898;
		constexpr std::uintptr_t weapon_unlock = 0x4850;
		constexpr std::uintptr_t unlock_name = 0x20;
		constexpr std::uintptr_t weapon_data = 0x30;
		constexpr std::uintptr_t data_firing = 0x90;
		constexpr std::uintptr_t firing_primary = 0x10;
		constexpr std::uintptr_t server_soldier_weapons = 0x7c0;
		constexpr std::uintptr_t server_weapon_slots = 0x828;
		constexpr std::uintptr_t server_firing = 0x200;
		constexpr std::uintptr_t firing_magazine = 0x1a0;
		// FiringFunctionData: Shot (ShotConfigData) at +0x60, FireLogic.HoldAndRelease at +0x168.
		constexpr std::uintptr_t shot_position = 0x60;
		constexpr std::uintptr_t shot_direction = 0x70;
		constexpr std::uintptr_t shot_speed = 0x80;
		constexpr std::uintptr_t shot_projectile = 0xb0;
		constexpr std::uintptr_t shot_spawn_delay = 0xd0;
		constexpr std::uintptr_t shot_force_to_camera = 0xe5;
		constexpr std::uintptr_t hold_delay = 0x178;
		constexpr std::uintptr_t projectile_detonate_on_timeout = 0xd8;
		constexpr int max_slots = 16;
		constexpr ULONGLONG pulse_ms = 80;
		constexpr ULONGLONG restore_ms = 400;
		constexpr ULONGLONG safe_restore_ms = 6000;
		constexpr ULONGLONG points_max_age_ms = 250;
		constexpr float max_speed = 30.0f;

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		template <typename T>
		bool write(std::uintptr_t address, const T& value)
		{
			return address && WriteProcessMemory(GetCurrentProcess(), reinterpret_cast<LPVOID>(address), &value, sizeof(value), nullptr);
		}

		bool is_grenade(const char* name)
		{
			for (const char* prefix : { "U_M67", "U_V40", "U_M34", "U_RGO", "U_Flashbang", "U_Smoke" })
			{
				if (std::strncmp(name, prefix, std::strlen(prefix)) == 0)
				{
					return true;
				}
			}
			return false;
		}

		struct grenade_weapon
		{
			bool valid{};
			char name[48]{};
			std::uintptr_t function{}; // FiringFunctionData (shared data: write only around our throw)
			int count{};               // ready + spare
		};

		grenade_weapon find_grenade_weapon()
		{
			grenade_weapon found;
			std::uintptr_t soldier{}, weapons{}, begin{}, end{}, server_soldier{}, server_weapons{};
			if (!read(local_soldier_global, soldier) || !read(soldier + soldier_weapons, weapons) || !read(weapons + weapons_begin, begin)
				|| !read(weapons + weapons_end, end) || !begin || end <= begin || !read(server_soldier_global, server_soldier)
				|| !read(server_soldier + server_soldier_weapons, server_weapons))
			{
				return found;
			}
			const int slots = static_cast<int>((end - begin) / 8);
			for (int slot = 0; slot < slots && slot < max_slots; ++slot)
			{
				std::uintptr_t client{}, unlock{}, name{};
				if (!read(begin + slot * 8, client) || !client || !read(client + weapon_unlock, unlock) || !read(unlock + unlock_name, name)
					|| !read(name, found.name))
				{
					continue;
				}
				found.name[sizeof(found.name) - 1] = 0;
				if (!is_grenade(found.name))
				{
					continue;
				}
				std::uintptr_t data{}, firing{}, server_weapon{}, server_ammo{};
				int ammo[2]{};
				if (!read(client + weapon_data, data) || !read(data + data_firing, firing) || !read(firing + firing_primary, found.function)
					|| !read(server_weapons + server_weapon_slots + slot * 8, server_weapon) || !read(server_weapon + server_firing, server_ammo)
					|| !read(server_ammo + firing_magazine, ammo))
				{
					continue;
				}
				found.count = (ammo[0] > 0 ? ammo[0] : 0) + (ammo[1] > 0 ? ammo[1] : 0);
				found.valid = found.function != 0;
				return found;
			}
			return found;
		}

		// The launch data of one throw, written into the shared FiringFunctionData and restored.
		struct launch_backup
		{
			std::uintptr_t function{};
			std::uintptr_t projectile{};
			float position[3]{}, direction[3]{}, speed[3]{};
			float spawn_delay{}, hold{};
			std::uint8_t force_to_camera{}, detonate{};
			bool safe{};
		};

		std::atomic<bool> physical{};
		std::atomic<float> throw_scale{ 1.5f };
		std::atomic<bool> holding{};
		std::atomic<bool> active{};
		std::atomic<bool> gun_busy{};
		std::atomic<ULONGLONG> pulse_until{};

		std::mutex lock;
		grenade_hands hands;
		launch_backup backup;
		bool pending{};
		ULONGLONG restore_at{};
		LARGE_INTEGER last_update{};
		std::uint64_t throws{};
		const char* last_event{ "none" };
		float last_velocity[3]{};

		void restore(const launch_backup& b)
		{
			write(b.function + shot_position, b.position);
			write(b.function + shot_direction, b.direction);
			write(b.function + shot_speed, b.speed);
			write(b.function + shot_spawn_delay, b.spawn_delay);
			write(b.function + shot_force_to_camera, b.force_to_camera);
			write(b.function + hold_delay, b.hold);
			if (b.safe)
			{
				write(b.projectile + projectile_detonate_on_timeout, b.detonate);
			}
		}

		// Called with lock held. position and velocity: OpenXR axes relative to the first-person camera
		// (x right, y up, -z forward); the shot frame is x left, y up, z forward, pitched with the aim.
		bool launch(const grenade_weapon& weapon, const float (&position)[3], const float (&velocity)[3], bool safe)
		{
			if (pending || !weapon.valid)
			{
				return false;
			}
			launch_backup saved;
			saved.function = weapon.function;
			saved.safe = safe;
			if (!read(weapon.function + shot_position, saved.position) || !read(weapon.function + shot_direction, saved.direction)
				|| !read(weapon.function + shot_speed, saved.speed) || !read(weapon.function + shot_spawn_delay, saved.spawn_delay)
				|| !read(weapon.function + shot_force_to_camera, saved.force_to_camera) || !read(weapon.function + hold_delay, saved.hold)
				|| !read(weapon.function + shot_projectile, saved.projectile)
				|| (safe && !read(saved.projectile + projectile_detonate_on_timeout, saved.detonate)))
			{
				return false;
			}
			float speed[3] = { -velocity[0], velocity[1], -velocity[2] };
			const float length = std::sqrt(speed[0] * speed[0] + speed[1] * speed[1] + speed[2] * speed[2]);
			if (length > max_speed)
			{
				for (float& s : speed)
				{
					s *= max_speed / length;
				}
			}
			const float local_position[3] = { -position[0], position[1], -position[2] };
			const float forward[3] = { 0.0f, 0.0f, 1.0f };
			const float zero = 0.0f;
			const std::uint8_t off = 0;
			write(weapon.function + shot_position, local_position);
			write(weapon.function + shot_direction, forward);
			write(weapon.function + shot_speed, speed);
			write(weapon.function + shot_spawn_delay, zero);
			write(weapon.function + shot_force_to_camera, off);
			write(weapon.function + hold_delay, zero);
			if (safe)
			{
				write(saved.projectile + projectile_detonate_on_timeout, off);
			}
			backup = saved;
			pending = true;
			const ULONGLONG now = GetTickCount64();
			restore_at = now + (safe ? safe_restore_ms : restore_ms);
			pulse_until = now + pulse_ms;
			++throws;
			last_velocity[0] = velocity[0];
			last_velocity[1] = velocity[1];
			last_velocity[2] = velocity[2];
			return true;
		}

		// The grenade pouch: 70 cm below the head, 10 cm ahead along the head's yaw.
		bool belt_point(weapon::v3& belt)
		{
			const stereo::head_sample head = stereo::latest_head_sample();
			if (!head.valid)
			{
				return false;
			}
			const float* l = head.eyes[0].position;
			const float* r = head.eyes[1].position;
			const float* o = head.eyes[0].orientation;
			float fx = -2.0f * (o[0] * o[2] + o[1] * o[3]);
			float fz = -(1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1]));
			const float length = std::sqrt(fx * fx + fz * fz);
			if (length > 1e-4f)
			{
				fx /= length;
				fz /= length;
			}
			belt = { 0.5f * (l[0] + r[0]) + 0.10f * fx, 0.5f * (l[1] + r[1]) - 0.70f, 0.5f * (l[2] + r[2]) + 0.10f * fz };
			return true;
		}

		const char* event_name(grenade_event event)
		{
			switch (event)
			{
			case grenade_event::none: return "none";
			case grenade_event::taken: return "taken";
			case grenade_event::pin_pulled: return "pin_pulled";
			case grenade_event::thrown: return "thrown";
			case grenade_event::dropped: return "dropped";
			case grenade_event::put_back: return "put_back";
			}
			return "?";
		}
	}

	void set_physical_grenades(bool enabled)
	{
		physical = enabled;
	}

	void set_grenade_throw_scale(float scale)
	{
		throw_scale = scale;
	}

	bool grenade_in_hand()
	{
		return holding.load(std::memory_order_relaxed);
	}

	void update_physical_grenades()
	{
#if defined(_M_X64)
		const std::lock_guard guard(lock);
		const ULONGLONG now = GetTickCount64();
		if (pending && now >= restore_at)
		{
			restore(backup);
			pending = false;
		}
		LARGE_INTEGER counter{}, frequency{};
		QueryPerformanceCounter(&counter);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last_update.QuadPart ? std::fmin(static_cast<float>(counter.QuadPart - last_update.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last_update = counter;

		if (!physical.load(std::memory_order_relaxed))
		{
			hands.reset();
			holding = false;
			active = false;
			gun_busy = false;
			return;
		}
		const grenade_weapon weapon = find_grenade_weapon();
		const body::weapon_points points = body::latest_weapon_points();
		const input::gamepad_state pad = input::latest();
		grenade_frame frame;
		frame.seconds = seconds;
		frame.hands_valid = points.hand_valid[0] && points.hand_valid[1] && now - points.at < points_max_age_ms;
		frame.off_hand = { points.hand[0][0], points.hand[0][1], points.hand[0][2] };
		frame.gun_hand = { points.hand[1][0], points.hand[1][1], points.hand[1][2] };
		if (!belt_point(frame.belt))
		{
			frame.belt = { 0.0f, -0.70f, -0.10f };
		}
		frame.off_grip = (pad.buttons & XINPUT_GAMEPAD_LEFT_SHOULDER) != 0;
		frame.gun_grip = (pad.buttons & XINPUT_GAMEPAD_RIGHT_SHOULDER) != 0;
		frame.have_grenade = weapon.valid && weapon.count > 0;
		const grenade_result result = hands.update(frame);
		if (result.event != grenade_event::none)
		{
			last_event = event_name(result.event);
		}
		if (result.event == grenade_event::taken || result.event == grenade_event::put_back)
		{
			play(cue::cloth);
		}
		else if (result.event == grenade_event::pin_pulled)
		{
			play(cue::pin);
		}
		if (result.event == grenade_event::thrown || result.event == grenade_event::dropped)
		{
			const float scale = result.event == grenade_event::thrown ? throw_scale.load(std::memory_order_relaxed) : 0.0f;
			const float position[3] = { result.position.x, result.position.y, result.position.z };
			// A drop still needs a direction: a slow forward push.
			const float velocity[3] = { result.velocity.x * scale, result.velocity.y * scale,
				result.event == grenade_event::dropped ? -0.3f : result.velocity.z * scale };
			if (!launch(weapon, position, velocity, false))
			{
				log::write("grenade: %s not launched (no grenade weapon, or a throw still pending)", last_event);
			}
		}
		holding = result.holding;
		gun_busy = result.gun_busy;
		active = weapon.valid;
#endif
	}

	input::gamepad_state filter_grenade_pad(const input::gamepad_state& pad)
	{
		input::gamepad_state out = pad;
		if (active.load(std::memory_order_relaxed))
		{
			// The game's quick throw is ours: only the pulse of a physical throw reaches it.
			out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_LEFT_SHOULDER);
			if (gun_busy.load(std::memory_order_relaxed))
			{
				out.buttons &= static_cast<std::uint16_t>(~XINPUT_GAMEPAD_RIGHT_SHOULDER);
			}
		}
		if (GetTickCount64() < pulse_until.load(std::memory_order_relaxed))
		{
			out.buttons |= XINPUT_GAMEPAD_LEFT_SHOULDER;
			out.connected = true; // also without a headset session (pipe tests)
		}
		return out;
	}

	std::string grenade_pipe_command(const std::string& argument)
	{
		const std::lock_guard guard(lock);
		const grenade_weapon weapon = find_grenade_weapon();
		if (argument.rfind("throw", 0) == 0)
		{
			if (!config::research_enabled())
			{
				return "error: 'grenade throw' needs research = 1 in bfvr.ini";
			}
			float yaw = 0.0f, pitch = 0.0f, speed = 0.0f;
			char safe_word[8]{};
			if (sscanf_s(argument.c_str() + 5, "%f %f %f %7s", &yaw, &pitch, &speed, safe_word, static_cast<unsigned>(sizeof(safe_word))) < 3)
			{
				return "error: usage: grenade throw <yaw right deg> <pitch up deg> <m/s> [safe]";
			}
			const body::weapon_points points = body::latest_weapon_points();
			float position[3] = { -0.2f, -0.2f, -0.3f };
			if (points.hand_valid[0])
			{
				std::memcpy(position, points.hand[0], sizeof(position));
			}
			constexpr float radians = 3.14159265f / 180.0f;
			const float velocity[3] = { speed * std::sin(yaw * radians) * std::cos(pitch * radians), speed * std::sin(pitch * radians),
				-speed * std::cos(yaw * radians) * std::cos(pitch * radians) };
			if (!launch(weapon, position, velocity, std::strcmp(safe_word, "safe") == 0))
			{
				return "error: no grenade weapon, or a throw still pending";
			}
			last_event = "thrown (pipe)";
		}
		char text[512];
		std::snprintf(text, sizeof(text),
			"grenade=%s weapon=%s count=%d holding=%d pending=%d throws=%llu last_event=%s last_velocity=(%.2f,%.2f,%.2f) scale=%.2f",
			physical ? "physical" : "button", weapon.valid ? weapon.name : "-", weapon.count, holding ? 1 : 0, pending ? 1 : 0,
			static_cast<unsigned long long>(throws), last_event, last_velocity[0], last_velocity[1], last_velocity[2], throw_scale.load());
		return text;
	}
}
