#include "weapon/grab_debug.hpp"

#include <windows.h>

#include <atomic>
#include <mutex>

namespace bfvr::weapon
{
	namespace
	{
		constexpr ULONGLONG fresh_ms = 250;

		std::atomic<bool> enabled{};
		std::mutex lock;
		grab_zone zones_[grab_zone_count];
		ULONGLONG published_at[grab_zone_count]{};
	}

	void set_grab_debug(bool on)
	{
		enabled = on;
	}

	bool grab_debug_enabled()
	{
		return enabled.load(std::memory_order_relaxed);
	}

	void publish_grab_zone(grab_slot slot, const float (&centre)[3], float radius, bool hot)
	{
		const int index = static_cast<int>(slot);
		if (index < 0 || index >= grab_zone_count || !enabled.load(std::memory_order_relaxed))
		{
			return;
		}
		const std::lock_guard guard(lock);
		grab_zone& zone = zones_[index];
		zone.valid = true;
		zone.hot = hot;
		zone.centre[0] = centre[0];
		zone.centre[1] = centre[1];
		zone.centre[2] = centre[2];
		zone.radius = radius;
		published_at[index] = GetTickCount64();
	}

	void clear_grab_zone(grab_slot slot)
	{
		const int index = static_cast<int>(slot);
		if (index < 0 || index >= grab_zone_count)
		{
			return;
		}
		const std::lock_guard guard(lock);
		zones_[index].valid = false;
	}

	int grab_zones(grab_zone (&zones)[grab_zone_count])
	{
		const ULONGLONG now = GetTickCount64();
		const std::lock_guard guard(lock);
		int valid = 0;
		for (int i = 0; i < grab_zone_count; ++i)
		{
			zones[i] = zones_[i];
			zones[i].valid = zones[i].valid && enabled.load(std::memory_order_relaxed) && now - published_at[i] < fresh_ms;
			valid += zones[i].valid ? 1 : 0;
		}
		return valid;
	}
}
