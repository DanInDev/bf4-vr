#pragma once

#include <d3d11.h>

#include <cstdint>
#include <string>

// Magnified sights (stereo/scope): the scope render only needs the lens circle in the middle of the
// image (~8 % of its pixels), so while it runs every draw into a target proportional to the back buffer
// (full, half, quarter... size, the viewport covering it) is scissored to that middle, with a margin for
// blurs. The game's rasterizer states are swapped for scissor-enabled copies and its own scissor rects
// are intersected; shadow maps and other targets are left alone. Compute passes are not affected.
namespace bfvr::stereo
{
	// Render thread: hooks the context's rasterizer and output-merger setters (once).
	void install_scope_scissor(ID3D11DeviceContext* context);
	// half_width, half_height: the kept rectangle's half extents as fractions of a target's size.
	void begin_scope_scissor(ID3D11DeviceContext* context, UINT back_width, UINT back_height, float half_width, float half_height);
	// Puts the game's own rasterizer state and scissor rects back.
	void end_scope_scissor(ID3D11DeviceContext* context);
	std::string scope_scissor_report();
	// Called by another OMSetRenderTargets hook (stereo/hud_capture) after the call went through.
	void scope_scissor_targets_changed(ID3D11DeviceContext* context);
}
