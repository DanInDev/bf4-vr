#include "weapon/wall_stop.hpp"
#include "weapon/wall_stop_game.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>

#include "body/arm_pose.hpp"
#include "physics/ray_cast.hpp"
#include "weapon/physical_reload.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// The barrel from Wep_Root: rifles reach ~60 cm ahead of it, pistols ~22 cm; the ray starts 15 cm behind
		// it (the hand inside a wall: the hit is behind the root, more than max_push inside: let go).
		constexpr float rifle_reach = 0.88f; // Wep_Root is at the back: trigger 0.32, handguard 0.51, rail 0.64 (re/notes/weapon.md); the AK-12 is 0.94 m long
		constexpr float pistol_reach = 0.22f;
		constexpr float behind = 0.15f;

		std::atomic<bool> on{ true };
		std::mutex lock;
		wall_stop stop;
		LARGE_INTEGER last{};
		float last_inside{};
		bool last_known{};
		v3 last_from, last_to;
		std::uint64_t pushes{};
		std::uint64_t frames{}, skipped{};
		const char* skip_reason{ "" };

		v3 rotate(const q4& q, const v3& v)
		{
			const float tx = 2.0f * (q.y * v.z - q.z * v.y), ty = 2.0f * (q.z * v.x - q.x * v.z), tz = 2.0f * (q.x * v.y - q.y * v.x);
			return { v.x + q.w * tx + (q.y * tz - q.z * ty), v.y + q.w * ty + (q.z * tx - q.x * tz), v.z + q.w * tz + (q.x * ty - q.y * tx) };
		}
	}

	void set_wall_stop(bool enabled)
	{
		on = enabled;
	}

	void update_wall_stop()
	{
		const std::lock_guard guard(lock);
		LARGE_INTEGER now{}, frequency{};
		QueryPerformanceCounter(&now);
		QueryPerformanceFrequency(&frequency);
		const float seconds = last.QuadPart ? std::fmin(static_cast<float>(now.QuadPart - last.QuadPart) / static_cast<float>(frequency.QuadPart), 0.1f) : 0.0f;
		last = now;
		const body::weapon_points p = body::latest_weapon_points();
		++frames;
		const bool applies = body::wall_push_applies();
		if (!on.load(std::memory_order_relaxed) || !p.valid || !p.has_held_root || !applies)
		{
			++skipped;
			skip_reason = !on.load() ? "off" : !p.valid ? "no rig" : !p.has_held_root ? "no held gun" : "no gun on the ray";
			stop.reset();
			body::set_wall_push(0.0f);
			return;
		}
		// The real gun, where the controller holds it (not the drawn one: pushed back, and short of a far reach).
		const q4 root_rotation{ p.held_root_rotation[0], p.held_root_rotation[1], p.held_root_rotation[2], p.held_root_rotation[3] };
		const v3 along = rotate(root_rotation, { 0.0f, 0.0f, 1.0f });
		const v3 root{ p.held_root[0], p.held_root[1], p.held_root[2] };
		const float reach = held_is_pistol() ? pistol_reach : rifle_reach;
		const v3 from_local{ root.x - along.x * behind, root.y - along.y * behind, root.z - along.z * behind };
		const v3 to_local{ root.x + along.x * reach, root.y + along.y * reach, root.z + along.z * reach };
		// LOCAL to the world: world = world_origin + rotate(conjugate(world_to_local), local).
		const q4 to_world{ -p.world_to_local[0], -p.world_to_local[1], -p.world_to_local[2], p.world_to_local[3] };
		const auto world = [&](const v3& local)
		{
			const v3 r = rotate(to_world, local);
			return v3{ p.world_origin[0] + r.x, p.world_origin[1] + r.y, p.world_origin[2] + r.z };
		};
		last_from = world(from_local);
		last_to = world(to_local);
		physics::request_barrel_ray(last_from, last_to);
		float fraction = 1.0f;
		std::uint64_t request = 0;
		const bool known = physics::latest_barrel_ray(fraction, request);
		const float length = behind + reach;
		const float inside = fraction < 1.0f ? length * (1.0f - fraction) : -1.0f;
		last_known = known;
		last_inside = inside;
		const float pushed = stop.update(seconds, known, inside);
		if (pushed > 0.0f)
		{
			++pushes;
		}
		body::set_wall_push(pushed);
	}

	std::string wall_stop_report()
	{
		const std::lock_guard guard(lock);
		char text[400];
		std::snprintf(text, sizeof(text), "wall_stop=%d known=%d inside=%.3f push=%.3f let_go=%d frames_pushed=%llu frames=%llu skipped=%llu (%s) ray=(%.3f,%.3f,%.3f)->(%.3f,%.3f,%.3f)",
			on.load() ? 1 : 0, last_known ? 1 : 0, last_inside, stop.push(), stop.let_go() ? 1 : 0, static_cast<unsigned long long>(pushes),
			static_cast<unsigned long long>(frames), static_cast<unsigned long long>(skipped), skip_reason, last_from.x, last_from.y, last_from.z, last_to.x,
			last_to.y, last_to.z);
		return text;
	}
}
