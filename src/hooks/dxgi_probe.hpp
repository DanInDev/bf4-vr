#pragma once

namespace bfvr::hooks
{
	// Research probe: logs the game's DXGI stereo-related calls (IsWindowedStereoEnabled,
	// RegisterStereoStatusWindow) and every CreateSwapChainForHwnd, including the Stereo flag.
	bool install_dxgi_stereo_probe();
}
