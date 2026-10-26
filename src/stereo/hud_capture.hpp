#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <cstdint>

namespace bfvr::stereo
{
	// hud_layer = 1: the HUD is drawn into its own transparent texture instead of the eye image
	// (re/notes/stereo-rendering.md). The UI render module draws the HUD into the back buffer after the
	// world module's tonemap (UI render 0x1408863E0); while it runs on a head-tracked frame, binding the
	// back buffer as render target binds our texture instead. The theater shows it as its own layer.
	void install_hud_capture();

	// Present thread, before the theater uses the frame: tracks the back buffer the HUD replaces.
	void hud_capture_on_present(IDXGISwapChain* swap_chain);

	struct hud_frame
	{
		ID3D11Texture2D* texture{}; // premultiplied alpha, back-buffer size; owned by the capture
		std::uint64_t frame{};      // Present count of the last frame whose HUD was captured
	};
	hud_frame latest_hud();
	bool hud_capture_enabled();
}
