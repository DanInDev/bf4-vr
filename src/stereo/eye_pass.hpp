#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <string>

namespace bfvr::stereo
{
	// Research: per-eye rendering from one game frame (docs/tasks/per-eye-rendering.md, approach 2).
	// Wraps the world renderer's per-view render 0x140CE7EA0 (render thread; G-buffer, shadows,
	// lighting, tonemap into the back buffer) and calls it twice per frame for view slot 0, the
	// second time with the camera moved sideways. The frame-level work around it (sub-renderer
	// begin/end callbacks, sync points) still runs once: repeating it crashes, because a callback
	// consumes a per-frame pointer (re/notes/stereo-rendering.md).
	// After the first render the back buffer is copied into the eye texture.
	//   mode 0 = off; flags: 1 = render twice, 2 = shifted eye last (the monitor shows it),
	//   4 = restore slot 0 from a snapshot between the eyes.
	std::string set_eye_pass(int mode, float shift_m);
	// One status line: eye_frames, eye_pairs, eye_rejected, eye_abandoned, eye_ms (last extra render).
	std::string eye_pass_report();
	// Present thread: tracks the back buffer the first eye is copied from.
	void eye_pass_on_present(IDXGISwapChain* swap_chain);
	// The first eye's image of the latest frame (back-buffer format and size), or null.
	ID3D11Texture2D* eye_pass_image();
}
