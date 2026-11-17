#pragma once

#include <cstdint>

// #73: dying sometimes left the rig broken (the gun firing, no movement, arms off; owner 2026-11-16, intermittent).
// This tells a death and the respawn after it from the local soldier's health, so held inputs can be closed and the
// state logged at both moments. Engine-free; tested by tests/death_watch_test.cpp.
namespace bfvr::weapon
{
	enum class life_event
	{
		none,
		died,      // health reached 0 on a soldier that was alive
		respawned, // after a death, a soldier with health again (a new one, or the checkpoint's)
	};

	class death_watch
	{
	public:
		// health_known: the local soldier and its health component could be read this frame.
		life_event update(bool health_known, float health);
		bool dead() const { return dead_; }
		std::uint32_t deaths() const { return deaths_; }

	private:
		bool alive_seen_{};
		bool dead_{};
		std::uint32_t deaths_{};
	};
}
