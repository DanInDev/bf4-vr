#pragma once

#include <d3d11.h>
#include <dxgi.h>

#include <string>

namespace bfvr::stereo
{
	// Stream view (stream_view): the game window shows a steadied, level, 16:9 picture of what the left
	// eye sees, for recording it with OBS (docs/TOOLING.md "Recording"). The left eye's image is
	// re-aimed at a camera that follows the head's yaw and pitch through a low-pass (stream_smoothing)
	// with the roll taken out, cropped to stream_fov degrees across, the HUD layer on top
	// (stream_hud), and drawn letterboxed into the back buffer so that the monitor-shaped window shows
	// it 16:9. Only head-tracked frames are drawn over; menus, loading screens and flat-screen cutscenes
	// stay the game's own picture. The maths is stereo/stream_math.
	void set_stream_view(bool on, float horizontal_degrees, float smoothing_seconds, bool hud);

	// Present thread, after the theater has taken this frame's eye images (the back buffer is drawn
	// over). menu: a menu is up over the level (the headset shows the flat screen).
	void stream_view_on_present(IDXGISwapChain* swap_chain, bool menu);

	// The latest stream picture (16:9, back-buffer format), or null; pipe streamshot.
	ID3D11Texture2D* stream_view_image();

	// Pipe "stream": state and counters; "stream trace": the last samples of the eye's and the
	// stream camera's look angles (degrees), one line each.
	std::string stream_view_command(const std::string& argument);
}
