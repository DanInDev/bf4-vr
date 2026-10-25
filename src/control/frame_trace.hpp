#pragma once

#include <dxgi.h>

#include <string>

namespace bfvr::control
{
	// Research: records one frame of the game's immediate-context calls (render target and depth
	// bindings, clears, viewports, draws, dispatches, copies, resolves, command lists) with the game
	// function that made each call, and writes it as text. Maps the render passes (world, depth,
	// post-processing, HUD) to engine code for per-eye rendering and a separate HUD layer.
	// Blocks until the frame is written (or 5 s pass). Hooks are enabled only while recording.
	std::string trace_frame(const std::string& path);

	// Call from the Present hook before anything else draws.
	void frame_trace_on_present(IDXGISwapChain* swap_chain);
}
