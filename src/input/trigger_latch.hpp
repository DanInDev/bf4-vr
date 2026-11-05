#pragma once

#include <cstdint>

// The Quest triggers as the game sees them: a finger resting on the trigger reads 25-50 % pulled
// (headset 2026-11-05: the gun kept firing), and the game fires at about 12 %. Each trigger is pressed
// above `press` and released below `press - trigger_release_gap`, and the game gets all or nothing.
// Engine-free; tested by tests/trigger_latch_test.cpp.
namespace bfvr::input
{
	constexpr float trigger_release_gap = 0.10f;

	// value 0..1 (the controller's analog trigger); held keeps the state between calls. 255 or 0.
	std::uint8_t latch_trigger(float value, bool& held, float press);
}
