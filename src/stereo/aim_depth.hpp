#pragma once

#include <d3d11.h>

#include <string>

// The green dot at the distance of what the gun points at: the scene depth (bound with the back buffer
// while the HUD draws, stereo/hud_capture) is sampled along the barrel line and the first surface it
// meets gives the dot's distance.
namespace bfvr::stereo
{
	// Render thread, from hud_capture while the HUD binds the back buffer: this frame's depth view.
	void aim_depth_on_hud_targets(ID3D11DeviceContext* context, ID3D11DepthStencilView* depth);

	// The distance along the shot line (input::shot_line) to the first surface it meets, a few frames old;
	// false when off, when nothing was hit or when the reading is older than 300 ms.
	bool aim_hit_distance(float& distance);

	// The dot on the target (always on since #48; was the setting reticle_depth).
	void set_aim_depth(bool on);

	// Research (pipe depthprobe): the depth view's and its texture's description.
	std::string aim_depth_report();
}
