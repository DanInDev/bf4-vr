#include "physics/ray_cast.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <mutex>

#include "body/arm_pose.hpp"
#include "common/log.hpp"
#include "stereo/eye_math.hpp"
#include "stereo/tracking.hpp"

namespace bfvr::physics
{
	ground_cache& level_ground()
	{
		static ground_cache cache;
		return cache;
	}

	namespace
	{
		// One ray to cast on the game thread for the pipe (the ground cache's rays are taken from it directly).
		struct pipe_ray
		{
			v3 from;
			v3 to;
			bool done{};
			bool hit{};
			v3 point;
			v3 normal;
			float fraction{};
			double micros{};
			unsigned long error{};
		};
		std::mutex pipe_lock;
		pipe_ray* pipe_pending{};

		// #63: the barrel ray (latest request wins).
		std::mutex barrel_lock;
		v3 barrel_from, barrel_to;
		std::uint64_t barrel_asked{}, barrel_cast{};
		float barrel_fraction{ 1.0f };
		ULONGLONG barrel_at{};

		std::atomic<std::uint64_t> rays_cast{};
		std::atomic<std::uint64_t> rays_hit{};
		std::atomic<std::uint64_t> rays_failed{};
		std::atomic<double> micros_total{};
		std::atomic<int> state{}; // 0 not checked, 1 ready, -1 off for the session

		struct ray_hit
		{
			bool hit{};
			v3 point;
			v3 normal;
			float fraction{};
		};

#if defined(_M_X64)
		// re/symbols/bf4-179547.md "Physics ray query", "Client physics world*".
		constexpr std::uintptr_t ray_query_address = 0x140986CA0;
		constexpr std::uintptr_t client_world_global = 0x142668F90;
		constexpr std::uint8_t ray_query_prologue[] = { 0x40, 0x55, 0x56, 0x57, 0x41, 0x54, 0x41, 0x55, 0x41, 0x56, 0x41, 0x57, 0x48, 0x8D, 0xAC, 0x24,
			0x10, 0xFE, 0xFF, 0xFF, 0x48, 0x81, 0xEC, 0xF0, 0x02, 0x00, 0x00 };
		constexpr int level_only = 0x34; // no water, ragdolls or characters
		constexpr const char* ray_name = "bfvr::physics::ray";

		using ray_query_t = bool(__fastcall*)(void* world, const char* name, int async, const float* from, const float* to, void* hit, int flags, void* skip);

		struct alignas(16) hit_record
		{
			float position[4];
			float normal[4];
			void* body;
			int unknown;
			int material;
			int part;
			float fraction;
			std::uint8_t rest[8];
		};
		static_assert(sizeof(hit_record) == 0x40);

		// eastl fixed_vector<const void*, 8>: begin, end, capacity end, one more pointer, the inline slots.
		struct skip_list
		{
			void** begin;
			void** end;
			void** capacity;
			void* other;
			void* slots[8];
		};
		static_assert(sizeof(skip_list) == 0x60);

		bool readable(std::uintptr_t address, void* out, std::size_t size)
		{
			SIZE_T got = 0;
			return address >= 0x10000 && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), out, size, &got) && got == size;
		}

		bool check_once()
		{
			const int now = state.load(std::memory_order_relaxed);
			if (now != 0)
			{
				return now > 0;
			}
			std::uint8_t bytes[sizeof(ray_query_prologue)]{};
			if (!readable(ray_query_address, bytes, sizeof(bytes)) || std::memcmp(bytes, ray_query_prologue, sizeof(bytes)) != 0)
			{
				log::write("physics: the ray query's bytes at %llx differ; ray casts off for this session", static_cast<unsigned long long>(ray_query_address));
				state = -1;
				return false;
			}
			log::write("physics: ray query bytes verified");
			state = 1;
			return true;
		}

		// No C++ objects here: the exception filter needs a plain frame.
		unsigned long call_query(void* world, const float* from, const float* to, hit_record* hit, skip_list* skip, bool* result)
		{
			__try
			{
				*result = reinterpret_cast<ray_query_t>(ray_query_address)(world, ray_name, 0, from, to, hit, level_only, skip);
				return 0;
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
				return GetExceptionCode();
			}
		}

		// Game thread only. False: not cast (not ready, no level, a ray under 1 cm, or a fault).
		bool cast(const v3& from, const v3& to, ray_hit& out, unsigned long* error = nullptr)
		{
			if (!check_once())
			{
				return false;
			}
			const float dx = to.x - from.x, dy = to.y - from.y, dz = to.z - from.z;
			if (!(dx * dx + dy * dy + dz * dz >= 1e-4f)) // a zero-length ray divides by zero (BF3)
			{
				return false;
			}
			void* world = nullptr;
			if (!readable(client_world_global, &world, sizeof(world)) || !world)
			{
				return false;
			}
			alignas(16) float a[4]{ from.x, from.y, from.z, 0.0f };
			alignas(16) float b[4]{ to.x, to.y, to.z, 0.0f };
			hit_record hit{};
			hit.material = hit.part = -1;
			skip_list skip{};
			skip.begin = skip.end = skip.slots;
			skip.capacity = skip.slots + 8;
			bool result = false;
			LARGE_INTEGER t0{}, t1{}, frequency{};
			QueryPerformanceCounter(&t0);
			const unsigned long code = call_query(world, a, b, &hit, &skip, &result);
			QueryPerformanceCounter(&t1);
			QueryPerformanceFrequency(&frequency);
			if (error)
			{
				*error = code;
			}
			if (code)
			{
				++rays_failed;
				if (rays_failed.load() >= 3)
				{
					log::write("physics: three faults in the ray query (last %08lx); ray casts off for this session", code);
					state = -1;
				}
				return false;
			}
			++rays_cast;
			micros_total = micros_total.load() + static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart);
			out.hit = result && hit.body != nullptr;
			if (out.hit)
			{
				++rays_hit;
				out.point = { hit.position[0], hit.position[1], hit.position[2] };
				out.normal = { hit.normal[0], hit.normal[1], hit.normal[2] };
				out.fraction = hit.fraction;
			}
			return true;
		}
#else
		bool cast(const v3&, const v3&, ray_hit&, unsigned long* = nullptr)
		{
			return false;
		}
#endif

		// The head's world position and forward (OpenXR LOCAL through the rig's world transform).
		bool head_in_world(v3& position, v3& forward)
		{
			const body::weapon_points points = body::latest_weapon_points();
			const stereo::head_sample head = stereo::latest_head_sample();
			if (!points.valid || !head.valid)
			{
				return false;
			}
			const stereo::pose centre = stereo::eye_math::centre_pose(head);
			const float* q = points.world_to_local;
			// world = world_origin + rotate(conjugate(world_to_local), local)
			const auto to_world = [&](const float (&v)[3], bool point)
			{
				const float ux = -q[0], uy = -q[1], uz = -q[2], w = q[3];
				const float tx = 2.0f * (uy * v[2] - uz * v[1]), ty = 2.0f * (uz * v[0] - ux * v[2]), tz = 2.0f * (ux * v[1] - uy * v[0]);
				v3 r{ v[0] + w * tx + (uy * tz - uz * ty), v[1] + w * ty + (uz * tx - ux * tz), v[2] + w * tz + (ux * ty - uy * tx) };
				if (point)
				{
					r = { r.x + points.world_origin[0], r.y + points.world_origin[1], r.z + points.world_origin[2] };
				}
				return r;
			};
			const float p[3]{ centre.position[0], centre.position[1], centre.position[2] };
			position = to_world(p, true);
			const float* o = centre.orientation;
			// The view's -z in LOCAL.
			const float f[3]{ -2.0f * (o[0] * o[2] + o[3] * o[1]), -2.0f * (o[1] * o[2] - o[3] * o[0]), -(1.0f - 2.0f * (o[0] * o[0] + o[1] * o[1])) };
			forward = to_world(f, false);
			return true;
		}
	}

	void ray_cast_on_game_thread()
	{
		{
			const std::lock_guard guard(pipe_lock);
			if (pipe_pending && !pipe_pending->done)
			{
				ray_hit hit;
				LARGE_INTEGER t0{}, t1{}, frequency{};
				QueryPerformanceCounter(&t0);
				const bool cast_ok = cast(pipe_pending->from, pipe_pending->to, hit, &pipe_pending->error);
				QueryPerformanceCounter(&t1);
				QueryPerformanceFrequency(&frequency);
				pipe_pending->micros = static_cast<double>(t1.QuadPart - t0.QuadPart) * 1e6 / static_cast<double>(frequency.QuadPart);
				pipe_pending->hit = cast_ok && hit.hit;
				pipe_pending->point = hit.point;
				pipe_pending->normal = hit.normal;
				pipe_pending->fraction = hit.fraction;
				pipe_pending->done = true;
				if (!cast_ok && !pipe_pending->error)
				{
					pipe_pending->error = 1; // not cast at all
				}
			}
		}
		{
			v3 from, to;
			std::uint64_t asked = 0;
			{
				const std::lock_guard guard(barrel_lock);
				asked = barrel_asked != barrel_cast ? barrel_asked : 0;
				from = barrel_from;
				to = barrel_to;
			}
			if (asked)
			{
				ray_hit hit;
				const bool cast_ok = cast(from, to, hit);
				const std::lock_guard guard(barrel_lock);
				barrel_cast = asked;
				barrel_fraction = cast_ok && hit.hit && std::isfinite(hit.fraction) ? std::fmax(0.0f, std::fmin(1.0f, hit.fraction)) : 1.0f;
				barrel_at = cast_ok ? GetTickCount64() : 0;
			}
		}
		ground_ray rays[6];
		const int count = level_ground().take_rays(rays, 6);
		for (int i = 0; i < count; ++i)
		{
			ray_hit hit;
			if (cast(rays[i].from, rays[i].to, hit))
			{
				level_ground().answer(rays[i].key, hit.hit, hit.point, hit.normal);
			}
			else
			{
				level_ground().retry_later(rays[i].key); // not castable now (no level yet): asked again later
			}
		}
	}

	void request_barrel_ray(const v3& from, const v3& to)
	{
		const std::lock_guard guard(barrel_lock);
		barrel_from = from;
		barrel_to = to;
		++barrel_asked;
	}

	bool latest_barrel_ray(float& fraction, std::uint64_t& request)
	{
		const std::lock_guard guard(barrel_lock);
		fraction = barrel_fraction;
		request = barrel_cast;
		return barrel_cast != 0 && barrel_at != 0 && GetTickCount64() - barrel_at < 100;
	}

	std::string raycast_command(const std::string& argument)
	{
		char text[512];
		if (argument == "stats" || argument.empty())
		{
			const ground_cache::counts c = level_ground().stats();
			const std::uint64_t cast_count = rays_cast.load();
			std::snprintf(text, sizeof(text), "state=%d cast=%llu hit=%llu faults=%llu mean_us=%.1f\nground columns=%zu queued=%zu asked=%llu hits=%llu none=%llu",
				state.load(), static_cast<unsigned long long>(cast_count), static_cast<unsigned long long>(rays_hit.load()),
				static_cast<unsigned long long>(rays_failed.load()), cast_count ? micros_total.load() / static_cast<double>(cast_count) : 0.0, c.columns, c.queued,
				static_cast<unsigned long long>(c.asked), static_cast<unsigned long long>(c.hits), static_cast<unsigned long long>(c.misses));
			return text;
		}
		pipe_ray ray;
		float length = 0.0f;
		char word[16]{};
		if (sscanf_s(argument.c_str(), "%f %f %f %f %f %f", &ray.from.x, &ray.from.y, &ray.from.z, &ray.to.x, &ray.to.y, &ray.to.z) == 6)
		{
		}
		else if (sscanf_s(argument.c_str(), "%15s %f", word, static_cast<unsigned>(sizeof(word)), &length) >= 1 && (std::strcmp(word, "down") == 0 || std::strcmp(word, "ahead") == 0))
		{
			v3 head, forward;
			if (!head_in_world(head, forward))
			{
				return "error: no head or rig yet (pipe fakehead 0.064 without a headset)";
			}
			const bool down = std::strcmp(word, "down") == 0;
			length = length > 0.0f ? length : down ? 3.0f : 20.0f;
			ray.from = head;
			ray.to = down ? v3{ head.x, head.y - length, head.z } : v3{ head.x + forward.x * length, head.y + forward.y * length, head.z + forward.z * length };
		}
		else
		{
			return "error: usage: raycast <x y z> <x y z> | down [metres] | ahead [metres] | stats";
		}
		{
			const std::lock_guard guard(pipe_lock);
			if (pipe_pending)
			{
				return "error: another raycast is waiting";
			}
			pipe_pending = &ray;
		}
		bool done = false;
		for (int waited = 0; waited < 100 && !done; ++waited)
		{
			Sleep(5);
			const std::lock_guard guard(pipe_lock);
			done = ray.done;
		}
		{
			const std::lock_guard guard(pipe_lock);
			pipe_pending = nullptr;
			done = ray.done;
		}
		if (!done)
		{
			return "error: the game thread did not cast it within 0.5 s (no level?)";
		}
		if (ray.error)
		{
			std::snprintf(text, sizeof(text), "error: not cast (state=%d, code %08lx)", state.load(), ray.error);
			return text;
		}
		const float dx = ray.point.x - ray.from.x, dy = ray.point.y - ray.from.y, dz = ray.point.z - ray.from.z;
		if (!ray.hit)
		{
			std::snprintf(text, sizeof(text), "miss from=(%.3f,%.3f,%.3f) to=(%.3f,%.3f,%.3f) us=%.1f", ray.from.x, ray.from.y, ray.from.z, ray.to.x, ray.to.y, ray.to.z,
				ray.micros);
			return text;
		}
		std::snprintf(text, sizeof(text), "hit at=(%.3f,%.3f,%.3f) distance=%.3f normal=(%.2f,%.2f,%.2f) fraction=%.3f from=(%.3f,%.3f,%.3f) us=%.1f", ray.point.x,
			ray.point.y, ray.point.z, std::sqrt(dx * dx + dy * dy + dz * dz), ray.normal.x, ray.normal.y, ray.normal.z, ray.fraction, ray.from.x, ray.from.y,
			ray.from.z, ray.micros);
		return text;
	}
}
