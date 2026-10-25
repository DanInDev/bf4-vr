#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>
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
	//   4 = restore slot 0 from a snapshot between the eyes, 8 = eye-2 constants (implies 2; the
	//   per-view constants are baked into job-filled arenas, see constant_patch.hpp), 32 = also move
	//   every view-projection built with the game's projection (tested, did not move the gun).
	// stereo_render = pair uses 1 | 8. Known gap: the gun and arms stay identical in both eyes.
	std::string set_eye_pass(int mode, float shift_m);
	// Research: during the next eye-pass frame, records the first eye's constant-buffer bindings and
	// writes <prefix>-bindings.txt, each bound buffer read back from the GPU (<prefix>-buffer-<ptr>.bin)
	// and view slot 0 (<prefix>-slot.bin).
	std::string probe_eye_pass(const std::string& prefix);
	// stereo_render = pair: the camera hook sets the right eye's offset (metres along the left eye's
	// right axis) every frame.
	void set_eye_pass_shift(float metres);
	// Complete pairs so far (both eyes rendered from one frame); the theater submits a pair only when
	// this advanced since its last one.
	std::uint64_t eye_pass_pairs();
	// One status line: eye_frames, eye_pairs, eye_rejected, eye_abandoned, eye_ms (last extra render).
	std::string eye_pass_report();
	// Present thread: tracks the back buffer the first eye is copied from.
	void eye_pass_on_present(IDXGISwapChain* swap_chain);
	// The first eye's image of the latest frame (back-buffer format and size), or null.
	ID3D11Texture2D* eye_pass_image();
}
