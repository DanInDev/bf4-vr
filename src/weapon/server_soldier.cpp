#include "weapon/server_soldier.hpp"

#include "common/controlled.hpp"

#include <windows.h>

#include <atomic>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <thread>
#include <vector>

#include "common/log.hpp"

namespace bfvr::weapon
{
	namespace
	{
		// BF4 179547, x64 only (re/symbols/bf4-179547.md "The player's server soldier").
		constexpr std::uintptr_t client_animatable = 0x1e8;               // first-person rig: +0x90 world transform, feet at +0x30
		constexpr std::uintptr_t client_feet = 0x90 + 0x30;
		constexpr std::uintptr_t server_soldier_vtable = static_cast<std::uintptr_t>(0x141e67ba0ull);   // ServerSoldierEntity (typeinfo vtable map)
		constexpr std::uintptr_t server_position = 0x720;                  // Vec3, the soldier's feet (live: 0.00 m from the client's)
		// Static soldier tables: soldiers spawned this session, order not fixed.
		constexpr std::uintptr_t table_a = static_cast<std::uintptr_t>(0x142311d88ull); // stride 0x10
		constexpr int table_a_count = 16;
		constexpr std::uintptr_t table_b = static_cast<std::uintptr_t>(0x14237f390ull); // stride 0x20
		constexpr int table_b_count = 8;
		constexpr std::uintptr_t single_slot = static_cast<std::uintptr_t>(0x1427248b0ull);
		constexpr float tolerance_m = 1.0f;
		constexpr ULONGLONG scan_interval_ms = 3000;
		constexpr std::size_t max_scan_finds = 256;

		template <typename T>
		bool read(std::uintptr_t address, T& value)
		{
			SIZE_T done = 0;
			return address && ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(address), &value, sizeof(value), &done)
				&& done == sizeof(value);
		}

		std::atomic<std::uintptr_t> current{};
		std::uintptr_t current_client{}; // the client soldier current was found for
		float off_seconds{};
		ULONGLONG last_call_ms{};
		std::mutex choose_lock;          // physical reload and grenades ask from different threads
		std::atomic<int> found_by{};        // 0 none, 1 kept, 2 static table, 3 heap scan
		std::atomic<std::uint64_t> changes{}, lookups{}, scans{}, misses{};
		std::atomic<ULONGLONG> last_scan_at{};
		std::atomic<bool> scanning{};
		std::mutex finds_lock;
		std::vector<std::uintptr_t> scan_finds;

		bool feet_of_client(std::uintptr_t& soldier, float (&feet)[3])
		{
			std::uintptr_t rig{};
			return (soldier = game::local_soldier()) != 0 && soldier && read(soldier + client_animatable, rig) && rig && read(rig + client_feet, feet);
		}

		soldier_candidate candidate(std::uintptr_t address)
		{
			soldier_candidate c;
			c.address = static_cast<std::uint64_t>(address);
			std::uintptr_t vtable{};
			c.typed = address && read(address, vtable) && vtable == server_soldier_vtable;
			c.placed = c.typed && read(address + server_position, c.position);
			return c;
		}

		void add_static(std::vector<soldier_candidate>& out, std::uintptr_t slot)
		{
			std::uintptr_t soldier{};
			if (read(slot, soldier) && soldier)
			{
				out.push_back(candidate(soldier));
			}
		}

		// One region's words equal to the soldier vtable (a region can be freed while it is read).
		std::size_t scan_region(const std::uintptr_t* begin, std::size_t words, std::uintptr_t* out, std::size_t room)
		{
			std::size_t found = 0;
			__try
			{
				for (std::size_t i = 0; i < words && found < room; ++i)
				{
					if (begin[i] == server_soldier_vtable)
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

		// Background: every 8-byte word of committed private read-write memory equal to the soldier vtable.
		void scan_heap()
		{
			std::vector<std::uintptr_t> finds(max_scan_finds);
			std::size_t count = 0;
			MEMORY_BASIC_INFORMATION region{};
			for (std::uintptr_t at = 0x10000; count < max_scan_finds
				&& VirtualQuery(reinterpret_cast<LPCVOID>(at), &region, sizeof(region)) == sizeof(region);
				at = reinterpret_cast<std::uintptr_t>(region.BaseAddress) + region.RegionSize)
			{
				if (region.State == MEM_COMMIT && region.Type == MEM_PRIVATE && region.Protect == PAGE_READWRITE)
				{
					count += scan_region(static_cast<const std::uintptr_t*>(region.BaseAddress), region.RegionSize / sizeof(std::uintptr_t),
						finds.data() + count, max_scan_finds - count);
				}
			}
			finds.resize(count);
			{
				const std::lock_guard guard(finds_lock);
				scan_finds = std::move(finds);
			}
			++scans;
			scanning = false;
		}

		void request_scan()
		{
			const ULONGLONG now = GetTickCount64();
			bool idle = false;
			if (now - last_scan_at.load() < scan_interval_ms || !scanning.compare_exchange_strong(idle, true))
			{
				return;
			}
			last_scan_at = now;
			std::thread(scan_heap).detach();
		}
	}

	std::uintptr_t player_server_soldier()
	{
		++lookups;
		float feet[3]{};
		std::uintptr_t client{};
		if (!feet_of_client(client, feet))
		{
			return 0;
		}
		const std::lock_guard choosing(choose_lock);
		const ULONGLONG now = GetTickCount64();
		const float seconds = last_call_ms ? static_cast<float>(now - last_call_ms) / 1000.0f : 0.0f;
		last_call_ms = now;
		const std::uintptr_t kept = current.load();
		if (kept)
		{
			const soldier_candidate c = candidate(kept);
			float distance = 1e9f;
			if (c.placed)
			{
				const float dx = c.position[0] - feet[0], dy = c.position[1] - feet[1], dz = c.position[2] - feet[2];
				distance = std::sqrt(dx * dx + dy * dy + dz * dz);
			}
			if (keep_server_soldier(c.typed, client == current_client, distance, seconds, off_seconds))
			{
				found_by = 1;
				return kept;
			}
		}
		std::vector<soldier_candidate> candidates;
		for (int i = 0; i < table_a_count; ++i)
		{
			add_static(candidates, table_a + 0x10 * i);
		}
		for (int i = 0; i < table_b_count; ++i)
		{
			add_static(candidates, table_b + 0x20 * i);
		}
		add_static(candidates, single_slot);
		std::uintptr_t chosen = static_cast<std::uintptr_t>(choose_server_soldier(candidates.data(), static_cast<int>(candidates.size()), feet, tolerance_m));
		int how = 2;
		if (!chosen)
		{
			candidates.clear();
			{
				const std::lock_guard guard(finds_lock);
				for (std::uintptr_t address : scan_finds)
				{
					candidates.push_back(candidate(address));
				}
			}
			chosen = static_cast<std::uintptr_t>(choose_server_soldier(candidates.data(), static_cast<int>(candidates.size()), feet, tolerance_m));
			how = 3;
			if (!chosen)
			{
				++misses;
				request_scan();
			}
		}
		current_client = chosen ? client : 0;
		off_seconds = 0.0f;
		if (chosen != kept)
		{
			current = chosen;
			found_by = chosen ? how : 0;
			++changes;
			log::write("server soldier: %p (%s)", reinterpret_cast<void*>(chosen), chosen ? (how == 2 ? "static table" : "heap scan") : "none");
		}
		return chosen;
	}

	std::string server_soldier_report()
	{
		const std::uintptr_t soldier = player_server_soldier();
		std::size_t finds = 0;
		{
			const std::lock_guard guard(finds_lock);
			finds = scan_finds.size();
		}
		static const char* const how[] = { "none", "kept", "static table", "heap scan" };
		char text[256];
		std::snprintf(text, sizeof(text), "server_soldier=%p found_by=%s changes=%llu lookups=%llu misses=%llu scans=%llu scan_finds=%zu",
			reinterpret_cast<void*>(soldier), how[found_by.load()], static_cast<unsigned long long>(changes.load()),
			static_cast<unsigned long long>(lookups.load()), static_cast<unsigned long long>(misses.load()),
			static_cast<unsigned long long>(scans.load()), finds);
		return text;
	}
}
