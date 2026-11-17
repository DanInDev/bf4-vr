#pragma once

#include <vector>

// #82: the grenade pin pull as samples (mono, -1..1). A soft metallic tink at 2.2 kHz with a short click, scaled by
// the setting pin_volume; the earlier 3.8 kHz ping at full level was too loud and piercing (owner, 2026-11-17).
// Engine-free; tested by tests/pin_sound_test.cpp.
namespace bfvr::weapon
{
	std::vector<float> pin_samples(int rate, float volume);
}
