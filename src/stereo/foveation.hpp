#pragma once

#include <d3d11.h>

#include <string>

// Fixed foveated rendering (live setting foveation = off | light | medium | strong): while the eye pass
// renders the two eyes, targets proportional to the back buffer are shaded at full rate in the middle and
// one shade per 2x2 / 4x4 pixels further out (NVIDIA variable rate shading through NVAPI, D3D11; tiles
// of 16 pixels). The lenses blur the edge, the GPU work there drops. Not on other GPUs (no NVAPI VRS),
// not for shadow maps, the scope render or the UI.
namespace bfvr::stereo
{
	void set_foveation(const std::string& level);
	// Render thread, around one eye's per-view render.
	void begin_foveation(ID3D11DeviceContext* context, UINT back_width, UINT back_height);
	void end_foveation(ID3D11DeviceContext* context);
	std::string foveation_report();
}
