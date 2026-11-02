#pragma once

#include <cstdint>
#include <string>

namespace bfvr::hooks
{
	struct xinput_stats
	{
		std::uint64_t state_calls{};
		std::uint64_t capability_calls{};
		std::uint64_t set_state_calls{}; // the game's rumble, any user index
	};
	xinput_stats xinput_call_counts();

	// physical_pad = 0: a real pad on the virtual pad's user index is ignored (a worn or half-pressed
	// trigger there fired the gun on its own, 2026-11-02). The report names a real pad and what it holds.
	void set_physical_pad(bool merged);
	std::string physical_pad_report();

	// Detours XInputGetState/XInputGetCapabilities in every loaded XInput DLL so the game
	// sees the VR controllers as an Xbox pad on user index 0 (merged with a real pad there).
	// Loads xinput9_1_0.dll if no XInput DLL is present yet. Returns false if nothing was hooked.
	bool install_xinput_hooks();
}
