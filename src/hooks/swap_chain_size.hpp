#pragma once

#include <string>

namespace bfvr::hooks
{
	// Hooks swap chain creation and sizing (IDXGIFactory::CreateSwapChain, IDXGISwapChain fullscreen
	// and resize methods, IDXGIOutput::FindClosestMatchingMode) and logs them. With
	// render_size = <w>x<h> the game is offered that size as its fullscreen display mode and renders
	// everything at it; the swap chain stays windowed ("virtual fullscreen"), DXGI scales the image into
	// the monitor-sized window, and the eye images get the full size. Takes effect at game start.
	bool install_swap_chain_hooks();
	std::string render_size_report();
}
