#pragma once

#include <dxgi.h>

namespace bfvr::hooks
{
	// Runs on the presenting thread, immediately before the real IDXGISwapChain::Present.
	using present_callback = void (*)(IDXGISwapChain* swap_chain);

	// Detours IDXGISwapChain::Present for every swap chain in the process.
	// Call once; returns false (and logs why) if the hook could not be installed.
	bool install_present_hook(present_callback callback);
}
