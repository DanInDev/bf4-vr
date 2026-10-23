#pragma once

#include <cstdint>

namespace bfvr::stereo
{
	struct camera_stats
	{
		unsigned long copy_thread{};
		std::uint64_t copies{};
	};
	camera_stats stats();

	// Hooks the per-frame copy of the game's view parameters into the renderer (BF4 build 179547,
	// see re/notes/camera.md) so the main camera can be shifted sideways before rendering.
	// Waits (in a background thread) until the game's code is decrypted and the function's bytes
	// match build 179547, then hooks; a different build is never hooked.
	bool install_camera_hook();

	// Sideways camera shift in metres along the camera's right vector (0 = untouched).
	void set_eye_offset(float metres);
	float eye_offset();

	// Research: odd copies get +amplitude, even copies -amplitude (0 = off).
	void set_alternating_offset(float amplitude);

	// Research: writes the candidate vertical-FOV field (radians, 0 = off) when not in stereo.
	void set_fov_override(float radians);

	// Research: per RenderView (0 world, 1 = +0x4E0, 2 = +0x9C0) FOV (radians, 0 = off) and
	// sideways shift (metres, 0 = off), applied when not in stereo.
	void set_view_override(int view, float fov_radians, float shift_metres);
}
