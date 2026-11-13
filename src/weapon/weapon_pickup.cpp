#include "weapon/weapon_pickup.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "common/log.hpp"
#include "hooks/game_window.hpp"
#include "input/haptics.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// BF4 179547, x64 only (re/symbols/bf4-179547.md "Pickups").
		constexpr std::uintptr_t pickup_vtable = static_cast<std::uintptr_t>(0x141e5fd70ull); // ClientPickupEntity
		constexpr std::uintptr_t pickup_data = 0x30;      // PickupEntityData*
		constexpr std::uintptr_t pickup_placement = 0x140; // -> +0x30 Vec3, where it lies
		constexpr std::uintptr_t placement_position = 0x30;
		constexpr std::uintptr_t data_radius = 0xcc;      // InteractionRadius (Float32)
		constexpr std::uintptr_t data_allow = 0xdd;       // AllowPickup (Boolean)
		constexpr unsigned short pickup_key = 0x52;       // R: ConceptPickUp (also ConceptReload)
		constexpr ULONGLONG census_interval_ms = 1500;
		constexpr std::size_t max_pickups = 64;

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		std::atomic<bool> enabled{ true };
		pickup_grab grab;
		bool key_down{};
		std::uint64_t takes_started{}, takes_done{}, presses{};
		int last_count{};
		float nearest[2]{ -1.0f, -1.0f };
		char last_event[96] = "none";

		std::mutex census_lock;
		std::vector<std::uintptr_t> census;
		std::atomic<bool> scanning{};
		std::atomic<ULONGLONG> last_scan{};
		std::atomic<std::uint64_t> scans{};
		std::atomic<std::uint32_t> scan_ms{};

		std::size_t scan_region(const std::uintptr_t* begin, std::size_t words, std::uintptr_t* out, std::size_t room)
		{
			std::size_t found = 0;
			__try
			{
				for (std::size_t i = 0; i < words && found < room; ++i)
				{
					if (begin[i] == pickup_vtable)
					{
						out[found++] = reinterpret_cast<std::uintptr_t>(begin + i);
					}
				}
			}
			__except (EXCEPTION_EXECUTE_HANDLER)
			{
			}
			return found;
		}

		// Background: the ClientPickupEntity objects of the heap (dropped and placed weapons).
		void scan_census()
		{
			const ULONGLONG start = GetTickCount64();
			std::vector<std::uintptr_t> found(max_pickups);
			std::size_t count = 0;
			MEMORY_BASIC_INFORMATION region{};
			for (std::uintptr_t at = 0x10000; count < max_pickups && VirtualQuery(reinterpret_cast<LPCVOID>(at), &region, sizeof(region)) == sizeof(region);
				at = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize)
			{
				if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE && region.Protect == PAGE_READWRITE)
				{
					count += scan_region(static_cast<const std::uintptr_t*>(region.BaseAddress), region.RegionSize / sizeof(std::uintptr_t),
						found.data() + count, max_pickups - count);
				}
			}
			found.resize(count);
			{
				const std::lock_guard guard(census_lock);
				census = std::move(found);
			}
			scan_ms = static_cast<std::uint32_t>(GetTickCount64() - start);
			++scans;
			scanning = false;
		}

		void request_census(ULONGLONG now)
		{
			bool idle = false;
			if (now - last_scan.load() < census_interval_ms || !scanning.compare_exchange_strong(idle, true))
			{
				return;
			}
			last_scan = now;
			std::thread(scan_census).detach();
		}

		void rotate(const float (&q)[4], const float (&v)[3], float (&out)[3])
		{
			// v + 2w (q x v) + 2 q x (q x v)
			const float tx = 2.0f * (q[1] * v[2] - q[2] * v[1]);
			const float ty = 2.0f * (q[2] * v[0] - q[0] * v[2]);
			const float tz = 2.0f * (q[0] * v[1] - q[1] * v[0]);
			out[0] = v[0] + q[3] * tx + (q[1] * tz - q[2] * ty);
			out[1] = v[1] + q[3] * ty + (q[2] * tx - q[0] * tz);
			out[2] = v[2] + q[3] * tz + (q[0] * ty - q[1] * tx);
		}

		// The census's pickups as they lie now, in OpenXR LOCAL.
		int read_pickups(const body::weapon_points& points, pickup_point (&out)[max_pickups])
		{
			std::vector<std::uintptr_t> list;
			{
				const std::lock_guard guard(census_lock);
				list = census;
			}
			int count = 0;
			for (std::uintptr_t p : list)
			{
				std::uintptr_t vtable{}, data{}, placement{};
				float world[3]{}, radius{};
				std::uint8_t allow{};
				if (!read(p, vtable) || vtable != pickup_vtable || !read(p + pickup_data, data) || !read(data + data_radius, radius)
					|| !read(data + data_allow, allow) || !allow || !(radius > 0.0f && radius < 20.0f) || !read(p + pickup_placement, placement)
					|| !read(placement + placement_position, world))
				{
					continue;
				}
				const float relative[3]{ world[0] - points.world_origin[0], world[1] - points.world_origin[1], world[2] - points.world_origin[2] };
				pickup_point& point = out[count++];
				rotate(points.world_to_local, relative, point.position);
				point.radius = radius;
			}
			return count;
		}

		void press(bool down)
		{
			if (down == key_down || !hooks::press_game_key(pickup_key, down))
			{
				return;
			}
			key_down = down;
			if (down)
			{
				++presses;
			}
		}
	}

	void set_weapon_pickup(bool on)
	{
		enabled = on;
	}

	bool update_weapon_pickup(const body::weapon_points& points, bool points_fresh, bool head_valid, const float (&head)[3], bool off_grip,
		bool gun_grip, const bool (&busy)[2], bool weapon_changed, float seconds)
	{
#if defined(_M_X64)
		const ULONGLONG now = GetTickCount64();
		if (!enabled.load(std::memory_order_relaxed) || !points_fresh)
		{
			press(false);
			return false;
		}
		request_census(now);
		static pickup_point pickups[max_pickups];
		const int count = read_pickups(points, pickups);
		last_count = count;
		pickup_frame frame;
		frame.seconds = seconds;
		frame.head_valid = head_valid;
		std::memcpy(frame.head, head, sizeof(frame.head));
		frame.pickups = pickups;
		frame.count = count;
		frame.weapon_changed = weapon_changed;
		const bool grips[2]{ off_grip, gun_grip };
		for (int h = 0; h < 2; ++h)
		{
			pickup_hand& hand = frame.hands[h];
			hand.valid = points.hand_valid[h];
			std::memcpy(hand.palm, points.palm[h], sizeof(hand.palm));
			hand.grip = grips[h];
			hand.busy = busy[h];
			nearest[h] = -1.0f;
			for (int i = 0; i < count; ++i)
			{
				const float dx = pickups[i].position[0] - hand.palm[0], dy = pickups[i].position[1] - hand.palm[1],
					dz = pickups[i].position[2] - hand.palm[2];
				const float d = std::sqrt(dx * dx + dy * dy + dz * dz);
				nearest[h] = nearest[h] < 0.0f || d < nearest[h] ? d : nearest[h];
			}
		}
		const bool was_holding = key_down;
		const pickup_result result = grab.update(frame);
		if (result.started)
		{
			++takes_started;
			input::haptic(result.hand, input::pulses::grab);
			std::snprintf(last_event, sizeof(last_event), "take by the %s hand", result.hand == 0 ? "off" : "gun");
			log::write("pickup: %s (R held), %d pickups near", last_event, count);
		}
		if (was_holding && weapon_changed)
		{
			++takes_done;
			std::snprintf(last_event, sizeof(last_event), "taken (the weapon in hand changed)");
			log::write("pickup: taken");
		}
		press(result.hold);
		return result.hold;
#else
		(void)points, (void)points_fresh, (void)head_valid, (void)head, (void)off_grip, (void)gun_grip, (void)busy, (void)weapon_changed, (void)seconds;
		return false;
#endif
	}

	std::string weapon_pickup_report()
	{
		std::size_t census_size = 0;
		{
			const std::lock_guard guard(census_lock);
			census_size = census.size();
		}
		char text[384];
		std::snprintf(text, sizeof(text),
			"weapon_pickup=%d census=%zu usable=%d scans=%llu scan_ms=%u nearest_off=%.2f nearest_gun=%.2f r_down=%d presses=%llu takes=%llu taken=%llu last=%s",
			enabled.load() ? 1 : 0, census_size, last_count, static_cast<unsigned long long>(scans.load()), scan_ms.load(), nearest[0], nearest[1], key_down ? 1 : 0,
			static_cast<unsigned long long>(presses), static_cast<unsigned long long>(takes_started), static_cast<unsigned long long>(takes_done), last_event);
		return text;
	}
}
